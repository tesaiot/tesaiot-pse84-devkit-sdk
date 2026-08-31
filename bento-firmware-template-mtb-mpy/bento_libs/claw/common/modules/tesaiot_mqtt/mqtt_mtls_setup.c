/******************************************************************************
* File Name:   mqtt_mtls_setup.c
*
* Description: mTLS + OPTIGA Trust M setup orchestrator.
*              Reads device cert from OPTIGA, registers PSA SE driver,
*              binds hardware key to TLS stack for mTLS handshake.
*
*              Private key NEVER leaves the OPTIGA secure element.
******************************************************************************/

#include "mqtt_mtls_setup.h"
#include "optiga_trust_helpers.h"
#include "optiga_psa_se.h"
#include "cy_tls_client_cert.h"
#include "cy_tls_optiga_key.h"
#include "tesaiot_config_store.h"
#include "psa/crypto.h"

#include <stdio.h>
#include <string.h>

/* Touch pause/resume — defined in per-project ipc_hsm_handler.c */
/* The counted hold in tesaiot_optiga_manager.c. This file used to pause and
 * resume touch directly while the console path used the counter, so a resume
 * here could lift a pause the console still needed — two mechanisms driving one
 * piece of state. Everything goes through the counter now. */
extern void optiga_manager_touch_hold_reason(const char *reason);
extern void optiga_manager_touch_release(void);

/* Weak: only the projects that have grown a reason-carrying pause define it.
 * Everywhere else the plain pause is used and CM55 shows a generic message. */

static void touch_pause_for(const char *reason)
{
    optiga_manager_touch_hold_reason(reason);
}

/* Declared here rather than by including tesaiot_optiga_manager.h, for the same
 * reason modoptiga.c does it: that header pulls the whole TESAIoT config surface
 * into a file six projects compile. */
extern bool optiga_manager_init(callback_handler_t callback, void *context);
extern void optiga_util_callback(void *context, optiga_lib_status_t return_status);

/* PSA_KEY_LOCATION_OPTIGA is defined in optiga_psa_se.c (not exported via header).
 * Must match the value used in optiga_psa_register() — see optiga_psa_se.c:17 */
#ifndef PSA_KEY_LOCATION_OPTIGA
#define PSA_KEY_LOCATION_OPTIGA  ((psa_key_location_t) 1)
#endif

/******************************************************************************
* Static State — persists across TLS sessions until mqtt_mtls_cleanup()
******************************************************************************/
static char         s_client_cert_pem[2048];
static uint16_t     s_client_cert_pem_len = 0;
static psa_key_id_t s_psa_key_id = 0;
static bool         s_mtls_initialized = false;

/******************************************************************************
* mqtt_mtls_setup_optiga
******************************************************************************/
bool mqtt_mtls_setup_optiga(void)
{
    if (s_mtls_initialized) {
        printf("[mTLS] Already initialized\n");
        return true;
    }

    /* Reconnect fast path.
     *
     * mqtt_mtls_cleanup() unbinds from the TLS stack but deliberately keeps the
     * PSA key handle and the certificate — see the note there. Everything the
     * OPTIGA side needs is therefore still valid; only the TLS-side binding was
     * dropped. Re-bind and return, without touching the chip.
     *
     * This is also what makes reconnect work at all. Regenerating the PSA key on
     * a second attempt fails with PSA_ERROR_INVALID_ARGUMENT (-135), inside
     * psa_generate_key and before the driver's p_generate is ever reached, so no
     * amount of retrying recovered — only a power cycle did. Since the key lives
     * in OPTIGA hardware there was never anything to regenerate. */
    if (s_psa_key_id != 0 && s_client_cert_pem_len != 0) {
        /* Only if the handle still means something.
         *
         * cy_tls_deinit() calls mbedtls_psa_crypto_free() when the MQTT client
         * is torn down, and that wipes every volatile key — ours is volatile by
         * design, since it does not hold key material, only the name of a key
         * that lives in the chip. Re-binding a handle to a key the store has
         * forgotten made mbedtls_pk_setup_opaque() fail with
         * MBEDTLS_ERR_PK_BAD_INPUT_DATA (-0x3E80), which reached the caller as a
         * TLS handshake failure and read like a server-side rejection.
         *
         * Observed 2026-08-05: the first connection of a boot worked and every
         * one after a disconnect failed, three retries at a time. */
        psa_key_attributes_t probe = PSA_KEY_ATTRIBUTES_INIT;
        if (psa_get_key_attributes(s_psa_key_id, &probe) != PSA_SUCCESS) {
            printf("[mTLS] PSA key %lu no longer exists — the TLS teardown wiped "
                   "the key store; setting up again\n", (unsigned long)s_psa_key_id);
            /* The driver's slot map still describes those keys: PSA wipes its
             * store without calling p_destroy. Left alone it would hand out a
             * fresh slot per reconnection and run out after four. */
            optiga_psa_forget_all_slots();
            s_psa_key_id = 0;
            s_client_cert_pem_len = 0;
            goto full_setup;
        }
        psa_reset_key_attributes(&probe);

        printf("[mTLS] Re-binding existing OPTIGA key (key_id=%lu, cert=%u bytes)\n",
               (unsigned long)s_psa_key_id, s_client_cert_pem_len);
        int rb = cy_tls_set_client_cert((const uint8_t *)s_client_cert_pem,
                                        s_client_cert_pem_len + 1, 1 /* is_pem */);
        if (rb != 0) {
            printf("[mTLS] re-bind cy_tls_set_client_cert failed: %d\n", rb);
            return false;
        }
        cy_tls_set_optiga_key_id(s_psa_key_id);
        s_mtls_initialized = true;
        return true;
    }

full_setup:;
    tesaiot_config_t cfg;
    tesaiot_config_get(&cfg);

    if (cfg.tls_mode != TESAIOT_MODE_MTLS) {
        return false;
    }

    /* Bootstrap on the Infineon factory pair, as the reference firmware does.
     *
     * This used to try the device pair (0xE0E1/0xE0F1) first and fall back to
     * the factory pair if the read came back empty. That fallback can never
     * run: reading an unprovisioned slot does not return zero bytes, it never
     * completes, and the wait inside read_certificate_from_optiga() spins
     * without a timeout — so a board that has not been enrolled yet hangs
     * CM33_NS instead of falling back. Observed 2026-08-04 on a Dev Kit whose
     * 0xE0E1 is empty.
     *
     * official_pse84_trustm_mTLS_tesaiot reads 0xE0E0 unconditionally at boot
     * (main.c:493) for the same reason, and tesaiot_select_mqtt_certificate()
     * there (tesaiot_optiga_trust_m.c:1349-1374) forces the factory pair even
     * when a device cert exists, because after a reset the key in 0xE0F1 may no
     * longer match the certificate in 0xE0E1 — which is exactly what happens
     * once a CSR has generated a fresh key. Until enrolment installs a matching
     * pair and something proves the match, the factory pair is the only one
     * that can be trusted to work.
     *
     * Per the Infineon pre-provisioning map, 0xE0E0/0xE0F0 are the IFX-
     * provisioned certificate and key; 0xE0E1/0xE0F1 are TESAIoT's device pair;
     * 0xE0E9 holds the TESA CA. */
    uint16_t cert_oid = 0xE0E0;  /* IFX-provisioned factory certificate */
    uint16_t key_oid  = 0xE0F0;  /* IFX-provisioned factory key         */

    printf("[mTLS] Setting up OPTIGA Trust M (cert=0x%04X, key=0x%04X)\n",
           cert_oid, key_oid);

    /* (1) Pause CM55 touch — shared I2C bus with the secure element */
    touch_pause_for("Preparing secure element for mTLS");

    /* (1a) Open the OPTIGA application.
     *
     * read_certificate_from_optiga() below only does optiga_util_create() plus
     * optiga_util_read_data() — it assumes somebody already opened the
     * application. In the reference firmware somebody has: optiga_trust_init()
     * runs once at boot and leaves it open for the life of the process. This
     * firmware never does — ipc_hsm_handler.c and modoptiga.c each open and
     * close around their own operations, and neither runs before this.
     *
     * Reading with the application closed is not a clean failure. The read
     * never completes, optiga_lib_status stays OPTIGA_LIB_BUSY, and the wait
     * inside read_certificate_from_optiga() is a bare `while (optiga_lib_status
     * == OPTIGA_LIB_BUSY);` with no yield and no timeout — CM33_NS hangs for
     * good, taking the REPL and every CM33 sensor with it. Observed 2026-08-04:
     * output stopped after the "Setting up OPTIGA Trust M" line and never
     * resumed, reproducibly, from a cold boot.
     *
     * optiga_trust_init() is the carried-over opener and is safe to call again;
     * it creates its own instance and opens the application. */
    if (!optiga_trust_init()) {
        printf("[mTLS] OPTIGA application would not open — aborting mTLS setup\n");
        optiga_manager_touch_release();
        return false;
    }

    /* (1b) Bring up the OPTIGA manager singleton.
     *
     * optiga_trust_init() opens the application; it does not create the manager.
     * trustm_ecdsa_sign() — the only thing that can produce the TLS
     * CertificateVerify signature, since the private key never leaves the chip —
     * starts with optiga_manager_lock(), which fails outright when the manager
     * was never initialised. It then returns 0x0001 without ever addressing the
     * secure element, optiga_psa_sign() turns that into
     * PSA_ERROR_HARDWARE_FAILURE, and mbedTLS reports -0x4F80 from the
     * handshake.
     *
     * Observed 2026-08-04: every TLS handshake reached CertificateVerify and
     * stopped there. A server-side capture showed the client Certificate and
     * ClientKeyExchange arriving normally and then the board closing the
     * connection with no CertificateVerify and no alert from either side.
     *
     * modoptiga.c does this for optiga.csr(); nothing on the mTLS path did.
     * The call only creates a mutex and a util instance and is idempotent. */
    if (!optiga_manager_init(optiga_util_callback, NULL)) {
        printf("[mTLS] optiga_manager_init failed — signing would be impossible\n");
        optiga_manager_touch_release();
        return false;
    }

    /* (1c) Choose WHICH identity to present, now that the chip can answer.
     *
     * This check must come after optiga_trust_init(): with the application
     * closed, any read spins in a bare `while (status == BUSY)` with no timeout
     * and CM33_NS never comes back. Placed before the open, it hung the board
     * from a cold boot — the same failure (1a) describes, reproduced 2026-08-31
     * by putting it two hundred lines too early.
     */
    /* The factory pair above is the safe default, unless the device pair can be PROVEN to match, which is the condition
     * the paragraph above names and could not test when it was written.
     *
     * optiga_verify_cert_key_pair() signs a digest with the key and checks the
     * signature against the public key inside the certificate. It answers the
     * one question the old fallback could not: are these two the same identity.
     * It is also safe on an unenrolled board — it asks the chip a bounded
     * question rather than reading a slot that may never return, which is what
     * made the previous attempt hang.
     *
     * This matters beyond tidiness. The TESAIoT API authenticates a client by
     * its certificate, and the factory pair is issued by Infineon, not by
     * TESAIoT MCU CA — presenting it gets AUTH_MISSING from the platform.
     * Measured on a Dev Kit 2026-08-31: verify_pair(0xE0E1, 0xE0F1) is true,
     * and the device certificate reads as CN = the configured device_id,
     * issued by CN = TESAIoT MCU CA. */
    if (optiga_verify_cert_key_pair(0xE0E1, 0xE0F1) == 1) {
        cert_oid = 0xE0E1;
        key_oid  = 0xE0F1;
        printf("[mTLS] device pair verified — using TESAIoT identity\n");
    } else {
        printf("[mTLS] device pair unproven — using Infineon factory identity\n");
    }

    /* (2) Read client certificate from OPTIGA */
    s_client_cert_pem_len = sizeof(s_client_cert_pem);
    read_certificate_from_optiga(cert_oid, s_client_cert_pem, &s_client_cert_pem_len);

    /* No second attempt to fall back to: 0xE0E0 is already the slot being read,
     * and every Trust M leaves the factory with it populated. If it is empty the
     * part is not what we think it is, and retrying another unprovisioned slot
     * would hang rather than fail. */
    if (s_client_cert_pem_len == 0 || s_client_cert_pem_len >= sizeof(s_client_cert_pem)) {
        printf("[mTLS] No usable certificate in OID 0x%04X (got %u bytes)\n",
               cert_oid, s_client_cert_pem_len);
        optiga_manager_touch_release();
        return false;
    }

    printf("[mTLS] Certificate read: %u bytes PEM\n", s_client_cert_pem_len);

    /* (3) Parse cert into TLS stack */
    int ret = cy_tls_set_client_cert((const uint8_t *)s_client_cert_pem,
                                      s_client_cert_pem_len + 1, /* +1 for NUL (PEM) */
                                      1 /* is_pem */);
    if (ret != 0) {
        printf("[mTLS] cy_tls_set_client_cert failed: %d\n", ret);
        optiga_manager_touch_release();
        return false;
    }

    /* (4) Rebuild the PSA world, in the order PSA requires.
     *
     * main.c registers the OPTIGA secure-element driver and then initialises PSA
     * once at boot, and that used to be enough. It is not: mbedtls_psa_crypto_free()
     * in cy_tls_deinit() does not only wipe volatile keys, it also calls
     * psa_unregister_all_se_drivers(). After the first MQTT teardown there is no
     * driver behind PSA_KEY_LOCATION_OPTIGA at all, so generating the key again
     * would have nowhere to go.
     *
     * The driver must go back before psa_crypto_init(), which is why main.c does
     * it in that order too. Both calls tolerate being made when they are not
     * needed — a driver already registered, or PSA already initialised — so this
     * is safe on the first connection as well as every one after a teardown. */
    psa_status_t reg = optiga_psa_register();
    if (reg != PSA_SUCCESS) {
        printf("[mTLS] optiga_psa_register: %d (already registered is fine)\n",
               (int)reg);
    }

    psa_status_t psa_ret = psa_crypto_init();
    if (psa_ret != PSA_SUCCESS) {
        printf("[mTLS] psa_crypto_init failed: %d\n", (int)psa_ret);
        optiga_manager_touch_release();
        return false;
    }

    /* (5) Set signing key OID */
    optiga_psa_set_signing_key_oid((optiga_key_id_t)key_oid);

    /* (6) Create PSA key handle (opaque — references OPTIGA hardware key) */
    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attr, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attr, 256);
    psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_SIGN_HASH);
    /* Any hash, not SHA-256 alone.
     *
     * TLS 1.2 does not pick the CertificateVerify hash from the client key's
     * curve. mbedTLS ties it to the negotiated ciphersuite's PRF and says so in
     * ssl_tls12_client.c ("Taking shortcut here"): a SHA-384 ciphersuite signs
     * CertificateVerify with SHA-384 even for a P-256 key. Against
     * mqtt.tesaiot.dev the server selects exactly such a suite, so a policy
     * naming only PSA_ALG_ECDSA(PSA_ALG_SHA_256) makes psa_sign_hash() refuse
     * with PSA_ERROR_NOT_PERMITTED before the OPTIGA driver is reached, and the
     * handshake dies at CertificateVerify with -0x4F80.
     *
     * PSA_ALG_ANY_HASH is a policy wildcard: it permits ECDSA with whichever
     * hash the handshake settles on. It deliberately does not cover
     * deterministic ECDSA (RFC 6979), which OPTIGA cannot do — mbedTLS attempts
     * that variant first, takes the PSA_ERROR_NOT_PERMITTED, and falls back to
     * the randomised one, which is the intended path. */
    psa_set_key_algorithm(&attr, PSA_ALG_ECDSA(PSA_ALG_ANY_HASH));
    psa_set_key_lifetime(&attr, PSA_KEY_LIFETIME_FROM_PERSISTENCE_AND_LOCATION(
        PSA_KEY_PERSISTENCE_VOLATILE, PSA_KEY_LOCATION_OPTIGA));

    psa_ret = psa_generate_key(&attr, &s_psa_key_id);
    if (psa_ret != PSA_SUCCESS) {
        printf("[mTLS] psa_generate_key failed: %d\n", (int)psa_ret);
        optiga_manager_touch_release();
        return false;
    }

    /* (7) Bind PSA key to TLS stack */
    cy_tls_set_optiga_key_id(s_psa_key_id);

    /* (8) Resume CM55 touch */
    optiga_manager_touch_release();

    s_mtls_initialized = true;
    printf("[mTLS] OPTIGA Trust M setup complete (key_id=%lu)\n",
           (unsigned long)s_psa_key_id);
    return true;
}

/******************************************************************************
* mqtt_mtls_cleanup
******************************************************************************/
void mqtt_mtls_cleanup(void)
{
    /* Unbind from the TLS stack, but keep the OPTIGA side intact.
     *
     * The PSA key handle and the certificate are deliberately NOT destroyed. The
     * private key lives in OPTIGA hardware and never moves, so the handle stays
     * valid for the life of the process; destroying it only meant the next
     * setup had to call psa_generate_key again, which fails on any attempt after
     * the first with PSA_ERROR_INVALID_ARGUMENT (-135). That turned every failed
     * connection into a permanent one — reconnect could not work without a power
     * cycle, and worse, a second attempt then ran the handshake with no client
     * certificate at all, producing a "connect failed" that looked exactly like a
     * server-side rejection. Two hours of 2026-08-04 went into chasing that.
     *
     * optiga_psa_clear_all_slots() already refuses to touch slot 0 for the same
     * reason, and says so in its own comment; this is the other half of it.
     *
     * mqtt_mtls_setup_optiga() sees the surviving handle and takes its re-bind
     * fast path. */
    cy_tls_set_optiga_key_id(0);
    s_mtls_initialized = false;
}
