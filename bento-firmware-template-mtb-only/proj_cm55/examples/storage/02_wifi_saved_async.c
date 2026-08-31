/* sdk-example: core=cm55 variant=both group=storage
 * id:      cm55/storage/02_wifi_saved_async
 * title:   Read saved networks without blocking the screen
 * teaches: the start / ready / finish split -- how to drive OPTIGA from an lv_timer inside the GFX task and never stall a frame
 * apis:    wifi_saved_probe_start, wifi_saved_probe_ready, wifi_saved_probe_finish, wifi_saved_read_start, wifi_saved_read_ready, wifi_saved_read_result
 * entry:   example_cm55_wifi_saved_async
 */
/*
 * The same six credential slots as cm55_core/02, reached the other way.
 *
 * Every blocking call in that file is really three steps with a wait in the
 * middle. This API exposes the three, so the wait can be YOUR timer instead of
 * a vTaskDelay loop inside the SDK:
 *
 *     wifi_saved_probe_start()   send the probe, return at once
 *     wifi_saved_probe_ready()   has CM33 answered? (poll, never blocks)
 *     wifi_saved_probe_finish()  commit the verdict: OPTIGA present or not
 *
 *     wifi_saved_read_start(i)   ask for slot i
 *     wifi_saved_read_ready()    has it landed?
 *     wifi_saved_read_result(&e) decode it
 *
 * Because nothing here blocks, the whole sequence runs in the GFX task from an
 * lv_timer, and the display keeps painting throughout. That is why this is the
 * right API for a UI and the blocking one is not.
 *
 * THREE THINGS THE API DOES NOT DO FOR YOU
 *
 *  1. NO TIMEOUT. probe_ready() polls a response flag; if CM33 never answers
 *     it stays false forever. The 1.5 s bound lives in the blocking path, not
 *     here. Your timer owns the deadline -- see PROBE_MAX_MS below.
 *  2. ONE OPERATION AT A TIME. There is a single static IPC buffer with no
 *     lock. Starting a read before the previous one is finished overwrites the
 *     response in flight, so this example advances one slot per completion,
 *     never in parallel.
 *  3. PROBE FIRST. read_start() behaves differently depending on whether
 *     OPTIGA was found: with it, an IPC round trip; without it, the RAM
 *     fallback answers instantly. Skipping the probe does not fail -- it just
 *     silently reads the wrong backend.
 *
 * A slot that reads back false is EMPTY, not broken. Six empty slots on a
 * board with no Trust M fitted is the correct answer, not a fault.
 *
 * PASSWORDS ARE NEVER LOGGED, even though the entry carries one in clear.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "../sdk_examples.h"
#include "wifi_saved.h"

#define POLL_MS        (50u)
#define PROBE_MAX_MS   (2000u)
#define READ_MAX_MS    (3000u)

typedef enum { ST_PROBE = 0, ST_READ_START, ST_READ_WAIT } state_t;

static struct {
    lv_timer_t *timer;
    state_t     state;
    uint32_t    waited_ms;
    int         slot;
    int         found;
    bool        running;
} s;

static void session_end(void)
{
    if (s.timer != NULL) { lv_timer_delete(s.timer); s.timer = NULL; }
    s.running = false;
}

static void step_cb(lv_timer_t *t)
{
    (void)t;
    s.waited_ms += POLL_MS;

    switch (s.state) {
    case ST_PROBE:
        if (!wifi_saved_probe_ready()) {
            if (s.waited_ms >= PROBE_MAX_MS) {
                /* Our deadline, not the SDK's. Commit anyway: finish() reads
                 * the response flag, so an unanswered probe settles as
                 * "OPTIGA unavailable" and the RAM fallback takes over. */
                sdk_example_logf("probe unanswered after %lu ms -- committing"
                                 " the verdict anyway",
                                 (unsigned long)s.waited_ms);
                wifi_saved_probe_finish();
                s.state     = ST_READ_START;
                s.waited_ms = 0u;
            }
            return;
        }
        wifi_saved_probe_finish();
        /* finish() is what makes the verdict stick. Until it is called, every
         * later call still thinks the backend is undecided. */
        sdk_example_logf("probe answered in %lu ms", (unsigned long)s.waited_ms);
        s.state     = ST_READ_START;
        s.waited_ms = 0u;
        return;

    case ST_READ_START:
        if (s.slot >= WIFI_SAVED_MAX) {
            sdk_example_logf("%d of %d slots hold a network",
                             s.found, WIFI_SAVED_MAX);
            if (s.found == 0) {
                sdk_example_logf("none saved -- an empty list, not an error");
            }
            session_end();
            return;
        }
        if (!wifi_saved_read_start(s.slot)) {
            sdk_example_logf("slot %d: read_start refused (IPC pipe busy)", s.slot);
            s.slot++;
            return;
        }
        s.state     = ST_READ_WAIT;
        s.waited_ms = 0u;
        return;

    case ST_READ_WAIT:
    default:
        if (!wifi_saved_read_ready()) {
            if (s.waited_ms >= READ_MAX_MS) {
                sdk_example_logf("slot %d: no answer in %lu ms -- skipping",
                                 s.slot, (unsigned long)s.waited_ms);
                s.slot++;
                s.state = ST_READ_START;
            }
            return;
        }
        {
            wifi_saved_entry_t e;
            if (wifi_saved_read_result(&e)) {
                s.found++;
                /* ssid is terminated by the module; bound the print anyway --
                 * these bytes came out of NVM. .password is never printed. */
                sdk_example_logf("slot %d: %.32s  sec=%u  auto=%u  last_used=%lu",
                                 s.slot, e.ssid, (unsigned)e.security,
                                 (unsigned)(e.flags & 0x01u),
                                 (unsigned long)e.last_used);
            } else {
                sdk_example_logf("slot %d: empty", s.slot);
            }
            memset(&e, 0, sizeof(e));   /* the passphrase was on our stack */
        }
        s.slot++;
        s.state     = ST_READ_START;
        s.waited_ms = 0u;
        return;
    }
}

int example_cm55_wifi_saved_async(lv_obj_t *parent)
{
    (void)parent;   /* results go to the log; nothing is drawn */

    if (s.running) {
        sdk_example_logf("a scan is already in flight -- one operation at a time");
        return SDK_EX_BUSY;
    }

    memset(&s, 0, sizeof(s));

    /* Returns true both when the probe was SENT and when one already happened
     * earlier this boot -- in the second case every ready() below answers
     * immediately. False means the IPC pipe would not take the message. */
    if (!wifi_saved_probe_start()) {
        sdk_example_logf("wifi_saved_probe_start() refused -- IPC pipe unavailable");
        return SDK_EX_UNAVAILABLE;
    }

    s.state = ST_PROBE;
    s.timer = lv_timer_create(step_cb, POLL_MS, NULL);
    if (s.timer == NULL) {
        return SDK_EX_UNAVAILABLE;
    }
    s.running = true;

    sdk_example_logf("probing OPTIGA, then reading %d slots -- the screen keeps"
                     " painting throughout", WIFI_SAVED_MAX);
    return SDK_EX_STARTED;
}
