/* sdk-example: core=cm33 variant=mtb-mpy group=connectivity
 * id:      cm33/connectivity/04_circuit_breaker
 * title:   Stop hammering a backend that is already failing
 * teaches: drive the breaker CLOSED -> OPEN -> HALFOPEN -> CLOSED and read the
 *          cooldown, so a dead backend costs three attempts, not thousands
 * apis:    claw_cb_init, claw_cb_allow, claw_cb_failure, claw_cb_success,
 *          claw_cb_state, claw_cb_cooldown_remaining
 * entry:   example_mpy_secure_circuit_breaker
 */

/*******************************************************************************
 * THE PATTERN, in the order a caller uses it:
 *
 *     if (!claw_cb_allow()) { give up now; }      <- ask BEFORE the work
 *     int rc = do_the_thing();
 *     if (rc == 0) claw_cb_success(); else claw_cb_failure();
 *
 * Forgetting either of the two reports is what breaks it: a breaker that is
 * never told about a failure never opens, and one that is never told about a
 * success never closes again.
 *
 * THE STATE MACHINE (constants read off claw_safety.c)
 *   CLOSED    normal. allow() is true.
 *   OPEN      3 failures with no intervening success. allow() is false for
 *             30 000 ms. This is the point of the whole thing: the caller
 *             stops paying the 15-second HTTP timeout over and over.
 *   HALFOPEN  the cooldown has expired. allow() lets exactly the next call
 *             through as a probe. Its success() closes the breaker; its
 *             failure() re-opens it and restarts the cooldown.
 *
 * TWO BEHAVIOURS WORTH KNOWING
 *
 *  1. claw_cb_state() is not a pure getter. When the breaker is OPEN and the
 *     cooldown has expired, reading the state MOVES it to HALFOPEN. That is
 *     deliberate — it is what makes the probe available to whoever asks first —
 *     but it means a monitoring thread polling the state is participating in
 *     the machine, not observing it.
 *
 *  2. claw_cb_cooldown_remaining() returns 0 in every state except OPEN. Zero
 *     therefore means "not waiting", never "waiting, 0 ms left".
 *
 * SCOPE: one global breaker for the whole agent, not one per endpoint. Tripping
 * it stops every backend call. This example leaves it CLOSED on exit.
 ******************************************************************************/

#include <stdio.h>

#include "claw_safety.h"

#include "../sdk_examples_cm33.h"

static const char *cb_state_str(claw_cb_state_t s)
{
    switch (s) {
        case CLAW_CB_CLOSED:   return "CLOSED";
        case CLAW_CB_OPEN:     return "OPEN";
        case CLAW_CB_HALFOPEN: return "HALFOPEN";
        default:               return "unknown";
    }
}

/* Read the cooldown FIRST, then the state: claw_cb_state() can move the machine
 * and this line is meant to describe it, not change it. (With the shipped
 * implementation the two orders give the same numbers, because the transition
 * only fires once the remaining time has already reached 0 — but relying on
 * that coincidence is how a report starts lying after a refactor.) */
static void report(const char *when)
{
    uint32_t left = claw_cb_cooldown_remaining();
    printf("  %-22s state=%-8s cooldown_remaining=%lu ms\r\n",
           when, cb_state_str(claw_cb_state()), (unsigned long)left);
}

int example_mpy_secure_circuit_breaker(void);

int example_mpy_secure_circuit_breaker(void)
{
    printf("\r\n--- mpy_secure/04_circuit_breaker ---\r\n");

    /* Known start. init() is a hard reset of the breaker — it does not consult
     * the cooldown, so calling it while OPEN discards a live cooldown. Call it
     * once at agent start-up, not as a "clear the error" button. */
    claw_cb_init();
    report("after init");

    if (!claw_cb_allow()) {
        printf("  allow() is false immediately after init — cannot happen; "
               "the breaker is not behaving as documented\r\n");
        return SDK_EX_UNAVAILABLE;
    }
    printf("  allow() = true (CLOSED lets everything through)\r\n");

    /* Two failures do NOT trip it. This is the part callers get wrong: they
     * back off after the first error and never discover that the threshold is
     * three. */
    claw_cb_failure();
    claw_cb_failure();
    report("after 2 failures");
    printf("  allow() = %s (still below the 3-failure threshold)\r\n",
           claw_cb_allow() ? "true" : "false");

    /* A success anywhere in there resets the counter to zero. The three
     * failures must be consecutive. */
    claw_cb_success();
    report("after a success");

    /* Now trip it properly. */
    claw_cb_failure();
    claw_cb_failure();
    claw_cb_failure();
    report("after 3 failures");

    if (claw_cb_allow()) {
        printf("  allow() = true while OPEN — the breaker did not trip\r\n");
        claw_cb_success();                      /* leave it usable */
        return SDK_EX_UNAVAILABLE;
    }
    printf("  allow() = false. Every backend call is refused for the next "
           "%lu ms without touching the network.\r\n",
           (unsigned long)claw_cb_cooldown_remaining());

    /* This example does NOT wait out the 30-second cooldown — a runner task
     * that blocks for half a minute is worse than an example that says so. In
     * real code you do not wait either: you return "unavailable" to your caller
     * and let the next request find the breaker HALFOPEN on its own.
     *
     * What a real recovery looks like:
     *
     *     if (claw_cb_allow()) {              // HALFOPEN: this is the probe
     *         if (backend_call() == 0) claw_cb_success();   // -> CLOSED
     *         else                     claw_cb_failure();   // -> OPEN again
     *     }
     */

    /* Put it back the way we found it. A left-tripped global breaker would
     * silently disable the agent for whoever runs the next example. */
    claw_cb_success();
    report("restored");

    if (claw_cb_state() != CLAW_CB_CLOSED) {
        printf("  breaker did not close on success()\r\n");
        return SDK_EX_REFUSED;
    }
    return SDK_EX_OK;
}
