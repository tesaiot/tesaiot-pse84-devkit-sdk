/* sdk-example: core=cm33 variant=both group=security
 * id:      cm33/security/04_touch_hold
 * title:   Keep the touch controller off the bus while the chip works
 * teaches: the counted hold/release pair, why it must wrap the WHOLE operation
 *          and not just its setup, and what the user sees while it is held
 * apis:    optiga_manager_touch_hold, optiga_manager_touch_hold_reason,
 *          optiga_manager_touch_release
 * entry:   example_tesaiot_hsm_touch_hold
 */

/*******************************************************************************
 * THE PROBLEM THIS SOLVES
 * -----------------------
 * On the TESAIoT Dev Kit the secure element and the touch controller share one
 * I2C bus, and they are driven by different cores. When CM55 polls touch while
 * CM33 is mid-transaction with the chip, the transaction sometimes never
 * completes — the vendor library has no timeout on that path, so the signature
 * does not fail, it HANGS. The same build connects in a second one run and
 * stalls forever the next. Observed repeatedly on hardware.
 *
 * The fix is not a retry. It is: do not let the other core on the bus.
 *
 *     optiga_manager_touch_hold_reason("Signing certificate");
 *     ... every chip operation, to the last byte ...
 *     optiga_manager_touch_release();
 *
 * WRAP THE WHOLE OPERATION, NOT THE SETUP
 * ---------------------------------------
 * This is the mistake that produced the bug. The mTLS path paused touch for its
 * setup and resumed at the end of it — but the signature that matters,
 * CertificateVerify, is produced later, inside cy_mqtt_connect(), by which time
 * touch was polling again. The hold has to cover the last chip byte, wherever
 * that happens to be, not the function that looks like the crypto function.
 *
 * IT IS COUNTED
 * -------------
 * Holds nest. Only the first pauses touch and only the last resumes it — which
 * is what lets a MicroPython session hold the chip for its whole lifetime while
 * individual signatures inside it hold and release freely.
 *
 * The counter is the whole risk. A hold with no matching release leaves the
 * screen permanently deaf, and a deaf screen is indistinguishable from a crash
 * to the person holding the board. Balance every path, including error returns.
 * There is no "force resume".
 *
 * COSTS, BEFORE YOU PUT THIS IN A LOOP
 * ------------------------------------
 *   - The FIRST hold sleeps ~50 ms, deliberately, so an in-flight touch
 *     transfer can finish before the chip starts talking. Nested holds are
 *     free.
 *   - It sleeps. It is NOT ISR-safe. Task context only.
 *   - While held, the screen ignores every tap. Put a busy modal up saying what
 *     the device is doing, using the same words you passed as the reason.
 *
 * THE REASON STRING
 * -----------------
 * optiga_manager_touch_hold() is optiga_manager_touch_hold_reason() with the
 * default text "Signing with secure element". Prefer the _reason form: the
 * string is carried to CM55 and shown to the user, and "Enrolling certificate"
 * explains a five-second freeze in a way that the default does not. Only the
 * FIRST hold's reason is displayed — a nested hold cannot change the message.
 *
 * CONSUMER RESPONSIBILITY: the archive calls ipc_hsm_touch_pause(),
 * ipc_hsm_touch_resume() and the optional weak ipc_hsm_touch_pause_reason() —
 * all three are in dist/tesaiot_hsm/consumer_must_provide.txt. On a board with
 * no touch controller, provide them as no-ops; the counting still works and
 * costs nothing but the 50 ms.
 *
 * SAFETY: this file touches no chip data and no metadata. It only gates a bus.
 ******************************************************************************/

#include <stdio.h>

#include "tesaiot_hsm_api.h"

#include "../sdk_examples_cm33.h"

int example_tesaiot_hsm_touch_hold(void);

int example_tesaiot_hsm_touch_hold(void)
{
    printf("\r\n--- tesaiot_hsm/02_touch_hold ---\r\n");

    /* 1. The outer hold, with a reason the user can read. Costs ~50 ms here. */
    printf("  touch_hold_reason(\"Reading device certificate\") — the screen "
           "stops responding NOW; show a modal saying so\r\n");
    optiga_manager_touch_hold_reason("Reading device certificate");

    /* 2. A nested hold, as a helper deeper in the call stack would take. Free,
     *    and the displayed reason does not change. */
    optiga_manager_touch_hold();
    printf("  touch_hold() nested — no second pause, no new message\r\n");

    /* The chip work belongs here, between the outermost hold and its release.
     * Note the ordering against example 01: take the chip gate and hold touch
     * for the same span. Two rules, one lifetime.
     *
     *     optiga_manager_touch_hold_reason("Signing");
     *     if (optiga_manager_lock()) {
     *         ... chip operations, including the wait for the callback ...
     *         optiga_manager_unlock();
     *     }
     *     optiga_manager_touch_release();
     */

    /* 3. Unwind. Inner first, and exactly as many releases as holds. */
    optiga_manager_touch_release();
    printf("  touch_release() — count 2 -> 1, touch still paused\r\n");

    optiga_manager_touch_release();
    printf("  touch_release() — count 1 -> 0, touch resumed; drop the modal\r\n");

    /* An extra release is not an error and does not "resume harder" — the
     * counter floors at zero. It is still a bug in your pairing, and the next
     * genuine hold/release cycle is the one that will misbehave. Do not use it
     * as a recovery. */

    return SDK_EX_OK;
}
