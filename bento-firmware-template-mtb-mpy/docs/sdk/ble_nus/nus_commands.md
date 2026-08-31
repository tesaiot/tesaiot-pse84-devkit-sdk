# nus_commands.h

Command dispatcher + ack/status emitter for NUS wire protocol. Separated from nus_protocol.c so the framer/parser stays transport-only and command-set changes are localized here.

## Functions (exported by the archive)

### `nus_commands_dispatch`

```c
void nus_commands_dispatch(const char *json, size_t json_len, const jsmntok_t *toks, int n_toks, const jsmntok_t *t_cmd);
```

File Name: nus_commands.h Description: Command dispatcher + ack/status emitter for NUS wire protocol. Separated from nus_protocol.c so the framer/parser stays transport-only and command-set changes are localized here. / #ifndef NUS_COMMANDS_H #define NUS_COMMANDS_H #include <stddef.h> #include <stdint.h> #include "vendor/jsmn.h" #ifdef __cplusplus extern "C" { #endif /* Called by nus_protocol when the outer object contains a top-level "cmd" string. Dispatches to the right handler and emits the ack frame.

### `nus_commands_emit_ack`

```c
void nus_commands_emit_ack(const char *ack_cmd, size_t ack_cmd_len, const char *ok_true_or_null, const char *error_or_null, int n);
```

Emit a generic ack frame. Passing n = 0 or error = NULL skips those fields. ack_cmd must be non-NULL; len specifies bytes to copy.

### `nus_commands_handle_time_sync`

```c
void nus_commands_handle_time_sync(const char *json, const jsmntok_t *toks, int n);
```

Handle the time-sync one-shot frame {"time":[epoch,tz]}. No ack emitted.

### `nus_radio_emit_state_event`

```c
void nus_radio_emit_state_event(const struct radio_status_s *st);
```

_No description in the header._

## Constants

| Name | Value |
|---|---|
| `NUS_COMMANDS_H` | `#include` |
