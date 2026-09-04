/* sdk-example: core=cm33 variant=mtb-mpy group=connectivity
 * id:      cm33/connectivity/06_session_memory
 * title:   Keep a conversation, and remember facts across it
 * teaches: the RAM ring vs the persistent key-value store, how to build an LLM
 *          context out of both, and the one call that must run on the MPY task
 * apis:    claw_session_init, claw_session_add, claw_session_count,
 *          claw_session_dirty, claw_session_clear, claw_session_build_context,
 *          claw_session_flush, claw_memory_set, claw_memory_get
 * entry:   example_mpy_secure_session_memory
 */

/*******************************************************************************
 * TWO STORES, DIFFERENT LIFETIMES
 *
 *   session   A ring of the last CLAW_SESSION_MAX_MSGS messages. Wraps. This is
 *             the running conversation and it is meant to be lost.
 *   memory    A small key-value store — facts that must outlive the
 *             conversation ("the user's name is X", "the sensor is in the
 *             kitchen"). claw_session_clear() does not touch it.
 *
 * Both live in RAM. Both are written to LittleFS only by claw_session_flush().
 *
 * THE ONE THREADING RULE THAT MATTERS
 * -----------------------------------
 * Everything here is plain RAM and safe from any task — EXCEPT
 * claw_session_flush(), which writes the two files by compiling and running
 * MicroPython source through the VFS bridge. It must be called from the
 * MicroPython task, with the interpreter initialised. From any other task it
 * re-enters the VM from outside its own context.
 *
 * The SDK example runner is NOT the MicroPython task. So the flush below is
 * behind a switch that is off by default. Turn it on only if you have moved
 * this code into the MPY task:
 *
 *     make build ENABLE_SDK_EXAMPLES=1 SDK_EXAMPLE_CM33=mpy_secure/06_session_memory \
 *                DEFINES+=EXAMPLE_SESSION_ON_MPY_TASK=1
 *
 * WRITE DISCIPLINE
 * ----------------
 * add() and memory_set() only mark the store dirty; nothing reaches flash until
 * flush(). Flush on a natural boundary — end of a turn, before a soft reset —
 * not per message: each flush is a full rewrite of the file.
 * claw_session_dirty() is the cheap "is there anything to lose?" check, and it
 * covers BOTH stores.
 *
 * SIZES, from claw_session.h: a message is truncated to CLAW_MSG_MAX_LEN, a key
 * to CLAW_MEM_KEY_MAX, a value to CLAW_MEM_VAL_MAX. Truncation is silent — add()
 * returns true for an over-long message. Check your own lengths if that matters.
 ******************************************************************************/

#include <stdio.h>
#include <string.h>

#include "claw_session.h"

#include "../sdk_examples_cm33.h"

/* Off unless the developer has moved this onto the MicroPython task. volatile
 * so the compiler cannot fold the branch away and quietly drop the call. */
#ifndef EXAMPLE_SESSION_ON_MPY_TASK
#define EXAMPLE_SESSION_ON_MPY_TASK  0
#endif
static volatile int s_on_mpy_task = EXAMPLE_SESSION_ON_MPY_TASK;

static char s_context[768];
static char s_value[CLAW_MEM_VAL_MAX];

int example_mpy_secure_session_memory(void);

int example_mpy_secure_session_memory(void)
{
    printf("\r\n--- mpy_secure/06_session_memory ---\r\n");

    /* Start clean. init() zeroes BOTH stores and clears both dirty flags —
     * including anything a live agent had pending, so this belongs at start-up.
     * (add() and memory_set() call it themselves if it has never run, so the
     * store is never used uninitialised.) */
    claw_session_init();
    printf("  after init: count=%u dirty=%s\r\n",
           (unsigned)claw_session_count(),
           claw_session_dirty() ? "true" : "false");

    /* ── Facts that outlive the conversation ─────────────────────────────── */
    if (!claw_memory_set("owner", "Wiroon")) {
        printf("  claw_memory_set() = false — the key-value store is full\r\n");
        return SDK_EX_REFUSED;
    }
    (void)claw_memory_set("room", "lab-2");
    /* set() on an existing key overwrites in place; it does not add a second
     * entry. That is what makes it usable as a fact store rather than a log. */
    (void)claw_memory_set("room", "lab-3");

    memset(s_value, 0, sizeof(s_value));
    if (claw_memory_get("room", s_value, sizeof(s_value))) {
        printf("  memory[\"room\"] = \"%s\" (overwritten, not duplicated)\r\n",
               s_value);
    } else {
        printf("  memory[\"room\"] missing right after set() — store is broken\r\n");
        return SDK_EX_UNAVAILABLE;
    }

    /* A miss is a normal answer and leaves the buffer untouched, so clear it
     * yourself before the call if you intend to print it either way. */
    memset(s_value, 0, sizeof(s_value));
    printf("  memory[\"nothing_here\"] found = %s\r\n",
           claw_memory_get("nothing_here", s_value, sizeof(s_value))
               ? "true" : "false");

    /* ── The conversation ────────────────────────────────────────────────── */
    (void)claw_session_add(CLAW_ROLE_SYSTEM,    "You control a PSoC Edge board.");
    (void)claw_session_add(CLAW_ROLE_USER,      "What is the temperature?");
    (void)claw_session_add(CLAW_ROLE_TOOL,      "sht40_read -> 28.4 C");
    (void)claw_session_add(CLAW_ROLE_ASSISTANT, "It is 28.4 C in lab-3.");

    printf("  count=%u (ring holds %u; older messages are dropped, not "
           "an error)\r\n",
           (unsigned)claw_session_count(), (unsigned)CLAW_SESSION_MAX_MSGS);
    printf("  dirty=%s\r\n", claw_session_dirty() ? "true" : "false");

    /* ── The context you actually send to the model ──────────────────────── */
    memset(s_context, 0, sizeof(s_context));
    size_t n = claw_session_build_context(s_context, sizeof(s_context));
    /* Returns bytes written, memory block first then the conversation, oldest
     * message first. It writes as much as fits; a return equal to your buffer
     * size means it ran out of room and the tail is missing. */
    printf("  build_context() wrote %u byte(s)%s:\r\n", (unsigned)n,
           (n >= sizeof(s_context) - 1u) ? " — BUFFER FULL, context truncated" : "");
    printf("%s\r\n", s_context);

    /* ── Persisting it ───────────────────────────────────────────────────── */
    if (s_on_mpy_task != 0) {
        bool ok = claw_session_flush();
        printf("  claw_session_flush() = %s; dirty is now %s\r\n",
               ok ? "true" : "false",
               claw_session_dirty() ? "true" : "false");
        if (!ok) {
            return SDK_EX_UNAVAILABLE;
        }
    } else {
        printf("  SKIPPED claw_session_flush(): it runs MicroPython source to\r\n"
               "    write /.bentoclaw_session and /.bentoclaw_memory, so it is\r\n"
               "    valid only on the MicroPython task. Rebuild with\r\n"
               "    DEFINES+=EXAMPLE_SESSION_ON_MPY_TASK=1 once this code lives\r\n"
               "    there. Nothing was written to flash.\r\n");
    }

    /* clear() empties the ring and marks dirty; it does NOT delete the file by
     * itself. The file is truncated by the next flush — so a clear that is
     * never flushed leaves the old conversation on flash. */
    claw_session_clear();
    printf("  after clear: count=%u dirty=%s (memory[\"owner\"] survives: %s)\r\n",
           (unsigned)claw_session_count(),
           claw_session_dirty() ? "true" : "false",
           claw_memory_get("owner", s_value, sizeof(s_value)) ? "yes" : "no");

    /* SDK_EX_REFUSED, not OK, when the flush was skipped: the example did not
     * do the whole job and the return code must say so rather than let a green
     * result stand in for work that never ran. */
    return (s_on_mpy_task != 0) ? SDK_EX_OK : SDK_EX_REFUSED;
}
