/* sdk-example: core=cm55 variant=both group=security
 * id:      cm55/security/01_hsm_screens
 * title:   Open the HSM enrol and protect screens
 * teaches: hand a seconds-long secure-element operation to a screen that polls, and always tear the previous overlay down first
 * apis:    hsm_provision_ui_teardown, hsm_enrol_open, hsm_protect_open
 * entry:   example_cm55_hsm_screens
 */
/*
 * Two teaching screens over OPTIGA Trust M:
 *
 *   hsm_enrol_open()    certificate enrolment by CSR -- key pair, CSR, proof
 *                       of possession, certificate written back.
 *   hsm_protect_open()  Protected Update -- pending change set, confirm, run,
 *                       read back.
 *
 * Each opens a full-screen overlay on the active screen, with its own Back
 * button, and each drives the work ASYNCHRONOUSLY: IPC_CMD_HSM_PROVISION
 * returns immediately and an lv_timer polls for the result, because the
 * operation takes seconds. Waiting for it inline would freeze the display and
 * simultaneously stop ui_busy_modal_service() drawing the overlay that would
 * have explained the freeze -- so the screen would be dead AND silent.
 *
 * ALWAYS TEAR DOWN FIRST. hsm_provision_ui_teardown() drops any existing
 * overlay and cancels its poll timer. It is idempotent and safe to call when
 * nothing is open, and calling it is how you avoid a stale poll timer writing
 * into a view that has been rebuilt. page_hsm_destroy() calls it for exactly
 * that reason; so does this example, before opening anything.
 *
 * The overlay COVERS this page while it is up, so the log below is only
 * readable once you press Back.
 *
 * ABOUT THE PROTECTED UPDATE LOCK, because a half-stated version of this has
 * misled people before: whether a lock on object E0E1 can be lifted again
 * depends on the chip's life-cycle state, metadata tag C0. While LcsO is below
 * `op` a metadata write is permitted, so the lock can be cleared and ordinary
 * writes resumed. Once that chip is advanced to `op` it cannot -- permanently,
 * on that chip, and no reflash recovers it. Read optiga.read_metadata(0xE0E1)
 * and look at tag C0 rather than assuming either case; the chip answers the
 * question in one line.
 */

#include <stdbool.h>

#include "../sdk_examples.h"
#include "hsm_provision_ui.h"

/* Which screen the next tap opens. Both are worth seeing, and one entry point
 * has to reach both. */
static bool s_open_protect_next;

int example_cm55_hsm_screens(lv_obj_t *parent)
{
    (void)parent;   /* both screens build their own full-screen overlay */

    /* Idempotent, and first. If an earlier run's overlay is still up, opening
     * another would be refused silently -- shell_open() returns when one
     * already exists -- and the tap would look like it did nothing. */
    hsm_provision_ui_teardown();

    if (s_open_protect_next) {
        s_open_protect_next = false;
        hsm_protect_open();
        sdk_example_logf("opened Protected Update.");
        sdk_example_logf("it shows the pending change set BEFORE running it --"
                         " nothing is written until you confirm.");
        sdk_example_logf("tap this example again for the Enrol screen.");
    } else {
        s_open_protect_next = true;
        hsm_enrol_open();
        sdk_example_logf("opened Enrol Certificate.");
        sdk_example_logf("key pair -> CSR -> proof of possession -> certificate;"
                         " each step polls, none of it blocks this task.");
        sdk_example_logf("tap this example again for the Protected Update screen.");
    }

    sdk_example_logf("press Back on the overlay to return here.");

    /* The screen is up and its own poll timer owns the work from here. That is
     * asynchronous progress, not a completed job. */
    return SDK_EX_STARTED;
}
