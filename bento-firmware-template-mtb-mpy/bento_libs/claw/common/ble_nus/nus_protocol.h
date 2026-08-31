/*******************************************************************************
 * File Name: nus_protocol.h
 *
 * Description: Public API of the Bento Desktop Buddy NUS wire protocol layer.
 *              See TESAIoT_PLAN/2026-4/Claude_Desktop_Buddy/FRAMER_CONTRACT.md
 *              for the wire-format spec this module implements (historical
 *              snapshot — Bento forked this protocol under its own branding;
 *              see Bento_Buddy/SPEC.md §3.1.1 Not Anthropic-compatible).
 *
 *              The protocol layer sits between ble_nus (AIROC BLE transport)
 *              and the CM55 UI (via IPC). It does:
 *                - newline-framed assembler with 4 KB overflow + RESYNC
 *                - jsmn-based JSON parse with 256-token ceiling
 *                - dispatch table for heartbeat / turn event / cmd / time frames
 *                - IPC emit to CM55 for state/prompt/message/tokens
 *                - ack/permission TX back to desktop via ble_nus_send
 *                - passive 30-s keepalive watchdog
 *
 ******************************************************************************/

#ifndef NUS_PROTOCOL_H
#define NUS_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Call once at ble_nus_init time (after BLE stack init). */
void nus_protocol_init(void);

/* Feed RX bytes from the NUS RX characteristic. May deliver one full frame,
 * a partial frame, or multiple concatenated frames. Non-blocking. */
void nus_on_rx_bytes(const uint8_t *buf, size_t len);

/* Passive watchdog tick — call periodically (e.g., from UI render at ~10 Hz).
 * Emits BUDDY_UI_STATE=SLEEP via IPC if 30 s have passed without a parseable
 * frame and the link is still considered connected. */
void nus_protocol_tick(uint32_t now_ms);

/* Called from ble_nus when Approve/Deny button pressed.
 * decision_approve: true = "once", false = "deny". Emits the permission JSON
 * frame on NUS TX with the stored prompt id. */
void nus_protocol_send_permission(const char *id, size_t id_len, int decision_approve);

/* Called from ble_nus BTM_ENCRYPTION_STATUS_EVT handler.
 * When true, status ack's data.sec field reports "sec":true. */
void nus_protocol_set_link_encrypted(int encrypted);

#ifdef __cplusplus
}
#endif

#endif /* NUS_PROTOCOL_H */
