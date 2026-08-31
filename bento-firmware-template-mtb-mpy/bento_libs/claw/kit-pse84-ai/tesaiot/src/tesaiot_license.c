/**
 * @file tesaiot_license.c
 * @brief TESAIoT Library License Implementation v2.1
 * @copyright (c) 2025 TESAIoT AIoT Foundation Platform
 *
 * License verification using ECDSA signature of OPTIGA Trust M UID.
 * Uses mbedTLS for signature verification (efficient, standard DER format).
 * Uses OPTIGA Trust M only for reading hardware UID.
 *
 * IMPORTANT: License data (UID + Key) provided by customer via extern variables.
 * Customer compiles tesaiot_license_data.c with their credentials.
 */

#include "tesaiot_license.h"
#include "optiga_util.h"
#include "optiga_lib_common.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"
#include "mbedtls/error.h"
#include "psa/crypto.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/*============================================================================
 * External License Data (provided by customer)
 *============================================================================*/

/**
 * Customer must provide these variables via tesaiot_license_data.c
 * Example:
 *   extern const char* tesaiot_device_uid = "CD163393...";
 *   extern const char* tesaiot_license_key = "MEUCIEjU...";
 */
extern const char* tesaiot_device_uid;
extern const char* tesaiot_license_key;

/*============================================================================
 * EMBEDDED PUBLIC KEY - From TESAIoT Server (DO NOT MODIFY)
 *
 * This is the ECDSA P-256 public key used to verify license signatures.
 * Only TESAIoT Library team should update this when server key changes.
 *
 * Format: PEM (-----BEGIN PUBLIC KEY-----)
 * Get this from TESAIoT Server team after they generate the signing keypair.
 *============================================================================*/

/* TESAIoT License Signing Public Key - DO NOT MODIFY
 * Generated: 2026-01-14
 * Key ID: tesaiot-license-v1
 */
#define TESAIOT_LICENSE_PUBLIC_KEY_PEM \
"-----BEGIN PUBLIC KEY-----\n" \
"MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE2HXVq7VVnsAo7GrIVOwG3PMy1HjW\n" \
"K3Y+TVJ1MgzZ3SObILI5XMEFo5YtZi2ScqC9JeiUvIoZIkd8yzZrnZvWIQ==\n" \
"-----END PUBLIC KEY-----\n"

/* OID for OPTIGA Trust M Coprocessor UID */
#define OPTIGA_COPROCESSOR_UID_OID  0xE0C2
#define OPTIGA_UID_LENGTH           27

/*============================================================================
 * Private Variables
 *============================================================================*/

static bool g_license_initialized = false;
static bool g_license_valid = false;
static uint8_t g_device_uid[OPTIGA_UID_LENGTH];
static uint16_t g_device_uid_len = 0;

/*============================================================================
 * OPTIGA Callback
 *============================================================================*/

static volatile optiga_lib_status_t g_optiga_status;

static void optiga_callback(void* context, optiga_lib_status_t status)
{
    (void)context;
    g_optiga_status = status;
}

static void wait_for_optiga(void)
{
    while (g_optiga_status == OPTIGA_LIB_BUSY) {
        /* Wait - in production, add timeout */
    }
}

/*============================================================================
 * Utility Functions - Hex String Parsing
 *============================================================================*/

/**
 * @brief Convert hex character to value
 */
static int hex_char_to_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/**
 * @brief Parse hex string to bytes
 * @param hex_str Input hex string (e.g., "CD163301...")
 * @param out_buf Output buffer
 * @param out_len Expected output length in bytes
 * @return true on success, false on error
 */
static bool parse_hex_string(const char* hex_str, uint8_t* out_buf, size_t out_len)
{
    if (hex_str == NULL || out_buf == NULL) {
        return false;
    }

    size_t hex_len = strlen(hex_str);
    if (hex_len != out_len * 2) {
        return false;  /* Hex string must be exactly 2x output length */
    }

    for (size_t i = 0; i < out_len; i++) {
        int hi = hex_char_to_val(hex_str[i * 2]);
        int lo = hex_char_to_val(hex_str[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return false;  /* Invalid hex character */
        }
        out_buf[i] = (uint8_t)((hi << 4) | lo);
    }

    return true;
}

/*============================================================================
 * Utility Functions - Base64 Decoding
 *============================================================================*/

static const char BASE64_CHARS[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int base64_char_to_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    if (c == '=') return 0;  /* Padding */
    return -1;
}

/**
 * @brief Decode Base64 string to bytes
 * @param b64_str Input Base64 string
 * @param out_buf Output buffer
 * @param out_max Maximum output buffer size
 * @param out_len Actual decoded length
 * @return true on success, false on error
 */
static bool parse_base64(const char* b64_str, uint8_t* out_buf, size_t out_max, size_t* out_len)
{
    if (b64_str == NULL || out_buf == NULL || out_len == NULL) {
        return false;
    }

    size_t b64_len = strlen(b64_str);
    if (b64_len == 0 || b64_len % 4 != 0) {
        return false;  /* Base64 length must be multiple of 4 */
    }

    /* Calculate output length */
    size_t padding = 0;
    if (b64_str[b64_len - 1] == '=') padding++;
    if (b64_str[b64_len - 2] == '=') padding++;

    size_t decoded_len = (b64_len / 4) * 3 - padding;
    if (decoded_len > out_max) {
        return false;  /* Output buffer too small */
    }

    size_t j = 0;
    for (size_t i = 0; i < b64_len; i += 4) {
        int v0 = base64_char_to_val(b64_str[i]);
        int v1 = base64_char_to_val(b64_str[i + 1]);
        int v2 = base64_char_to_val(b64_str[i + 2]);
        int v3 = base64_char_to_val(b64_str[i + 3]);

        if (v0 < 0 || v1 < 0 || v2 < 0 || v3 < 0) {
            return false;
        }

        if (j < decoded_len) out_buf[j++] = (uint8_t)((v0 << 2) | (v1 >> 4));
        if (j < decoded_len) out_buf[j++] = (uint8_t)((v1 << 4) | (v2 >> 2));
        if (j < decoded_len) out_buf[j++] = (uint8_t)((v2 << 6) | v3);
    }

    *out_len = decoded_len;
    return true;
}

/*============================================================================
 * Public Key Validation
 *============================================================================*/

/**
 * @brief Check if public key is placeholder
 */
static bool is_public_key_placeholder(void)
{
    const char* pem = TESAIOT_LICENSE_PUBLIC_KEY_PEM;
    if (strstr(pem, "AAAAAAAAAAAAAAAA") != NULL) {
        return true;  /* Placeholder pattern detected */
    }
    return false;
}

/*============================================================================
 * OPTIGA Functions
 *============================================================================*/

/**
 * @brief Read OPTIGA Trust M Factory UID
 */
static bool read_optiga_uid(uint8_t* uid_buf, uint16_t* uid_len)
{
    optiga_util_t* util_instance = NULL;
    bool result = false;

    util_instance = optiga_util_create(0, optiga_callback, NULL);
    if (util_instance == NULL) {
        return false;
    }

    g_optiga_status = OPTIGA_LIB_BUSY;

    optiga_lib_status_t status = optiga_util_read_data(
        util_instance,
        OPTIGA_COPROCESSOR_UID_OID,
        0x0000,
        uid_buf,
        uid_len
    );

    if (status == OPTIGA_LIB_SUCCESS) {
        wait_for_optiga();
        if (g_optiga_status == OPTIGA_LIB_SUCCESS) {
            result = true;
        }
    }

    optiga_util_destroy(util_instance);
    return result;
}

/*============================================================================
 * License Verification
 *============================================================================*/

/**
 * @brief Check if config has placeholder values
 */
static bool is_config_placeholder(void)
{
    /* Check for common placeholder patterns */
    if (strcmp(tesaiot_device_uid, "YOUR_DEVICE_UID_HERE") == 0) {
        return true;
    }
    if (strcmp(tesaiot_license_key, "YOUR_LICENSE_KEY_HERE") == 0) {
        return true;
    }
    if (strlen(tesaiot_device_uid) == 0 || strlen(tesaiot_license_key) == 0) {
        return true;
    }
    return false;
}

/**
 * @brief Compare UIDs with constant-time comparison (timing attack protection)
 */
static bool constant_time_compare(const uint8_t* a, const uint8_t* b, size_t len)
{
    uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) {
        diff |= a[i] ^ b[i];
    }
    return (diff == 0);
}

/**
 * @brief Verify ECDSA signature using mbedTLS (software)
 *
 * Verifies that the license key (ECDSA signature) was created by TESAIoT Server
 * for this specific device UID.
 *
 * Uses mbedTLS for verification - efficient, supports standard DER format,
 * and doesn't require OPTIGA format conversions.
 *
 * @param uid Device UID (27 bytes)
 * @param uid_len Length of UID
 * @param signature ECDSA signature (DER encoded)
 * @param sig_len Length of signature
 * @return true if signature is valid, false otherwise
 */
static bool verify_license_signature(const uint8_t* uid, size_t uid_len,
                                     const uint8_t* signature, size_t sig_len)
{
    mbedtls_pk_context pk;
    unsigned char hash[32];
    int ret;
    bool result = false;
    psa_status_t psa_status;

    /* Check if public key is placeholder.
     *
     * This path exists so a board can run before its licence key generation is
     * set up. It is also a licence bypass: the check is a substring test for
     * "AAAAAAAAAAAAAAAA" in TESAIOT_LICENSE_PUBLIC_KEY_PEM, and that PEM lives
     * in a header the integrator supplies. Anyone holding the library can
     * therefore authorise every device by editing one header — no signature,
     * no OPTIGA UID, no key.
     *
     * That is acceptable on a bench and not acceptable in anything shipped, so
     * a release build refuses instead of passing. The distributed archive is
     * always compiled with -DTESAIOT_RELEASE_BUILD=1
     * (TESAIoT_Firmware_Development_and_Release/bento-release.sh), and its
     * `doctor` command fails the build if this guard is ever removed.
     *
     * Development builds are unchanged: same warning, same permissive return.
     */
    if (is_public_key_placeholder()) {
#if defined(TESAIOT_RELEASE_BUILD) && (TESAIOT_RELEASE_BUILD == 1)
        printf("[TESAIoT] ERROR: placeholder public key in a release build - refusing\n");
        return false;
#else
        printf("[TESAIoT] WARNING: Using placeholder public key - verification skipped\n");
        return true;  /* development/bench only; see the release guard above */
#endif
    }

    /* Initialize PSA Crypto - required when MBEDTLS_USE_PSA_CRYPTO is enabled */
    psa_status = psa_crypto_init();
    if (psa_status != PSA_SUCCESS) {
        printf("[TESAIoT] ERROR: psa_crypto_init failed: %d\n", (int)psa_status);
        return false;
    }

    /* Initialize PK context */
    mbedtls_pk_init(&pk);

    /* Parse the PEM public key directly - mbedTLS handles all formats */
    ret = mbedtls_pk_parse_public_key(&pk,
                                       (const unsigned char*)TESAIOT_LICENSE_PUBLIC_KEY_PEM,
                                       strlen(TESAIOT_LICENSE_PUBLIC_KEY_PEM) + 1);
    if (ret != 0) {
        printf("[TESAIoT] ERROR: Failed to parse public key: -0x%04X\n", (unsigned)-ret);
        goto cleanup;
    }

    /* Hash the UID with SHA-256 */
    ret = mbedtls_sha256(uid, uid_len, hash, 0);  /* 0 = SHA-256 (not SHA-224) */
    if (ret != 0) {
        printf("[TESAIoT] ERROR: SHA-256 hash failed: -0x%04X\n", (unsigned)-ret);
        goto cleanup;
    }

    /* Verify ECDSA signature - mbedTLS accepts standard DER format directly */
    ret = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, sizeof(hash),
                            signature, sig_len);
    if (ret != 0) {
        printf("[TESAIoT] License verification FAILED: -0x%04X\n", (unsigned)-ret);
        goto cleanup;
    }

    /* Signature verified successfully! */
    result = true;

cleanup:
    mbedtls_pk_free(&pk);
    return result;
}

/*============================================================================
 * Public API Functions
 *============================================================================*/

tesaiot_license_status_t tesaiot_license_init(void)
{
    uint8_t configured_uid[OPTIGA_UID_LENGTH];
    uint8_t license_signature[128];  /* ECDSA signature (DER or raw) */
    size_t sig_len = 0;

    /* Already initialized? */
    if (g_license_initialized) {
        return g_license_valid ? TESAIOT_LICENSE_OK : TESAIOT_LICENSE_INVALID_UID;
    }

    printf("[TESAIoT] License verification v%s\n", TESAIOT_VERSION_STRING);

    /* Step 1: Check if config is placeholder */
    if (is_config_placeholder()) {
        printf("[TESAIoT] License not configured - use Menu 1 to get your Device UID\n");
        g_license_initialized = true;
        g_license_valid = false;
        return TESAIOT_LICENSE_INVALID_CONFIG;
    }

    /* Step 2: Parse configured UID from hex string */
    if (!parse_hex_string(tesaiot_device_uid, configured_uid, OPTIGA_UID_LENGTH)) {
        printf("[TESAIoT] Invalid UID format in tesaiot_license_config.h\n");
        printf("[TESAIoT] Expected: 54 hex characters (27 bytes)\n");
        g_license_initialized = true;
        g_license_valid = false;
        return TESAIOT_LICENSE_INVALID_KEY;
    }

    /* Step 3: Parse license key from Base64 */
    if (!parse_base64(tesaiot_license_key, license_signature, sizeof(license_signature), &sig_len)) {
        printf("[TESAIoT] Invalid license key format in tesaiot_license_config.h\n");
        printf("[TESAIoT] Expected: Base64-encoded ECDSA signature\n");
        g_license_initialized = true;
        g_license_valid = false;
        return TESAIOT_LICENSE_INVALID_KEY;
    }

    /* Step 4: Read actual UID from OPTIGA Trust M */
    g_device_uid_len = sizeof(g_device_uid);
    if (!read_optiga_uid(g_device_uid, &g_device_uid_len)) {
        printf("[TESAIoT] Failed to read OPTIGA Trust M UID\n");
        g_license_initialized = true;
        g_license_valid = false;
        return TESAIOT_LICENSE_ERROR_OPTIGA;
    }

    /* Step 5: Verify configured UID matches actual device UID */
    if (g_device_uid_len != OPTIGA_UID_LENGTH ||
        !constant_time_compare(g_device_uid, configured_uid, OPTIGA_UID_LENGTH)) {
        printf("\n[TESAIoT] License Error: Device UID mismatch!\n");
        printf("[TESAIoT] Configured UID: %s\n", tesaiot_device_uid);
        printf("[TESAIoT] Actual UID:     ");
        for (uint16_t i = 0; i < g_device_uid_len; i++) {
            printf("%02X", g_device_uid[i]);
        }
        printf("\n[TESAIoT] Please update tesaiot_license_config.h with correct UID\n\n");
        g_license_initialized = true;
        g_license_valid = false;
        return TESAIOT_LICENSE_INVALID_UID;
    }

    /* Step 6: Verify ECDSA signature */
    if (!verify_license_signature(configured_uid, OPTIGA_UID_LENGTH,
                                  license_signature, sig_len)) {
        printf("[TESAIoT] License key verification failed!\n");
        printf("[TESAIoT] Please get a valid license key from TESAIoT Server\n");
        g_license_initialized = true;
        g_license_valid = false;
        return TESAIOT_LICENSE_INVALID_KEY;
    }

    /* License valid! */
    g_license_initialized = true;
    g_license_valid = true;

    printf("[TESAIoT] License verified successfully!\n");
    printf("[TESAIoT] Device UID: %s\n", tesaiot_device_uid);

    return TESAIOT_LICENSE_OK;
}

bool tesaiot_is_licensed(void)
{
    return g_license_valid;
}

bool tesaiot_get_device_uid(uint8_t* uid_buf, uint16_t* uid_len)
{
    if (uid_buf == NULL || uid_len == NULL) {
        return false;
    }

    /* If already read, return cached value */
    if (g_device_uid_len > 0) {
        if (*uid_len < g_device_uid_len) {
            return false;
        }
        memcpy(uid_buf, g_device_uid, g_device_uid_len);
        *uid_len = g_device_uid_len;
        return true;
    }

    /* Read from OPTIGA - this works without license! */
    return read_optiga_uid(uid_buf, uid_len);
}

void tesaiot_print_device_uid(void)
{
    uint8_t uid[OPTIGA_UID_LENGTH];
    uint16_t uid_len = sizeof(uid);

    printf("\n");
    printf("============================================================\n");
    printf("  TESAIoT Device Registration\n");
    printf("  Library Version: %s\n", TESAIOT_VERSION_STRING);
    printf("============================================================\n\n");

    if (tesaiot_get_device_uid(uid, &uid_len)) {
        printf("Your OPTIGA Trust M UID:\n\n");

        /* Print as continuous hex string (for copy-paste) */
        printf("  ");
        for (uint16_t i = 0; i < uid_len; i++) {
            printf("%02X", uid[i]);
        }
        printf("\n\n");

        printf("Next steps:\n");
        printf("  1. Copy the UID above\n");
        printf("  2. Register your device on TESAIoT Server\n");
        printf("  3. Download tesaiot_license_config.h\n");
        printf("  4. Place it in proj_cm33_ns/ folder\n");
        printf("  5. Rebuild and flash\n");
    } else {
        printf("ERROR: Failed to read OPTIGA Trust M UID\n");
        printf("Please check OPTIGA Trust M connection.\n");
    }

    printf("\n============================================================\n\n");
}

const char* tesaiot_license_status_str(tesaiot_license_status_t status)
{
    switch (status) {
        case TESAIOT_LICENSE_OK:
            return "License valid";
        case TESAIOT_LICENSE_NOT_INITIALIZED:
            return "License not initialized";
        case TESAIOT_LICENSE_INVALID_UID:
            return "Device UID mismatch";
        case TESAIOT_LICENSE_ERROR_OPTIGA:
            return "Failed to read OPTIGA Trust M";
        case TESAIOT_LICENSE_INVALID_KEY:
            return "License key verification failed";
        case TESAIOT_LICENSE_INVALID_CONFIG:
            return "License not configured";
        default:
            return "Unknown license status";
    }
}

uint32_t tesaiot_get_version(void)
{
    return TESAIOT_VERSION_INT;
}

const char* tesaiot_get_version_string(void)
{
    return TESAIOT_VERSION_STRING;
}
