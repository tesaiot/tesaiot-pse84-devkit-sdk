/*******************************************************************************
 * File Name: tacp.h
 *
 * Description: TESAIoT Control Protocol (TACP) — header file.
 *              Defines magic bytes, command IDs, ring buffer, and the new
 *              binary file transfer sub-protocol for fast IDE uploads.
 *
 * Protocol Summary:
 *   Control commands:  0xAA 0x55 <CMD>        (3 bytes, instant)
 *   File transfer:     0xAA 0x55 0x20 <frame>  (binary bulk, see below)
 *
 * Binary File Transfer Sub-Protocol (TACP_CMD_FILE_XFER = 0x20):
 * ───────────────────────────────────────────────────────────────
 *   After the 3-byte magic, the host sends a binary frame:
 *
 *   Byte 0      : path_len          (1 byte, max 127)
 *   Byte 1..N   : path              (path_len bytes, UTF-8, e.g. "/main.py")
 *   Byte N+1..4 : file_len          (4 bytes, little-endian uint32)
 *   Byte N+5..M : file_data         (file_len bytes, raw content)
 *   Byte M+1..2 : crc16             (2 bytes, little-endian, CRC-CCITT)
 *
 *   Device responds over UART with a single-line ASCII response:
 *     "TACP:FILE_OK <path> <size>\r\n"      on success
 *     "TACP:FILE_ERR <code> <msg>\r\n"       on failure
 *
 *   Error codes:
 *     1 = path too long          4 = CRC mismatch
 *     2 = file too large         5 = filesystem write error
 *     3 = timeout                6 = out of memory
 *
 *   Flow control: after receiving the header (path_len + path + file_len),
 *   the device sends "TACP:FILE_RDY\r\n" to signal it is ready for data.
 *   The host should wait for this before streaming file_data bytes.
 *
 *   Timeout: 5 seconds between any two bytes during transfer.
 *
 * Copyright (c) 2026 TESAIoT
 ******************************************************************************/

#ifndef TACP_H
#define TACP_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Magic Bytes & Command IDs
 ******************************************************************************/
#define TACP_MAGIC_0            0xAA
#define TACP_MAGIC_1            0x55

/* Existing control commands (3-byte: AA 55 CMD) */
#define TACP_CMD_TERMINATE      0x01    /* Ctrl-C: KeyboardInterrupt          */
#define TACP_CMD_PROGRAM_MODE   0x02    /* Safe boot + soft reset for upload  */
#define TACP_CMD_CONNECT        0x03    /* IDE ping (no side effects)         */
#define TACP_CMD_STATUS         0x04    /* Show USB icon on LCD               */

/* Firmware version query (AA 55 05) — responds with build info */
#define TACP_CMD_VERSION_QUERY  0x05    /* Query firmware build info          */

/* Hard reset (AA 55 06) — NVIC_SystemReset(), all cores restart */
#define TACP_CMD_HARD_RESET     0x06    /* Force hardware reset immediately   */

/* Binary file transfer (AA 55 20 <frame>) */
#define TACP_CMD_FILE_XFER      0x20    /* Start binary file upload           */

/*******************************************************************************
 * Board SKU Registry
 * Format: KIT_<chip>_<variant>_<revision>
 * ─────────────────────────────────────────────
 * KIT_PSE84_AI_001         AI Kit Sensor Hub
 * KIT_PSE84_AI_002         AI Kit Game Console
 * KIT_PSE84_EVAL_EPC2_001  Eva Kit Sensor Hub
 * ─────────────────────────────────────────────
 * Add new boards by incrementing revision number.
 * SKU is defined in mpconfigboard.h (default) or overridden
 * via -DMICROPY_HW_BOARD_SKU in per-project Makefile.micropython.
 ******************************************************************************/

/*******************************************************************************
 * Binary File Transfer Limits
 ******************************************************************************/
#define TACP_FILE_PATH_MAX      127     /* Max path length in bytes           */
#define TACP_FILE_SIZE_MAX      (512 * 1024)  /* 512 KB max file size         */
#define TACP_FILE_CHUNK_SIZE    512     /* Internal write chunk size          */
#define TACP_FILE_TIMEOUT_MS    5000    /* Byte-level timeout in ms           */

/*******************************************************************************
 * Ring Buffer
 ******************************************************************************/
#define TACP_RING_BUF_SIZE      256     /* Must be power of 2                 */

typedef struct {
    uint8_t buf[TACP_RING_BUF_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
} tacp_ring_buf_t;

/*******************************************************************************
 * Public API
 ******************************************************************************/

/** Initialize TACP state machine and ring buffer. */
void tacp_init(void);

/** Poll UART for TACP commands and REPL bytes.
 *  Returns true if a command was detected. */
bool tacp_poll_uart(void);

/** Read one byte from the ring buffer (for REPL consumption).
 *  Returns -1 if empty. */
int tacp_ring_buf_read(void);

/** Check if the ring buffer has data. */
bool tacp_ring_buf_readable(void);

#ifdef __cplusplus
}
#endif

#endif /* TACP_H */
