# nus_events.h

Device -> Desktop NUS event emitter. Separates device-originated frames (media / system commands, user acks, reply text) from the desktop -> device command dispatcher in nus_commands.c. All outbound event JSON is line-delimited — this module owns the framing so callers never need to remember to append '\n'. Also owns the pending-ack FIFO for ack_required=true notifications (SPEC §7.5). nus_commands.c pushes ids when a bento.notify.show lands; CM55 LCD taps drain them back via nus_events_drain_pending_ack(). Reference: TESAIoT_PLAN/2026-4/Bento_Buddy/SPEC.md §5.2.

## Functions (exported by the archive)

### `nus_emit_event`

```c
int nus_emit_event(const char *json);
```

File Name: nus_events.h Description: Device -> Desktop NUS event emitter. Separates device-originated frames (media / system commands, user acks, reply text) from the desktop -> device command dispatcher in nus_commands.c. All outbound event JSON is line-delimited — this module owns the framing so callers never need to remember to append '\n'. Also owns the pending-ack FIFO for ack_required=true notifications (SPEC §7.5). nus_commands.c pushes ids when a bento.notify.show lands; CM55 LCD taps drain them back via nus_events_drain_pending_ack(). Reference: TESAIoT_PLAN/2026-4/Bento_Buddy/SPEC.md §5.2. / #ifndef NUS_EVENTS_H #define NUS_EVENTS_H #include <stdbool.h> #include <stddef.h> #include <stdint.h> #ifdef __cplusplus extern "C" { #endif /* Emit a NUS event frame. Appends '\n' (line delimiter) then writes to BLE via ble_nus_send(). `json` must be a NUL-terminated JSON object. Returns 0 on success, -1 on transport error or disconnected link.

### `nus_events_drain_pending_ack`

```c
bool nus_events_drain_pending_ack(const char *id);
```

Drain an id from the pending-ack FIFO. Returns true if found (and removed), false otherwise. Called from ipc_bento_buddy_bridge.c when an IPC_CMD_BUDDY_DEVICE_CMD carrying a bento.user.ack is received from CM55.

### `nus_events_pending_ack_count`

```c
size_t nus_events_pending_ack_count(void);
```

Number of ids currently in the pending-ack FIFO. Exposed for telemetry + host unit tests.

### `nus_events_push_pending_ack`

```c
void nus_events_push_pending_ack(const char *id);
```

Pending-ack FIFO for notifications with ack_required=true. Called from nus_commands.c:cmd_bento_notify_show. Dedups; evicts oldest when full.

## Constants

| Name | Value |
|---|---|
| `NUS_EVENTS_H` | `#include` |
