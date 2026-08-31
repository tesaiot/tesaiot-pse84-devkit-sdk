/*******************************************************************************
 * File Name: ble_nus.h
 *
 * Description: Nordic UART Service (NUS) transport for BENTO Bento Desktop
 *              Buddy. Runs on CM33_NS over AIROC CYW55500 BLE host stack.
 *              Wire protocol: JSON messages framed by NUS RX/TX characteristics
 *              (see https://github.com/anthropics/claude-desktop-buddy REFERENCE.md
 *              for the original protocol source — Bento forked this protocol
 *              under its own branding; see Bento_Buddy/SPEC.md §3.1.1 Not
 *              Anthropic-compatible).
 *
 *              Exclusive with WiFi: CYW55500 has a single RF transceiver.
 *              When BLE is active, the WiFi stack must not be initialized.
 *
 *              NUS UUIDs (128-bit, Nordic-defined):
 *                Service:  6E400001-B5A3-F393-E0A9-E50E24DCCA9E
 *                RX char:  6E400002-...  (host -> device, WRITE/WRITE_NO_RSP)
 *                TX char:  6E400003-...  (device -> host, NOTIFY)
 *
 *******************************************************************************/

#ifndef BLE_NUS_H
#define BLE_NUS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Max payload size per NUS frame (BLE MTU 247 minus ATT header). */
#define BLE_NUS_MAX_PAYLOAD 244

/** Connection state reported to higher layers. */
typedef enum {
    BLE_NUS_STATE_OFF = 0,       /* stack not initialized */
    BLE_NUS_STATE_ADVERTISING,   /* broadcasting, waiting for Bento Desktop Buddy */
    BLE_NUS_STATE_CONNECTED,     /* paired, NUS exchanging data */
    BLE_NUS_STATE_ERROR,
} ble_nus_state_t;

/** RX callback invoked from BLE task context when host writes to NUS RX.
 *  Payload is NOT null-terminated. Copy before returning. */
typedef void (*ble_nus_rx_cb_t)(const uint8_t *data, size_t len, void *user_ctx);

/** State-change callback (advertising -> connected -> disconnected). */
typedef void (*ble_nus_state_cb_t)(ble_nus_state_t state, void *user_ctx);

typedef struct {
    const char         *device_name;    /* Advertised name, e.g. "BENTO Buddy" */
    ble_nus_rx_cb_t     on_rx;
    ble_nus_state_cb_t  on_state;
    void               *user_ctx;
} ble_nus_config_t;

/** Initialize the AIROC BLE host stack, register NUS GATT service, and
 *  start advertising. Returns false on stack init failure (check UART log). */
bool ble_nus_init(const ble_nus_config_t *cfg);

/** Send a notification on NUS TX characteristic. Fragmented into MTU-sized
 *  chunks internally. Thread-safe (enqueues to BLE task). Returns bytes queued,
 *  or -1 if disconnected / not initialized. */
int ble_nus_send(const uint8_t *data, size_t len);

/** Soft-stop: stop advertising + drop any active GATT link, but keep the
 *  AIROC host stack alive. Pair with `ble_nus_rearm_advertising` to
 *  toggle the link without re-running the heavy stack init/deinit cycle
 *  (which crashed CM33 when invoked from the IPC RX task — see ISSUE-029). */
void ble_nus_deinit(void);

/** Re-arm undirected-high advertising on a stack that's already been
 *  initialized once via `ble_nus_init`. Idempotent — safe to call from any
 *  state (skips the start_advertisements call if state isn't OFF). */
void ble_nus_rearm_advertising(void);

/** Current connection state (polled from UI layer). */
ble_nus_state_t ble_nus_get_state(void);

/** Pointer to the static "Bento-XXXX" advertising name (lifetime = process).
 *  Returns NULL until `ble_nus_init` has resolved the local BD address
 *  and built the suffix. */
const char *ble_nus_get_adv_name(void);

/** Weak callback for a pairing passkey notification.
 *
 *  NOT REACHED IN THIS BUILD, and no passkey is ever shown. Pairing is
 *  configured as Just Works: ble_nus.c sets
 *      local_io_cap = BTM_IO_CAPABILITIES_NONE     (NoInputNoOutput)
 *      auth_req     = BTM_LE_AUTH_REQ_SC_BOND      (no MITM bit)
 *  and a NoInputNoOutput association never raises
 *  BTM_PASSKEY_NOTIFICATION_EVT, so this callback is not invoked.
 *
 *  The DisplayOnly + 6-digit passkey flow it was written for is NOT YET
 *  IMPLEMENTED. The hook is kept so the integration layer (e.g.
 *  bento_buddy_task.c) can forward the code to the CM55 UI once that flow
 *  ships. The default implementation is a no-op. */
void ble_nus_passkey_cb(const char *passkey_6_digits);

/** Diagnostic snapshot exposed via `bento.fw.query`'s `_diag.ble` block.
 *  Lets the desktop root-cause Issue #2 (after a BLE link drop the host
 *  cannot rediscover the board without a power-cycle) by reporting what
 *  the disconnect callback did, whether wiced_bt_start_advertisements
 *  succeeded, and the deinit-in-progress flag state at the moment of
 *  disconnect. Numeric `last_advert_restart_result` follows the
 *  `wiced_result_t` enum (0 = WICED_BT_SUCCESS). */
typedef struct {
    uint32_t disconnect_count;                       /* total disconnect events seen */
    uint8_t  last_disconnect_reason;                 /* p->connection_status.reason */
    uint32_t advert_restart_attempts;                /* total auto-readvertise attempts */
    int      last_advert_restart_result;             /* return of last wiced_bt_start_advertisements */
    uint8_t  deinit_in_progress_at_last_disconnect;  /* 0 or 1 — was the flag set when we disconnected? */
    uint32_t boot_advert_attempts;                   /* total BTM_ENABLED-path start_advertisements calls */
    int      last_boot_advert_result;                /* return of last boot-time advert start */
} ble_nus_diag_t;

void ble_nus_get_diagnostics(ble_nus_diag_t *out);

#ifdef __cplusplus
}
#endif

#endif /* BLE_NUS_H */
