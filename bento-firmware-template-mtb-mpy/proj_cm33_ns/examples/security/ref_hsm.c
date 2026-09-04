/* sdk-example: core=cm33 variant=both group=security
 * id:      cm33/security/ref_hsm
 * title:   Reference list — read the HSM's state without starting anything
 * teaches: which calls answer a question without a chip transaction, and the
 *          one probe that is balanced rather than free
 * apis:    optiga_manager_lock, optiga_manager_unlock,
 *          trustm_requested_target_oid, trustm_requested_anchor_oid,
 *          trustm_current_correlation_id
 * entry:   example_tesaiot_hsm_reference
 */

/*******************************************************************************
 * THIS IS A REFERENCE LIST, NOT A JOB.
 *
 * It enrols nothing, publishes nothing, and writes nothing — to the chip or
 * anywhere else. It is one function that reads everything the tesaiot_hsm API
 * will tell you about the current state of enrolment and of chip ownership, so
 * a status screen or a health check can be written from one place.
 *
 * The working examples are 01..04 in this directory.
 *
 * SAFETY: no chip operation is issued, no metadata is written, and metadata tag
 * C0 — the OPTIGA life-cycle state, which moves one way only and which no
 * reflash undoes — is neither read nor written here. See 04_protected_update.c
 * for what would change it and why nothing in this SDK does.
 *
 * THE THREE ENROLMENT READERS ARE FREE. THE OWNERSHIP PROBE IS NOT.
 * -----------------------------------------------------------------
 * trustm_requested_target_oid(), trustm_requested_anchor_oid() and
 * trustm_current_correlation_id() read variables. Call them from any task, as
 * often as you like, including while an enrolment is in flight.
 *
 * optiga_manager_lock() TAKES THE GATE. It is the honest readiness test —
 * optiga_chip_enter() deliberately succeeds when the manager is not up, so it
 * cannot answer this question — but every true return must be matched by
 * optiga_manager_unlock(), and a false return can mean a 10-second wait has
 * just elapsed. Do not poll it from a UI tick.
 ******************************************************************************/

#include <stdio.h>

#include "tesaiot_hsm_api.h"

#include "../sdk_examples_cm33.h"

int example_tesaiot_hsm_reference(void);

int example_tesaiot_hsm_reference(void)
{
    printf("\r\n--- tesaiot_hsm/ref_tesaiot_hsm (reference list) ---\r\n");

    /* ── Enrolment bookkeeping — free, safe, any task ────────────────────── */

    /* The OID a Protected Update request named, or 0xE0E1 from reset. Written
     * only by tesaiot_publish_protected_update(); publish_csr() does NOT set
     * it, so this never describes a CSR-only enrolment. */
    printf("  trustm_requested_target_oid()  = 0x%04X\r\n",
           (unsigned)trustm_requested_target_oid());

    /* The anchor that would authorise writing that slot, or 0xE0E8 from reset.
     * Same caveat as above. */
    printf("  trustm_requested_anchor_oid()  = 0x%04X\r\n",
           (unsigned)trustm_requested_anchor_oid());

    /* NULL when nothing is in flight; otherwise the id the platform's reply is
     * matched against. This is the "is an enrolment outstanding?" test, and it
     * is the value trustm_reset_state() destroys — so check it before you
     * reset anything. Print it guarded: %s given NULL faults on this
     * platform's newlib. */
    const char *cid = trustm_current_correlation_id();
    printf("  trustm_current_correlation_id()= %s\r\n",
           (cid != NULL) ? cid : "(none — nothing in flight)");

    /* ── Chip readiness — a balanced probe, not a free read ──────────────── */

    if (optiga_manager_lock()) {
        /* True means both "the manager is up" and "the gate is now ours". The
         * unlock is not optional and there is no path out of here without it. */
        printf("  optiga_manager_lock()          = true  (manager up, chip free)\r\n");
        optiga_manager_unlock();
        printf("  optiga_manager_unlock()        — gate returned\r\n");
    } else {
        /* Either optiga_manager_init() has never run, or another task has held
         * the chip for the full 10-second timeout. Those are different problems
         * and the return value does not separate them; if you need to know,
         * track whether your own code has called init(). */
        printf("  optiga_manager_lock()          = false (manager not "
               "initialised, or another task holds the chip)\r\n");
    }

    return SDK_EX_OK;
}
