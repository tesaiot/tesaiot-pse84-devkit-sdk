# bento_devmode.h

Developer-mode unlock state for the Bento Desktop Buddy bridge. Gates bento.exec and other privileged verbs behind an HMAC-SHA256 challenge/response. See SPEC §5.5 + §7.3. Lifecycle: 1. Desktop sends bento.devmode.nonce → firmware returns a fresh 16-byte nonce (64-ms TTL per SPEC). 2. Desktop computes HMAC-SHA256(shared_secret, nonce) and sends bento.devmode.unlock. 3. Firmware verifies and sets the unlocked flag. 4. bento.devmode.lock clears the flag. Shared secret storage: Primary — OPTIGA Trust M arbitrary-data slot 0xE120 Fallback — RAM-only, regenerated every boot (means desktop must re-provision after every reflash; see ISSUES.md ISSUE-006). v1 uses the RAM-only fallback unconditionally; OPTIGA persistence lands in a follow-up along with the proper first-pair provisioning UX (SPEC §7.3 steps 1-4).

## Functions (exported by the archive)

### `bento_devmode_emit_provision`

```c
void bento_devmode_emit_provision(void);
```

Emit `{"evt":"bento.devmode.provision","secret":"<64 hex>"}` over BLE NUS once per boot so the desktop side can store the secret in its keychain. Without this emit the desktop holds a randomly-generated placeholder secret that will never match firmware-side state and every `bento.devmode.unlock` will return `not_permitted`. Wired into ble_nus_lazy.c's CONNECTED transition, paired with `bento_fw_emit_boot_complete()`. Internally guards with a static "already sent this boot" flag — multiple BLE connect/disconnect cycles within one boot will not re-send (the secret in firmware doesn't change without a reset).

### `bento_devmode_init`

```c
void bento_devmode_init(void);
```

File Name: bento_devmode.h Description: Developer-mode unlock state for the Bento Desktop Buddy bridge. Gates bento.exec and other privileged verbs behind an HMAC-SHA256 challenge/response. See SPEC §5.5 + §7.3. Lifecycle: 1. Desktop sends bento.devmode.nonce → firmware returns a fresh 16-byte nonce (64-ms TTL per SPEC). 2. Desktop computes HMAC-SHA256(shared_secret, nonce) and sends bento.devmode.unlock. 3. Firmware verifies and sets the unlocked flag. 4. bento.devmode.lock clears the flag. Shared secret storage: Primary — OPTIGA Trust M arbitrary-data slot 0xE120 Fallback — RAM-only, regenerated every boot (means desktop must re-provision after every reflash; see ISSUES.md ISSUE-006). v1 uses the RAM-only fallback unconditionally; OPTIGA persistence lands in a follow-up along with the proper first-pair provisioning UX (SPEC §7.3 steps 1-4). / #ifndef BENTO_DEVMODE_H #define BENTO_DEVMODE_H #include <stdbool.h> #include <stddef.h> #include <stdint.h> #ifdef __cplusplus extern "C" { #endif #define BENTO_DEVMODE_NONCE_LEN   (16u) #define BENTO_DEVMODE_SECRET_LEN  (32u) #define BENTO_DEVMODE_HMAC_LEN    (32u)    /* SHA-256 output */ #define BENTO_DEVMODE_NONCE_TTL_MS (60000u) /* Initialise the secret (RAM-only fallback — seeds from boot entropy + TickCount on first call). Idempotent.

### `bento_devmode_is_unlocked`

```c
bool bento_devmode_is_unlocked(void);
```

Query whether exec is currently permitted.

### `bento_devmode_lock`

```c
void bento_devmode_lock(void);
```

Manually clear the unlocked flag.

### `bento_devmode_nonce_issue`

```c
void bento_devmode_nonce_issue(uint8_t nonce_out[BENTO_DEVMODE_NONCE_LEN]);
```

Generate a fresh 16-byte nonce and return it via `nonce_out`. Overwrites any previous pending nonce (only one in-flight challenge). Sets the internal ttl clock.

### `bento_devmode_secret_fp_hex`

```c
size_t bento_devmode_secret_fp_hex(char *out, size_t out_sz);
```

First 4 bytes of SHA-256(secret) as 8 hex chars + NUL — safe to log, used to cross-check that desktop and firmware are operating on the same secret without leaking it. Matches the `secret_fp` tracing on the Rust side (`verbs/devmode.rs::unlock`).

### `bento_devmode_secret_hex`

```c
size_t bento_devmode_secret_hex(char *out, size_t out_sz);
```

Expose the provisioning secret as a hex string for the first-pair emit-once flow (SPEC §7.3 step 2). Returns length written (64 hex chars + NUL) or 0 if the out buffer is too small. RAM-only fallback; the caller must NOT log this to UART.

### `bento_devmode_unlock`

```c
bool bento_devmode_unlock(const char *hmac_hex, size_t hmac_hex_len);
```

Verify an HMAC-SHA256 hex string against the current pending nonce + the shared secret. Returns true on match AND ttl window unexpired, in which case the internal unlocked flag is set. Consumes the nonce on any call (one-shot, prevents replay).

## Constants

| Name | Value |
|---|---|
| `BENTO_DEVMODE_H` | `#include` |
| `BENTO_DEVMODE_NONCE_LEN` | `(16u)` |
| `BENTO_DEVMODE_SECRET_LEN` | `(32u)` |
| `BENTO_DEVMODE_HMAC_LEN` | `(32u)` |
| `BENTO_DEVMODE_NONCE_TTL_MS` | `(60000u)` |
