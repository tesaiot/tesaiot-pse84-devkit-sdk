/*******************************************************************************
 * File Name: ipc_hsm_handler.h
 *
 * Description: HSM IPC handler — CM33_NS side.
 *              Receives HSM IPC commands from CM55 (page_hsm.c),
 *              performs OPTIGA Trust M operations, returns response
 *              via shared memory.
 *
 *              Supported commands:
 *                IPC_CMD_HSM_REQUEST   - Read chip data (UID, LCS, certs, counters)
 *                IPC_CMD_HSM_BENCHMARK - Run crypto benchmarks
 *                IPC_CMD_HSM_READ_CERT - Read + parse X.509 cert DER
 *                IPC_CMD_HSM_PIN_CHECK - Check if PIN exists
 *                IPC_CMD_HSM_PIN_SET   - Store SHA-256(PIN) in OPTIGA DATA_3
 *                IPC_CMD_HSM_PIN_VERIFY- Verify PIN against stored hash
 *                IPC_CMD_HSM_HEALTH    - Run 8 self-tests
 *
 *******************************************************************************/

#ifndef IPC_HSM_HANDLER_H
#define IPC_HSM_HANDLER_H

#include <stdint.h>
#include <stdbool.h>

/* IPC_CMD_HSM_SLOT_INFO and IPC_CMD_HSM_PROVISION response layouts live in
 * ipc_communication.h, which both cores already include. Every other layout in
 * this file is mirrored by hand into proj_cm55/.../page_hsm.c and the two copies
 * have to be kept in step; there is no reason to add a third pair. */

/*******************************************************************************
 * IPC_CMD_HSM_REQUEST response layout (41 bytes)
 *******************************************************************************/
#define HSM_RESP_UID_OFF     0    /* 27 bytes: Trust M UID */
#define HSM_RESP_LCS_OFF     27   /*  1 byte:  Lifecycle state */
#define HSM_RESP_CTR0_OFF    28   /*  4 bytes: Counter 0 (big-endian) */
#define HSM_RESP_CTR1_OFF    32   /*  4 bytes: Counter 1 (big-endian) */
#define HSM_RESP_CERT_OFF    36   /*  4 bytes: Cert present (E0E0..E0E3) */
#define HSM_RESP_HEALTH_OFF  40   /*  1 byte:  1=operational, 0=error */
#define HSM_RESP_TOTAL_LEN   41

/*******************************************************************************
 * IPC_CMD_HSM_BENCHMARK response layout (61 bytes)
 *
 * Three result sets of 5 operations, each timing a uint32_t LE in
 * MICROSECONDS. Op order within every set:
 *   [0] Random 32B, [1] SHA-256 256B, [2] ECC P-256 keygen,
 *   [3] ECDSA sign, [4] ECDSA verify
 *
 * Set 0: OPTIGA Trust M (secure element)
 * Set 1: MCU hardware   (MXCRYPTO via mbedTLS ALT + PDL ECC)
 * Set 2: MCU software   (local SHA-256/DRBG + mbedTLS SW ECC)
 *
 * data[60] = 1 if OPTIGA responded, 0 if absent (MCU sets still valid).
 * Timing value 0xFFFFFFFF = operation unavailable or failed.
 *******************************************************************************/
#define HSM_BENCH_COUNT          5
#define HSM_BENCH_SET_OPTIGA     0
#define HSM_BENCH_SET_MCU_HW     1
#define HSM_BENCH_SET_MCU_SW     2
#define HSM_BENCH_SET_COUNT      3
#define HSM_BENCH_SET_LEN        (HSM_BENCH_COUNT * 4)
#define HSM_BENCH_SET_OFF(s)     ((s) * HSM_BENCH_SET_LEN)
#define HSM_BENCH_PRESENT_OFF    (HSM_BENCH_SET_COUNT * HSM_BENCH_SET_LEN)
#define HSM_BENCH_TOTAL_LEN      (HSM_BENCH_PRESENT_OFF + 1)
#define HSM_BENCH_FAIL_SENTINEL  0xFFFFFFFFu

/*******************************************************************************
 * IPC_CMD_HSM_READ_CERT response layout
 * resp->cmd = cert slot index (0-3) on input
 * data[0..1] = cert DER size (uint16_t LE)
 * data[2..]  = packed null-terminated strings:
 *   [subject_cn\0][issuer_cn\0][org\0][country\0][not_before\0][not_after\0]
 *******************************************************************************/
#define HSM_CERT_SIZE_OFF      0   /* uint16_t LE: DER cert size */
#define HSM_CERT_STRINGS_OFF   2   /* Packed null-terminated strings */

/*******************************************************************************
 * IPC_CMD_HSM_PIN_CHECK response: data[0] = 1 if PIN set, 0 if not
 * IPC_CMD_HSM_PIN_SET:   resp->data[0..3] = 4 digits (input from CM55)
 * IPC_CMD_HSM_PIN_VERIFY: resp->data[0..3] = 4 digits (input from CM55)
 *   response: data[0] = 1 if match, 0 if mismatch
 *******************************************************************************/
#define HSM_PIN_DATA_OID       0xF1D2   /* OPTIGA DATA_3 (same as hsm_dashboard.py) */

/*******************************************************************************
 * IPC_CMD_HSM_HEALTH response layout (8 bytes)
 * Each byte = 1 pass, 0 fail. Order: HW, UID, Cert, RNG, SHA, ECC, RW, Meta
 *******************************************************************************/
#define HSM_HEALTH_HW_OFF      0
#define HSM_HEALTH_UID_OFF     1
#define HSM_HEALTH_CERT_OFF    2
#define HSM_HEALTH_RNG_OFF     3
#define HSM_HEALTH_SHA_OFF     4
#define HSM_HEALTH_ECC_OFF     5
#define HSM_HEALTH_RW_OFF      6
#define HSM_HEALTH_META_OFF    7
#define HSM_HEALTH_TOTAL_LEN   8

void ipc_hsm_handler_init(void);

/**
 * @brief Read a credential slot directly from OPTIGA (synchronous, blocking).
 *
 * Thread-safe: serialized with HSM IPC task via internal mutex.
 * Must be called from task context (not ISR).
 *
 * @param slot    OPTIGA slot number (5, 6, 8, 9, 10, 11)
 * @param buf     Output buffer
 * @param buf_len Buffer capacity
 * @param out_len Actual bytes read (set on success, may be NULL)
 * @return true on success, false on timeout/error/OPTIGA unavailable
 */
bool ipc_hsm_cred_read_sync(uint8_t slot, uint8_t *buf, uint16_t buf_len,
                             uint16_t *out_len);

/**
 * @brief Batch-read multiple credential slots in a single OPTIGA session.
 *
 * Opens OPTIGA once, reads all requested slots, closes once.
 * Much faster than per-slot ipc_hsm_cred_read_sync() calls.
 *
 * Thread-safe: serialized with HSM IPC task via internal mutex.
 * Must be called from task context (not ISR).
 *
 * IMPORTANT — SCB0 I2C bus sharing:
 *   OPTIGA Trust M and CM55 touch (FT5406) share SCB0 I2C.
 *   This function pauses CM55 touch before OPTIGA access and
 *   resumes it after. If a future board revision separates the
 *   I2C buses, the touch pause/resume can be removed.
 *
 * @param slots     Array of OPTIGA slot numbers (5, 6, 8, 9, 10, 11)
 * @param num_slots Number of slots to read
 * @param bufs      Output buffer (contiguous: num_slots × buf_each bytes)
 * @param buf_each  Capacity per slot in bufs
 * @param out_lens  Array of uint16_t[num_slots] — actual bytes read per slot
 * @return Number of slots successfully read (0..num_slots)
 */
int ipc_hsm_cred_read_batch(const uint8_t *slots, int num_slots,
                             uint8_t *bufs, uint16_t buf_each,
                             uint16_t *out_lens);

/**
 * @brief Pause CM55 touch polling (free SCB0 I2C bus for OPTIGA access).
 *        Includes 50 ms settle delay after sending IPC_CMD_TOUCH_PAUSE.
 */
void ipc_hsm_touch_pause(void);

/* As above, but names what the device is doing so CM55 can show it on the
 * "touch paused" overlay. */
void ipc_hsm_touch_pause_reason(const char *reason);

/**
 * @brief Resume CM55 touch polling after OPTIGA access is complete.
 */
void ipc_hsm_touch_resume(void);

#endif /* IPC_HSM_HANDLER_H */
