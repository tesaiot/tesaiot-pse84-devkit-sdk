# bento_fw.h

Bento Desktop Buddy firmware-auto-update handlers. Owns the state machine for: bento.fw.query         — cheap read-only metadata probe bento.fw.update.begin  — Y/N physical-ack + pre-flash prep bento.fw.update.complete — outbound event emitted on boot Physical-ack: every USB-over-SWD flash triggered from the desktop side MUST pass through an on-device LCD Y/N prompt showing the first 8 hex chars of the target SHA-256. No side-channel flash bypass, no timeout-default-to-yes. Re-pair model: bonds are RAM-only, so every reboot forces the desktop to re-pair. This module emits bento.fw.update.complete unconditionally on each first post-boot NUS connection — the desktop compares to its cached hash and decides whether to treat the event as "update just landed" or "just a reboot".

## Functions (exported by the archive)

### `bento_fw_emit_boot_complete`

```c
void bento_fw_emit_boot_complete(void);
```

Called from the BLE state-change hook the moment the link transitions to CONNECTED. Emits bento.fw.update.complete ONCE per boot so the desktop can reconcile whatever hash it cached against what's actually running. Idempotent — subsequent calls within the same boot are no-ops.

### `bento_fw_handle_query`

```c
void bento_fw_handle_query(const char *json, const jsmntok_t *toks, int n_toks);
```

File Name: bento_fw.h Description: Bento Desktop Buddy firmware-auto-update handlers. Owns the state machine for: bento.fw.query         — cheap read-only metadata probe bento.fw.update.begin  — Y/N physical-ack + pre-flash prep bento.fw.update.complete — outbound event emitted on boot Physical-ack: every USB-over-SWD flash triggered from the desktop side MUST pass through an on-device LCD Y/N prompt showing the first 8 hex chars of the target SHA-256. No side-channel flash bypass, no timeout-default-to-yes. Re-pair model: bonds are RAM-only, so every reboot forces the desktop to re-pair. This module emits bento.fw.update.complete unconditionally on each first post-boot NUS connection — the desktop compares to its cached hash and decides whether to treat the event as "update just landed" or "just a reboot". / #ifndef BENTO_FW_H #define BENTO_FW_H #include <stddef.h> #include <stdint.h> /* jsmn tokens — forward decl so this header stays light. */ struct jsmntok; typedef struct jsmntok jsmntok_t; #ifdef __cplusplus extern "C" { #endif /* Firmware version string reported to the desktop in the bento.fw.query reply and the boot-complete event. That reply also carries a `_diag` block (firmware hash, BLE counters, uptime) so a connectivity report can be diagnosed in one round trip. */ /* A build that defines BENTO_FW_VERSION (for example with DEFINES+=BENTO_FW_VERSION=\"x.y.z\" in its makefile) gets that number here; the constant below applies only when the build supplies none. This changes only code compiled against this header: the prebuilt libbento_secure.a was built with its own value and is not affected. */ #ifdef BENTO_FW_VERSION #undef  BENTO_BUDDY_FW_VERSION #define BENTO_BUDDY_FW_VERSION BENTO_FW_VERSION #endif #ifndef BENTO_BUDDY_FW_VERSION #define BENTO_BUDDY_FW_VERSION "1.4.0" #endif /* Handler for bento.fw.query. Emits the JSON ack on the NUS link.

### `bento_fw_handle_update_begin`

```c
void bento_fw_handle_update_begin(const char *json, const jsmntok_t *toks, int n_toks);
```

Handler for bento.fw.update.begin. Launches the LCD Y/N prompt and emits the final ack (approve → ok:true + sensor streams stopped; decline → error; timeout → error). Rate-limits duplicate prompts within 5 seconds.

### `bento_fw_on_user_decision`

```c
void bento_fw_on_user_decision(int approve);
```

Called by the CM55 bridge inside libbento_secure.a when the CM55 LCD returns a Y/N decision for the current firmware-update prompt. `approve` is non-zero for Y (proceed with flash), zero for N (decline). No-op when there is no pending prompt.

## Constants

| Name | Value |
|---|---|
| `BENTO_FW_H` | `#include` |
| `BENTO_BUDDY_FW_VERSION` | `BENTO_FW_VERSION` |
| `BENTO_BUDDY_FW_VERSION` | `"1.4.0"` |
