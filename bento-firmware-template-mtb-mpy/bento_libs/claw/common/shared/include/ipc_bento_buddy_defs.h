/*******************************************************************************
 * File Name: ipc_bento_buddy_defs.h
 *
 * Description: Shared defines for the Bento Desktop Buddy IPC channel
 *              between CM33_NS (BLE NUS task) and CM55 (LVGL UI).
 *
 *              Follows the same pattern as ipc_bentoclaw_defs.h — a single
 *              client ID registered with the IPC pipe, plus update-type
 *              codes carried in ipc_msg_t.cmd / .value.
 *
 *              Direction of data flow:
 *                CM33_NS -> CM55 : BLE heartbeat, approval prompts, tokens
 *                CM55 -> CM33_NS : Approve/Deny button actions (TX on NUS)
 *
 *******************************************************************************/

#ifndef IPC_BENTO_BUDDY_DEFS_H
#define IPC_BENTO_BUDDY_DEFS_H

#include <stdint.h>

/*******************************************************************************
 * IPC Pipe client ID — distinct from CLAW (3) / SENSORHUB / SERVICE.
 ******************************************************************************/
#define CM55_IPC_BENTO_BUDDY_CLIENT_ID   (4UL)
/* Reverse direction: CM33_NS registers a callback with this client id so
 * Start/Stop button taps on the LCD can reach the ble_nus lazy trigger. */
#define CM33_IPC_BENTO_BUDDY_CLIENT_ID   (5UL)

/*******************************************************************************
 * Commands carried in ipc_msg_t.cmd
 * CM33_NS -> CM55 uses 0xCC* range (distinct from CLAW 0xCA/0xCB).
 * CM55 -> CM33_NS uses 0xCD* range.
 ******************************************************************************/
#define IPC_CMD_BUDDY_UI_UPDATE      (0xCC)  /* data[0]=update_type, data[1..]=payload */
#define IPC_CMD_BUDDY_PASSKEY        (0xCE)  /* data=6-digit ASCII passkey during pairing */
#define IPC_CMD_BUDDY_USER_DECISION  (0xCD)  /* value=0 deny, 1 approve, data=request id */
#define IPC_CMD_BUDDY_REQUEST_START  (0xCF)  /* CM55 -> CM33_NS: tap on buddy card → bring up stack */
#define IPC_CMD_BUDDY_REQUEST_STOP   (0xD0)  /* CM55 -> CM33_NS: user backed out → tear stack down */

/* Bento Desktop Buddy notification + character + hint handlers.
 * Values overlap numerically with IPC_CMD_WIFI_* in ipc_communication.h but
 * dispatch is per-client-id (Cy_IPC_Pipe_RegisterCallback indexes by
 * client_id), and Buddy lives under CM55_IPC_BENTO_BUDDY_CLIENT_ID=4 while
 * WiFi lives on a separate client id — no runtime collision. */
#define IPC_CMD_BUDDY_NOTIFY_SHOW    (0xD1)  /* CM33_NS -> CM55: OS notification mirror
                                              * data layout: id\0source\0title\0body\0icon\0
                                              *              + 1 byte priority + 2 bytes ttl_ms LE */
#define IPC_CMD_BUDDY_CHARACTER_SET  (0xD2)  /* CM33_NS -> CM55: force Lottie state
                                              * value = duration_ms (clamped to uint16)
                                              * data  = animation_id NUL-terminated ASCII */
#define IPC_CMD_BUDDY_HINT_TEXT      (0xD3)  /* CM33_NS -> CM55: marquee hint text
                                              * value = scroll_speed px/s (clamped to uint16)
                                              * data  = UTF-8 text NUL-terminated (<=120 B) */

/* Device -> Desktop. CM55 LCD-originated NUS events — media tile taps,
 * long-press system commands, notification ack buttons. CM33_NS forwards
 * the packed JSON payload straight to ble_nus_send via nus_emit_event(). */
#define IPC_CMD_BUDDY_DEVICE_CMD     (0xD4)  /* CM55 -> CM33_NS: device-originated NUS event
                                              * value = payload length (bytes, excl. NUL)
                                              * data  = UTF-8 JSON (no trailing newline;
                                              *         CM33 side appends '\n' before send) */

/* Desktop -> Device agent reply (final text of a bento.agent.ask flow).
 * CM33_NS nus_agent.c accumulates stream tokens and, on bento.agent.done,
 * forwards the final text string here so the CM55 Bento Buddy page can
 * render it in the ASK submode. Shares the 0xD* cmd range with WiFi codes
 * by client-id isolation — same collision-note as IPC_CMD_BUDDY_NOTIFY_SHOW
 * (PDL pipe dispatches by client_id, and Buddy lives on its own client). */
#define IPC_CMD_BUDDY_AGENT_RESPONSE (0xD5)  /* CM33_NS -> CM55: final agent answer
                                              * value = payload length (bytes, excl. NUL)
                                              * data  = UTF-8 text NUL-terminated */

/* Firmware-update physical-ack prompt (SPEC §4.8.5 + ISSUE-027).
 * A USB flash MUST NOT proceed without on-device Y/N confirmation showing the
 * first 8 hex chars of the target SHA-256. The decision returns on the
 * existing IPC_CMD_BUDDY_USER_DECISION pipe (value=0 decline, 1 approve) with
 * data[] carrying the fixed request id "fw.update" so the CM33 handler can
 * distinguish this from approval-prompt responses. */
#define IPC_CMD_BUDDY_FW_UPDATE_PROMPT (0xD6)  /* CM33_NS -> CM55: full-screen Y/N
                                                * value = prompt TTL ms (30000 default)
                                                * data  = hash_prefix8 \0 fw_version \0
                                                *         (ASCII, NUL-separated) */

/* Voice-loop scaffold (SPEC §6 stretch). CM55 taps a mic button in the
 * STATUS submode, which posts this to CM33; the CM33 side calls
 * voice_capture_start() with the supplied clip id to produce stub
 * `bento.voice.chunk` frames on the wire. A follow-up STOP uses the same
 * cmd byte with `value=0`. */
#define IPC_CMD_BUDDY_VOICE_CAPTURE   (0xD7)   /* CM55 -> CM33_NS: mic tap
                                                * value = 1 start, 0 stop
                                                * data  = clip id \0 (ASCII, NUL-terminated)
                                                *         only used when value == 1 */

/*******************************************************************************
 * Notification + character payload size constants (shared between CM33 queue
 * and CM55 renderer so both sides truncate identically).
 ******************************************************************************/
#define BUDDY_NOTIF_ID_MAX        (16)   /* "n_abc123" style, truncated */
#define BUDDY_NOTIF_SOURCE_MAX    (16)   /* "Messages", "Calendar" */
#define BUDDY_NOTIF_TITLE_MAX     (40)   /* displayed truncated */
#define BUDDY_NOTIF_BODY_MAX      (40)   /* displayed truncated */
#define BUDDY_NOTIF_ICON_MAX      (8)    /* small UTF-8 emoji/ASCII hint */
#define BUDDY_HINT_TEXT_MAX       (120)  /* per SPEC §5.1.3 max 120 chars */
#define BUDDY_CHARACTER_ID_MAX    (16)   /* "celebrate", "attention" */
#define BUDDY_NOTIF_RING_SIZE     (8)    /* recent notifications buffer */
#define BUDDY_PENDING_ACKS_MAX    (4)    /* concurrent ack_required ids (SPEC §7.5) */

/*******************************************************************************
 * UI update types (data[0] in IPC_CMD_BUDDY_UI_UPDATE)
 * MUST match the switch in page_bento_buddy_ipc_update().
 ******************************************************************************/
#define BUDDY_UI_STATE        (0)   /* 1 byte: bento_buddy_state_t */
#define BUDDY_UI_PROMPT       (1)   /* UTF-8 approval prompt text */
#define BUDDY_UI_MESSAGE      (2)   /* Recent session message snippet */
#define BUDDY_UI_TOKENS       (3)   /* 4 bytes LE: tokens_today uint32 */
#define BUDDY_UI_PASSKEY      (4)   /* 6 ASCII digits: pairing passkey */
#define BUDDY_UI_CLEAR_PROMPT (5)   /* no payload */
#define BUDDY_UI_ADV_NAME     (6)   /* up to 16 ASCII bytes — full advertised name e.g. "Bento-3B96"; CM55 caches and renders in BLE state line */

#endif /* IPC_BENTO_BUDDY_DEFS_H */
