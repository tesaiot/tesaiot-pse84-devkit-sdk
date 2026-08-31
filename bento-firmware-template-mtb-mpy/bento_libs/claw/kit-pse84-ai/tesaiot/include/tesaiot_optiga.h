/**
 * @file tesaiot_optiga.h
 * @brief Certificate enrolment with the TESAIoT platform, and OPTIGA Trust M
 *        data-object helpers.
 *
 * Two groups of calls:
 *   - requesting a certificate or a Protected Update from the platform and
 *     following the exchange's progress (provided by libbento_hsm.a);
 *   - blocking read, write and verify helpers for OPTIGA data objects
 *     (defined in optiga_trust_helpers.c, which ships as source).
 *
 * For shared access to the chip itself see tesaiot_optiga_core.h.
 */

#ifndef TESAIOT_OPTIGA_H
#define TESAIOT_OPTIGA_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "FreeRTOS.h"
#include "optiga_util.h"
#include "optiga_trust_helpers.h"

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------------------------------------------------------------
 * Enrolment progress
 *--------------------------------------------------------------------------*/

/** Stage the certificate / Protected Update exchange with the platform has
 *  reached. The numbering is fixed: libbento_hsm.a is built against it. */
typedef enum
{
    TRUSTM_STATE_IDLE = 0,
    TRUSTM_STATE_PUBLISHING_CSR,
    TRUSTM_STATE_WAITING_FOR_MANIFEST,
    TRUSTM_STATE_APPLYING_UPDATE,
    TRUSTM_STATE_WAITING_FOR_CERTIFICATE,
    TRUSTM_STATE_COMPLETE,
    TRUSTM_STATE_ERROR,
    TRUSTM_STATE_WAITING_FOR_JSON_BUNDLE,
    TRUSTM_STATE_PROCESSING_JSON_BUNDLE,
    TRUSTM_STATE_WRITING_TRUST_ANCHOR,
    TRUSTM_STATE_VERIFYING_MANIFEST,
    TRUSTM_STATE_APPLYING_FRAGMENTS,
    TRUSTM_STATE_PROTECTED_UPDATE_SUCCESS,
    TRUSTM_STATE_PROTECTED_UPDATE_FAILED
} trustm_state_t;

/** Record a new stage. `code` and `text` are short strings reported with it. */
void trustm_update_state(trustm_state_t stage, const char *code, const char *text);

/** Return to TRUSTM_STATE_IDLE. */
void trustm_reset_state(void);

/*----------------------------------------------------------------------------
 * Requests to the platform
 *--------------------------------------------------------------------------*/

/** Send a certificate signing request, already built by the caller, to the
 *  platform for signing. `cert_oid` is the object the issued certificate is
 *  meant for, `anchor_oid` the trust anchor that will authorise writing it,
 *  and `version` the payload version to use. */
int publish_csr(uint8_t *req, size_t req_len, uint16_t cert_oid, uint16_t anchor_oid, uint32_t version);

/** Ask the platform for a Protected Update of the object `target` (written as
 *  hex text, e.g. "E0E1"), authorised by the trust anchor `anchor`. With
 *  `new_key` set, a fresh key pair is generated on the chip and its CSR travels
 *  with the request, so the certificate that comes back belongs to a key this
 *  chip holds; without it, the certificate is not bound to any key on this
 *  chip. */
int tesaiot_publish_protected_update(const char *target, const char *anchor, uint32_t version, bool new_key);

/** Object ids named by the request currently in flight. */
uint16_t trustm_requested_target_oid(void);
uint16_t trustm_requested_anchor_oid(void);

/** Correlation id pairing the platform's reply with the request in flight;
 *  NULL when there is none. */
const char *trustm_current_correlation_id(void);

/*----------------------------------------------------------------------------
 * Data-object helpers
 *
 * Each call borrows the chip (tesaiot_optiga_core.h), waits for the operation
 * to finish, and returns the OPTIGA library status.
 *--------------------------------------------------------------------------*/

/** Fetch the chip's life-cycle state byte into `*state`. */
optiga_lib_status_t tesaiot_read_lcso(uint8_t *state);

/** Replace / fetch the metadata of object `oid`. For the read, `*len` is the
 *  buffer size going in and the metadata size coming out. */
optiga_lib_status_t tesaiot_write_metadata(uint16_t oid, uint8_t *meta, uint16_t len);
optiga_lib_status_t tesaiot_read_metadata(uint16_t oid, uint8_t *meta, uint16_t *len);

/** Store `len` bytes in object `oid` / fetch its content (same `*len` rule). */
optiga_lib_status_t tesaiot_write_data(uint16_t oid, const uint8_t *buf, uint16_t len);
optiga_lib_status_t tesaiot_read_data(uint16_t oid, uint8_t *buf, uint16_t *len);

/** Store a trust anchor in object `oid`, replacing whatever it held. */
optiga_lib_status_t tesaiot_write_trust_anchor(uint16_t oid, const uint8_t *buf, uint16_t len);

/** Empty the object `obj_oid`. */
optiga_lib_status_t tesaiot_erase_data(uint16_t obj_oid);

/** Check the signature of a Protected Update manifest against the trust
 *  anchor held in object `anchor_oid`. */
optiga_lib_status_t tesaiot_verify_manifest_with_trustanchor(const uint8_t *manifest, uint16_t manifest_len, uint16_t anchor_oid);

#ifdef __cplusplus
}
#endif

#endif /* TESAIOT_OPTIGA_H */
