/* sdk-example: core=cm33 variant=both group=security
 * id:      cm33/security/05_csr_enrolment
 * title:   Publish a CSR and track the request that follows it
 * teaches: the request bookkeeping — correlation id, target and anchor OIDs —
 *          and the buffer contract publish_csr() imposes on its caller
 * apis:    publish_csr, trustm_update_state, trustm_reset_state,
 *          trustm_current_correlation_id, trustm_requested_target_oid,
 *          trustm_requested_anchor_oid
 * entry:   example_tesaiot_hsm_csr_enrolment
 */

/*******************************************************************************
 * ENROLMENT, END TO END
 *
 *   1. The device generates a keypair in the secure element and builds a PEM
 *      CSR from the public half.            <- YOURS. See "you must supply the
 *                                              CSR" below.
 *   2. publish_csr() wraps it in a JSON envelope and publishes it to
 *      device/<id>/commands/csr over the live MQTT session.
 *   3. The platform signs it and answers on another topic.
 *   4. Your MQTT subscriber ingests the certificate and writes it to the slot.
 *
 * This example is step 2 and the bookkeeping around it. Steps 1, 3 and 4 are
 * the consumer's — optiga_generate_csr_pem, cy_mqtt_publish, mqtt_connection
 * and mqtt_device_id are all in dist/tesaiot_hsm/consumer_must_provide.txt, so
 * the archive cannot do any of them for you.
 *
 * THE BUFFER CONTRACT — READ THIS BEFORE YOU CALL publish_csr()
 * ------------------------------------------------------------
 * The first parameter is `uint8_t *csr`, not `const uint8_t *`, and that is not
 * an oversight. publish_csr() BUILDS THE JSON ENVELOPE IN YOUR BUFFER, on top
 * of the CSR, to avoid a second large allocation (confirmed by reading
 * BENTO-TESAIoT-libraries/claw/kit-pse84-ai/modules/tesaiot/
 * tesaiot_optiga_trust_m.c). So:
 *
 *   - the buffer must be WRITABLE. A const PEM string in flash faults.
 *   - the buffer must be LARGER than the CSR. It has to hold
 *         {"device_id":"<id>","csr":"<CSR with \n escaped>",
 *          "correlation_id":"<uuid>"}
 *     which is the CSR plus one byte per newline, plus ~45 bytes of fixed
 *     text, plus the device id and the correlation id. Allow 256 bytes of
 *     headroom and you will not think about it again.
 *   - csr_length is the LENGTH OF THE CSR, not the size of the buffer.
 *   - your CSR is GONE when the call returns. Keep a copy if you need one.
 *
 * WHAT THE THREE READERS ACTUALLY TRACK — AND THE TRAP IN TWO OF THEM
 * ------------------------------------------------------------------
 *   trustm_current_correlation_id()  a fresh id, generated from the TRNG by
 *                                    publish_csr() on every call. It is how the
 *                                    platform's reply is matched to this
 *                                    request. NULL means nothing is in flight.
 *
 *   trustm_requested_target_oid()    the OID pair a Protected Update request
 *   trustm_requested_anchor_oid()    named. publish_csr() DOES NOT SET THESE.
 *                                    They are written only by
 *                                    tesaiot_publish_protected_update(), and
 *                                    they read 0xE0E1 / 0xE0E8 from reset. So
 *                                    after a bare publish_csr() they describe
 *                                    the previous PU request, or the defaults —
 *                                    never this CSR. Do not use them to label a
 *                                    CSR-only enrolment.
 *
 * trust_anchor_oid and payload_version are accepted and currently unused by
 * publish_csr(); they are reserved. Pass the values you mean anyway, so the
 * call site stays correct when they start mattering.
 *
 * trustm_reset_state() CLEARS THE CORRELATION ID. Call it while a reply is
 * outstanding and the reply arrives with nothing to match against — the
 * certificate you asked for is dropped unread. Reset on completion or on a
 * timeout you decided, not "to clean up".
 *
 * SAFETY: nothing here writes chip metadata, and nothing here can advance the
 * OPTIGA life-cycle state. Publishing a CSR is an MQTT publish.
 ******************************************************************************/

#include <stdio.h>
#include <string.h>

#include "tesaiot_hsm_api.h"

#include "../sdk_examples_cm33.h"

/* Off by default: publishing needs a live MQTT session AND a real CSR, and a
 * request published with neither is noise on somebody's broker.
 *     DEFINES+=EXAMPLE_HSM_PUBLISH_CSR=1
 * volatile so the call is really linked, not folded away. */
#ifndef EXAMPLE_HSM_PUBLISH_CSR
#define EXAMPLE_HSM_PUBLISH_CSR  0
#endif
static volatile int s_publish = EXAMPLE_HSM_PUBLISH_CSR;

/* Slots this project pairs: certificate 0xE0E1 with ECC key 0xE0F1, anchor
 * 0xE0E8. DEVICE_CERTIFICATE_OID in optiga_trust_helpers.h is the same 0xE0E1;
 * it is spelled out here so this file has no dependency on that header. */
#define EXAMPLE_TARGET_OID   (0xE0E1U)
#define EXAMPLE_ANCHOR_OID   (0xE0E8U)
#define EXAMPLE_PAYLOAD_VER  (1U)

/* Writable, and roomy enough for the JSON envelope publish_csr() builds in it.
 * A 256-byte margin over the largest CSR you will produce. */
#define EXAMPLE_CSR_CAPACITY  (1280U)
static uint8_t s_csr[EXAMPLE_CSR_CAPACITY];
static size_t  s_csr_len;          /* 0 until you fill s_csr — see below */

int example_tesaiot_hsm_csr_enrolment(void);

int example_tesaiot_hsm_csr_enrolment(void)
{
    printf("\r\n--- tesaiot_hsm/03_csr_enrolment ---\r\n");

    /* 1. What is in flight right now? NULL is the idle answer. Print it
     *    defensively: a %s given NULL faults on this platform's newlib. */
    const char *cid = trustm_current_correlation_id();
    printf("  correlation id on entry : %s\r\n", (cid != NULL) ? cid : "(none)");

    /* 2. The OID pair the last Protected Update request named. Read them, but
     *    read the warning above about what they do NOT tell you. */
    printf("  requested target OID    : 0x%04X\r\n",
           (unsigned)trustm_requested_target_oid());
    printf("  requested anchor OID    : 0x%04X\r\n",
           (unsigned)trustm_requested_anchor_oid());

    /* 3. Drive the state machine — but only if nothing is in flight. A request
     *    is outstanding exactly when there is a correlation id, and moving the
     *    machine underneath it is how a certificate that arrived correctly ends
     *    up discarded. */
    if (cid != NULL) {
        printf("  a request is already outstanding — NOT touching the state\r\n"
               "    machine. Let it finish or time out; trustm_reset_state()\r\n"
               "    here would clear the id the reply is matched against.\r\n");
    } else {
        /* The transition your ingest path makes when it has the certificate and
         * is about to write it. status_code and detail are surfaced on the
         * provisioning screen, so write them for the person reading it. */
        trustm_update_state(TRUSTM_STATE_WAITING_FOR_CERTIFICATE,
                            "example_probe",
                            "SDK example: state machine reachable");
        printf("  trustm_update_state(WAITING_FOR_CERTIFICATE) — visible on "
               "the provisioning screen\r\n");

        /* Back to IDLE. Safe here precisely because we established there was
         * nothing in flight before we moved it. */
        trustm_reset_state();
        printf("  trustm_reset_state() — machine IDLE, correlation id cleared "
               "(now %s)\r\n",
               (trustm_current_correlation_id() != NULL) ? "set" : "NULL");
    }

    /* 4. The publish itself. */
    if (s_publish == 0) {
        printf("  SKIPPED publish_csr(): it needs a live MQTT session to the\r\n"
               "    TESAIoT platform and a real PEM CSR built from a key in\r\n"
               "    the secure element. Fill s_csr / s_csr_len and rebuild\r\n"
               "    with DEFINES+=EXAMPLE_HSM_PUBLISH_CSR=1.\r\n");
        return SDK_EX_REFUSED;
    }

    /* Refuse rather than publish a fabricated CSR. A CSR the platform signs is
     * a certificate on a real device; the wrong one is worse than none. */
    if (s_csr_len == 0u) {
        printf("  publish_csr() NOT called: s_csr is empty. Build a CSR first —\r\n"
               "    generate the keypair in the chip, then produce the PEM. The\r\n"
               "    archive cannot do this for you (optiga_generate_csr_pem is\r\n"
               "    in consumer_must_provide.txt).\r\n");
        return SDK_EX_NO_DATA;
    }
    if (s_csr_len + 256u > sizeof(s_csr)) {
        printf("  publish_csr() NOT called: %u-byte CSR in a %u-byte buffer "
               "leaves no room for the JSON envelope built in place\r\n",
               (unsigned)s_csr_len, (unsigned)sizeof(s_csr));
        return SDK_EX_REFUSED;
    }

    printf("  publish_csr(len=%u, target=0x%04X, anchor=0x%04X, ver=%u) ...\r\n",
           (unsigned)s_csr_len, (unsigned)EXAMPLE_TARGET_OID,
           (unsigned)EXAMPLE_ANCHOR_OID, (unsigned)EXAMPLE_PAYLOAD_VER);

    int rc = publish_csr(s_csr, s_csr_len,
                         (uint16_t)EXAMPLE_TARGET_OID,
                         (uint16_t)EXAMPLE_ANCHOR_OID,
                         (uint32_t)EXAMPLE_PAYLOAD_VER);

    /* s_csr now holds the JSON envelope, not the CSR. Do not reuse it as a CSR. */
    s_csr_len = 0u;

    if (rc != 0) {
        printf("  publish_csr() = %d — not published. MQTT session down, the\r\n"
               "    envelope did not fit, or the broker refused.\r\n", rc);
        /* Leave the machine where publish_csr() left it; it reports its own
         * failure state, and overwriting that loses the reason. */
        return SDK_EX_UNAVAILABLE;
    }

    /* On success the machine has moved to WAITING_FOR_MANIFEST and a fresh
     * correlation id has been generated. Record it: it is what your subscriber
     * matches the platform's reply against. */
    cid = trustm_current_correlation_id();
    printf("  publish_csr() = 0. Waiting for the platform. correlation id: %s\r\n",
           (cid != NULL) ? cid : "(none — the TRNG fallback was used)");

    /* Do NOT reset the state here. The reply has not arrived yet. */
    return SDK_EX_STARTED;
}
