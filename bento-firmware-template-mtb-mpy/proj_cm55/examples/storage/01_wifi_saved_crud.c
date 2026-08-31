/* sdk-example: core=cm55 variant=both group=storage
 * id:      cm55/storage/01_wifi_saved_crud
 * title:   Saved networks: add, find, load, update, list, erase
 * teaches: the full CRUD over the OPTIGA credential slots -- and why every one of these calls belongs on a worker task, never on the GFX task
 * apis:    wifi_saved_count, wifi_saved_add, wifi_saved_find, wifi_saved_load, wifi_saved_store, wifi_saved_load_all, wifi_saved_erase
 * entry:   example_cm55_wifi_saved_crud
 */
/*
 * Six network credentials live in OPTIGA Trust M data objects (slots 5, 6, 8,
 * 9, 10, 11 -- 4 is reserved and 7 is the API key), 106 packed bytes each,
 * reached over the CRED_READ/WRITE/ERASE IPC to CM33. When Trust M is not
 * connected the module falls back to a RAM cache that lives until reboot.
 *
 * THESE SEVEN CALLS BLOCK. Each OPTIGA operation is an IPC round trip polled
 * with vTaskDelay for up to 3 s (1.5 s for the very first probe). And they
 * compose: wifi_saved_count() is six loads back to back, wifi_saved_find() is
 * up to six, and wifi_saved_add() calls find() and then possibly six more for
 * the LRU scan. Worst case is tens of seconds.
 *
 * So they must NOT run on the GFX task -- which is where run() is called, and
 * where an lv_timer callback runs too, so moving the work into a timer would
 * change nothing. This example does the only correct thing: it starts a WORKER
 * TASK, returns SDK_EX_STARTED immediately, and a small lv_timer on the GFX
 * side reports the results once the worker publishes them. That split -- work
 * off-task, reporting on-task -- is the pattern to copy.
 *
 * If you only need to READ one slot, do not do any of this: the non-blocking
 * probe/read API in cm55_core/03_wifi_saved_async runs entirely inside the
 * GFX task and needs no worker at all.
 *
 * NOT RE-ENTRANT. The module owns ONE static IPC message buffer in shared
 * memory with no lock around it. Do not run this while the WiFi Connect page
 * is open, and do not call these from two tasks at once.
 *
 * NON-DESTRUCTIVE BY CONSTRUCTION. It writes one obviously disposable SSID and
 * erases it again. If all six slots are already full it REFUSES rather than
 * running, because wifi_saved_add() evicts the least recently used entry when
 * the list is full -- and that would be one of the user's real networks.
 *
 * PASSWORDS ARE NEVER LOGGED. wifi_saved_entry_t carries the passphrase in
 * clear; the log below is on-screen text.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "../sdk_examples.h"
#include "wifi_saved.h"

#define TEST_SSID        "TESAIoT-Example"
#define TEST_PSK         "example-only-not-a-real-key"
#define TEST_SECURITY    (3u)          /* 3 = WPA2 */

#define WORKER_STACK_WORDS  (768u)
#define WORKER_PRIO         (tskIDLE_PRIORITY + 1u)
#define REPORT_POLL_MS      (200u)
#define WORKER_MAX_MS       (60000u)

/* Written by the worker, read by the GFX timer AFTER `done` is seen. Static
 * so nothing large sits on the worker's stack. */
static struct {
    volatile uint8_t   done;
    int                rc;
    int                count_before;
    int                count_after;
    int                idx;
    int                listed;
    bool               added;
    bool               loaded;
    bool               stored;
    bool               update_seen;
    bool               erased;
    wifi_saved_entry_t scratch;
    wifi_saved_entry_t all[WIFI_SAVED_MAX];
} r;

static struct {
    lv_timer_t *timer;
    uint32_t    waited_ms;
    bool        running;
} s;

/* The blocking half. Runs on the worker task, touches no LVGL, logs nothing. */
static int crud_sequence(void)
{
    r.count_before = wifi_saved_count();
    r.idx          = wifi_saved_find(TEST_SSID);

    /* The guard that keeps this example non-destructive. */
    if (r.idx < 0 && r.count_before >= WIFI_SAVED_MAX) {
        return SDK_EX_REFUSED;
    }

    /* CREATE (or update in place if a previous run left it behind). */
    r.added = wifi_saved_add(TEST_SSID, TEST_PSK, TEST_SECURITY);
    if (!r.added) {
        return SDK_EX_REFUSED;
    }

    /* READ back by name -- the only way to learn which slot add() chose. */
    r.idx = wifi_saved_find(TEST_SSID);
    if (r.idx < 0) {
        return SDK_EX_NO_DATA;
    }
    r.loaded = wifi_saved_load(r.idx, &r.scratch);
    if (!r.loaded) {
        return SDK_EX_NO_DATA;
    }

    /* UPDATE: clear the auto-connect bit add() sets, and write it back.
     * store() takes the whole 106-byte record, so read-modify-write is the
     * only safe way to change one field. */
    r.scratch.flags &= (uint8_t)~0x01u;
    r.stored = wifi_saved_store(r.idx, &r.scratch);
    if (!r.stored) {
        return SDK_EX_REFUSED;
    }
    memset(&r.scratch, 0, sizeof(r.scratch));
    if (wifi_saved_load(r.idx, &r.scratch)) {
        r.update_seen = ((r.scratch.flags & 0x01u) == 0u);
    }

    /* LIST. Fills from index 0 with only the VALID entries, so the array
     * position is not the slot index -- use find() when you need the slot. */
    r.listed = wifi_saved_load_all(r.all);

    /* DELETE, restoring the board to the state we found it in. */
    r.erased      = wifi_saved_erase(r.idx);
    r.count_after = wifi_saved_count();
    return SDK_EX_OK;
}

static void crud_worker(void *arg)
{
    (void)arg;
    r.rc = crud_sequence();
    /* Release: everything written above must be visible before `done` is. */
    __atomic_store_n(&r.done, 1u, __ATOMIC_RELEASE);
    vTaskDelete(NULL);
}

/* The reporting half. Runs on the GFX task and does nothing that can block. */
static void report_cb(lv_timer_t *t)
{
    (void)t;
    s.waited_ms += REPORT_POLL_MS;

    if (__atomic_load_n(&r.done, __ATOMIC_ACQUIRE) == 0u) {
        /* Say it is slow, ONCE, and keep waiting. Giving up here would clear
         * the busy flag while the worker still owns `r`, and the next tap
         * would memset that struct underneath it. Every call the worker makes
         * has its own bounded timeout, so it always finishes. */
        if (s.waited_ms == WORKER_MAX_MS) {
            sdk_example_logf("still running after %lu ms -- every OPTIGA"
                             " operation is timing out. Waiting it out.",
                             (unsigned long)s.waited_ms);
        }
        return;
    }

    sdk_example_logf("saved before: %d of %d slots", r.count_before, WIFI_SAVED_MAX);

    if (r.rc == SDK_EX_REFUSED && !r.added) {
        sdk_example_logf("all %d slots are full and '%s' is not one of them.",
                         WIFI_SAVED_MAX, TEST_SSID);
        sdk_example_logf("adding would evict the least recently used REAL"
                         " network. Refusing.");
    } else {
        sdk_example_logf("add('%s')  %s", TEST_SSID, r.added ? "ok" : "FAILED");
        sdk_example_logf("find()     slot %d", r.idx);
        sdk_example_logf("load()     %s", r.loaded ? "ok" : "FAILED");
        sdk_example_logf("store()    %s, auto-connect bit cleared: %s",
                         r.stored ? "ok" : "FAILED",
                         r.update_seen ? "confirmed" : "NOT confirmed");
        sdk_example_logf("load_all() %d valid entr%s:",
                         r.listed, (r.listed == 1) ? "y" : "ies");
        for (int i = 0; i < r.listed && i < WIFI_SAVED_MAX; i++) {
            /* ssid is NUL-terminated by the module; bound the print anyway --
             * these bytes came out of NVM. Never print .password. */
            sdk_example_logf("   %.32s  sec=%u  last_used=%lu",
                             r.all[i].ssid, (unsigned)r.all[i].security,
                             (unsigned long)r.all[i].last_used);
        }
        sdk_example_logf("erase()    %s", r.erased ? "ok" : "FAILED");
        sdk_example_logf("saved after: %d of %d slots (back to where we started)",
                         r.count_after, WIFI_SAVED_MAX);
    }

    /* Wipe the passphrase copies this example made. */
    memset(&r.scratch, 0, sizeof(r.scratch));
    memset(r.all, 0, sizeof(r.all));

    lv_timer_delete(s.timer);
    s.timer   = NULL;
    s.running = false;
}

int example_cm55_wifi_saved_crud(lv_obj_t *parent)
{
    (void)parent;   /* results go to the log; nothing is drawn */

    if (s.running) {
        sdk_example_logf("the worker is still running -- OPTIGA operations take"
                         " seconds each");
        return SDK_EX_BUSY;
    }

    memset(&r, 0, sizeof(r));
    memset(&s, 0, sizeof(s));

    /* Create the reporter FIRST: if the worker cannot be created there is
     * nothing to clean up, whereas a running worker with no reporter would
     * leave `r` unread. */
    s.timer = lv_timer_create(report_cb, REPORT_POLL_MS, NULL);
    if (s.timer == NULL) {
        return SDK_EX_UNAVAILABLE;
    }

    /* Below the GFX task, so a blocked OPTIGA poll can never delay a frame.
     * Dynamic rather than static allocation: the task deletes itself, and the
     * idle task reclaims the stack whenever it next runs -- reusing a static
     * buffer would need that to have happened already. */
    if (xTaskCreate(crud_worker, "ws_crud", WORKER_STACK_WORDS, NULL,
                    WORKER_PRIO, NULL) != pdPASS) {
        lv_timer_delete(s.timer);
        s.timer = NULL;
        sdk_example_logf("could not create the worker task -- FreeRTOS heap");
        return SDK_EX_UNAVAILABLE;
    }

    s.running = true;
    sdk_example_logf("running the CRUD sequence on a worker task; each OPTIGA"
                     " operation may take seconds");
    return SDK_EX_STARTED;
}
