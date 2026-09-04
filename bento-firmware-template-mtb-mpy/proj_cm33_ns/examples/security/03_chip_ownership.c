/* sdk-example: core=cm33 variant=both group=security
 * id:      cm33/security/03_chip_ownership
 * title:   Take the secure element, use it, give it back
 * teaches: the init-before-anything rule, the three names for one re-entrant
 *          gate, and why optiga_chip_enter() is not an "is the chip up?" test
 * apis:    optiga_manager_init, optiga_manager_lock, optiga_manager_unlock,
 *          optiga_manager_acquire, optiga_manager_release, optiga_chip_enter,
 *          optiga_chip_exit
 * entry:   example_tesaiot_hsm_chip_ownership
 */

/*******************************************************************************
 * There is ONE OPTIGA Trust M on ONE I2C bus, and at least four things in this
 * firmware want it: the mTLS/MQTT path, the HSM provisioning screen, the
 * MicroPython optiga module, and whatever you are writing. Everything that
 * touches the chip goes through this manager. Two owners at once is not a
 * performance problem; it is a wedged transaction that only a power cut clears.
 *
 * ORDER. THIS PART IS NOT NEGOTIABLE.
 * -----------------------------------
 *   optiga_manager_init()  must run before anything else here. It creates the
 *   mutex and the shared optiga_util_t instance. Before it has run:
 *       - optiga_manager_acquire() returns NULL
 *       - optiga_manager_lock()    returns false
 *       - optiga_chip_enter()      returns TRUE, having locked nothing
 *
 * That last one is the trap, and it is deliberate. enter() fails ONLY on
 * contention, so a false return means exactly one thing — "somebody else has
 * the chip" — and every call site can treat it as fatal. It is not a
 * readiness test. optiga_manager_lock() is the readiness test.
 *
 * THREE NAMES, ONE GATE
 * ---------------------
 *   optiga_chip_enter()      / optiga_chip_exit()
 *   optiga_manager_lock()    / optiga_manager_unlock()
 *   optiga_manager_acquire() / optiga_manager_release()
 *
 * All three take and give back the same re-entrant, per-task gate.
 * acquire() = enter() + hand back the util instance; lock() = "is the manager
 * up?" + enter(). Pair each take with exactly one give and pair them by name,
 * so a reader can see the balance. Nesting inside one task is free — the chip
 * helpers in this tree call each other, so a plain mutex would deadlock on the
 * first nested call — but every enter still needs its exit.
 *
 * A take from a DIFFERENT task waits up to 10 seconds and then returns false.
 * Ten seconds is a long time to a UI; do the work off the drawing task.
 *
 * WHAT A `true` FROM init() DOES AND DOES NOT PROVE
 * -------------------------------------------------
 * It proves the mutex and the host-side util instance exist (confirmed by
 * reading BENTO-TESAIoT-libraries/claw/kit-pse84-ai/modules/tesaiot/
 * tesaiot_optiga_manager.c). It is not by itself a round trip to the chip —
 * treat your first real operation as the liveness test, and be ready for it to
 * fail on a board whose secure element is not fitted or is wedged.
 *
 * SAFETY: nothing in this file writes to the chip. It takes the gate, reads
 * nothing, and gives it back. In particular it does not go near metadata tag
 * C0 (the life-cycle state), which moves one way only and is not recoverable by
 * any reflash. See tesaiot_hsm/04_protected_update.c.
 ******************************************************************************/

#include <stdio.h>

#include "tesaiot_hsm_api.h"

#include "../sdk_examples_cm33.h"

/* The manager keeps this callback in the util instance and calls it when an
 * asynchronous optiga_util_* operation finishes. Record the status and return.
 *
 * Do NOT printf from here. It runs from the OPTIGA library's completion path,
 * and on this platform a printf from the wrong context is a kernel panic, not a
 * garbled line. Set a flag; print from the task that was waiting.
 *
 * volatile because the waiting task polls it and the writer is not that task. */
static volatile optiga_lib_status_t s_last_event = OPTIGA_LIB_SUCCESS;
static volatile uint32_t            s_event_count;

static void example_optiga_callback(void *callback_ctx, optiga_lib_status_t event)
{
    (void)callback_ctx;          /* the void* handed to optiga_manager_init() */
    s_last_event = event;
    s_event_count++;
}

int example_tesaiot_hsm_chip_ownership(void);

int example_tesaiot_hsm_chip_ownership(void)
{
    printf("\r\n--- tesaiot_hsm/01_chip_ownership ---\r\n");

    /* 1. FIRST. Idempotent — a second call returns true without redoing
     *    anything, and it is internally serialised, so several tasks racing to
     *    be first is safe. Call it from wherever you first need the chip
     *    rather than trying to guarantee an order between tasks. */
    if (!optiga_manager_init(example_optiga_callback, NULL)) {
        printf("  optiga_manager_init() = false — no mutex or no util "
               "instance. The secure element is unusable this boot.\r\n");
        return SDK_EX_UNAVAILABLE;
    }
    printf("  optiga_manager_init() = true\r\n");

    /* 2. The readiness question. Use THIS, not optiga_chip_enter(), when what
     *    you mean is "can I do chip work?". */
    if (!optiga_manager_lock()) {
        /* Two causes: the manager is not up (ruled out by step 1), or another
         * task held the gate for the full 10 s timeout. */
        printf("  optiga_manager_lock() = false — another task has held the "
               "chip for 10 s\r\n");
        return SDK_EX_BUSY;
    }
    printf("  optiga_manager_lock() = true (gate held by this task)\r\n");

    /* 3. Nesting is free WITHIN this task. This is what lets the chip helpers
     *    call each other: a key generation takes the gate and then calls a
     *    metadata read that takes it again. Every enter still needs an exit —
     *    the depth counter is what makes the outermost exit the one that
     *    actually releases. */
    if (!optiga_chip_enter()) {
        printf("  optiga_chip_enter() = false while this task already owns the "
               "gate — re-entrancy is broken\r\n");
        optiga_manager_unlock();
        return SDK_EX_UNAVAILABLE;
    }
    printf("  optiga_chip_enter() nested (depth 2)\r\n");
    optiga_chip_exit();
    printf("  optiga_chip_exit()  unnested (depth 1, still ours)\r\n");

    optiga_manager_unlock();
    printf("  optiga_manager_unlock() — gate released\r\n");

    /* 4. The other way in: acquire() gives you the shared optiga_util_t AND
     *    takes the gate in one step. So it is release() that pairs with it, and
     *    an early return between them leaks the chip for the rest of the boot.
     *    Structure the body so there is exactly one exit. */
    optiga_util_t *util = optiga_manager_acquire();
    if (util == NULL) {
        /* Either the gate timed out or the instance is missing. acquire()
         * releases the gate itself before returning NULL, so there is nothing
         * to give back here — do not call release() on a NULL. */
        printf("  optiga_manager_acquire() = NULL — busy, or the manager is "
               "not up\r\n");
        return SDK_EX_BUSY;
    }
    printf("  optiga_manager_acquire() = %p (gate held)\r\n", (void *)util);

    /* Your chip work goes here, using `util`:
     *
     *     optiga_lib_status = OPTIGA_LIB_BUSY;
     *     optiga_util_read_data(util, 0xE0E0, 0, buf, &len);
     *     while (optiga_lib_status == OPTIGA_LIB_BUSY) { vTaskDelay(1); }
     *
     * Every optiga_util_* call is ASYNCHRONOUS. It returns immediately and the
     * result arrives at the callback registered in step 1. Keep the gate held
     * across the whole wait — releasing it while an operation is in flight
     * hands the bus to another task mid-transaction.
     *
     * This example issues no chip operation, so the callback has not fired. */
    printf("  callback fired %lu time(s), last event 0x%04X\r\n",
           (unsigned long)s_event_count, (unsigned)s_last_event);

    /* 5. Always. Every path. */
    optiga_manager_release();
    printf("  optiga_manager_release() — gate released\r\n");

    /* 6. And the gate is genuinely free again: taking it once more succeeds. */
    if (!optiga_manager_lock()) {
        printf("  gate was not released cleanly\r\n");
        return SDK_EX_UNAVAILABLE;
    }
    optiga_manager_unlock();
    printf("  re-took and released the gate — balanced\r\n");

    return SDK_EX_OK;
}
