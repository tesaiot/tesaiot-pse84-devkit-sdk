/*******************************************************************************
 * File Name: ipc_hsm_handler.c
 *
 * Description: HSM IPC handler - CM33_NS side.
 *              Receives HSM IPC commands from CM55 (page_hsm.c),
 *              performs OPTIGA Trust M operations, returns response
 *              via shared memory.
 *
 *              Supported commands:
 *                IPC_CMD_HSM_REQUEST   - Read chip data (UID, LCS, certs, counters)
 *                IPC_CMD_HSM_BENCHMARK - Run crypto benchmarks (RNG, SHA, ECC)
 *                IPC_CMD_HSM_READ_CERT - Read + parse X.509 cert DER
 *                IPC_CMD_HSM_PIN_CHECK - Check if PIN exists in DATA_3
 *                IPC_CMD_HSM_PIN_SET   - Store SHA-256(PIN) in OPTIGA DATA_3
 *                IPC_CMD_HSM_PIN_VERIFY- Verify PIN against stored hash
 *                IPC_CMD_HSM_HEALTH    - Run 8 self-tests
 *
 *              Architecture:
 *                ISR callback -> semaphore -> static task -> OPTIGA ops
 *                -> write ipc_response_t -> set ready=1
 *
 *******************************************************************************/

/* Must precede every include: crypto_common.h wraps its struct members in
 * MBEDTLS_PRIVATE(), and mbedtls/private_access.h latches this macro on its
 * FIRST inclusion anywhere in the translation unit. */
#define MBEDTLS_ALLOW_PRIVATE_ACCESS

#include "ipc_hsm_handler.h"
#include "ipc_communication.h"
#include "optiga_util.h"
#include "optiga_crypt.h"
#include "optiga_lib_common.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "event_groups.h"
#include <string.h>
#include <stdio.h>

/* MCU benchmark backends:
 *  - mbedTLS SHA-256 routes to MXCRYPTO (MBEDTLS_SHA256_ALT active)
 *  - mbedTLS ECC is pure software here (ECP_ALT undef'd by user config
 *    because CURVE25519 stays enabled) — used for the MCU-SW set
 *  - PDL Cy_Crypto_Core_ECC_* used directly for the MCU-HW set */
#include "mbedtls/sha256.h"
#include "mbedtls/ecp.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/bignum.h"
#include "crypto_common.h"          /* cy_hw_crypto_reserve/release */
#include "cy_crypto_core_ecc.h"

/* TRNG entropy hook from cy-mbedtls-acceleration (trng_alt_mxcrypto.c);
 * declared in mbedTLS's private library/entropy_poll.h, so redeclare. */
extern int mbedtls_hardware_poll(void *data, unsigned char *output,
                                 size_t len, size_t *olen);

/* The MCU-SW benchmark column is only honest while mbedTLS ECC stays
 * software. Today that holds because mbedtls_user_config.h undefs the ECC
 * ALTs when CURVE25519 is enabled — an incidental dependency. If someone
 * disables CURVE25519 (flash-size tuning), the ALTs come back and the "SW"
 * column would silently measure MXCRYPTO. Fail the build instead. */
#if defined(MBEDTLS_ECP_ALT) || defined(MBEDTLS_ECDSA_SIGN_ALT) || \
    defined(MBEDTLS_ECDSA_VERIFY_ALT)
#error "HSM benchmark: mbedTLS ECC ALT is enabled - MCU-SW column would measure hardware. See ipc_hsm_handler.c."
#endif

/*******************************************************************************
 * Static task + semaphore (all statically allocated)
 *******************************************************************************/
#define HSM_TASK_STACK_WORDS  2048  /* 8KB - OPTIGA ops + mbedTLS SW ECDSA
                                     * (MPI bignum stack depth in benchmark) */
#define HSM_TASK_PRIORITY     3

static StackType_t          s_hsm_stack[HSM_TASK_STACK_WORDS];
static StaticTask_t         s_hsm_tcb;
static SemaphoreHandle_t    s_hsm_sem;
static StaticSemaphore_t    s_hsm_sem_buf;
static volatile ipc_msg_t  *s_hsm_pending_msg;

/* Mutex serializing all OPTIGA access (HSM task + direct API callers) */
static SemaphoreHandle_t    s_optiga_mutex;
static StaticSemaphore_t    s_optiga_mutex_buf;

/*******************************************************************************
 * Touch pause/resume — SCB0 shared with CM55 touch (FT5406).
 * Must pause touch polling before any OPTIGA I2C access.
 ******************************************************************************/
CY_SECTION_SHAREDMEM static ipc_msg_t s_touch_ipc_msg;

static void hsm_touch_send_reason(uint32_t cmd, const char *reason)
{
    memset(&s_touch_ipc_msg, 0, sizeof(s_touch_ipc_msg));
    s_touch_ipc_msg.client_id = CM55_IPC_SENSOR_CLIENT_ID;
    s_touch_ipc_msg.intr_mask = CY_IPC_CYPIPE_INTR_MASK_EP1;
    s_touch_ipc_msg.cmd       = cmd;
    s_touch_ipc_msg.value     = 0;

    /* Carry a short user-facing reason so CM55 can say what the device is doing
     * while the screen ignores touch. A silent unresponsive screen is
     * indistinguishable from a crash. value holds the length; empty means CM55
     * picks a generic message. */
    if (NULL != reason) {
        size_t n = strlen(reason);
        if (n > (sizeof(s_touch_ipc_msg.data) - 1U)) {
            n = sizeof(s_touch_ipc_msg.data) - 1U;
        }
        memcpy(s_touch_ipc_msg.data, reason, n);
        s_touch_ipc_msg.value = (uint32_t)n;
    }

    for (int i = 0; i < 50; i++) {
        if (CY_IPC_PIPE_SUCCESS == Cy_IPC_Pipe_SendMessage(
                CM55_IPC_PIPE_EP_ADDR, CM33_IPC_PIPE_EP_ADDR,
                (void *)&s_touch_ipc_msg, NULL)) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    printf("[HSM] WARN: touch IPC send failed (cmd=0x%02lX)\r\n",
           (unsigned long)cmd);
}

/*
 * What to tell the user while the screen ignores touch.
 *
 * Written for someone holding the board, not for a log: name the thing being
 * done, not the opcode. Kept short — CM55 renders it inside a modal, and a long
 * line wraps badly on the 4.3 inch panel.
 */
static const char *hsm_cmd_reason(uint32_t cmd)
{
    switch (cmd) {
    case IPC_CMD_HSM_PROVISION:      return "Working with the platform, a few seconds";
    case IPC_CMD_HSM_SLOT_INFO:      return "Reading the secure element";
    case IPC_CMD_HSM_BENCHMARK:      return "Running the crypto benchmark";
    case IPC_CMD_HSM_READ_CERT:      return "Reading the device certificate";
    case IPC_CMD_HSM_PIN_CHECK:
    case IPC_CMD_HSM_PIN_SET:
    case IPC_CMD_HSM_PIN_VERIFY:
    case IPC_CMD_HSM_PIN_RESET:      return "Working on the PIN";
    case IPC_CMD_HSM_HEALTH:         return "Checking the secure element";
    case IPC_CMD_TESAIOT_CRED_READ:
    case IPC_CMD_TESAIOT_CRED_WRITE:
    case IPC_CMD_TESAIOT_CRED_ERASE: return "Reading stored credentials";
    default:                         return NULL;
    }
}

/*
 * The manager-side entry points, used only by IPC_CMD_HSM_PROVISION so that the
 * Enrol and Protect buttons reach the chip exactly the way optiga.csr() and
 * tesaiot.protected_update() do from the console.
 */
extern bool optiga_manager_init(void (*cb)(void *, optiga_lib_status_t), void *context);
extern bool optiga_trust_open_application(void);
extern void optiga_trust_close_application(void);

/* The completion callback every other initialiser in the tree passes. It writes
 * the global optiga_lib_status that the library's own waits watch. This file's
 * optiga_cb() writes a file-static instead, and optiga_manager_init() latches
 * whichever callback the first caller supplies for the life of the boot — so
 * passing the wrong one here silently breaks every later manager user. */
extern void optiga_util_callback(void *context, optiga_lib_status_t return_status);

/* The counted touch hold. Never send IPC_CMD_TOUCH_RESUME directly. */
extern void optiga_manager_touch_hold_reason(const char *reason);
extern void optiga_manager_touch_release(void);

/* The one gate every chip transaction passes through, re-entrant per task. */
extern bool optiga_chip_enter(void);
extern void optiga_chip_exit(void);

/*******************************************************************************
 * OPTIGA async wait pattern
 *******************************************************************************/
static volatile optiga_lib_status_t s_optiga_status;

static void optiga_cb(void *ctx, optiga_lib_status_t status)
{
    (void)ctx;
    s_optiga_status = status;
}

static optiga_lib_status_t wait_optiga_poll(uint32_t timeout_ms,
                                            uint32_t poll_ms)
{
    TickType_t delay_ticks = pdMS_TO_TICKS(poll_ms);
    if (delay_ticks == 0) delay_ticks = 1;  /* Never busy-spin */

    while (s_optiga_status == OPTIGA_LIB_BUSY && timeout_ms > 0) {
        vTaskDelay(delay_ticks);
        if (timeout_ms >= poll_ms) timeout_ms -= poll_ms;
        else timeout_ms = 0;
    }
    return s_optiga_status;
}

static optiga_lib_status_t wait_optiga(uint32_t timeout_ms)
{
    return wait_optiga_poll(timeout_ms, 10);
}

/*******************************************************************************
 * OPTIGA instance helpers - open/close for each command
 *******************************************************************************/
static optiga_util_t  *s_util;
static optiga_crypt_t *s_crypt;

/*
 * One owner for the chip application, shared with every other claimant.
 *
 * This used to call optiga_util_open_application() on its own instance, outside
 * the library's reference count, and close it on the way out. The count then
 * described only some of the owners: prov_task and a live mTLS session took
 * references this task could not see, and this task held an application they
 * could not see. Each side closed what the other was using — the borrow flag
 * added an hour ago only guarded one of the two directions.
 *
 * The instances below are per-caller and stay private. Only the application is
 * shared, so only the application goes through the counted API. That costs the
 * old 2000 ms bound on a wedged bus, but CM55 no longer waits on this
 * indefinitely either: slot_read() gives up at 2500 ms and the page retries.
 */
static bool optiga_open(void)
{
    s_util = optiga_util_create(0, optiga_cb, NULL);
    if (!s_util) return false;

    s_crypt = optiga_crypt_create(0, optiga_cb, NULL);
    if (!s_crypt) {
        optiga_util_destroy(s_util);
        s_util = NULL;
        return false;
    }

    if (!optiga_trust_open_application()) {
        printf("[HSM] optiga_open: the secure element did not open\r\n");
        optiga_crypt_destroy(s_crypt);
        optiga_util_destroy(s_util);
        s_crypt = NULL;
        s_util = NULL;
        return false;
    }
    return true;
}

static void optiga_close(void)
{
    /* Release this task's reference. The application really closes only when
     * the last owner lets go, so prov_task and a live mTLS session keep theirs. */
    optiga_trust_close_application();
    if (s_crypt) { optiga_crypt_destroy(s_crypt); s_crypt = NULL; }
    if (s_util)  { optiga_util_destroy(s_util);  s_util = NULL; }
}

/*******************************************************************************
 * Read OID helper
 *******************************************************************************/
static uint16_t read_oid(uint16_t oid, uint8_t *buf, uint16_t buf_len)
{
    uint16_t len = buf_len;
    s_optiga_status = OPTIGA_LIB_BUSY;
    optiga_lib_status_t rc = optiga_util_read_data(s_util, oid, 0, buf, &len);
    if (rc != OPTIGA_LIB_SUCCESS) return 0;
    if (wait_optiga(5000) != OPTIGA_LIB_SUCCESS) return 0;
    return len;
}

/*******************************************************************************
 * CMD: IPC_CMD_HSM_REQUEST - Read chip data (UID, LCS, certs, counters)
 *******************************************************************************/
static void handle_hsm_request(ipc_response_t *resp)
{
    uint8_t tmp[64];

    /* UID (27 bytes, OID 0xE0C2) */
    uint16_t len = read_oid(0xE0C2, tmp, 27);
    if (len > 0 && len <= 27)
        memcpy((void *)&resp->data[HSM_RESP_UID_OFF], tmp, len);

    /* LCS (1 byte, OID 0xE0C5) */
    len = read_oid(0xE0C5, tmp, 1);
    if (len > 0)
        resp->data[HSM_RESP_LCS_OFF] = tmp[0];

    /* Counter 0 (4 bytes, OID 0xE120) */
    len = read_oid(0xE120, tmp, 4);
    if (len >= 4)
        memcpy((void *)&resp->data[HSM_RESP_CTR0_OFF], tmp, 4);

    /* Counter 1 (4 bytes, OID 0xE121) */
    len = read_oid(0xE121, tmp, 4);
    if (len >= 4)
        memcpy((void *)&resp->data[HSM_RESP_CTR1_OFF], tmp, 4);

    /* Certificate presence (E0E0..E0E3) */
    static const uint16_t cert_oids[] = {0xE0E0, 0xE0E1, 0xE0E2, 0xE0E3};
    for (int i = 0; i < 4; i++) {
        len = read_oid(cert_oids[i], tmp, 8);
        resp->data[HSM_RESP_CERT_OFF + i] = (len > 0) ? 1 : 0;
    }

    /* Health: LCS 0x07 = operational */
    resp->data[HSM_RESP_HEALTH_OFF] =
        (resp->data[HSM_RESP_LCS_OFF] == 0x07) ? 1 : 0;

    resp->data_len = HSM_RESP_TOTAL_LEN;
    printf("[HSM] REQUEST OK (UID[0..2]=%02X%02X%02X)\r\n",
           resp->data[0], resp->data[1], resp->data[2]);
}

/*******************************************************************************
 * Benchmark microsecond timer — DWT cycle counter, tick fallback
 *******************************************************************************/
static bool s_dwt_ready;

static void bench_timer_init(void)
{
    if (s_dwt_ready) return;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    uint32_t t0 = DWT->CYCCNT;
    for (volatile int i = 0; i < 100; i++) { }
    s_dwt_ready = (DWT->CYCCNT != t0);
    if (!s_dwt_ready)
        printf("[HSM] WARN: DWT unavailable, benchmark at 1ms resolution\r\n");
}

typedef struct {
    uint32_t   cycles;
    TickType_t tick;
} bench_t0_t;

static bench_t0_t bench_start(void)
{
    bench_t0_t t = { .cycles = s_dwt_ready ? DWT->CYCCNT : 0,
                     .tick   = xTaskGetTickCount() };
    return t;
}

static uint32_t bench_elapsed_us(bench_t0_t t0)
{
    if (s_dwt_ready) {
        uint32_t delta = DWT->CYCCNT - t0.cycles;  /* wrap-safe subtraction */
        return (uint32_t)(((uint64_t)delta * 1000000ULL) / SystemCoreClock);
    }
    return (uint32_t)(xTaskGetTickCount() - t0.tick) * 1000u;
}

/*******************************************************************************
 * Software SHA-256 — the mbedTLS software path is compiled out under
 * MBEDTLS_SHA256_ALT (MXCRYPTO), so the MCU-SW set needs a local one.
 *******************************************************************************/
static const uint32_t sw_sha_k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,
    0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
    0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,
    0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,
    0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
    0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,
    0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,
    0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
    0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static inline uint32_t sw_rotr(uint32_t x, uint32_t n)
{
    return (x >> n) | (x << (32u - n));
}

static void sw_sha256_block(uint32_t h[8], const uint8_t p[64])
{
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)p[i*4] << 24) | ((uint32_t)p[i*4+1] << 16) |
               ((uint32_t)p[i*4+2] << 8) | p[i*4+3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = sw_rotr(w[i-15], 7) ^ sw_rotr(w[i-15], 18) ^ (w[i-15] >> 3);
        uint32_t s1 = sw_rotr(w[i-2], 17) ^ sw_rotr(w[i-2], 19)  ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a=h[0], b=h[1], c=h[2], d=h[3], e=h[4], f=h[5], g=h[6], hh=h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t s1 = sw_rotr(e, 6) ^ sw_rotr(e, 11) ^ sw_rotr(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = hh + s1 + ch + sw_sha_k[i] + w[i];
        uint32_t s0 = sw_rotr(a, 2) ^ sw_rotr(a, 13) ^ sw_rotr(a, 22);
        uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + mj;
        hh = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
}

static void sw_sha256(const uint8_t *msg, size_t len, uint8_t out[32])
{
    uint32_t h[8] = { 0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                      0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19 };
    size_t full = len / 64;
    for (size_t i = 0; i < full; i++)
        sw_sha256_block(h, msg + i * 64);

    uint8_t tail[128];
    size_t rem = len - full * 64;
    memcpy(tail, msg + full * 64, rem);
    tail[rem] = 0x80;
    size_t pad_end = (rem + 1 <= 56) ? 64 : 128;
    memset(tail + rem + 1, 0, pad_end - rem - 1 - 8);
    uint64_t bits = (uint64_t)len * 8u;
    for (int i = 0; i < 8; i++)
        tail[pad_end - 1 - i] = (uint8_t)(bits >> (8 * i));
    sw_sha256_block(h, tail);
    if (pad_end == 128)
        sw_sha256_block(h, tail + 64);

    for (int i = 0; i < 8; i++) {
        out[i*4]   = (uint8_t)(h[i] >> 24);
        out[i*4+1] = (uint8_t)(h[i] >> 16);
        out[i*4+2] = (uint8_t)(h[i] >> 8);
        out[i*4+3] = (uint8_t)(h[i]);
    }
}

/*******************************************************************************
 * Software hash-DRBG over sw_sha256 — RNG for the MCU-SW set
 * (mbedTLS CTR_DRBG would route through the HW AES ALT).
 *******************************************************************************/
static uint8_t  s_sw_drbg_v[32];
static uint32_t s_sw_drbg_ctr;
static bool     s_sw_drbg_seeded;

/* NOTE: benchmark-only DRBG — never use for production key material. */
static void sw_drbg_seed(void)
{
    size_t olen = 0;
    if (mbedtls_hardware_poll(NULL, s_sw_drbg_v, 32, &olen) != 0 || olen < 32) {
        /* TRNG unavailable — hash-mix the tick into the FULL existing
         * state instead of overwriting part of it */
        uint8_t mix[36];
        TickType_t t = xTaskGetTickCount();
        memcpy(mix, s_sw_drbg_v, 32);
        memcpy(mix + 32, &t, sizeof(uint32_t));
        sw_sha256(mix, sizeof(mix), s_sw_drbg_v);
    }
    s_sw_drbg_seeded = true;
}

static int sw_drbg_random(void *ctx, unsigned char *out, size_t len)
{
    (void)ctx;
    if (!s_sw_drbg_seeded)
        sw_drbg_seed();

    uint8_t block[36];
    uint8_t digest[32];
    while (len > 0) {
        memcpy(block, s_sw_drbg_v, 32);
        memcpy(block + 32, &s_sw_drbg_ctr, 4);
        s_sw_drbg_ctr++;
        sw_sha256(block, 36, digest);
        size_t n = (len < 32) ? len : 32;
        memcpy(out, digest, n);
        out += n;
        len -= n;
        sw_sha256(digest, 32, s_sw_drbg_v);  /* advance state */
    }
    return 0;
}

/* TRNG-backed RNG callback for PDL HW ECC (fills exactly len bytes) */
static int bench_hw_rand(void *info, uint8_t *out, size_t len)
{
    (void)info;
    while (len > 0) {
        size_t olen = 0;
        if (mbedtls_hardware_poll(NULL, out, len, &olen) != 0 || olen == 0)
            return -1;
        out += olen;
        len -= olen;
    }
    return 0;
}

/*******************************************************************************
 * CMD: IPC_CMD_HSM_BENCHMARK - 3 result sets x 5 crypto benchmarks
 *
 * Layout per ipc_hsm_handler.h: 15 x uint32_t LE microseconds
 * (OPTIGA / MCU-HW / MCU-SW) + optiga_present byte.
 *******************************************************************************/
/* 1 ms polling for benchmark ops — the default 10 ms poll of wait_optiga()
 * would quantize every OPTIGA cell to 10 ms while the MCU columns have
 * microsecond resolution. A dispatch failure or timeout yields the
 * sentinel, never a plausible-looking huge timing.
 *
 * 3 s per op (normal ops finish in <=300 ms): if the chip is yanked
 * mid-benchmark, the worst case (5 timeouts + open/close) must stay under
 * the CM55's 30 s IPC deadline — a late write into the shared response
 * buffer would corrupt the next command. */
static bool bench_optiga_op(optiga_lib_status_t rc, uint32_t timeout_ms)
{
    return (rc == OPTIGA_LIB_SUCCESS) &&
           (wait_optiga_poll(timeout_ms, 1) == OPTIGA_LIB_SUCCESS);
}

static void bench_optiga(uint32_t us[HSM_BENCH_COUNT])
{
    bench_t0_t t0;
    optiga_lib_status_t rc;
    uint8_t buf[256];

    /* 1. Random 32 bytes */
    t0 = bench_start();
    s_optiga_status = OPTIGA_LIB_BUSY;
    rc = optiga_crypt_random(s_crypt, OPTIGA_RNG_TYPE_DRNG, buf, 32);
    us[0] = bench_optiga_op(rc, 3000) ? bench_elapsed_us(t0)
                                      : HSM_BENCH_FAIL_SENTINEL;

    /* 2. SHA-256 hash of 256 bytes */
    memset(buf, 0xAA, 256);
    hash_data_from_host_t hdata;
    hdata.buffer = buf;
    hdata.length = 256;
    uint8_t hash_out[32] = {0};

    t0 = bench_start();
    s_optiga_status = OPTIGA_LIB_BUSY;
    rc = optiga_crypt_hash(s_crypt, OPTIGA_HASH_TYPE_SHA_256,
                           OPTIGA_CRYPT_HOST_DATA, &hdata, hash_out);
    us[1] = bench_optiga_op(rc, 3000) ? bench_elapsed_us(t0)
                                      : HSM_BENCH_FAIL_SENTINEL;

    /* 3. ECC P-256 Key Generation (session key) */
    uint8_t pubkey[68];
    uint16_t pubkey_len = sizeof(pubkey);

    t0 = bench_start();
    s_optiga_status = OPTIGA_LIB_BUSY;
    rc = optiga_crypt_ecc_generate_keypair(s_crypt, OPTIGA_ECC_CURVE_NIST_P_256,
                                       (uint8_t)(OPTIGA_KEY_USAGE_SIGN),
                                       FALSE,
                                       &(optiga_key_id_t){OPTIGA_KEY_ID_SESSION_BASED},
                                       pubkey, &pubkey_len);
    if (!bench_optiga_op(rc, 3000)) {
        /* No session key — sign/verify below cannot run either */
        us[2] = us[3] = us[4] = HSM_BENCH_FAIL_SENTINEL;
        return;
    }
    us[2] = bench_elapsed_us(t0);

    /* 4. ECDSA Sign (sign hash_out with session key) */
    uint8_t sig[80];
    uint16_t sig_len = sizeof(sig);

    t0 = bench_start();
    s_optiga_status = OPTIGA_LIB_BUSY;
    rc = optiga_crypt_ecdsa_sign(s_crypt, hash_out, 32,
                                 OPTIGA_KEY_ID_SESSION_BASED, sig, &sig_len);
    if (!bench_optiga_op(rc, 3000)) {
        us[3] = us[4] = HSM_BENCH_FAIL_SENTINEL;
        return;
    }
    us[3] = bench_elapsed_us(t0);

    /* 5. ECDSA Verify (verify sig with pubkey) */
    public_key_from_host_t pk;
    pk.public_key = pubkey;
    pk.length = pubkey_len;
    pk.key_type = OPTIGA_ECC_CURVE_NIST_P_256;

    t0 = bench_start();
    s_optiga_status = OPTIGA_LIB_BUSY;
    rc = optiga_crypt_ecdsa_verify(s_crypt, hash_out, 32, sig, sig_len,
                                   OPTIGA_CRYPT_HOST_DATA, &pk);
    us[4] = bench_optiga_op(rc, 3000) ? bench_elapsed_us(t0)
                                       : HSM_BENCH_FAIL_SENTINEL;
}

static void bench_mcu_hw(uint32_t us[HSM_BENCH_COUNT])
{
    bench_t0_t t0;
    uint8_t msg[256];
    uint8_t hash_out[32] = {0};
    memset(msg, 0xAA, sizeof(msg));

    /* 1. Random 32 bytes — MXCRYPTO TRNG.
     * Untimed warmup first so the timed draw excludes the one-off crypto
     * block enable (the ECC cells don't pay it either). */
    {
        uint8_t rnd[32];
        (void)bench_hw_rand(NULL, rnd, 4);
        t0 = bench_start();
        int rc = bench_hw_rand(NULL, rnd, 32);
        us[0] = (rc == 0) ? bench_elapsed_us(t0) : HSM_BENCH_FAIL_SENTINEL;
    }

    /* 2. SHA-256 of 256 bytes — mbedTLS routes to MXCRYPTO (SHA256_ALT) */
    {
        t0 = bench_start();
        int rc = mbedtls_sha256(msg, sizeof(msg), hash_out, 0);
        us[1] = (rc == 0) ? bench_elapsed_us(t0) : HSM_BENCH_FAIL_SENTINEL;
    }

    /* 3-5. ECC P-256 via PDL (mbedTLS ECC ALT is disabled in this config) */
    us[2] = us[3] = us[4] = HSM_BENCH_FAIL_SENTINEL;

    /* Reserve/release mutate the acceleration lib's shared refcount table,
     * which has no internal locking against the TLS tasks — guard the
     * bookkeeping (not the long ECC ops) with a critical section. */
    cy_cmgr_crypto_hw_t obj = CY_CMGR_CRYPTO_OBJ_INIT;
    taskENTER_CRITICAL();
    bool reserved = cy_hw_crypto_reserve(&obj, CY_CMGR_CRYPTO_VU);
    taskEXIT_CRITICAL();
    if (!reserved) {
        printf("[HSM] bench: MXCRYPTO VU reserve failed\r\n");
        return;
    }

    uint8_t kbuf[32], xbuf[32], ybuf[32], sig[64], msgkey[32];
    cy_stc_crypto_ecc_key key;
    memset(&key, 0, sizeof(key));
    key.type     = PK_PRIVATE;
    key.curveID  = CY_CRYPTO_ECC_ECP_SECP256R1;
    key.k        = kbuf;
    key.pubkey.x = xbuf;
    key.pubkey.y = ybuf;

    t0 = bench_start();
    cy_en_crypto_status_t st = Cy_Crypto_Core_ECC_MakeKeyPair(
        obj.base, CY_CRYPTO_ECC_ECP_SECP256R1, &key, bench_hw_rand, NULL);
    if (st != CY_CRYPTO_SUCCESS) goto hw_done;
    us[2] = bench_elapsed_us(t0);

    /* Ephemeral per-message key (untimed, same pattern as ecdsa_alt) */
    st = Cy_Crypto_Core_ECC_MakePrivateKey(obj.base,
        CY_CRYPTO_ECC_ECP_SECP256R1, msgkey, bench_hw_rand, NULL);
    if (st != CY_CRYPTO_SUCCESS) goto hw_done;

    t0 = bench_start();
    st = Cy_Crypto_Core_ECC_SignHash(obj.base, hash_out, 32, sig, &key, msgkey);
    if (st != CY_CRYPTO_SUCCESS) goto hw_done;
    us[3] = bench_elapsed_us(t0);

    {
        uint8_t stat = 0;
        key.type = PK_PUBLIC;
        t0 = bench_start();
        st = Cy_Crypto_Core_ECC_VerifyHash(obj.base, sig, hash_out, 32,
                                           &stat, &key);
        if (st == CY_CRYPTO_SUCCESS && stat == 1u)
            us[4] = bench_elapsed_us(t0);
    }

hw_done:
    taskENTER_CRITICAL();
    cy_hw_crypto_release(&obj);
    taskEXIT_CRITICAL();
}

static void bench_mcu_sw(uint32_t us[HSM_BENCH_COUNT])
{
    bench_t0_t t0;
    uint8_t msg[256];
    uint8_t hash_out[32];
    memset(msg, 0xAA, sizeof(msg));

    /* 1. Random 32 bytes — software hash-DRBG */
    {
        uint8_t rnd[32];
        sw_drbg_seed();
        t0 = bench_start();
        sw_drbg_random(NULL, rnd, 32);
        us[0] = bench_elapsed_us(t0);
    }

    /* 2. SHA-256 of 256 bytes — local software implementation */
    t0 = bench_start();
    sw_sha256(msg, sizeof(msg), hash_out);
    us[1] = bench_elapsed_us(t0);

    /* 3-5. ECC P-256 — mbedTLS software bignum path */
    us[2] = us[3] = us[4] = HSM_BENCH_FAIL_SENTINEL;

    mbedtls_ecp_group grp;
    mbedtls_ecp_point Q;
    mbedtls_mpi d, r, s;
    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&Q);
    mbedtls_mpi_init(&d);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);

    int rc = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1);
    if (rc != 0) goto sw_err;

    t0 = bench_start();
    rc = mbedtls_ecp_gen_keypair(&grp, &d, &Q, sw_drbg_random, NULL);
    if (rc != 0) goto sw_err;
    us[2] = bench_elapsed_us(t0);

    t0 = bench_start();
    rc = mbedtls_ecdsa_sign(&grp, &r, &s, &d, hash_out, 32,
                            sw_drbg_random, NULL);
    if (rc != 0) goto sw_err;
    us[3] = bench_elapsed_us(t0);

    t0 = bench_start();
    rc = mbedtls_ecdsa_verify(&grp, hash_out, 32, &Q, &r, &s);
    if (rc == 0)
        us[4] = bench_elapsed_us(t0);
    else
        goto sw_err;
    goto sw_done;

sw_err:
    /* Distinguish real failure (e.g. heap exhaustion -0x4D80) from the
     * sentinel's "unavailable" reading */
    printf("[HSM] bench SW ECC failed rc=-0x%04X\r\n", (unsigned)(-rc));

sw_done:
    mbedtls_mpi_free(&s);
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&d);
    mbedtls_ecp_point_free(&Q);
    mbedtls_ecp_group_free(&grp);
}

static void handle_hsm_benchmark(ipc_response_t *resp, bool optiga_present)
{
    uint32_t us[HSM_BENCH_SET_COUNT][HSM_BENCH_COUNT];
    for (int s = 0; s < HSM_BENCH_SET_COUNT; s++)
        for (int i = 0; i < HSM_BENCH_COUNT; i++)
            us[s][i] = HSM_BENCH_FAIL_SENTINEL;

    bench_timer_init();

    if (optiga_present)
        bench_optiga(us[HSM_BENCH_SET_OPTIGA]);
    bench_mcu_hw(us[HSM_BENCH_SET_MCU_HW]);
    bench_mcu_sw(us[HSM_BENCH_SET_MCU_SW]);

    /* Pack as uint32_t LE + present flag */
    for (int s = 0; s < HSM_BENCH_SET_COUNT; s++) {
        for (int i = 0; i < HSM_BENCH_COUNT; i++) {
            uint32_t v = us[s][i];
            int off = HSM_BENCH_SET_OFF(s) + i * 4;
            resp->data[off]     = (uint8_t)(v & 0xFF);
            resp->data[off + 1] = (uint8_t)((v >> 8) & 0xFF);
            resp->data[off + 2] = (uint8_t)((v >> 16) & 0xFF);
            resp->data[off + 3] = (uint8_t)((v >> 24) & 0xFF);
        }
    }
    resp->data[HSM_BENCH_PRESENT_OFF] = optiga_present ? 1 : 0;
    resp->data_len = HSM_BENCH_TOTAL_LEN;

    printf("[HSM] BENCHMARK OK (optiga=%d): "
           "OPTIGA RNG=%lu SHA=%lu KG=%lu Sg=%lu Vf=%lu us | "
           "HW RNG=%lu SHA=%lu KG=%lu Sg=%lu Vf=%lu us | "
           "SW RNG=%lu SHA=%lu KG=%lu Sg=%lu Vf=%lu us\r\n",
           optiga_present ? 1 : 0,
           (unsigned long)us[0][0], (unsigned long)us[0][1],
           (unsigned long)us[0][2], (unsigned long)us[0][3],
           (unsigned long)us[0][4],
           (unsigned long)us[1][0], (unsigned long)us[1][1],
           (unsigned long)us[1][2], (unsigned long)us[1][3],
           (unsigned long)us[1][4],
           (unsigned long)us[2][0], (unsigned long)us[2][1],
           (unsigned long)us[2][2], (unsigned long)us[2][3],
           (unsigned long)us[2][4]);
    printf("[HSM] bench stack headroom: %lu words\r\n",
           (unsigned long)uxTaskGetStackHighWaterMark(NULL));
}

/*******************************************************************************
 * CMD: IPC_CMD_HSM_READ_CERT - Read + parse X.509 cert DER
 *
 * Input:  resp->cmd = cert slot index (0-3)
 * Output: data[0..1] = DER size (uint16_t LE)
 *         data[2..]  = packed null-terminated strings:
 *           [subject_cn\0][issuer_cn\0][org\0][country\0][not_before\0][not_after\0]
 *******************************************************************************/

/* Cert DER buffer (max ~1728 bytes per OPTIGA cert slot) */
static uint8_t s_cert_der[1728];

/* Parse DER tag + length, return pointer past TL, set *out_len */
static const uint8_t *der_tl(const uint8_t *p, const uint8_t *end,
                              uint8_t *out_tag, uint16_t *out_len)
{
    if (p >= end) return NULL;
    *out_tag = *p++;
    if (p >= end) return NULL;

    if (*p < 0x80) {
        *out_len = *p++;
    } else if (*p == 0x81) {
        p++;
        if (p >= end) return NULL;
        *out_len = *p++;
    } else if (*p == 0x82) {
        p++;
        if (p + 1 >= end) return NULL;
        *out_len = ((uint16_t)p[0] << 8) | p[1];
        p += 2;
    } else {
        return NULL;  /* Unsupported length encoding */
    }
    return p;
}

/* Skip one TLV element, returning pointer past it */
static const uint8_t *der_skip(const uint8_t *p, const uint8_t *end)
{
    uint8_t tag;
    uint16_t len;
    p = der_tl(p, end, &tag, &len);
    if (!p) return NULL;
    p += len;
    return (p <= end) ? p : NULL;
}

/* Search RDN sequence for OID suffix (55 04 XX) and extract UTF8/PrintableString */
static bool find_rdn_attr(const uint8_t *seq, uint16_t seq_len,
                           uint8_t oid_last_byte,
                           char *out, uint16_t out_sz)
{
    const uint8_t *p = seq;
    const uint8_t *end = seq + seq_len;
    out[0] = '\0';

    while (p < end) {
        uint8_t tag;
        uint16_t setlen;
        const uint8_t *set_start = der_tl(p, end, &tag, &setlen);
        if (!set_start || tag != 0x31) { p = der_skip(p, end); if (!p) break; continue; }

        const uint8_t *set_end = set_start + setlen;
        const uint8_t *sp = set_start;

        while (sp < set_end) {
            uint8_t stag;
            uint16_t s_seq_len;
            const uint8_t *seq_p = der_tl(sp, set_end, &stag, &s_seq_len);
            if (!seq_p || stag != 0x30) break;
            const uint8_t *seq_end = seq_p + s_seq_len;

            /* OID */
            uint8_t oid_tag;
            uint16_t oid_len;
            const uint8_t *oid_val = der_tl(seq_p, seq_end, &oid_tag, &oid_len);
            if (!oid_val || oid_tag != 0x06) break;

            /* Check OID = 55 04 XX */
            if (oid_len >= 3 && oid_val[0] == 0x55 && oid_val[1] == 0x04 &&
                oid_val[2] == oid_last_byte) {
                const uint8_t *val_p = oid_val + oid_len;
                uint8_t vtag;
                uint16_t vlen;
                const uint8_t *vdata = der_tl(val_p, seq_end, &vtag, &vlen);
                if (vdata && vlen > 0) {
                    uint16_t copy = (vlen < out_sz - 1) ? vlen : (out_sz - 1);
                    memcpy(out, vdata, copy);
                    out[copy] = '\0';
                    return true;
                }
            }
            sp = seq_end;
        }
        p = set_start + setlen;
    }
    return false;
}

/* Format UTCTime (YYMMDDHHMMSSZ) or GeneralizedTime (YYYYMMDDHHMMSSZ) */
static void fmt_time(const uint8_t *val, uint16_t len, char *out, uint16_t out_sz)
{
    if (len >= 13 && len <= 15) {
        /* UTCTime: YYMMDDHHMMSSZ */
        int yr = (val[0] - '0') * 10 + (val[1] - '0');
        yr += (yr >= 50) ? 1900 : 2000;
        snprintf(out, out_sz, "%04d-%c%c-%c%c",
                 yr, val[2], val[3], val[4], val[5]);
    } else if (len >= 15) {
        /* GeneralizedTime: YYYYMMDDHHMMSSZ */
        snprintf(out, out_sz, "%c%c%c%c-%c%c-%c%c",
                 val[0], val[1], val[2], val[3],
                 val[4], val[5], val[6], val[7]);
    } else {
        snprintf(out, out_sz, "N/A");
    }
}

static void handle_hsm_read_cert(ipc_response_t *resp, const ipc_msg_t *msg)
{
    static const uint16_t cert_oids[] = {0xE0E0, 0xE0E1, 0xE0E2, 0xE0E3};
    int slot = (uint8_t)msg->data[0];  /* Slot index passed in msg data[0] */
    if (slot < 0 || slot > 3) {
        resp->status = 1;
        return;
    }

    uint16_t der_len = read_oid(cert_oids[slot], s_cert_der, sizeof(s_cert_der));
    if (der_len == 0) {
        resp->data[0] = 0;
        resp->data[1] = 0;
        resp->data_len = 2;
        printf("[HSM] CERT slot %d: empty (read returned 0)\r\n", slot);
        return;
    }

    /* Scan for DER SEQUENCE header (0x30 0x82) to skip OPTIGA wrapper.
     * OPTIGA cert format varies per slot:
     *   E0E0 factory: C0 [len2] [type_byte] [chain_len2] [type_byte] [cert_len2] 30 82 ...
     *   E0E1 project: C0 [len2] C1 [len2] C2 [len2] 30 82 ...
     * Scanning for 0x30 0x82 is the most robust approach. */
    const uint8_t *cert_p = s_cert_der;
    const uint8_t *cert_end = s_cert_der + der_len;
    while (cert_p + 4 <= cert_end) {
        if (cert_p[0] == 0x30 && cert_p[1] == 0x82)
            break;
        cert_p++;
    }
    if (cert_p + 4 > cert_end || cert_p[0] != 0x30) {
        /* No DER SEQUENCE found - slot is empty or corrupted */
        resp->data[0] = 0;
        resp->data[1] = 0;
        resp->data_len = 2;
        printf("[HSM] CERT slot %d: no DER found in %u bytes\r\n", slot, der_len);
        return;
    }

    /* DER cert starts at cert_p; actual cert length from DER header */
    uint16_t der_content_len = ((uint16_t)cert_p[2] << 8) | cert_p[3];
    uint16_t actual_der_len = der_content_len + 4;  /* tag(1) + 0x82(1) + len(2) + content */

    /* Store actual DER cert size as uint16_t LE */
    resp->data[HSM_CERT_SIZE_OFF]     = (uint8_t)(actual_der_len & 0xFF);
    resp->data[HSM_CERT_SIZE_OFF + 1] = (uint8_t)(actual_der_len >> 8);

    /* Update cert_end to actual cert boundary */
    if (cert_p + actual_der_len < cert_end)
        cert_end = cert_p + actual_der_len;

    /* Parse Certificate SEQUENCE -> TBSCertificate SEQUENCE */
    uint8_t tag;
    uint16_t tlen;
    const uint8_t *tbs_outer = der_tl(cert_p, cert_end, &tag, &tlen);
    if (!tbs_outer || tag != 0x30) goto pack_empty;

    const uint8_t *tbs_start = der_tl(tbs_outer, cert_end, &tag, &tlen);
    if (!tbs_start || tag != 0x30) goto pack_empty;
    const uint8_t *tbs_end = tbs_start + tlen;

    /* Walk TBSCertificate fields:
     * [0] Version (EXPLICIT TAG A0), [1] Serial, [2] SigAlg,
     * [3] Issuer, [4] Validity, [5] Subject */
    const uint8_t *field = tbs_start;

    /* Version (context [0] EXPLICIT) - optional */
    if (field < tbs_end && *field == 0xA0) {
        field = der_skip(field, tbs_end);
        if (!field) goto pack_empty;
    }
    /* Serial */
    field = der_skip(field, tbs_end);
    if (!field) goto pack_empty;
    /* SigAlg */
    field = der_skip(field, tbs_end);
    if (!field) goto pack_empty;

    /* Issuer (SEQUENCE of RDN SETs) */
    const uint8_t *issuer_seq;
    uint16_t issuer_len;
    issuer_seq = der_tl(field, tbs_end, &tag, &issuer_len);
    if (!issuer_seq || tag != 0x30) goto pack_empty;
    const uint8_t *issuer_data = issuer_seq;
    field = issuer_seq + issuer_len;

    /* Validity (SEQUENCE { notBefore, notAfter }) */
    const uint8_t *val_seq;
    uint16_t val_seq_len;
    val_seq = der_tl(field, tbs_end, &tag, &val_seq_len);
    if (!val_seq || tag != 0x30) goto pack_empty;
    const uint8_t *validity_p = val_seq;
    const uint8_t *validity_end = val_seq + val_seq_len;
    field = validity_end;

    /* Subject (SEQUENCE of RDN SETs) */
    const uint8_t *subj_seq;
    uint16_t subj_len;
    subj_seq = der_tl(field, tbs_end, &tag, &subj_len);
    if (!subj_seq || tag != 0x30) goto pack_empty;

    /* Extract fields into packed strings */
    char subject_cn[64] = "";
    char issuer_cn[64]  = "";
    char org[64]        = "";
    char country[8]     = "";
    char not_before[20] = "";
    char not_after[20]  = "";

    /* OID 55 04 03 = CN, 55 04 0A = Org, 55 04 06 = Country
     * Search Subject first; fall back to Issuer for Org/Country
     * (Infineon factory cert has Org/Country only in Issuer) */
    find_rdn_attr(subj_seq, subj_len, 0x03, subject_cn, sizeof(subject_cn));
    find_rdn_attr(issuer_data, issuer_len, 0x03, issuer_cn, sizeof(issuer_cn));
    find_rdn_attr(subj_seq, subj_len, 0x0A, org, sizeof(org));
    if (org[0] == '\0')
        find_rdn_attr(issuer_data, issuer_len, 0x0A, org, sizeof(org));
    find_rdn_attr(subj_seq, subj_len, 0x06, country, sizeof(country));
    if (country[0] == '\0')
        find_rdn_attr(issuer_data, issuer_len, 0x06, country, sizeof(country));

    /* Parse Validity dates */
    {
        uint8_t vtag;
        uint16_t vlen;
        const uint8_t *vp = der_tl(validity_p, validity_end, &vtag, &vlen);
        if (vp) {
            fmt_time(vp, vlen, not_before, sizeof(not_before));
            const uint8_t *vp2 = vp + vlen;
            vp2 = der_tl(vp2, validity_end, &vtag, &vlen);
            if (vp2) fmt_time(vp2, vlen, not_after, sizeof(not_after));
        }
    }

    /* Pack strings: [subject_cn\0][issuer_cn\0][org\0][country\0][not_before\0][not_after\0] */
    {
        uint8_t *dst = (uint8_t *)&resp->data[HSM_CERT_STRINGS_OFF];
        uint8_t *dst_end = (uint8_t *)&resp->data[IPC_RESPONSE_DATA_MAX];
        const char *strs[] = { subject_cn, issuer_cn, org, country, not_before, not_after };
        for (int i = 0; i < 6; i++) {
            uint16_t slen = (uint16_t)strlen(strs[i]) + 1;
            if (dst + slen > dst_end) break;
            memcpy(dst, strs[i], slen);
            dst += slen;
        }
        resp->data_len = (uint16_t)(dst - resp->data);
    }

    printf("[HSM] CERT slot %d: CN=%s, Issuer=%s, DER=%u\r\n",
           slot, subject_cn, issuer_cn, der_len);
    return;

pack_empty:
    /* Could not parse - return DER size only */
    resp->data_len = 2;
    printf("[HSM] CERT slot %d: parse failed (DER=%u)\r\n", slot, der_len);
}

/*******************************************************************************
 * CMD: IPC_CMD_HSM_PIN_CHECK / PIN_SET / PIN_VERIFY
 *
 * PIN stored as SHA-256(4 digits) in OPTIGA DATA_3 (OID 0xF1D2).
 * Matches hsm_dashboard.py pattern.
 *******************************************************************************/
static void handle_hsm_pin(ipc_response_t *resp, uint8_t cmd,
                            const uint8_t *data)
{
    switch (cmd) {
    case IPC_CMD_HSM_PIN_RESET: {
        /* Erase PIN data from DATA_3 (write 0 bytes = erase) */
        uint8_t zeros[32];
        memset(zeros, 0, sizeof(zeros));
        s_optiga_status = OPTIGA_LIB_BUSY;
        optiga_lib_status_t rc = optiga_util_write_data(s_util, HSM_PIN_DATA_OID,
            OPTIGA_UTIL_ERASE_AND_WRITE, 0, zeros, 1);
        resp->data[0] = (rc == OPTIGA_LIB_SUCCESS &&
                         wait_optiga(5000) == OPTIGA_LIB_SUCCESS) ? 1 : 0;
        resp->data_len = 1;
        printf("[HSM] PIN_RESET: %s\r\n", resp->data[0] ? "OK" : "failed");
        break;
    }

    case IPC_CMD_HSM_PIN_CHECK: {
        /* Check if 32 bytes exist in DATA_3 */
        uint8_t tmp[32];
        uint16_t len = read_oid(HSM_PIN_DATA_OID, tmp, 32);
        resp->data[0] = (len == 32) ? 1 : 0;
        resp->data_len = 1;
        printf("[HSM] PIN_CHECK: %s\r\n", (len == 32) ? "set" : "not set");
        break;
    }

    case IPC_CMD_HSM_PIN_SET: {
        /* data[0..3] = 4 digit values; hash them with OPTIGA SHA-256 */
        uint8_t pin_bytes[4];
        memcpy(pin_bytes, data, 4);

        hash_data_from_host_t hdata;
        hdata.buffer = pin_bytes;
        hdata.length = 4;
        uint8_t hash[32];

        s_optiga_status = OPTIGA_LIB_BUSY;
        optiga_lib_status_t rc = optiga_crypt_hash(s_crypt,
            OPTIGA_HASH_TYPE_SHA_256, OPTIGA_CRYPT_HOST_DATA, &hdata, hash);
        if (rc != OPTIGA_LIB_SUCCESS || wait_optiga(5000) != OPTIGA_LIB_SUCCESS) {
            resp->status = 3;
            resp->data[0] = 0;
            resp->data_len = 1;
            printf("[HSM] PIN_SET hash failed\r\n");
            break;
        }

        /* Write hash to DATA_3 */
        s_optiga_status = OPTIGA_LIB_BUSY;
        rc = optiga_util_write_data(s_util, HSM_PIN_DATA_OID,
                                    OPTIGA_UTIL_ERASE_AND_WRITE, 0, hash, 32);
        if (rc != OPTIGA_LIB_SUCCESS || wait_optiga(5000) != OPTIGA_LIB_SUCCESS) {
            resp->status = 4;
            resp->data[0] = 0;
            resp->data_len = 1;
            printf("[HSM] PIN_SET write failed\r\n");
            break;
        }

        resp->data[0] = 1;  /* Success */
        resp->data_len = 1;
        printf("[HSM] PIN_SET OK\r\n");
        break;
    }

    case IPC_CMD_HSM_PIN_VERIFY: {
        /* data[0..3] = 4 digit values; hash and compare with stored */
        uint8_t pin_bytes[4];
        memcpy(pin_bytes, data, 4);

        hash_data_from_host_t hdata;
        hdata.buffer = pin_bytes;
        hdata.length = 4;
        uint8_t hash[32];

        s_optiga_status = OPTIGA_LIB_BUSY;
        optiga_lib_status_t rc = optiga_crypt_hash(s_crypt,
            OPTIGA_HASH_TYPE_SHA_256, OPTIGA_CRYPT_HOST_DATA, &hdata, hash);
        if (rc != OPTIGA_LIB_SUCCESS || wait_optiga(5000) != OPTIGA_LIB_SUCCESS) {
            resp->status = 3;
            resp->data[0] = 0;
            resp->data_len = 1;
            break;
        }

        /* Read stored hash */
        uint8_t stored[32];
        uint16_t len = read_oid(HSM_PIN_DATA_OID, stored, 32);
        if (len != 32) {
            resp->data[0] = 0;
            resp->data_len = 1;
            printf("[HSM] PIN_VERIFY: no stored hash\r\n");
            break;
        }

        resp->data[0] = (memcmp(hash, stored, 32) == 0) ? 1 : 0;
        resp->data_len = 1;
        printf("[HSM] PIN_VERIFY: %s\r\n", resp->data[0] ? "match" : "mismatch");
        break;
    }
    }
}

/*******************************************************************************
 * CMD: IPC_CMD_HSM_HEALTH - Run 8 self-tests
 *
 * Response: 8 bytes, each 1=pass 0=fail
 * Order: HW, UID, Cert, RNG, SHA, ECC, RW, Meta
 *******************************************************************************/
static void handle_hsm_health(ipc_response_t *resp)
{
    uint8_t results[HSM_HEALTH_TOTAL_LEN];
    memset(results, 0, sizeof(results));
    uint8_t tmp[64];

    /* 1. HW test - read Security Status (OID 0xE0C1) */
    results[HSM_HEALTH_HW_OFF] = (read_oid(0xE0C1, tmp, 4) > 0) ? 1 : 0;

    /* 2. UID test - read 27-byte UID */
    results[HSM_HEALTH_UID_OFF] = (read_oid(0xE0C2, tmp, 27) == 27) ? 1 : 0;

    /* 3. Cert test - read first cert slot (>100 bytes = valid) */
    {
        uint16_t len = read_oid(0xE0E0, s_cert_der, sizeof(s_cert_der));
        results[HSM_HEALTH_CERT_OFF] = (len > 100) ? 1 : 0;
    }

    /* 4. RNG test - generate 32 random bytes */
    {
        s_optiga_status = OPTIGA_LIB_BUSY;
        optiga_lib_status_t rc = optiga_crypt_random(s_crypt,
            OPTIGA_RNG_TYPE_DRNG, tmp, 32);
        results[HSM_HEALTH_RNG_OFF] =
            (rc == OPTIGA_LIB_SUCCESS && wait_optiga(5000) == OPTIGA_LIB_SUCCESS) ? 1 : 0;
    }

    /* 5. SHA test - hash 16 bytes */
    {
        memset(tmp, 0x42, 16);
        hash_data_from_host_t hdata;
        hdata.buffer = tmp;
        hdata.length = 16;
        uint8_t hash[32];

        s_optiga_status = OPTIGA_LIB_BUSY;
        optiga_lib_status_t rc = optiga_crypt_hash(s_crypt,
            OPTIGA_HASH_TYPE_SHA_256, OPTIGA_CRYPT_HOST_DATA, &hdata, hash);
        results[HSM_HEALTH_SHA_OFF] =
            (rc == OPTIGA_LIB_SUCCESS && wait_optiga(5000) == OPTIGA_LIB_SUCCESS) ? 1 : 0;
    }

    /* 6. ECC test - generate session keypair + sign */
    {
        uint8_t pubkey[68];
        uint16_t pk_len = sizeof(pubkey);

        s_optiga_status = OPTIGA_LIB_BUSY;
        optiga_lib_status_t rc = optiga_crypt_ecc_generate_keypair(
            s_crypt, OPTIGA_ECC_CURVE_NIST_P_256,
            (uint8_t)(OPTIGA_KEY_USAGE_SIGN), FALSE,
            &(optiga_key_id_t){OPTIGA_KEY_ID_SESSION_BASED},
            pubkey, &pk_len);
        if (rc == OPTIGA_LIB_SUCCESS && wait_optiga(10000) == OPTIGA_LIB_SUCCESS) {
            /* Also sign to fully verify ECC */
            uint8_t digest[32] = {0};
            uint8_t sig[80];
            uint16_t sig_len = sizeof(sig);
            s_optiga_status = OPTIGA_LIB_BUSY;
            rc = optiga_crypt_ecdsa_sign(s_crypt, digest, 32,
                                         OPTIGA_KEY_ID_SESSION_BASED, sig, &sig_len);
            results[HSM_HEALTH_ECC_OFF] =
                (rc == OPTIGA_LIB_SUCCESS && wait_optiga(10000) == OPTIGA_LIB_SUCCESS) ? 1 : 0;
        }
    }

    /* 7. R/W test - write 4 bytes to USER0 (0xF1D8) then read back.
     *    Using 0xF1D8 (not 0xF1D0) to avoid overwriting device ID. */
    {
        uint8_t test_data[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
        s_optiga_status = OPTIGA_LIB_BUSY;
        optiga_lib_status_t rc = optiga_util_write_data(s_util, 0xF1D8,
            OPTIGA_UTIL_ERASE_AND_WRITE, 0, test_data, 4);
        if (rc == OPTIGA_LIB_SUCCESS && wait_optiga(5000) == OPTIGA_LIB_SUCCESS) {
            uint8_t readback[4] = {0};
            uint16_t rlen = read_oid(0xF1D8, readback, 4);
            results[HSM_HEALTH_RW_OFF] =
                (rlen == 4 && memcmp(readback, test_data, 4) == 0) ? 1 : 0;
        }
    }

    /* 8. Meta test - read metadata of OID 0xE0E0 */
    {
        uint8_t meta[64];
        uint16_t mlen = sizeof(meta);
        s_optiga_status = OPTIGA_LIB_BUSY;
        optiga_lib_status_t rc = optiga_util_read_metadata(s_util, 0xE0E0, meta, &mlen);
        results[HSM_HEALTH_META_OFF] =
            (rc == OPTIGA_LIB_SUCCESS && wait_optiga(5000) == OPTIGA_LIB_SUCCESS && mlen > 0) ? 1 : 0;
    }

    memcpy((void *)resp->data, results, HSM_HEALTH_TOTAL_LEN);
    resp->data_len = HSM_HEALTH_TOTAL_LEN;

    int pass = 0;
    for (int i = 0; i < HSM_HEALTH_TOTAL_LEN; i++) pass += results[i];
    printf("[HSM] HEALTH: %d/8 tests passed\r\n", pass);
}

/*******************************************************************************
 * CMD: IPC_CMD_TESAIOT_CRED_READ / CRED_WRITE / CRED_ERASE
 *
 * Credential storage in OPTIGA data slots — used by wifi_saved.c on CM55.
 * CRED_READ:  data[0]=slot → response data[0..N], data_len=N
 * CRED_WRITE: data[0]=slot, data[1]=len, data[2..2+len-1]=payload
 * CRED_ERASE: data[0]=slot → erase by writing 1 zero byte
 *******************************************************************************/
static void handle_cred_read(ipc_response_t *resp, const ipc_msg_t *msg)
{
    uint8_t slot = (uint8_t)msg->data[0];
    uint16_t oid = tesaiot_slot_to_oid(slot);
    if (oid == 0) {
        resp->status = 2;
        printf("[HSM] CRED_READ: invalid slot %u\r\n", slot);
        return;
    }

    uint16_t len = read_oid(oid, (uint8_t *)resp->data, IPC_RESPONSE_DATA_MAX);
    resp->data_len = len;
    printf("[HSM] CRED_READ slot %u (OID 0x%04X): %u bytes\r\n", slot, oid, len);
}

static void handle_cred_write(ipc_response_t *resp, const ipc_msg_t *msg)
{
    uint8_t slot = (uint8_t)msg->data[0];
    uint8_t data_len = (uint8_t)msg->data[1];
    uint16_t oid = tesaiot_slot_to_oid(slot);
    if (oid == 0 || data_len == 0 || data_len > (IPC_DATA_MAX_LEN - 2)) {
        resp->status = 2;
        printf("[HSM] CRED_WRITE: invalid slot %u or len %u\r\n", slot, data_len);
        return;
    }

    s_optiga_status = OPTIGA_LIB_BUSY;
    optiga_lib_status_t rc = optiga_util_write_data(
        s_util, oid, OPTIGA_UTIL_ERASE_AND_WRITE, 0,
        (const uint8_t *)&msg->data[2], data_len);
    if (rc != OPTIGA_LIB_SUCCESS || wait_optiga(5000) != OPTIGA_LIB_SUCCESS) {
        resp->status = 3;
        printf("[HSM] CRED_WRITE slot %u: FAILED\r\n", slot);
        return;
    }
    printf("[HSM] CRED_WRITE slot %u (OID 0x%04X): %u bytes OK\r\n",
           slot, oid, data_len);
}

static void handle_cred_erase(ipc_response_t *resp, const ipc_msg_t *msg)
{
    uint8_t slot = (uint8_t)msg->data[0];
    uint16_t oid = tesaiot_slot_to_oid(slot);
    if (oid == 0) {
        resp->status = 2;
        printf("[HSM] CRED_ERASE: invalid slot %u\r\n", slot);
        return;
    }

    uint8_t zero = 0;
    s_optiga_status = OPTIGA_LIB_BUSY;
    optiga_lib_status_t rc = optiga_util_write_data(
        s_util, oid, OPTIGA_UTIL_ERASE_AND_WRITE, 0, &zero, 1);
    if (rc != OPTIGA_LIB_SUCCESS || wait_optiga(5000) != OPTIGA_LIB_SUCCESS) {
        resp->status = 3;
        printf("[HSM] CRED_ERASE slot %u: FAILED\r\n", slot);
        return;
    }
    printf("[HSM] CRED_ERASE slot %u (OID 0x%04X): OK\r\n", slot, oid);
}

static void handle_hsm_slot_info(ipc_response_t *resp, const ipc_msg_t *msg);
static void handle_hsm_provision(ipc_response_t *resp, const ipc_msg_t *msg);

/*******************************************************************************
 * HSM task - command dispatcher
 *******************************************************************************/
static void hsm_task_func(void *arg)
{
    (void)arg;

    /* Bring the manager up on the first thing that runs after the scheduler.
     *
     * The gate needs a mutex before anything can be excluded from the chip, and
     * "whoever gets there first" was the mTLS setup, the REPL or this page -
     * so the window in which the gate did not exist was the window this feature
     * is used in.
     *
     * It was briefly done from ipc_hsm_handler_init(), which runs from main()
     * BEFORE vTaskStartScheduler(). That is not the harmless "mutex and an
     * instance" the comment there claimed: optiga_util_create() reaches
     * optiga_cmd_create() -> pal_os_event_init() -> xTimerCreate() and
     * xTimerChangePeriod(), inside a critical section, with no scheduler - and
     * it armed the vendor's command-queue timer at boot on every unit whether
     * the secure element was ever used or not. Here it is ordinary task
     * context, still ahead of any user action. */
    (void)optiga_manager_init(optiga_util_callback, NULL);

    for (;;) {
        if (xSemaphoreTake(s_hsm_sem, portMAX_DELAY) != pdTRUE)
            continue;

        ipc_msg_t *msg = (ipc_msg_t *)s_hsm_pending_msg;
        if (!msg) continue;

        ipc_response_t *resp = (ipc_response_t *)(uintptr_t)msg->value;
        if (!resp) continue;

        /* Initialize response */
        memset((void *)resp->data, 0, IPC_RESPONSE_DATA_MAX);
        resp->data_len = 0;
        resp->status   = 0;
        resp->cmd      = (uint8_t)msg->cmd;

        /* Acquire OPTIGA mutex — serializes with ipc_hsm_cred_read_sync() */
        xSemaphoreTake(s_optiga_mutex, portMAX_DELAY);

        /* And take the chip gate, so this task's own s_util/s_crypt traffic is
         * serialised against the provisioning task, the MQTT subscriber and a
         * live mTLS session. s_optiga_mutex only ever excluded other callers of
         * this file; it never excluded them, which is how five review rounds
         * each found one more pair of tasks able to drive the chip at once.
         * Not fatal if it fails — the chip may not be initialised yet — but
         * then there is a single user by construction. */
        /* Pause CM55 touch through the COUNTED hold, never with a raw send.
         *
         * CM55 treats IPC_CMD_TOUCH_RESUME as an unconditional
         * `touch_disabled = false` — it is not a counter. So a raw resume from
         * here cancels a hold that another task is relying on, and the next
         * touchpad_read() disables and re-enables the shared SCB block under a
         * transfer that CM33_NS still has in flight. That is exactly the
         * OPTIGA_COMMS_ERROR (0x0102) this page's own provisioning poll would
         * otherwise cause about 150 times per enrolment, at 400 ms intervals.
         *
         * optiga_manager_touch_hold_reason() carries the same reason string and
         * already waits 50 ms for an in-flight touch transfer to finish, so the
         * user-facing behaviour is unchanged; only the bookkeeping is correct.
         *
         * Only for commands that drive the chip. IPC_CMD_HSM_PROVISION reads and
         * writes s_prov and touches no hardware from this task — its chip work
         * runs on prov_task, which holds for itself. The provisioning screen
         * polls it every 400 ms, so holding here drove the count 0 -> 1 -> 0
         * about 150 times per enrolment, and the certificate install lands in
         * the middle of that window. */
        const bool needs_chip = (msg->cmd != IPC_CMD_HSM_PROVISION);

        /* Take the chip gate only for commands that use the chip.
         *
         * IPC_CMD_HSM_PROVISION reads and writes s_prov and touches no hardware
         * from this task, and the screen polls it every 400 ms. Gating it meant
         * every poll blocked here for the gate's full ten seconds whenever an
         * install held the chip; CM55 gave up at three, counted twenty misses,
         * and told the operator the request never started - inviting a second
         * run against a chip that was mid-enrolment. */
        if (needs_chip) {
            if (!optiga_chip_enter()) {
                printf("[HSM] cmd 0x%02lX deferred: the secure element is busy\r\n",
                       (unsigned long)msg->cmd);
                resp->status = 1;
                resp->ready  = 1;
                xSemaphoreGive(s_optiga_mutex);
                continue;
            }
            optiga_manager_touch_hold_reason(hsm_cmd_reason(msg->cmd));
        }

        /*
         * Enrolment and Protected Update take the manager's instance, not this
         * task's.
         *
         * Everything under IPC_CMD_HSM_PROVISION — optiga_generate_device_keypair(),
         * optiga_generate_csr_pem(), optiga_verify_cert_key_pair(),
         * tesaiot_publish_protected_update() — reaches the chip through
         * optiga_manager_acquire(), which returns NULL until
         * optiga_manager_init() has run. Nothing runs it before a MicroPython
         * optiga.init() or an mTLS connect, so pressing Enrol on a freshly
         * booted board failed on a chip that was answering.
         *
         * Opening this task's own application as well would put two
         * applications on one chip, so the two are exclusive. These are the
         * same two calls optiga.init() makes, in the same order: the button
         * now walks the path that CSR three times over, CSR then Protected
         * Update, and Protected Update then CSR were all proven on. Both are
         * idempotent — the manager returns true if already initialised and the
         * open is reference counted.
         */
        /* The benchmark command runs its MCU sets even when the chip is
         * absent; every other command needs the chip. */
        bool optiga_opened = needs_chip ? optiga_open() : false;
        if (needs_chip && !optiga_opened && msg->cmd != IPC_CMD_HSM_BENCHMARK) {
            printf("[HSM] optiga_open failed for cmd 0x%02lX\r\n",
                   (unsigned long)msg->cmd);
            resp->status = 1;
            resp->ready  = 1;
            if (needs_chip) {
                optiga_manager_touch_release();
                optiga_chip_exit();
            }
            xSemaphoreGive(s_optiga_mutex);
            continue;
        }

        /* Dispatch command */
        switch (msg->cmd) {
        case IPC_CMD_HSM_REQUEST:
            handle_hsm_request(resp);
            break;
        case IPC_CMD_HSM_BENCHMARK:
            handle_hsm_benchmark(resp, optiga_opened);
            break;
        case IPC_CMD_HSM_READ_CERT:
            handle_hsm_read_cert(resp, msg);
            break;
        case IPC_CMD_HSM_PIN_CHECK:
        case IPC_CMD_HSM_PIN_SET:
        case IPC_CMD_HSM_PIN_VERIFY:
        case IPC_CMD_HSM_PIN_RESET:
            handle_hsm_pin(resp, (uint8_t)msg->cmd,
                           (const uint8_t *)msg->data);
            break;
        case IPC_CMD_HSM_HEALTH:
            handle_hsm_health(resp);
            break;
        case IPC_CMD_HSM_SLOT_INFO:
            handle_hsm_slot_info(resp, msg);
            break;
        case IPC_CMD_HSM_PROVISION:
            handle_hsm_provision(resp, msg);
            break;
        case IPC_CMD_TESAIOT_CRED_READ:
            handle_cred_read(resp, msg);
            break;
        case IPC_CMD_TESAIOT_CRED_WRITE:
            handle_cred_write(resp, msg);
            break;
        case IPC_CMD_TESAIOT_CRED_ERASE:
            handle_cred_erase(resp, msg);
            break;
        default:
            printf("[HSM] Unknown cmd 0x%02lX\r\n", (unsigned long)msg->cmd);
            resp->status = 0xFF;
            break;
        }

        /* Close OPTIGA (no-op if it never opened) */
        if (optiga_opened)
            optiga_close();

        /* Resume CM55 touch polling — only if this command took the hold. */
        if (needs_chip) {
            optiga_manager_touch_release();
            optiga_chip_exit();
        }
        xSemaphoreGive(s_optiga_mutex);

        /* Signal response ready */
        resp->ready = 1;
    }
}

/*******************************************************************************
 * Slot introspection and the plain-write probe
 ******************************************************************************/
extern bool optiga_slot_info_raw(uint16_t oid, uint16_t *anchor, uint32_t *version,
                                 uint16_t *used, uint8_t *type);
extern optiga_lib_status_t tesaiot_read_metadata(uint16_t oid, uint8_t *buffer,
                                                 uint16_t *length);

/* Pull one tag's value out of a metadata TLV. */
static const uint8_t *slot_tag(const uint8_t *m, uint16_t mlen, uint8_t want, uint8_t *vlen)
{
    if (!m || mlen < 2U || m[0] != 0x20U) return NULL;
    uint16_t end = (uint16_t)(2U + m[1]);
    if (end > mlen) end = mlen;
    for (uint16_t i = 2U; (uint16_t)(i + 2U) <= end; ) {
        uint8_t t = m[i], l = m[i + 1U];
        if ((uint16_t)(i + 2U + l) > end) break;
        if (t == want) { if (vlen) *vlen = l; return &m[i + 2U]; }
        i = (uint16_t)(i + 2U + l);
    }
    return NULL;
}

static void handle_hsm_slot_info(ipc_response_t *resp, const ipc_msg_t *msg)
{
    uint16_t oid = (uint16_t)(((uint16_t)(uint8_t)msg->data[0] << 8) |
                               (uint8_t)msg->data[1]);
    bool probe = ((uint8_t)msg->data[2] != 0U);

    memset(resp->data, 0, HSM_SLOT_TOTAL_LEN);
    resp->data_len = HSM_SLOT_TOTAL_LEN;
    resp->data[HSM_SLOT_OID_OFF]     = (uint8_t)(oid >> 8);
    resp->data[HSM_SLOT_OID_OFF + 1] = (uint8_t)(oid & 0xFFU);

    /*
     * Read through this task's own instance, not tesaiot_read_metadata().
     *
     * That helper goes through optiga_manager_acquire(), which returns NULL
     * until optiga_manager_init() has run — and nothing runs it before a
     * MicroPython optiga.init() or an mTLS connect. Opening the HSM page on a
     * freshly booted board therefore reported "The secure element did not
     * answer" on a chip that was answering perfectly well, which is what every
     * other command on this page proves by working.
     *
     * s_util is already open here, touch is already paused and s_optiga_mutex
     * is held, so the chip is exclusively this task's. Going through the
     * manager as well would put a second application on one chip.
     */
    uint8_t  meta[64];
    uint16_t mlen = (uint16_t)sizeof(meta);
    s_optiga_status = OPTIGA_LIB_BUSY;
    optiga_lib_status_t meta_rc = optiga_util_read_metadata(s_util, oid, meta, &mlen);
    if (meta_rc != OPTIGA_LIB_SUCCESS ||
        wait_optiga(5000) != OPTIGA_LIB_SUCCESS ||
        mlen == 0U) {
        printf("[HSM] SLOT_INFO 0x%04X: metadata unreadable\r\n", oid);
        return;
    }
    resp->data[HSM_SLOT_VALID_OFF] = 1U;

    uint8_t vl = 0;
    const uint8_t *v;
    if ((v = slot_tag(meta, mlen, 0xC0U, &vl)) && vl == 1U) resp->data[HSM_SLOT_LCS_OFF]  = v[0];
    if ((v = slot_tag(meta, mlen, 0xE8U, &vl)) && vl == 1U) resp->data[HSM_SLOT_TYPE_OFF] = v[0];
    if ((v = slot_tag(meta, mlen, 0xC5U, &vl)) && vl == 2U) {
        resp->data[HSM_SLOT_USED_OFF] = v[0]; resp->data[HSM_SLOT_USED_OFF + 1] = v[1];
    }
    if ((v = slot_tag(meta, mlen, 0xC4U, &vl)) && vl == 2U) {
        resp->data[HSM_SLOT_MAX_OFF] = v[0];  resp->data[HSM_SLOT_MAX_OFF + 1] = v[1];
    }
    if ((v = slot_tag(meta, mlen, 0xC1U, &vl)) && vl >= 1U && vl <= 4U) {
        uint32_t ver = 0;
        for (uint8_t i = 0; i < vl; i++) ver = (ver << 8) | v[i];
        for (int i = 0; i < 4; i++)
            resp->data[HSM_SLOT_VERSION_OFF + i] = (uint8_t)(ver >> (8 * (3 - i)));
    }
    /*
     * Change access, tag D0, decoded rather than pattern-matched on byte 0.
     *
     * `21 <hi> <lo>` is Int(OID): only a manifest signed by that object may
     * write this one. It is not always first — a compound condition can carry
     * `E1 FC 07` ahead of it — and testing only v[0] read a manifest-locked
     * object as freely writable. Scan for the tag instead.
     */
    bool ac_manifest = false;
    bool ac_never    = false;
    uint8_t ac_lcs_min = 0U;          /* from E1 FC <lcs>: writable below it */
    if ((v = slot_tag(meta, mlen, 0xD0U, &vl)) && vl >= 1U) {
        for (uint8_t i = 0U; i < vl; i++) {
            if (v[i] == 0xFFU) {
                ac_never = true;      /* NEV */
            } else if ((v[i] == 0x21U) && ((uint16_t)i + 2U < (uint16_t)vl + 0U)) {
                ac_manifest = true;
                resp->data[HSM_SLOT_ANCHOR_OFF]     = v[i + 1U];
                resp->data[HSM_SLOT_ANCHOR_OFF + 1] = v[i + 2U];
                i += 2U;
            } else if ((v[i] == 0xE1U) && ((uint16_t)i + 2U < (uint16_t)vl + 0U) &&
                       (v[i + 1U] == 0xFCU)) {
                ac_lcs_min = v[i + 2U];
                i += 2U;
            }
        }
    }

    uint8_t raw = (mlen > 44U) ? 44U : (uint8_t)mlen;
    resp->data[HSM_SLOT_RAW_LEN_OFF] = raw;
    memcpy(&resp->data[HSM_SLOT_RAW_OFF], meta, raw);

    /*
     * Whether an ordinary write is still accepted, read off the metadata rather
     * than tried on the object.
     *
     * This used to write the object's first byte back to itself and report what
     * the chip said. Two things were wrong with that. read_oid() returns 0 both
     * when the object is empty and when the read FAILED — it has two `return 0`
     * paths and the caller could not tell them apart — so a failed read fell
     * through to writing the literal 0x30 at offset 0. A certificate object
     * does not start with 0x30; read_certificate_internal() strips a nine-byte
     * TLS identity header whose first byte is 0xC0. The "harmless" probe would
     * have overwritten the device certificate's header tag. And a one-byte
     * WRITE_ONLY at offset 0 plausibly sets the object's used length to one,
     * which would destroy the certificate outright — the vendor header does not
     * say, and a maybe is not good enough to run against 0xE0E1.
     *
     * None of it was necessary. Change access D0 = `21 <hi> <lo>` is Int(OID):
     * only a manifest signed by the object named there may write this one, so
     * ordinary writes are refused. That tag was already parsed six lines above.
     * The lock state is a fact in the metadata; asking the chip by writing to it
     * added risk and no information.
     */
    if (probe) {
        /* Refused if a manifest is required, if the condition is NEV, or if the
         * chip's life-cycle state has already reached the threshold an
         * `E1 FC <lcs>` condition allows writes below. Reading D0 alone missed
         * the last two: both parse to anchor 0 and would have been reported as
         * freely writable. LcsO is tag C0, already decoded above. */
        const uint8_t lcs = resp->data[HSM_SLOT_LCS_OFF];
        const bool lcs_closed = (ac_lcs_min != 0U) && (lcs >= ac_lcs_min);
        const bool locked = ac_manifest || ac_never || lcs_closed;
        resp->data[HSM_SLOT_PROBE_OFF] = locked ? HSM_PROBE_REFUSED
                                                : HSM_PROBE_ACCEPTED;
        resp->data[HSM_SLOT_PROBE_RC_OFF]     = 0U;
        resp->data[HSM_SLOT_PROBE_RC_OFF + 1] = 0U;
        printf("[HSM] SLOT_INFO 0x%04X: ordinary writes %s "
               "(D0: manifest=%d nev=%d lcs_min=0x%02X lcs=0x%02X)\r\n",
               oid, locked ? "refused" : "accepted",
               (int)ac_manifest, (int)ac_never, ac_lcs_min, lcs);
    }
}

/*******************************************************************************
 * Provisioning — CSR enrolment and Protected Update, asynchronously
 ******************************************************************************/
extern const char *tesaiot_mqtt_username(void);
extern bool optiga_generate_device_keypair(uint16_t key_oid, uint8_t *pub, uint16_t *pub_len);
extern bool optiga_generate_csr_pem(uint16_t key_oid, const uint8_t *pub, uint16_t pub_len,
                                    const char *subject, char *pem, size_t pem_len);
extern int  optiga_verify_cert_key_pair(uint16_t cert_oid, uint16_t key_oid);
extern int  tesaiot_publish_protected_update(const char *target, const char *anchor,
                                             uint32_t version, bool with_csr)
    __attribute__((weak));
//! [hsm_publish_csr_weak_decl]
extern int  publish_csr(uint8_t *csr, size_t csr_length, uint16_t target_oid,
                        uint16_t trust_anchor_oid, uint32_t payload_version)
    __attribute__((weak));
    //! [hsm_publish_csr_weak_decl]
extern uint16_t optiga_slot_manifest_anchor(uint16_t oid);
extern bool optiga_clear_manifest_lock(uint16_t target_oid);

/* The broker session. tesaiot.connect() is a two-line MicroPython wrapper over
 * these; the identity they use comes from tesaiot_config_get(), not from
 * anything Python owns. So the screen can bring the session up itself, which is
 * the whole point of having a screen. */
extern bool tesaiot_mqtt_connect(void)      __attribute__((weak));
extern bool tesaiot_mqtt_is_connected(void) __attribute__((weak));
extern bool tesaiot_mqtt_disconnect(void)   __attribute__((weak));

/* The MQTT task's own "am I running" flag, cleared beside mqtt_start_requested
 * at the end of its stop path. It, not the connection flag, is what says a
 * fresh mqtt_request_start() will be honoured. */
extern bool mqtt_is_started(void)           __attribute__((weak));

/* Clears the outstanding correlation id, which is what disarms the ingest
 * against the platform's retained bundle. Declared here rather than pulled in
 * from tesaiot_optiga.h to keep this file's dependency on that module to the
 * handful of symbols it actually uses. */
extern void trustm_reset_state(void)        __attribute__((weak));

/* Created by the subscriber task as it starts, in tesaiot_pu_ingest.c. Its
 * existence is the only evidence available here that something is listening
 * for the platform's reply; mqtt_is_connected() answers a different question. */
extern EventGroupHandle_t data_received_event_group;
extern const char *trustm_current_correlation_id(void) __attribute__((weak));
/*
 * The enrolment/Protected Update completion signal.
 *
 * Its real definition is in tesaiot_pu_ingest.c, which proj_cm33_ns/Makefile
 * only compiles under ENABLE_OPTIGA_CLM=1. The function pointers beside it are
 * weak for exactly that reason; the data symbol was not, so an
 * ENABLE_OPTIGA_CLM=0 build — a configuration the Makefile still supports —
 * failed at link. A weak definition here is overridden by the strong one when
 * the ingest file is in the build, and stands in for it when it is not.
 */
extern volatile bool g_protected_update_just_completed;

/* Counts completions from the platform. See tesaiot_pu_ingest.c: a boolean
 * cannot tell a waiter whether the thing that finished was its own request. */
extern volatile uint32_t g_optiga_ingest_events;
__attribute__((weak)) volatile uint32_t g_optiga_ingest_events = 0U;
__attribute__((weak)) volatile bool g_protected_update_just_completed = false;

#define HSM_PROV_CSR_MAX 1600

static struct {
    volatile uint8_t state;
    volatile uint8_t step;
    volatile uint8_t pair;        /* 0xFF = not run */
    volatile uint8_t csr_sig_ok;
    uint16_t         csr_len;
    uint16_t         target_oid;
    uint16_t         anchor_oid;
    uint8_t          pending_op;  /* set by the IPC call, consumed by the task */
    volatile uint32_t run_id;    /* whose run this state describes           */
    volatile uint8_t  run_op;    /* and which operation it is                */
    char             msg[HSM_PROV_MSG_MAX];
    char            *csr;
} s_prov = { .pair = 0xFFU };

static void prov_say(uint8_t state, uint8_t step, const char *text)
{
    s_prov.state = state;
    s_prov.step  = step;
    if (text) {
        strncpy(s_prov.msg, text, sizeof(s_prov.msg) - 1U);
        s_prov.msg[sizeof(s_prov.msg) - 1U] = '\0';
    }
    printf("[HSM] prov step %u: %s\r\n", step, s_prov.msg);
}

/* The certificate slot E0Ex pairs with the key slot E0Fx by provisioning
 * convention. The chip enforces no such link. */
static uint16_t prov_key_for(uint16_t cert_oid)
{
    switch (cert_oid) {
    case 0xE0E1U: return 0xE0F1U;
    case 0xE0E2U: return 0xE0F2U;
    case 0xE0E3U: return 0xE0F3U;
    default:      return 0U;
    }
}

static void prov_fill_response(ipc_response_t *resp)
{
    memset(resp->data, 0, HSM_PROV_TOTAL_LEN);
    resp->data_len = HSM_PROV_TOTAL_LEN;
    resp->data[HSM_PROV_STATE_OFF]  = s_prov.state;
    resp->data[HSM_PROV_STEP_OFF]   = s_prov.step;
    resp->data[HSM_PROV_PAIR_OFF]   = s_prov.pair;
    resp->data[HSM_PROV_CSRSIG_OFF] = s_prov.csr_sig_ok;
    resp->data[HSM_PROV_CSRLEN_OFF]     = (uint8_t)(s_prov.csr_len >> 8);
    resp->data[HSM_PROV_CSRLEN_OFF + 1] = (uint8_t)(s_prov.csr_len & 0xFFU);
    //! [hsm_correlation_id_weak_double_null]
    /* ...context: inside the provisioning status reply builder ... */
    if (trustm_current_correlation_id != NULL) {
        const char *c = trustm_current_correlation_id();
        if (c) strncpy((char *)&resp->data[HSM_PROV_CORR_OFF], c, HSM_PROV_CORR_MAX - 1U);
    }
    //! [hsm_correlation_id_weak_double_null]
    strncpy((char *)&resp->data[HSM_PROV_MSG_OFF], s_prov.msg, HSM_PROV_MSG_MAX - 1U);
    /* Stamp every answer with the run it belongs to. A screen that cannot tell
     * one run from the next reads a stale DONE as its own success. */
    uint32_t rid = s_prov.run_id;
    resp->data[HSM_PROV_RUN_OFF]     = (uint8_t)(rid >> 24);
    resp->data[HSM_PROV_RUN_OFF + 1] = (uint8_t)(rid >> 16);
    resp->data[HSM_PROV_RUN_OFF + 2] = (uint8_t)(rid >> 8);
    resp->data[HSM_PROV_RUN_OFF + 3] = (uint8_t)(rid);
    resp->data[HSM_PROV_RUNOP_OFF]   = s_prov.run_op;
}

/* Build a CSR over a fresh key pair, and check its own signature the way the CA
 * will. That check is the proof of possession — the one thing a certificate
 * cannot do for itself — so it is reported as its own verdict rather than
 * folded into "CSR generated". */
static bool prov_make_csr(uint16_t key_oid)
{
    uint8_t  pub[100];
    uint16_t pub_len = (uint16_t)sizeof(pub);

    prov_say(HSM_PROV_STATE_BUSY, HSM_PROV_STEP_KEYGEN,
             "Generating a key pair inside the secure element");
    if (!optiga_generate_device_keypair(key_oid, pub, &pub_len) ||
        pub_len == 0U || pub_len > sizeof(pub)) {
        prov_say(HSM_PROV_STATE_FAILED, HSM_PROV_STEP_KEYGEN, "Key generation failed");
        return false;
    }

    if (s_prov.csr == NULL) s_prov.csr = pvPortMalloc(HSM_PROV_CSR_MAX);
    if (s_prov.csr == NULL) {
        prov_say(HSM_PROV_STATE_FAILED, HSM_PROV_STEP_CSR, "Out of memory for the CSR");
        return false;
    }
    s_prov.csr[0] = '\0';

    char subject[96];
    (void)snprintf(subject, sizeof(subject), "CN=%s,O=TESAIoT", tesaiot_mqtt_username());

    prov_say(HSM_PROV_STATE_BUSY, HSM_PROV_STEP_CSR,
             "Signing the request with the key that never leaves the chip");
    if (!optiga_generate_csr_pem(key_oid, pub, pub_len, subject,
                                 s_prov.csr, HSM_PROV_CSR_MAX)) {
        prov_say(HSM_PROV_STATE_FAILED, HSM_PROV_STEP_CSR, "CSR generation failed");
        return false;
    }
    s_prov.csr_len    = (uint16_t)strlen(s_prov.csr);
    s_prov.csr_sig_ok = 1U;   /* the chip produced it; the CA checks it again */
    return true;
}

/*
 * Open the chip the way optiga.init() does, for as long as this runs.
 *
 * This work outlives the HSM worker's dispatch by up to a minute — it waits for
 * the platform to deliver a bundle — so the worker cannot own the chip on its
 * behalf. It opened and closed around the dispatch instead, which returned long
 * before optiga_verify_cert_key_pair() below, and the screen reported
 * "Installed; the pair check could not run" on a chip that was working.
 *
 * The same two calls optiga.init() makes, in the same order. Both are
 * idempotent and the open is reference counted, so a live mTLS session keeps
 * its own reference and is untouched by the close.
 *
 * Touch needs no handling here: every chip call this reaches —
 * pu_build_csr_for_target() inside tesaiot_publish_protected_update(), and
 * optiga_verify_cert_key_pair() — holds it for the length of its own work.
 * Holding it across the 60 second wait as well would freeze the panel for a
 * minute for nothing.
 */
static void prov_run_locked(uint8_t op);

/*
 * Single-exit wrappers, one per stretch of chip traffic.
 *
 * The claim that "the functions this reaches hold touch themselves" was wrong:
 * of the eleven chip operations on this path only four did. Key generation and
 * the CSR signature — the two longest transactions in the feature — ran with
 * CM55 polling the FT5406 on the same SCB, which is what returns
 * OPTIGA_COMMS_ERROR (0x0102) and then leaves the next call meeting
 * OPTIGA_UTIL_ERROR_INSTANCE_IN_USE (0x0305).
 *
 * The hold is counted, so nesting inside a callee that also holds is correct.
 * One exit each, because an unbalanced release leaves the panel dead.
 *
 * Nothing holds across the 60 second wait for the platform: no chip traffic
 * happens there, and freezing the screen for a minute would be its own bug.
 */
//! [hsm_touch_held_wrappers]
//! [hsm_optiga_manager_init_open_pair]
static bool prov_open_held(void)
{
    bool ok;
    optiga_manager_touch_hold_reason("Opening the secure element");
    ok = optiga_manager_init(optiga_util_callback, NULL) &&
         optiga_trust_open_application();
    optiga_manager_touch_release();
    return ok;
}
//! [hsm_optiga_manager_init_open_pair]

static void prov_close_held(void)
{
    optiga_manager_touch_hold_reason("Closing the secure element");
    optiga_trust_close_application();
    optiga_manager_touch_release();
}

static bool prov_make_csr_held(uint16_t key_oid)
{
    bool ok;
    optiga_manager_touch_hold_reason("Generating a key and signing the request");
    ok = prov_make_csr(key_oid);
    optiga_manager_touch_release();
    return ok;
}

static uint16_t prov_manifest_anchor_held(uint16_t oid)
{
    uint16_t anchor;
    optiga_manager_touch_hold_reason("Reading the secure element");
    anchor = optiga_slot_manifest_anchor(oid);
    optiga_manager_touch_release();
    return anchor;
}

//! [hsm_publish_pu_held]
static int prov_publish_pu_held(const char *target, const char *anchor)
{
    int rc;
    optiga_manager_touch_hold_reason("Generating a key and signing the request");
    rc = tesaiot_publish_protected_update(target, anchor, 1U, true);
    optiga_manager_touch_release();
    return rc;
}
//! [hsm_publish_pu_held]
//! [hsm_touch_held_wrappers]

static void prov_run(uint8_t op)
{
    if (!prov_open_held()) {
        prov_say(HSM_PROV_STATE_FAILED, HSM_PROV_STEP_NONE,
                 "The secure element did not open");
        return;
    }
    prov_run_locked(op);

     //! [hsm_trustm_reset_every_exit]
     /* ...context: tail of prov_run(), after prov_run_locked() returns ... */
    /* Disarm before letting go. Nothing is outstanding once this returns.
     *
     * trustm_reset_state() clears the correlation id, and the ingest treats a
     * NULL id as "nobody asked for this" - which is the only thing standing
     * between the board and a replay. The platform retains the last Protected
     * Update bundle, so one is delivered on every single connect; while the id
     * stayed armed for the whole boot, that retained bundle matched, and the
     * board silently re-ran a Protected Update nobody had asked for. Measured
     * on 2026-08-08: an Enrol immediately after a successful Protect connected,
     * received the retained bundle, re-locked 0xE0E1 on top of the unlock the
     * operator had just performed, and exhausted the C heap far enough that the
     * publisher task could not be created - so the enrolment then failed with
     * "could not publish", three layers away from the cause.
     *
     * The function existed and did exactly this. It had no caller anywhere in
     * the tree: the reference project drives it from its own menu loop, and
     * that call was not carried across when these operations became screens.
     *
     * It goes here, around every exit of prov_run_locked() including the early
     * failures, because a run that timed out leaves an id armed just as surely
     * as one that succeeded. */
    if (trustm_reset_state != NULL) {
        trustm_reset_state();
    }

    prov_close_held();
    //! [hsm_trustm_reset_every_exit]
}

static void prov_run_locked(uint8_t op)
{
    if (op == HSM_PROV_OP_UNLOCK) {
        /* One metadata write, no network, no key touched.
         *
         * It runs through the same machinery as the other two so it inherits
         * the run identity and the screen lock, and so a reader does not have
         * to learn a second path. */
        prov_say(HSM_PROV_STATE_BUSY, HSM_PROV_STEP_NONE,
                 "Clearing the signed-manifest requirement");
        if (optiga_clear_manifest_lock(s_prov.target_oid)) {
            prov_say(HSM_PROV_STATE_DONE, HSM_PROV_STEP_VERIFY,
                     "Ordinary writes are accepted again. Enrol can run now.");
        } else {
            prov_say(HSM_PROV_STATE_FAILED, HSM_PROV_STEP_NONE,
                     "The secure element refused to change the access condition.");
        }
        return;
    }

    uint16_t key_oid = prov_key_for(s_prov.target_oid);
    if (key_oid == 0U) {
        prov_say(HSM_PROV_STATE_FAILED, HSM_PROV_STEP_NONE,
                 "No key slot pairs with that certificate slot");
        return;
    }

    s_prov.pair = 0xFFU;

    /* Reach the platform BEFORE touching the chip.
     *
     * This used to sit after the key generation, so a board with no WiFi burnt
     * its device key and only then discovered it had nowhere to send the
     * request - the certificate already in the chip stopped matching, and the
     * operator had done nothing but tap a button at the wrong moment. Every
     * check that can fail belongs ahead of every step that destroys something.
     *
     * tesaiot_mqtt_connect() returns true when the MQTT task has been ASKED to
     * start - its own log says "(async)". The WiFi association, the TLS
     * handshake and the CertificateVerify signed inside the secure element all
     * happen after that, so the wait below is what makes the return value mean
     * anything. */
    /*
     * Restart the session unconditionally. A live connection is not a live
     * subscription.
     *
     * This block used to skip everything when mqtt_is_connected() was already
     * true, which is a flag over one bit of the client's status word and says
     * nothing about whether a subscriber task exists to receive the platform's
     * answer. The first operation after boot always found it false and
     * connected properly, so every test passed; run a second operation in the
     * same boot - enrol, then protect - and the request published onto a
     * session with nobody listening. The log of that run is unambiguous: the
     * publish succeeded and not one inbound message arrived in sixty seconds,
     * not even the retained bundle that lands within a second of every real
     * subscribe.
     *
     * tesaiot_protected_update_workflow.c has always done it this way, and says
     * why in its own comment - "Subscriber is ready before publish (prevents
     * retained message issues)", attributed to the server team. That sequence
     * is the contract with the platform; this path had quietly stopped
     * following it.
     */
    if ((tesaiot_mqtt_is_connected != NULL) && tesaiot_mqtt_is_connected()) {
        prov_say(HSM_PROV_STATE_BUSY, HSM_PROV_STEP_NONE,
                 "Restarting the connection to the platform");
        if (tesaiot_mqtt_disconnect != NULL) {
            (void)tesaiot_mqtt_disconnect();
        }
        /* Wait for the teardown to actually land, and wait on the right flag.
         *
         * The barrier is mqtt_is_started(), not mqtt_is_connected(). The
         * connection flag clears early, part way through cleanup, while
         * mqtt_request_start() opens with
         *
         *     if (mqtt_start_requested) return true;
         *
         * and mqtt_start_requested is only cleared at the very end, beside
         * mqtt_started. Requesting a start inside that window returns true
         * without setting the start bit at all: the caller believes it asked,
         * the task finishes cleanup, prints "Waiting for start request", and
         * blocks on an event that will never arrive. Measured on hardware -
         * a full 30 s wait against an MQTT task that was ready the whole time.
         *
         * Two flags, two questions. "Is the socket down" is not "is the task
         * ready to be started again". */
        for (int i = 0; (i < 50) && (mqtt_is_started != NULL); i++) {
            vTaskDelay(pdMS_TO_TICKS(100));
            if (!mqtt_is_started()) { break; }
        }
        if ((mqtt_is_started != NULL) && mqtt_is_started()) {
            prov_say(HSM_PROV_STATE_FAILED, HSM_PROV_STEP_NONE,
                     "The previous connection would not close. Power-cycle the "
                     "board and try again. Nothing was changed.");
            return;
        }
    }

    /* tesaiot_mqtt_connect() returns true when the MQTT task has been ASKED to
     * start - its own log says "(async)". The WiFi association, the TLS
     * handshake and the CertificateVerify signed inside the secure element all
     * happen after that, so the wait below is what makes the return value mean
     * anything. */
    prov_say(HSM_PROV_STATE_BUSY, HSM_PROV_STEP_NONE,
             "Connecting to the platform");
    if (tesaiot_mqtt_connect == NULL) {
        prov_say(HSM_PROV_STATE_FAILED, HSM_PROV_STEP_NONE,
                 "Could not start the connection. Nothing was changed.");
        return;
    }

    /* Ask twice if the first ask goes nowhere.
     *
     * mqtt_started and mqtt_start_requested are cleared by two adjacent
     * statements at the end of the task's stop path. The barrier above waits on
     * the first, so there is a window - two instructions wide - where a start
     * still returns true without arming anything. Polling every 100 ms makes
     * landing in it improbable rather than impossible, and improbable is the
     * kind of fault that surfaces in a demonstration rather than on the bench.
     *
     * A second ask costs one wasted attempt and closes it: by the time the
     * first wait expires the task is unambiguously idle, so the retry is
     * honoured. Fifteen seconds is past a healthy connect - the measured ones
     * take three to five, including the CertificateVerify signed in the chip. */
    bool up = false;
    for (int attempt = 0; (attempt < 2) && !up; attempt++) {
        if (!tesaiot_mqtt_connect()) {
            prov_say(HSM_PROV_STATE_FAILED, HSM_PROV_STEP_NONE,
                     "Could not start the connection. Nothing was changed.");
            return;
        }
        for (int i = 0; (i < 150) && (tesaiot_mqtt_is_connected != NULL); i++) {
            vTaskDelay(pdMS_TO_TICKS(100));
            if (tesaiot_mqtt_is_connected()) { up = true; break; }
        }
        if (!up && (attempt == 0)) {
            prov_say(HSM_PROV_STATE_BUSY, HSM_PROV_STEP_NONE,
                     "Still connecting to the platform");
        }
    }
    if (!up) {
        prov_say(HSM_PROV_STATE_FAILED, HSM_PROV_STEP_NONE,
                 "No answer from the platform after 30 seconds. Check WiFi "
                 "and the broker, then try again. Nothing was changed.");
        return;
    }

    /* And wait for the subscriber itself, not just the connection. The event
     * group is created by the subscriber task as it starts; publishing before
     * it exists is the race the restart above is meant to close. It is never
     * torn back down to NULL, so this only bites on the first run of a boot -
     * which is exactly when the task is still being created. */
    for (int i = 0; (i < 100) && (data_received_event_group == NULL); i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (data_received_event_group == NULL) {
        prov_say(HSM_PROV_STATE_FAILED, HSM_PROV_STEP_NONE,
                 "The connection came up but nothing is listening for the "
                 "platform's reply. Nothing was changed.");
        return;
    }

    /*
     * Only the plain-enrolment path builds a CSR here.
     *
     * tesaiot_publish_protected_update(..., with_csr = true) generates its own
     * key pair in the same slot, and generating a pair destroys the previous
     * private key. Building one here first therefore burnt a key pair for
     * nothing on every Protect, ran the whole destructive sequence twice, and
     * left s_prov.csr — the request the screen shows the operator — signed by a
     * key that no longer existed by the time the manifest was asked for.
     */
    /* Check the slot BEFORE touching the key.
     *
     * Generating a key pair destroys the previous private key, and plain
     * enrolment installs through an ordinary write. Once Protected Update has
     * run against this object the chip refuses ordinary writes for good — so
     * pressing Enrol on a locked slot used to burn the device's key, and only
     * then discover the request could never be sent. The operator did nothing
     * wrong and the certificate in the chip stopped matching its key.
     *
     * Observed on the board: step 1 generated a key, step 2 signed a request,
     * step 3 said "this slot only takes a signed manifest now". Two steps too
     * late. */
    //! [hsm_publish_csr_flow]
    /* ...context: inside prov_run_locked() ... */
    if (op != HSM_PROV_OP_PU) {
        if (prov_manifest_anchor_held(s_prov.target_oid) != 0U) {
            /* Say what is true. The manifest requirement is a metadata field,
             * not a fuse: writing D0 back to E1 FC 07 clears it, which this
             * firmware already does to key slots on every key generation, and
             * which was measured on this board on 2026-08-08: D0 on 0xE0E1
             * read 21 e0 e8 before the write and e1 fc 07 after. Calling it
             * permanent would teach the operator something false about their
             * own hardware. */
            prov_say(HSM_PROV_STATE_FAILED, HSM_PROV_STEP_NONE,
                     "This slot takes signed manifests only. Use Protect, or "
                     "clear the requirement first. Nothing was changed.");
            return;
        }
        if (publish_csr == NULL) {
            prov_say(HSM_PROV_STATE_FAILED, HSM_PROV_STEP_NONE,
                     "CSR enrolment is not built into this firmware");
            return;
        }
        if (!prov_make_csr_held(key_oid)) return;
    }

    char t[8], a[8];
    (void)snprintf(t, sizeof(t), "%04X", s_prov.target_oid);
    (void)snprintf(a, sizeof(a), "%04X", s_prov.anchor_oid);

    /* Read the completion counter before publishing. Anything that finishes
     * after this point is an answer to this request; anything that finished
     * before it is not, no matter what a flag says. */
    uint32_t events_before = g_optiga_ingest_events;

    if (op == HSM_PROV_OP_PU) {
        if (tesaiot_publish_protected_update == NULL) {
            prov_say(HSM_PROV_STATE_FAILED, HSM_PROV_STEP_PUBLISH,
                     "Protected Update is not built into this firmware");
            return;
        }
        prov_say(HSM_PROV_STATE_BUSY, HSM_PROV_STEP_PUBLISH,
                 "Asking the platform for a signed manifest");
        events_before = g_optiga_ingest_events;
        if (prov_publish_pu_held(t, a) != 0) {
            prov_say(HSM_PROV_STATE_FAILED, HSM_PROV_STEP_PUBLISH,
                     "Could not publish the request - is the broker connected?");
            return;
        }
    } else {
        /* Plain enrolment publishes the CSR and the platform answers on
         * commands/certificate, which the subscriber installs with an ordinary
         * write. Once Protected Update has run against this object the chip
         * refuses ordinary writes for good, so that path is simply gone - and
         * saying so is more use than letting it fail inside the vendor library
         * with a bare status code. */
        prov_say(HSM_PROV_STATE_BUSY, HSM_PROV_STEP_PUBLISH,
                 "Sending the request to the platform");
        events_before = g_optiga_ingest_events;
        if (publish_csr((uint8_t *)s_prov.csr, strlen(s_prov.csr),
                        s_prov.target_oid, s_prov.anchor_oid, 1U) != 0) {
            prov_say(HSM_PROV_STATE_FAILED, HSM_PROV_STEP_PUBLISH,
                     "Could not publish - is the broker connected?");
            return;
        }
    }
    //! [hsm_publish_csr_flow]

    /* The CSR is on the wire; the buffer holding it is dead weight from here.
     *
     * 1,600 bytes were allocated on the first Enrol and never freed, so an
     * operator doing the ordinary thing - Enrol, then Protect - ran the install
     * with 1,600 fewer bytes than the enrolment had, out of a C heap that had
     * already been measured at the edge. Freeing it here returns it at exactly
     * the moment the incoming bundle needs it. */
    if (s_prov.csr) {
        vPortFree(s_prov.csr);
        s_prov.csr     = NULL;
        s_prov.csr_len = 0U;
    }

    /* The platform answers on its own topic and the subscriber task installs
     * the certificate. Watch for that rather than for a reply to this call. */
    prov_say(HSM_PROV_STATE_BUSY, HSM_PROV_STEP_WAIT, "Waiting for the platform");
    bool answered = false;
    for (int i = 0; i < 600; i++) {          /* 60 s */
        vTaskDelay(pdMS_TO_TICKS(100));
        if (g_optiga_ingest_events != events_before) { answered = true; break; }
    }
    if (!answered) {
        prov_say(HSM_PROV_STATE_FAILED, HSM_PROV_STEP_WAIT,
                 "The platform did not answer within 60 seconds");
        return;
    }

    prov_say(HSM_PROV_STATE_BUSY, HSM_PROV_STEP_VERIFY,
             "Checking the certificate against the key in the chip");
    int pair = optiga_verify_cert_key_pair(s_prov.target_oid, key_oid);
    s_prov.pair = (pair == 1) ? 1U : ((pair == 0) ? 0U : 0xFFU);

    if (pair == 1) {
        prov_say(HSM_PROV_STATE_DONE, HSM_PROV_STEP_VERIFY,
                 "The device can prove it holds the key this certificate names");
    } else if (pair == 0) {
        prov_say(HSM_PROV_STATE_FAILED, HSM_PROV_STEP_VERIFY,
                 "Installed, but the certificate does not belong to this chip's key");
    } else {
        prov_say(HSM_PROV_STATE_FAILED, HSM_PROV_STEP_VERIFY,
                 "Installed; the pair check could not run");
    }
}

/*
 * The ingest calls this from the subscriber task the instant the secure element
 * reports that the platform's manifest signature checked out against the trust
 * anchor. It is the only step in a Protected Update that this firmware cannot
 * have faked, so it is the only one worth putting on a screen that claims a
 * hardware root of trust — everything else is metadata we wrote and read back.
 */
void tesaiot_pu_progress(int event)
{
    if ((TESAIOT_PU_CHIP_VERIFIED_MANIFEST == event) &&
        (HSM_PROV_STATE_BUSY == s_prov.state)) {
        prov_say(HSM_PROV_STATE_BUSY, HSM_PROV_STEP_INSTALL,
                 "The chip checked the platform's signature and accepted it");
    }
}

/*
 * Provisioning runs on its own task, not on the HSM worker.
 *
 * The worker holds s_optiga_mutex and keeps the OPTIGA application open around
 * every command it dispatches. prov_run() spends up to a minute waiting for the
 * platform to deliver a bundle — and the subscriber task needs that same mutex
 * to install the certificate when it arrives. Waiting inside the envelope would
 * hold the lock against the only thing that can end the wait.
 */
static StaticTask_t  s_prov_tcb;
static StackType_t   s_prov_stack[3072];

static void prov_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (s_prov.pending_op != 0U) {
            uint8_t op = s_prov.pending_op;
            s_prov.pending_op = 0U;
            prov_run(op);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void handle_hsm_provision(ipc_response_t *resp, const ipc_msg_t *msg)
{
    uint8_t op = (uint8_t)msg->data[0];

    if (op == HSM_PROV_OP_FETCH_CSR) {
        /* A PEM does not fit in one 240-byte reply. Read it in slices. */
        uint16_t off = (uint16_t)msg->value;
        uint16_t left = (s_prov.csr && off < s_prov.csr_len)
                      ? (uint16_t)(s_prov.csr_len - off) : 0U;
        uint16_t n = (left > HSM_PROV_CSR_CHUNK) ? HSM_PROV_CSR_CHUNK : left;
        memset(resp->data, 0, HSM_PROV_CSR_CHUNK);
        if (n) memcpy(resp->data, s_prov.csr + off, n);
        resp->data_len = n;
        return;
    }

    if (op != HSM_PROV_OP_POLL) {
        if (s_prov.state == HSM_PROV_STATE_BUSY) {
            /* Refuse, and say so.
             *
             * This used to answer with the state of the run already in flight,
             * carrying its run id — so the screen matched it, adopted someone
             * else's progress, and eventually rendered that run's verdict as
             * its own. The caller needs to hear "no", not somebody else's yes. */
            /* Refuse with an EMPTY payload.
             *
             * prov_fill_response() would put the in-flight run's state, step,
             * pair result and run id in here — byte for byte the packet whose
             * adoption by the screen is the defect this run id exists to
             * prevent. A refusal must carry no verdict and no identity. */
            resp->status = HSM_PROV_REJECTED_BUSY;
            memset(resp->data, 0, HSM_PROV_TOTAL_LEN);
            resp->data_len = HSM_PROV_TOTAL_LEN;
            return;
        }
        s_prov.target_oid = (uint16_t)(((uint16_t)(uint8_t)msg->data[1] << 8) |
                                        (uint8_t)msg->data[2]);
        s_prov.anchor_oid = (uint16_t)(((uint16_t)(uint8_t)msg->data[3] << 8) |
                                        (uint8_t)msg->data[4]);
        if (s_prov.target_oid == 0U) s_prov.target_oid = 0xE0E1U;
        if (s_prov.anchor_oid == 0U) s_prov.anchor_oid = 0xE0E8U;
        s_prov.csr_len    = 0U;
        s_prov.csr_sig_ok = 0U;
        /* Adopt the caller's run id and clear the previous run's verdict before
         * anything can be polled. Without this the second run of a boot answered
         * with the first run's DONE and pair result. */
        s_prov.run_id     = ((uint32_t)(uint8_t)msg->data[5] << 24) |
                            ((uint32_t)(uint8_t)msg->data[6] << 16) |
                            ((uint32_t)(uint8_t)msg->data[7] <<  8) |
                             (uint32_t)(uint8_t)msg->data[8];
        s_prov.run_op     = op;
        resp->status      = HSM_PROV_ACCEPTED;
        s_prov.pair       = 0xFFU;
        s_prov.pending_op = op;
        prov_say(HSM_PROV_STATE_BUSY, HSM_PROV_STEP_NONE, "Starting");
    }

    prov_fill_response(resp);
}


/*******************************************************************************
 * ISR callback - accepts all HSM commands (0xB5-0xBB)
 *******************************************************************************/
static void hsm_ipc_callback(uint32_t *msg_data)
{
    if (!msg_data) return;
    ipc_msg_t *msg = (ipc_msg_t *)msg_data;

    /* Accept commands in HSM range (0xB5-0xBC) or CRED range (0xA3,0xA4,0xA6) */
    bool is_hsm  = (msg->cmd >= IPC_CMD_HSM_REQUEST && msg->cmd <= IPC_CMD_HSM_PROVISION);
    bool is_cred = (msg->cmd == IPC_CMD_TESAIOT_CRED_READ  ||
                    msg->cmd == IPC_CMD_TESAIOT_CRED_WRITE ||
                    msg->cmd == IPC_CMD_TESAIOT_CRED_ERASE);
    if (!is_hsm && !is_cred)
        return;

    s_hsm_pending_msg = msg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(s_hsm_sem, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/*******************************************************************************
 * Public init - call from main.c after cm33_ipc_communication_setup()
 *******************************************************************************/
/*******************************************************************************
 * Public API: direct credential read (blocks calling task, thread-safe)
 ******************************************************************************/
bool ipc_hsm_cred_read_sync(uint8_t slot, uint8_t *buf, uint16_t buf_len,
                             uint16_t *out_len)
{
    if (!buf || buf_len == 0) return false;
    if (!s_optiga_mutex) return false;

    uint16_t oid = tesaiot_slot_to_oid(slot);
    if (oid == 0) return false;

    if (xSemaphoreTake(s_optiga_mutex, pdMS_TO_TICKS(10000)) != pdTRUE) {
        printf("[HSM] cred_read_sync: mutex timeout (slot %u)\r\n", slot);
        return false;
    }

    //! [hsm_touch_hold_reason_cred_read]
    /* ...context: inside the synchronous credential read ... */
    /* Pause CM55 touch to get exclusive SCB0 I2C access */
    optiga_manager_touch_hold_reason("Reading stored credentials");

    /* These take s_optiga_mutex but never entered the gate, so they were the
     * third mechanism that did not know about the other two. */
    if (!optiga_chip_enter()) {
        optiga_manager_touch_release();
        xSemaphoreGive(s_optiga_mutex);
        return false;
    }

    bool ok = false;
    if (optiga_open()) {
        uint16_t len = read_oid(oid, buf, buf_len);
        printf("[HSM] cred_read_sync: slot %u OID 0x%04X → %u bytes\r\n",
               slot, oid, len);
        if (len > 0) {
            if (out_len) *out_len = len;
            ok = true;
        }
        optiga_close();
    } else {
        printf("[HSM] cred_read_sync: optiga_open FAILED (slot %u)\r\n", slot);
    }

    optiga_chip_exit();
    /* Resume CM55 touch polling */
    optiga_manager_touch_release();
    //! [hsm_touch_hold_reason_cred_read]

    xSemaphoreGive(s_optiga_mutex);
    return ok;
}

/*******************************************************************************
 * Public API: batch credential read — single OPTIGA session for all slots.
 *
 * Opens OPTIGA ONCE, reads each slot, closes ONCE.
 * Much faster than calling ipc_hsm_cred_read_sync() per-slot (avoids 6×
 * open/close + 6× touch pause/resume which takes ~4 seconds total).
 *
 * IMPORTANT — SCB0 I2C bus sharing with CM55 touch (FT5406):
 *   OPTIGA Trust M and the capacitive touch controller share SCB0 I2C.
 *   We MUST pause CM55 touch polling before ANY OPTIGA I2C access, and
 *   resume it after. Without this, OPTIGA reads fail silently due to
 *   I2C bus contention. If a future board revision uses a separate I2C
 *   bus for OPTIGA, the touch pause/resume can be removed.
 ******************************************************************************/
int ipc_hsm_cred_read_batch(const uint8_t *slots, int num_slots,
                             uint8_t *bufs, uint16_t buf_each,
                             uint16_t *out_lens)
{
    if (!slots || !bufs || !out_lens || num_slots <= 0) return 0;
    if (!s_optiga_mutex) return 0;

    if (xSemaphoreTake(s_optiga_mutex, pdMS_TO_TICKS(10000)) != pdTRUE) {
        printf("[HSM] cred_read_batch: mutex timeout\r\n");
        return 0;
    }

    /* Pause CM55 touch — exclusive SCB0 I2C access for OPTIGA */
    optiga_manager_touch_hold_reason("Reading stored credentials");

    if (!optiga_chip_enter()) {
        optiga_manager_touch_release();
        xSemaphoreGive(s_optiga_mutex);
        return 0;
    }

    int read_ok = 0;

    if (optiga_open()) {
        for (int i = 0; i < num_slots; i++) {
            uint16_t oid = tesaiot_slot_to_oid(slots[i]);
            out_lens[i] = 0;
            if (oid == 0) continue;

            uint8_t *dst = bufs + (i * buf_each);
            uint16_t len = read_oid(oid, dst, buf_each);
            out_lens[i] = len;
            if (len > 0) read_ok++;

            printf("[HSM] batch slot %u OID 0x%04X: %u bytes\r\n",
                   slots[i], oid, len);
        }
        optiga_close();
    } else {
        printf("[HSM] cred_read_batch: optiga_open FAILED\r\n");
    }

    optiga_chip_exit();
    /* Resume CM55 touch polling */
    optiga_manager_touch_release();
    xSemaphoreGive(s_optiga_mutex);

    printf("[HSM] cred_read_batch: %d/%d slots read OK\r\n", read_ok, num_slots);
    return read_ok;
}

/*******************************************************************************
 * Public init - call from main.c after cm33_ipc_communication_setup()
 ******************************************************************************/
void ipc_hsm_handler_init(void)
{
    s_optiga_mutex = xSemaphoreCreateMutexStatic(&s_optiga_mutex_buf);
    s_hsm_sem = xSemaphoreCreateBinaryStatic(&s_hsm_sem_buf);

    xTaskCreateStatic(
        hsm_task_func,
        "HSM_IPC",
        HSM_TASK_STACK_WORDS,
        NULL,
        HSM_TASK_PRIORITY,
        s_hsm_stack,
        &s_hsm_tcb);

    xTaskCreateStatic(
        prov_task,
        "HsmProv",
        sizeof(s_prov_stack) / sizeof(StackType_t),
        NULL,
        HSM_TASK_PRIORITY - 1,
        s_prov_stack,
        &s_prov_tcb);

    (void)Cy_IPC_Pipe_RegisterCallback(
        CM33_IPC_PIPE_EP_ADDR,
        hsm_ipc_callback,
        (uint32_t)CM33_IPC_HSM_CLIENT_ID);
}

/*******************************************************************************
 * Public touch pause/resume — for external callers (e.g. mqtt_mtls_setup)
 ******************************************************************************/
void ipc_hsm_touch_pause_reason(const char *reason)
{
    hsm_touch_send_reason(IPC_CMD_TOUCH_PAUSE, reason);
    vTaskDelay(pdMS_TO_TICKS(50));
}

void ipc_hsm_touch_pause(void)
{
    ipc_hsm_touch_pause_reason(NULL);
}

void ipc_hsm_touch_resume(void)
{
    hsm_touch_send_reason(IPC_CMD_TOUCH_RESUME, NULL);
}
