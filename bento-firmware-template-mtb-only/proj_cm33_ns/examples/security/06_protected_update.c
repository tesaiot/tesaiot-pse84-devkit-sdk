/* sdk-example: core=cm33 variant=both group=security
 * id:      cm33/security/06_protected_update
 * title:   Ask the platform for a Protected Update — and what it changes
 * teaches: the request, the anti-rollback counter, what a manifest lock does
 *          and undoes, and the one chip change that no reflash can undo
 * apis:    tesaiot_publish_protected_update,
 *          tesaiot_run_protected_update_isolated_test
 * entry:   example_tesaiot_hsm_protected_update
 */

/*******************************************************************************
 * ══ READ THIS BEFORE YOU SET EITHER SWITCH ══════════════════════════════════
 *
 * THE ONE THING THAT CANNOT BE UNDONE — and it is NOT what this file does.
 *
 * The OPTIGA life-cycle state, metadata tag C0 (LcsO), moves in one direction
 * only and no reflash, erase or power cycle takes it back:
 *
 *       cr (0x01)  ->  in (0x03)  ->  op (0x07)  ->  te (0x0F)
 *
 * While the chip is below `op`, a metadata write is still permitted — which is
 * what makes a Protected Update lock CLEARABLE and lets enrolment be run again.
 * Advance to `op` and that stops, permanently, on that chip. The "allow
 * ordinary writes again" path stops working. There is no recovery.
 *
 * WHAT WOULD ADVANCE IT: a metadata write whose TLV contains tag C0. Nothing in
 * this file does that, and nothing in libbento_hsm.a's eighteen exported
 * functions does that. This example therefore READS AND REPORTS and never
 * writes a life-cycle byte. Advancing the life cycle deliberately belongs in a
 * separate, clearly named tool, run by someone who has decided to ship that
 * board — not in an SDK example that somebody might run to see what happens.
 *
 * WHAT A PROTECTED UPDATE *DOES* CHANGE, PERMANENTLY-ISH
 * ------------------------------------------------------
 * Applying one to a target OID does two lasting things:
 *
 *   1. It sets that object's Change access condition to Int(anchor) — from then
 *      on the slot accepts only writes carried by a manifest signed by that
 *      trust anchor. An ordinary write is refused. This is a LOCK, and it is
 *      the point of the feature.
 *
 *      REVERSIBLE — but only while the chip is below `op`, which is where every
 *      board on this bench sits. Say which board you mean whenever you write
 *      this down: "the lock is permanent" is true of a shipped device and false
 *      of a development one, and stating only one of those teaches something
 *      false. Note that the clearing helper is NOT among the archive's exported
 *      symbols, so from the SDK the practical answer is "plan not to need it".
 *
 *   2. It advances the chip's anti-rollback counter (tag C1) for that object.
 *      The NEXT manifest must carry a strictly greater payload_version. That
 *      counter only goes up.
 *
 * So: pick your target OID deliberately. Running this against the live device
 * certificate slot locks the live device certificate.
 *
 * ══ WHAT THE TWO FUNCTIONS ARE ══════════════════════════════════════════════
 *
 * tesaiot_publish_protected_update(target, anchor, version, with_csr)
 *     Publishes a REQUEST over the live MQTT session and returns 0/-1. It does
 *     not perform the update. The platform replies with a signed manifest and
 *     your subscriber applies it — so a 0 here means "asked", not "done".
 *
 *     The OIDs are hex STRINGS: "E0E1", not 0xE0E1.
 *
 *     Before publishing it reads the chip and refuses locally if the target is
 *     already locked to a DIFFERENT anchor — worth knowing, because the chip's
 *     own refusal in that case is 0x800F, which is the same code it returns for
 *     a stale version, and neither says which it was.
 *
 *     with_csr=true enrols a fresh key in the same exchange. with_csr=false
 *     updates the object without touching the key.
 *
 * tesaiot_run_protected_update_isolated_test()
 *     A bench tool that exercises the whole Protected Update path against the
 *     isolated test slot, leaving the live certificate alone. Two things about
 *     it that its signature does not tell you, both confirmed by reading
 *     BENTO-TESAIoT-libraries/claw/kit-pse84-ai/modules/tesaiot/
 *     tesaiot_protected_update_isolated.c:
 *
 *       - IT IS AN INTERACTIVE MENU. It prints options and blocks on scanf()
 *         for a keypress, in a loop, and does not return until the operator
 *         chooses to exit. Never call it from an unattended task, a boot path,
 *         or anything with a watchdog.
 *       - It writes metadata on the TEST OIDs. It reads LcsO and prints it; it
 *         does not write it.
 *
 * Both are off by default. Turn on exactly one, deliberately:
 *
 *     DEFINES+=EXAMPLE_HSM_REQUEST_PU=1        (network request)
 *     DEFINES+=EXAMPLE_HSM_ISOLATED_TEST=1     (interactive, needs a console)
 ******************************************************************************/

#include <stdio.h>

#include "tesaiot_hsm_api.h"

#include "../sdk_examples_cm33.h"

#ifndef EXAMPLE_HSM_REQUEST_PU
#define EXAMPLE_HSM_REQUEST_PU     0
#endif
#ifndef EXAMPLE_HSM_ISOLATED_TEST
#define EXAMPLE_HSM_ISOLATED_TEST  0
#endif

/* volatile: these gates must reach the object file, not be folded out. */
static volatile int s_request_pu    = EXAMPLE_HSM_REQUEST_PU;
static volatile int s_isolated_test = EXAMPLE_HSM_ISOLATED_TEST;

/* Hex strings, as the API takes them. "E0E1" is the TESAIoT device certificate
 * slot and "E0E8" the trust anchor this project pairs with it. Change the
 * target to the isolated test slot if you are only rehearsing. */
#define EXAMPLE_PU_TARGET   "E0E1"
#define EXAMPLE_PU_ANCHOR   "E0E8"

/* The platform takes max(chip counter, its own record, this) + 1, so a value
 * that is merely plausible is fine; a value LOWER than the chip's counter is
 * not, and the chip's refusal will look like a signature failure. */
#define EXAMPLE_PU_VERSION  (1U)

/* false: update the object, do not enrol a new key at the same time. */
#define EXAMPLE_PU_WITH_CSR  false

int example_tesaiot_hsm_protected_update(void);

int example_tesaiot_hsm_protected_update(void)
{
    printf("\r\n--- tesaiot_hsm/04_protected_update ---\r\n");

    /* Where a request would go, and what it would change. Printing the plan
     * before doing anything is not decoration: this is the one operation in the
     * SDK whose target OID you want to have read twice. */
    printf("  target OID   : %s   (this slot gets locked to the anchor)\r\n",
           EXAMPLE_PU_TARGET);
    printf("  anchor OID   : %s   (only manifests it signed will be accepted)\r\n",
           EXAMPLE_PU_ANCHOR);
    printf("  version      : %u   (must exceed the chip's counter for this OID)\r\n",
           (unsigned)EXAMPLE_PU_VERSION);
    printf("  with_csr     : %s\r\n", EXAMPLE_PU_WITH_CSR ? "true" : "false");
    printf("  life cycle   : NOT touched. This example never writes metadata\r\n"
           "                 tag C0, and nothing in the archive's eighteen\r\n"
           "                 exported functions does either.\r\n");

    if (s_isolated_test != 0) {
        /* Interactive and blocking. It will not return until the operator picks
         * the exit option on the serial console. */
        printf("  entering tesaiot_run_protected_update_isolated_test() — this\r\n"
               "    is an INTERACTIVE MENU on this console and it will not\r\n"
               "    return until you choose to exit it. Have the serial\r\n"
               "    terminal in front of you.\r\n");
        tesaiot_run_protected_update_isolated_test();
        printf("  isolated test menu exited\r\n");
        return SDK_EX_OK;
    }

    if (s_request_pu == 0) {
        printf("  SKIPPED both paths.\r\n"
               "    DEFINES+=EXAMPLE_HSM_REQUEST_PU=1     publishes a real\r\n"
               "      Protected Update request over the live MQTT session, and\r\n"
               "      the manifest that comes back LOCKS the target slot.\r\n"
               "    DEFINES+=EXAMPLE_HSM_ISOLATED_TEST=1  runs the bench menu\r\n"
               "      against the isolated test slot. Interactive: it blocks on\r\n"
               "      console input and needs an operator.\r\n");
        return SDK_EX_REFUSED;
    }

    /* The request. Returns 0 when it was published, -1 when it was not — and
     * -1 also covers the local refusals it makes on your behalf, such as a
     * target already locked to a different anchor. Its own printf says which. */
    int rc = tesaiot_publish_protected_update(EXAMPLE_PU_TARGET,
                                              EXAMPLE_PU_ANCHOR,
                                              (uint32_t)EXAMPLE_PU_VERSION,
                                              EXAMPLE_PU_WITH_CSR);
    if (rc != 0) {
        printf("  tesaiot_publish_protected_update() = %d — nothing was sent.\r\n"
               "    Either MQTT is down, or the request was refused locally\r\n"
               "    (target already locked to another anchor). The chip is\r\n"
               "    unchanged.\r\n", rc);
        return SDK_EX_UNAVAILABLE;
    }

    /* Published. The chip has still not changed: the manifest has to arrive and
     * be applied by your subscriber before anything is written. Track it by the
     * correlation id (example 03) and do not reset the state machine until the
     * reply lands or you have decided it timed out. */
    printf("  tesaiot_publish_protected_update() = 0 — REQUEST PUBLISHED.\r\n"
           "    The chip is not modified yet. When the platform's manifest is\r\n"
           "    applied, %s is locked to anchor %s and its version counter\r\n"
           "    advances. LcsO is untouched either way.\r\n",
           EXAMPLE_PU_TARGET, EXAMPLE_PU_ANCHOR);

    return SDK_EX_STARTED;
}
