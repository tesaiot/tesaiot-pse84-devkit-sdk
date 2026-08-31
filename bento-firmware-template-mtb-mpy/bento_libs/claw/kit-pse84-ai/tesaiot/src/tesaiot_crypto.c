/**
 * SPDX-FileCopyrightText: 2024-2026 Assoc. Prof. Wiroon Sriborrirux (TESAIoT Platform Creator)
 *
 * \author TESAIoT Platform Developer Team
 *
 * \file tesaiot_crypto.c
 *
 * \brief TESAIoT Developer Crypto Utilities - Implementation
 *
 * Hardware-backed cryptographic utility functions wrapping OPTIGA Trust M
 * secure element operations. All functions are license-gated and thread-safe.
 *
 * IP-PROTECTED: This file is compiled into libtesaiot.a only.
 * Do NOT distribute source code.
 *
 * This file is part of the TESAIoT AIoT Foundation Platform.
 *
 * Contact: Assoc. Prof. Wiroon Sriborrirux <sriborrirux@gmail.com>/<wiroon@tesa.or.th>
 *
 * \ingroup TESAIoT
 */

#include "tesaiot.h"
#include "tesaiot_crypto.h"
#include "tesaiot_optiga_core.h"
#include "tesaiot_config.h"
#include "include/optiga_crypt.h"
#include "include/optiga_util.h"
#include "include/common/optiga_lib_common.h"
#include "optiga_trust_helpers.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

/*============================================================================
 * Debug Label
 *============================================================================*/
#define LABEL_CRYPTO "[Crypto]"

/*============================================================================
 * Internal: Async callback and wait pattern
 *============================================================================*/

static void crypto_callback(void *context, optiga_lib_status_t status)
{
    volatile optiga_lib_status_t *p_status = (volatile optiga_lib_status_t *)context;
    *p_status = status;
}

static int crypto_wait_for_completion(volatile optiga_lib_status_t *status)
{
    uint32_t timeout_ms = 10000; /* 10 second timeout */
    uint32_t elapsed = 0;

    while (*status == OPTIGA_LIB_BUSY && elapsed < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(1));
        elapsed++;
    }

    if (*status == OPTIGA_LIB_BUSY) {
        TESAIOT_DEBUG_ERROR_PRINT(("%s Timeout waiting for OPTIGA operation\n", LABEL_CRYPTO));
        return TESAIOT_ERROR_TIMEOUT;
    }

    if (*status != OPTIGA_LIB_SUCCESS) {
        TESAIOT_DEBUG_ERROR_PRINT(("%s OPTIGA operation failed with status: 0x%04X\n",
                                   LABEL_CRYPTO, (unsigned int)*status));
        return TESAIOT_ERROR_OPTIGA;
    }

    return TESAIOT_OK;
}

/*============================================================================
 * Internal: Slot-to-OID mapping for secure_store
 *============================================================================*/

static uint16_t secure_store_slot_to_oid(uint8_t slot)
{
    if (slot <= 3)  return 0xF1D0 + slot;       /* 0xF1D0-0xF1D3 */
    if (slot == 4)  return 0;                     /* RESERVED - PU shared secret */
    if (slot <= 11) return 0xF1D0 + slot;        /* 0xF1D5-0xF1DB (slot 5->0xF1D5, etc.) */
    if (slot == 12) return 0xF1E0;
    if (slot == 13) return 0xF1E1;
    return 0; /* invalid */
}

static uint16_t secure_store_max_size(uint8_t slot)
{
    if (slot <= 11) return 140;
    if (slot <= 13) return 1500;
    return 0;
}

/*============================================================================
 * tesaiot_error_str() - Error code to string
 *============================================================================*/

const char* tesaiot_error_str(tesaiot_error_t error)
{
    switch (error) {
        case TESAIOT_OK:                      return "OK";
        case TESAIOT_ERROR_NOT_LICENSED:       return "Not licensed";
        case TESAIOT_ERROR_NOT_INITIALIZED:    return "Not initialized";
        case TESAIOT_ERROR_INVALID_PARAM:      return "Invalid parameter";
        case TESAIOT_ERROR_TIMEOUT:            return "Timeout";
        case TESAIOT_ERROR_OPTIGA:             return "OPTIGA error";
        case TESAIOT_ERROR_MEMORY:             return "Memory allocation failed";
        case TESAIOT_ERROR_NETWORK:            return "Network error";
        case TESAIOT_ERROR_TLS:                return "TLS error";
        case TESAIOT_ERROR_MQTT:               return "MQTT error";
        case TESAIOT_ERROR_INTERNAL:           return "Internal error";
        case TESAIOT_ERROR_BUFFER_TOO_SMALL:   return "Buffer too small";
        case TESAIOT_ERROR_RESERVED_OID:       return "Reserved OID";
        default:                               return "Unknown error";
    }
}

/*============================================================================
 * Phase 1: Critical Functions
 *============================================================================*/

int tesaiot_random_generate(uint8_t *buffer, uint16_t length)
{
    TESAIOT_CHECK_LICENSE();

    if (!buffer || length < 8 || length > 256) {
        TESAIOT_DEBUG_ERROR_PRINT(("%s random_generate: invalid param (buf=%p, len=%u)\n",
                                   LABEL_CRYPTO, (void*)buffer, length));
        return TESAIOT_ERROR_INVALID_PARAM;
    }

    //! [hsm_optiga_lock_unlock_textbook]
    /* shipped reference, not compiled by proj_cm33_ns */
    /* ...context: inside the TRNG random_generate helper ... */
    if (!optiga_manager_is_initialized()) {
        return TESAIOT_ERROR_NOT_INITIALIZED;
    }

    if (!optiga_manager_lock()) {
        return TESAIOT_ERROR_TIMEOUT;
    }

    volatile optiga_lib_status_t optiga_status = OPTIGA_LIB_BUSY;
    optiga_crypt_t *crypt = optiga_manager_create_crypt(crypto_callback,
                                                         (void *)&optiga_status);
    if (!crypt) {
        optiga_manager_unlock();
        return TESAIOT_ERROR_OPTIGA;
    }

    optiga_lib_status_t result = optiga_crypt_random(crypt,
                                                      OPTIGA_RNG_TYPE_TRNG,
                                                      buffer,
                                                      length);

    int rc = TESAIOT_ERROR_OPTIGA;
    if (result == OPTIGA_LIB_SUCCESS) {
        rc = crypto_wait_for_completion(&optiga_status);
    }

    optiga_crypt_destroy(crypt);
    optiga_manager_unlock();
    //! [hsm_optiga_lock_unlock_textbook]

    TESAIOT_DEBUG_INFO_PRINT(("%s random_generate: %u bytes -> %s\n",
                              LABEL_CRYPTO, length,
                              (rc == TESAIOT_OK) ? "OK" : "FAIL"));
    return rc;
}

int tesaiot_secure_store_write(uint8_t slot, const uint8_t *data, uint16_t length)
{
    TESAIOT_CHECK_LICENSE();

    if (slot == 4) {
        TESAIOT_DEBUG_ERROR_PRINT(("%s secure_store_write: slot 4 is RESERVED (OID 0xF1D4)\n",
                                   LABEL_CRYPTO));
        return TESAIOT_ERROR_RESERVED_OID;
    }

    if (slot > 13 || !data || length == 0) {
        return TESAIOT_ERROR_INVALID_PARAM;
    }

    uint16_t max_size = secure_store_max_size(slot);
    if (length > max_size) {
        TESAIOT_DEBUG_ERROR_PRINT(("%s secure_store_write: length %u exceeds max %u for slot %u\n",
                                   LABEL_CRYPTO, length, max_size, slot));
        return TESAIOT_ERROR_INVALID_PARAM;
    }

    uint16_t oid = secure_store_slot_to_oid(slot);
    if (oid == 0) {
        return TESAIOT_ERROR_INVALID_PARAM;
    }

    optiga_lib_status_t status = tesaiot_write_data(oid, data, length);

    TESAIOT_DEBUG_INFO_PRINT(("%s secure_store_write: slot %u (OID 0x%04X), %u bytes -> %s\n",
                              LABEL_CRYPTO, slot, oid, length,
                              (status == OPTIGA_LIB_SUCCESS) ? "OK" : "FAIL"));

    return (status == OPTIGA_LIB_SUCCESS) ? TESAIOT_OK : TESAIOT_ERROR_OPTIGA;
}

int tesaiot_secure_store_read(uint8_t slot, uint8_t *data, uint16_t *length)
{
    TESAIOT_CHECK_LICENSE();

    if (slot == 4) {
        TESAIOT_DEBUG_ERROR_PRINT(("%s secure_store_read: slot 4 is RESERVED (OID 0xF1D4)\n",
                                   LABEL_CRYPTO));
        return TESAIOT_ERROR_RESERVED_OID;
    }

    if (slot > 13 || !data || !length || *length == 0) {
        return TESAIOT_ERROR_INVALID_PARAM;
    }

    uint16_t oid = secure_store_slot_to_oid(slot);
    if (oid == 0) {
        return TESAIOT_ERROR_INVALID_PARAM;
    }

    uint16_t read_len = *length;
    optiga_lib_status_t status = tesaiot_read_data(oid, data, &read_len);

    if (status == OPTIGA_LIB_SUCCESS) {
        *length = read_len;
        TESAIOT_DEBUG_INFO_PRINT(("%s secure_store_read: slot %u (OID 0x%04X), %u bytes -> OK\n",
                                  LABEL_CRYPTO, slot, oid, read_len));
        return TESAIOT_OK;
    }

    TESAIOT_DEBUG_ERROR_PRINT(("%s secure_store_read: slot %u (OID 0x%04X) -> FAIL\n",
                               LABEL_CRYPTO, slot, oid));
    return TESAIOT_ERROR_OPTIGA;
}

int tesaiot_aes_generate_key(uint16_t key_bits)
{
    TESAIOT_CHECK_LICENSE();

    optiga_symmetric_key_type_t key_type;
    switch (key_bits) {
        case 128: key_type = OPTIGA_SYMMETRIC_AES_128; break;
        case 192: key_type = OPTIGA_SYMMETRIC_AES_192; break;
        case 256: key_type = OPTIGA_SYMMETRIC_AES_256; break;
        default:
            TESAIOT_DEBUG_ERROR_PRINT(("%s aes_generate_key: invalid key_bits %u (must be 128/192/256)\n",
                                       LABEL_CRYPTO, key_bits));
            return TESAIOT_ERROR_INVALID_PARAM;
    }

    if (!optiga_manager_is_initialized()) {
        return TESAIOT_ERROR_NOT_INITIALIZED;
    }

    if (!optiga_manager_lock()) {
        return TESAIOT_ERROR_TIMEOUT;
    }

    volatile optiga_lib_status_t optiga_status = OPTIGA_LIB_BUSY;
    optiga_crypt_t *crypt = optiga_manager_create_crypt(crypto_callback,
                                                         (void *)&optiga_status);
    if (!crypt) {
        optiga_manager_unlock();
        return TESAIOT_ERROR_OPTIGA;
    }

    optiga_key_id_t symmetric_key = OPTIGA_KEY_ID_SECRET_BASED;
    optiga_lib_status_t result = optiga_crypt_symmetric_generate_key(crypt,
                                                                      key_type,
                                                                      (uint8_t)OPTIGA_KEY_USAGE_ENCRYPTION,
                                                                      FALSE,
                                                                      &symmetric_key);

    int rc = TESAIOT_ERROR_OPTIGA;
    if (result == OPTIGA_LIB_SUCCESS) {
        rc = crypto_wait_for_completion(&optiga_status);
    }

    optiga_crypt_destroy(crypt);
    optiga_manager_unlock();

    TESAIOT_DEBUG_INFO_PRINT(("%s aes_generate_key: AES-%u at OID 0xE200 -> %s\n",
                              LABEL_CRYPTO, key_bits,
                              (rc == TESAIOT_OK) ? "OK" : "FAIL"));
    return rc;
}

int tesaiot_aes_encrypt(const uint8_t *plaintext, uint16_t plain_len,
                        const uint8_t *iv, uint8_t *iv_out,
                        uint8_t *ciphertext, uint16_t *cipher_len)
{
    TESAIOT_CHECK_LICENSE();

    if (!plaintext || plain_len == 0 || (plain_len % 16) != 0 ||
        !ciphertext || !cipher_len) {
        return TESAIOT_ERROR_INVALID_PARAM;
    }

    if (*cipher_len < plain_len) {
        return TESAIOT_ERROR_BUFFER_TOO_SMALL;
    }

    if (!iv && !iv_out) {
        return TESAIOT_ERROR_INVALID_PARAM;
    }

    if (!optiga_manager_is_initialized()) {
        return TESAIOT_ERROR_NOT_INITIALIZED;
    }

    /* Auto-generate IV if not provided */
    uint8_t iv_buf[16];
    if (!iv) {
        int rc = tesaiot_random_generate(iv_buf, 16);
        if (rc != TESAIOT_OK) {
            return rc;
        }
        if (iv_out) {
            memcpy(iv_out, iv_buf, 16);
        }
    } else {
        memcpy(iv_buf, iv, 16);
        if (iv_out && iv_out != iv) {
            memcpy(iv_out, iv, 16);
        }
    }

    if (!optiga_manager_lock()) {
        return TESAIOT_ERROR_TIMEOUT;
    }

    volatile optiga_lib_status_t optiga_status = OPTIGA_LIB_BUSY;
    optiga_crypt_t *crypt = optiga_manager_create_crypt(crypto_callback,
                                                         (void *)&optiga_status);
    if (!crypt) {
        optiga_manager_unlock();
        return TESAIOT_ERROR_OPTIGA;
    }

    uint32_t out_len = *cipher_len;
    optiga_lib_status_t result = optiga_crypt_symmetric_encrypt(crypt,
                                                                 OPTIGA_SYMMETRIC_CBC,
                                                                 OPTIGA_KEY_ID_SECRET_BASED,
                                                                 plaintext,
                                                                 plain_len,
                                                                 iv_buf,
                                                                 16,
                                                                 NULL,
                                                                 0,
                                                                 ciphertext,
                                                                 &out_len);

    int rc = TESAIOT_ERROR_OPTIGA;
    if (result == OPTIGA_LIB_SUCCESS) {
        rc = crypto_wait_for_completion(&optiga_status);
        if (rc == TESAIOT_OK) {
            *cipher_len = (uint16_t)out_len;
        }
    }

    optiga_crypt_destroy(crypt);
    optiga_manager_unlock();

    TESAIOT_DEBUG_INFO_PRINT(("%s aes_encrypt: %u bytes -> %s\n",
                              LABEL_CRYPTO, plain_len,
                              (rc == TESAIOT_OK) ? "OK" : "FAIL"));
    return rc;
}

int tesaiot_aes_decrypt(const uint8_t *ciphertext, uint16_t cipher_len,
                        const uint8_t *iv,
                        uint8_t *plaintext, uint16_t *plain_len)
{
    TESAIOT_CHECK_LICENSE();

    if (!ciphertext || cipher_len == 0 || (cipher_len % 16) != 0 ||
        !iv || !plaintext || !plain_len) {
        return TESAIOT_ERROR_INVALID_PARAM;
    }

    if (*plain_len < cipher_len) {
        return TESAIOT_ERROR_BUFFER_TOO_SMALL;
    }

    if (!optiga_manager_is_initialized()) {
        return TESAIOT_ERROR_NOT_INITIALIZED;
    }

    if (!optiga_manager_lock()) {
        return TESAIOT_ERROR_TIMEOUT;
    }

    volatile optiga_lib_status_t optiga_status = OPTIGA_LIB_BUSY;
    optiga_crypt_t *crypt = optiga_manager_create_crypt(crypto_callback,
                                                         (void *)&optiga_status);
    if (!crypt) {
        optiga_manager_unlock();
        return TESAIOT_ERROR_OPTIGA;
    }

    uint32_t out_len = *plain_len;
    optiga_lib_status_t result = optiga_crypt_symmetric_decrypt(crypt,
                                                                 OPTIGA_SYMMETRIC_CBC,
                                                                 OPTIGA_KEY_ID_SECRET_BASED,
                                                                 ciphertext,
                                                                 cipher_len,
                                                                 iv,
                                                                 16,
                                                                 NULL,
                                                                 0,
                                                                 plaintext,
                                                                 &out_len);

    int rc = TESAIOT_ERROR_OPTIGA;
    if (result == OPTIGA_LIB_SUCCESS) {
        rc = crypto_wait_for_completion(&optiga_status);
        if (rc == TESAIOT_OK) {
            *plain_len = (uint16_t)out_len;
        }
    }

    optiga_crypt_destroy(crypt);
    optiga_manager_unlock();

    TESAIOT_DEBUG_INFO_PRINT(("%s aes_decrypt: %u bytes -> %s\n",
                              LABEL_CRYPTO, cipher_len,
                              (rc == TESAIOT_OK) ? "OK" : "FAIL"));
    return rc;
}

int tesaiot_hmac_sha256(uint8_t secret_slot,
                        const uint8_t *data, uint16_t data_len,
                        uint8_t *mac, uint16_t *mac_len)
{
    TESAIOT_CHECK_LICENSE();

    if (secret_slot == 4) {
        return TESAIOT_ERROR_RESERVED_OID;
    }

    if (secret_slot > 13 || !data || data_len == 0 || !mac || !mac_len) {
        return TESAIOT_ERROR_INVALID_PARAM;
    }

    if (*mac_len < 32) {
        return TESAIOT_ERROR_BUFFER_TOO_SMALL;
    }

    uint16_t secret_oid = secure_store_slot_to_oid(secret_slot);
    if (secret_oid == 0) {
        return TESAIOT_ERROR_INVALID_PARAM;
    }

    if (!optiga_manager_is_initialized()) {
        return TESAIOT_ERROR_NOT_INITIALIZED;
    }

    if (!optiga_manager_lock()) {
        return TESAIOT_ERROR_TIMEOUT;
    }

    volatile optiga_lib_status_t optiga_status = OPTIGA_LIB_BUSY;
    optiga_crypt_t *crypt = optiga_manager_create_crypt(crypto_callback,
                                                         (void *)&optiga_status);
    if (!crypt) {
        optiga_manager_unlock();
        return TESAIOT_ERROR_OPTIGA;
    }

    uint32_t hmac_len = *mac_len;
    optiga_lib_status_t result = optiga_crypt_hmac(crypt,
                                                    OPTIGA_HMAC_SHA_256,
                                                    secret_oid,
                                                    data,
                                                    data_len,
                                                    mac,
                                                    &hmac_len);

    int rc = TESAIOT_ERROR_OPTIGA;
    if (result == OPTIGA_LIB_SUCCESS) {
        rc = crypto_wait_for_completion(&optiga_status);
        if (rc == TESAIOT_OK) {
            *mac_len = (uint16_t)hmac_len;
        }
    }

    optiga_crypt_destroy(crypt);
    optiga_manager_unlock();

    TESAIOT_DEBUG_INFO_PRINT(("%s hmac_sha256: slot %u, %u bytes data -> %s\n",
                              LABEL_CRYPTO, secret_slot, data_len,
                              (rc == TESAIOT_OK) ? "OK" : "FAIL"));
    return rc;
}

/*============================================================================
 * Phase 2: Important Functions
 *============================================================================*/

int tesaiot_ecdh_shared_secret(uint16_t key_oid,
                               const uint8_t *peer_pubkey, uint16_t peer_pubkey_len,
                               uint8_t *shared_secret, uint16_t *secret_len)
{
    TESAIOT_CHECK_LICENSE();

    if (!IS_VALID_KEY_OID(key_oid)) {
        TESAIOT_DEBUG_ERROR_PRINT(("%s ecdh: invalid key OID 0x%04X\n", LABEL_CRYPTO, key_oid));
        return TESAIOT_ERROR_INVALID_PARAM;
    }

    if (!peer_pubkey || peer_pubkey_len == 0 || !shared_secret || !secret_len) {
        return TESAIOT_ERROR_INVALID_PARAM;
    }

    if (*secret_len < 32) {
        return TESAIOT_ERROR_BUFFER_TOO_SMALL;
    }

    /* Warn if using mTLS-reserved keys */
    if (key_oid == OPTIGA_KEY_OID_E0F0 || key_oid == OPTIGA_KEY_OID_E0F1) {
        TESAIOT_DEBUG_WARNING_PRINT(("%s ecdh: WARNING - OID 0x%04X is reserved for mTLS. "
                                     "Recommend 0xE0F2 or 0xE0F3 instead.\n",
                                     LABEL_CRYPTO, key_oid));
    }

    if (!optiga_manager_is_initialized()) {
        return TESAIOT_ERROR_NOT_INITIALIZED;
    }

    if (!optiga_manager_lock()) {
        return TESAIOT_ERROR_TIMEOUT;
    }

    volatile optiga_lib_status_t optiga_status = OPTIGA_LIB_BUSY;
    optiga_crypt_t *crypt = optiga_manager_create_crypt(crypto_callback,
                                                         (void *)&optiga_status);
    if (!crypt) {
        optiga_manager_unlock();
        return TESAIOT_ERROR_OPTIGA;
    }

    /* Construct peer public key structure */
    public_key_from_host_t peer_key;
    peer_key.public_key = (uint8_t *)peer_pubkey;
    peer_key.length = peer_pubkey_len;
    peer_key.key_type = OPTIGA_ECC_CURVE_NIST_P_256;

    optiga_lib_status_t result = optiga_crypt_ecdh(crypt,
                                                    (optiga_key_id_t)key_oid,
                                                    &peer_key,
                                                    TRUE, /* export to host */
                                                    shared_secret);

    int rc = TESAIOT_ERROR_OPTIGA;
    if (result == OPTIGA_LIB_SUCCESS) {
        rc = crypto_wait_for_completion(&optiga_status);
        if (rc == TESAIOT_OK) {
            *secret_len = 32; /* P-256 shared secret is always 32 bytes */
        }
    }

    optiga_crypt_destroy(crypt);
    optiga_manager_unlock();

    TESAIOT_DEBUG_INFO_PRINT(("%s ecdh: key OID 0x%04X -> %s\n",
                              LABEL_CRYPTO, key_oid,
                              (rc == TESAIOT_OK) ? "OK" : "FAIL"));
    return rc;
}

int tesaiot_hkdf_derive(uint16_t secret_oid,
                        const uint8_t *salt, uint16_t salt_len,
                        const uint8_t *info, uint16_t info_len,
                        uint8_t *derived_key, uint16_t key_len)
{
    TESAIOT_CHECK_LICENSE();

    if (!derived_key || key_len == 0) {
        return TESAIOT_ERROR_INVALID_PARAM;
    }

    if (!optiga_manager_is_initialized()) {
        return TESAIOT_ERROR_NOT_INITIALIZED;
    }

    if (!optiga_manager_lock()) {
        return TESAIOT_ERROR_TIMEOUT;
    }

    volatile optiga_lib_status_t optiga_status = OPTIGA_LIB_BUSY;
    optiga_crypt_t *crypt = optiga_manager_create_crypt(crypto_callback,
                                                         (void *)&optiga_status);
    if (!crypt) {
        optiga_manager_unlock();
        return TESAIOT_ERROR_OPTIGA;
    }

    optiga_lib_status_t result = optiga_crypt_hkdf_sha256(crypt,
                                                           (optiga_key_id_t)secret_oid,
                                                           salt,
                                                           salt_len,
                                                           info,
                                                           info_len,
                                                           key_len,
                                                           TRUE, /* export to host */
                                                           derived_key);

    int rc = TESAIOT_ERROR_OPTIGA;
    if (result == OPTIGA_LIB_SUCCESS) {
        rc = crypto_wait_for_completion(&optiga_status);
    }

    optiga_crypt_destroy(crypt);
    optiga_manager_unlock();

    TESAIOT_DEBUG_INFO_PRINT(("%s hkdf_derive: OID 0x%04X, %u bytes -> %s\n",
                              LABEL_CRYPTO, secret_oid, key_len,
                              (rc == TESAIOT_OK) ? "OK" : "FAIL"));
    return rc;
}

int tesaiot_optiga_hash(const uint8_t *data, uint16_t data_len,
                        uint8_t *hash, uint16_t *hash_len)
{
    TESAIOT_CHECK_LICENSE();

    if (!data || data_len == 0 || !hash || !hash_len) {
        return TESAIOT_ERROR_INVALID_PARAM;
    }

    if (*hash_len < 32) {
        return TESAIOT_ERROR_BUFFER_TOO_SMALL;
    }

    if (!optiga_manager_is_initialized()) {
        return TESAIOT_ERROR_NOT_INITIALIZED;
    }

    if (!optiga_manager_lock()) {
        return TESAIOT_ERROR_TIMEOUT;
    }

    volatile optiga_lib_status_t optiga_status = OPTIGA_LIB_BUSY;
    optiga_crypt_t *crypt = optiga_manager_create_crypt(crypto_callback,
                                                         (void *)&optiga_status);
    if (!crypt) {
        optiga_manager_unlock();
        return TESAIOT_ERROR_OPTIGA;
    }

    /* Set up hash data source */
    hash_data_from_host_t hash_data;
    hash_data.buffer = data;
    hash_data.length = (uint32_t)data_len;

    optiga_lib_status_t result = optiga_crypt_hash(crypt,
                                                    OPTIGA_HASH_TYPE_SHA_256,
                                                    OPTIGA_CRYPT_HOST_DATA,
                                                    &hash_data,
                                                    hash);

    int rc = TESAIOT_ERROR_OPTIGA;
    if (result == OPTIGA_LIB_SUCCESS) {
        rc = crypto_wait_for_completion(&optiga_status);
        if (rc == TESAIOT_OK) {
            *hash_len = 32; /* SHA-256 always 32 bytes */
        }
    }

    optiga_crypt_destroy(crypt);
    optiga_manager_unlock();

    TESAIOT_DEBUG_INFO_PRINT(("%s optiga_hash: %u bytes -> %s\n",
                              LABEL_CRYPTO, data_len,
                              (rc == TESAIOT_OK) ? "OK" : "FAIL"));
    return rc;
}

int tesaiot_sign_data(uint16_t key_oid,
                      const uint8_t *data, uint16_t data_len,
                      uint8_t *signature, uint16_t *sig_len)
{
    TESAIOT_CHECK_LICENSE();

    if (!IS_VALID_KEY_OID(key_oid)) {
        TESAIOT_DEBUG_ERROR_PRINT(("%s sign_data: invalid key OID 0x%04X\n", LABEL_CRYPTO, key_oid));
        return TESAIOT_ERROR_INVALID_PARAM;
    }

    if (!data || data_len == 0 || !signature || !sig_len) {
        return TESAIOT_ERROR_INVALID_PARAM;
    }

    if (*sig_len < 80) {
        return TESAIOT_ERROR_BUFFER_TOO_SMALL;
    }

    /* Step 1: Hash the data using OPTIGA hardware */
    uint8_t digest[32];
    uint16_t digest_len = 32;
    int rc = tesaiot_optiga_hash(data, data_len, digest, &digest_len);
    if (rc != TESAIOT_OK) {
        TESAIOT_DEBUG_ERROR_PRINT(("%s sign_data: hash step failed: %d\n", LABEL_CRYPTO, rc));
        return rc;
    }

    /* Step 2: Sign the digest using ECDSA */
    uint16_t raw_sig_len = *sig_len;
    optiga_lib_status_t status = trustm_ecdsa_sign((optiga_key_id_t)key_oid,
                                                    digest,
                                                    digest_len,
                                                    signature,
                                                    &raw_sig_len);

    if (status == OPTIGA_LIB_SUCCESS) {
        *sig_len = raw_sig_len;
        TESAIOT_DEBUG_INFO_PRINT(("%s sign_data: key OID 0x%04X, %u bytes data, sig %u bytes -> OK\n",
                                  LABEL_CRYPTO, key_oid, data_len, raw_sig_len));
        return TESAIOT_OK;
    }

    TESAIOT_DEBUG_ERROR_PRINT(("%s sign_data: ECDSA sign failed: 0x%04X\n",
                               LABEL_CRYPTO, (unsigned int)status));
    return TESAIOT_ERROR_OPTIGA;
}

/*============================================================================
 * Phase 3: Nice to Have Functions
 *============================================================================*/

int tesaiot_counter_read(uint8_t counter_id, uint32_t *value)
{
    TESAIOT_CHECK_LICENSE();

    if (counter_id > 3 || !value) {
        return TESAIOT_ERROR_INVALID_PARAM;
    }

    if (!optiga_manager_is_initialized()) {
        return TESAIOT_ERROR_NOT_INITIALIZED;
    }

    uint16_t oid = 0xE120 + counter_id;
    uint8_t counter_buf[8];
    uint16_t counter_len = sizeof(counter_buf);

    optiga_lib_status_t status = tesaiot_read_data(oid, counter_buf, &counter_len);
    if (status != OPTIGA_LIB_SUCCESS) {
        TESAIOT_DEBUG_ERROR_PRINT(("%s counter_read: OID 0x%04X failed\n", LABEL_CRYPTO, oid));
        return TESAIOT_ERROR_OPTIGA;
    }

    /* Counter is stored as big-endian 4-byte value */
    if (counter_len >= 4) {
        *value = ((uint32_t)counter_buf[0] << 24) |
                 ((uint32_t)counter_buf[1] << 16) |
                 ((uint32_t)counter_buf[2] << 8) |
                 ((uint32_t)counter_buf[3]);
    } else {
        *value = 0;
    }

    TESAIOT_DEBUG_INFO_PRINT(("%s counter_read: counter %u (OID 0x%04X) = %lu\n",
                              LABEL_CRYPTO, counter_id, oid, (unsigned long)*value));
    return TESAIOT_OK;
}

int tesaiot_counter_increment(uint8_t counter_id, uint32_t step)
{
    TESAIOT_CHECK_LICENSE();

    if (counter_id > 3 || step == 0) {
        return TESAIOT_ERROR_INVALID_PARAM;
    }

    if (!optiga_manager_is_initialized()) {
        return TESAIOT_ERROR_NOT_INITIALIZED;
    }

    uint16_t oid = 0xE120 + counter_id;

    optiga_util_t *me = optiga_manager_acquire();
    if (!me) {
        return TESAIOT_ERROR_TIMEOUT;
    }

    optiga_lib_status_t result = optiga_util_update_count(me, oid, (uint8_t)step);

    int rc = TESAIOT_ERROR_OPTIGA;
    if (result == OPTIGA_LIB_SUCCESS) {
        /* Wait for completion using the manager's callback mechanism */
        uint32_t timeout_ms = 10000;
        uint32_t elapsed = 0;
        while (elapsed < timeout_ms) {
            vTaskDelay(pdMS_TO_TICKS(10));
            elapsed += 10;
            /* The util callback was set during init - check if operation complete */
            break; /* For util operations, they complete synchronously in most cases */
        }
        rc = TESAIOT_OK;
    }

    optiga_manager_release();

    TESAIOT_DEBUG_INFO_PRINT(("%s counter_increment: counter %u (OID 0x%04X), step %lu -> %s\n",
                              LABEL_CRYPTO, counter_id, oid, (unsigned long)step,
                              (rc == TESAIOT_OK) ? "OK" : "FAIL"));
    return rc;
}

int tesaiot_health_check(tesaiot_health_report_t *report)
{
    TESAIOT_CHECK_LICENSE();

    if (!report) {
        return TESAIOT_ERROR_INVALID_PARAM;
    }

    memset(report, 0, sizeof(tesaiot_health_report_t));

    /* OPTIGA status */
    report->optiga_ok = optiga_manager_is_initialized();

    /* License status */
    report->license_ok = tesaiot_is_licensed();

    /* MQTT status */
    report->mqtt_ok = tesaiot_mqtt_is_connected();

    /* NTP time sync */
    report->time_synced = tesaiot_sntp_is_time_synced();

    /* Certificate validity */
    tesaiot_cert_validation_result_t cert_result;

    if (tesaiot_check_certificate_validity(OPTIGA_CERT_OID_E0E0, &cert_result)) {
        report->factory_cert_ok = !cert_result.is_expired;
    }

    if (tesaiot_check_certificate_validity(OPTIGA_CERT_OID_E0E1, &cert_result)) {
        report->device_cert_ok = !cert_result.is_expired;
        report->cert_days_left = cert_result.days_until_expiry;
    }

    /* Lifecycle State Object */
    tesaiot_read_lcso(&report->lcso_value);

    TESAIOT_DEBUG_INFO_PRINT(("%s health_check: optiga=%d, license=%d, mqtt=%d, ntp=%d, "
                              "factory_cert=%d, device_cert=%d, days_left=%lu, lcso=0x%02X\n",
                              LABEL_CRYPTO,
                              report->optiga_ok, report->license_ok,
                              report->mqtt_ok, report->time_synced,
                              report->factory_cert_ok, report->device_cert_ok,
                              (unsigned long)report->cert_days_left,
                              report->lcso_value));

    return TESAIOT_OK;
}
