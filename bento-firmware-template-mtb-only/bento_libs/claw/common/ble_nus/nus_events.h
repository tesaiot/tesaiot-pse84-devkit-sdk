/*******************************************************************************
 * File Name: nus_events.h
 *
 * Description: Device -> Desktop NUS event emitter. Separates device-originated
 *              frames (media / system commands, user acks, reply text) from
 *              the desktop -> device command dispatcher (nus_commands.h).
 *
 *              All outbound event JSON is line-delimited — this module owns
 *              the framing so callers never need to remember to append '\n'.
 *
 *              Also owns the pending-ack FIFO for ack_required=true
 *              notifications. The command dispatcher pushes ids when a
 *              bento.notify.show lands; CM55 LCD taps drain them back via
 *              nus_events_drain_pending_ack().
 *
 ******************************************************************************/

#ifndef NUS_EVENTS_H
#define NUS_EVENTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Emit a NUS event frame. Appends '\n' (line delimiter) then writes to BLE
 * via ble_nus_send(). `json` must be a NUL-terminated JSON object. Returns
 * 0 on success, -1 on transport error or disconnected link. */
int nus_emit_event(const char *json);

/* Pending-ack FIFO for notifications with ack_required=true. Called from
 * the notify-show command handler. Dedups; evicts oldest when full. */
void nus_events_push_pending_ack(const char *id);

/* Drain an id from the pending-ack FIFO. Returns true if found (and removed),
 * false otherwise. Called inside libbento_secure.a when an
 * IPC_CMD_BUDDY_DEVICE_CMD carrying a bento.user.ack is received from CM55. */
bool nus_events_drain_pending_ack(const char *id);

/* Number of ids currently in the pending-ack FIFO. Exposed for telemetry +
 * host unit tests. */
size_t nus_events_pending_ack_count(void);

#ifdef __cplusplus
}
#endif

#endif /* NUS_EVENTS_H */
