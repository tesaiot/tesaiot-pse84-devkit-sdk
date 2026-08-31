/**
 * @file tesaiot_optiga.c
 * @brief TESAIoT OPTIGA Trust M Wrapper Implementation
 * @version 1.0
 * @date 2026-01-12
 *
 * Part of TESAIoT Firmware SDK
 *
 * This module wraps existing OPTIGA Trust M functions with a clean API.
 * For PSoC Edge firmware, this delegates to optiga_trust_helpers.c functions.
 */

#include "tesaiot_optiga.h"
#include "tesaiot.h"

/*----------------------------------------------------------------------------
 * External Functions (from optiga_trust_helpers.c)
 *---------------------------------------------------------------------------*/

/**
 * Generate device keypair in OPTIGA Trust M
 * (External function from optiga_trust_helpers.c)
 */
extern bool optiga_generate_device_keypair(
    uint16_t key_oid,
    uint8_t *public_key_der,
    uint16_t *public_key_der_len
);

/**
 * Generate CSR signed by OPTIGA Trust M
 * (External function from optiga_trust_helpers.c)
 */
extern bool optiga_generate_csr_pem(
    uint16_t key_oid,
    const uint8_t *public_key_der,
    uint16_t public_key_der_len,
    const char *subject,
    char *csr_pem,
    size_t csr_pem_len
);

/*----------------------------------------------------------------------------
 * Public API Implementation
 *---------------------------------------------------------------------------*/

/**
 * Generate ECC P-256 keypair in OPTIGA Trust M
 */
bool tesaiot_optiga_generate_keypair(
    uint16_t key_oid,
    uint8_t *public_key_der,
    uint16_t *pubkey_len
)
{
    /* License enforcement - function disabled without valid license */
    TESAIOT_LICENSE_GATE_BOOL();

    /* Validate parameters */
    if (!public_key_der || !pubkey_len) {
        return false;
    }

    /* Delegate to existing OPTIGA helper */
    return optiga_generate_device_keypair(key_oid, public_key_der, pubkey_len);
}

/**
 * Generate Certificate Signing Request (CSR) signed by OPTIGA
 */
bool tesaiot_optiga_generate_csr(
    uint16_t key_oid,
    const uint8_t *public_key_der,
    uint16_t pubkey_len,
    const char *subject,
    char *csr_pem,
    size_t csr_pem_len
)
{
    /* License enforcement - function disabled without valid license */
    TESAIOT_LICENSE_GATE_BOOL();

    /* Validate parameters */
    if (!public_key_der || pubkey_len == 0 || !subject || !csr_pem || csr_pem_len == 0) {
        return false;
    }

    /* Delegate to existing OPTIGA helper */
    return optiga_generate_csr_pem(key_oid, public_key_der, pubkey_len, subject, csr_pem, csr_pem_len);
}
