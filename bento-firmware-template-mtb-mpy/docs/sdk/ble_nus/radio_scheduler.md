# radio_scheduler.h

## Functions (exported by the archive)

### `radio_mode_str`

```c
const char *radio_mode_str(radio_mode_t m);
```

Stringise a mode for log / event payloads. Returns a const literal — never NULL, never dynamically allocated.

### `radio_scheduler_get_boot_mode`

```c
radio_boot_mode_t radio_scheduler_get_boot_mode(void);
```

Optional: get the boot_mode currently persisted. Useful for the UI to render an "override" badge.

### `radio_scheduler_get_mode`

```c
radio_mode_t radio_scheduler_get_mode(void);
```

Inspector: returns the live mode without blocking. Safe from any task.

### `radio_scheduler_get_status`

```c
void radio_scheduler_get_status(radio_status_t *out);
```

Fill the supplied status struct atomically. The `ssid` field points into scheduler-owned storage — valid until the next mode transition. The caller MUST copy out before relinquishing the calling context.

### `radio_scheduler_init`

```c
bool radio_scheduler_init(const radio_scheduler_config_t *cfg);
```

Persistence hooks — supplied by the project so this library does not need to link the MicroPython VFS. Pass NULL to disable persistence (boot will always default to AUTO). */ radio_boot_mode_persist_fn_t persist_boot_mode; radio_boot_mode_load_fn_t    load_boot_mode; } radio_scheduler_config_t; /* Initialise the scheduler. Honours saved boot_mode + presence of saved Wi-Fi creds to pick the initial mode: boot_mode = FORCE_BLE → BLE_ADV boot_mode = FORCE_WIFI + creds → WIFI_CONNECTING boot_mode = AUTO + creds       → WIFI_CONNECTING boot_mode = AUTO + no creds    → BLE_ADV Returns true on success. False if the underlying BLE / Wi-Fi subsystem refuses to come up (caller should fall back to a REPL-only safe mode).

### `radio_scheduler_request_mode`

```c
bool radio_scheduler_request_mode(radio_mode_t target);
```

Switch radio mode. Idempotent — calling with the current mode is a no-op. Returns true if the transition was queued, false if rejected (e.g. switch to Wi-Fi without saved creds). NOTE: actual mode change is async — callers should listen to the radio:state event (or poll radio_scheduler_get_mode) for confirmation.

### `radio_scheduler_set_boot_mode`

```c
void radio_scheduler_set_boot_mode(radio_boot_mode_t mode);
```

Optional: persist a new boot_mode. The default boot_mode is AUTO; a user who long-presses the LCD toggle gets FORCE_BLE (or FORCE_WIFI via the Desktop Buddy).

### `radio_scheduler_set_on_state`

```c
void radio_scheduler_set_on_state(radio_scheduler_on_state_fn_t cb);
```

_No description in the header._

### `radio_scheduler_set_wifi_creds`

```c
bool radio_scheduler_set_wifi_creds(const char *ssid, const char *password, const char *security, bool auto_switch);
```

Save Wi-Fi credentials. When auto_switch=true, also queue a transition to WIFI_CONNECTING after the save completes. Returns false on validation failure (ssid too long, etc.) or LFS write failure.

## Enums

### `radio_mode_t`

```c
typedef enum {
    RADIO_MODE_UNKNOWN = 0,          /* pre-init */
    RADIO_MODE_BLE_ADV,              /* BLE advertising, Wi-Fi off */
    RADIO_MODE_BLE_PAIRED,           /* BLE link up (NUS connected) */
    RADIO_MODE_SWITCHING_TO_WIFI,    /* Wi-Fi connect in flight */
    RADIO_MODE_WIFI_ACTIVE,          /* Wi-Fi joined, BLE stack resident but adv off */
    RADIO_MODE_SWITCHING_TO_BLE,     /* Wi-Fi disconnect in flight, BLE re-arming */
    RADIO_MODE_WIFI_FAILED,          /* fallback transient — re-arms BLE next */} radio_mode_t;
```

### `radio_boot_mode_t`

```c
typedef enum {
    /* User-locked default: try Wi-Fi if creds saved, else BLE. */
    RADIO_BOOT_AUTO = 0,
    /* User forced BLE permanently — survives reboot. */
    RADIO_BOOT_FORCE_BLE = 1,
    /* User forced Wi-Fi — boot path tries Wi-Fi even if connect previously
     * failed (escape from the 3-fail-fallback loop on a router move). */
    RADIO_BOOT_FORCE_WIFI = 2,} radio_boot_mode_t;
```

## Structs

### `radio_scheduler_config_t`

```c
typedef struct {
    /* Persistence hooks — supplied by the project so this library does not
     * need to link the MicroPython VFS. Pass NULL to disable persistence
     * (boot will always default to AUTO). */
    radio_boot_mode_persist_fn_t persist_boot_mode;
    radio_boot_mode_load_fn_t    load_boot_mode;} radio_scheduler_config_t;
```

## Functions (implemented in open source in this package)

### `bool`

```c
typedef bool (*radio_boot_mode_load_fn_t)(radio_boot_mode_t *out);
```

_No description in the header._

### `void`

```c
typedef void (*radio_boot_mode_persist_fn_t)(radio_boot_mode_t mode);
```

File: radio_scheduler.h Description: Single-radio scheduler for the BentoClaw Playground variant. The CYW55513 silicon can run BLE + Wi-Fi concurrently via its on-die COEX time-division multiplexer, but the Playground firmware deliberately does not enable COEX so MicroPython keeps its 64 KB GC heap free of WHD's transient buffers. Instead, this scheduler arbitrates the radio between two mutually-exclusive modes and exposes a verb-level API for the Desktop Buddy + LCD to switch between them. Mode transitions: BLE_ADV          → user/desktop requests Wi-Fi WIFI_CONNECTING  → cy_wcm_connect_ap in flight WIFI_ACTIVE      → joined; BLE adv off, stack still resident WIFI_FAILED      → 3 consecutive connect failures → fall back to BLE_ADV automatically Persistence: the last requested mode + presence of saved Wi-Fi credentials drive the boot decision so a reset on a configured board comes back to Wi-Fi without user action. / #pragma once #include <stdbool.h> #include <stdint.h> #ifdef __cplusplus extern "C" { #endif typedef enum { RADIO_MODE_UNKNOWN = 0,          /* pre-init */ RADIO_MODE_BLE_ADV,              /* BLE advertising, Wi-Fi off */ RADIO_MODE_BLE_PAIRED,           /* BLE link up (NUS connected) */ RADIO_MODE_SWITCHING_TO_WIFI,    /* Wi-Fi connect in flight */ RADIO_MODE_WIFI_ACTIVE,          /* Wi-Fi joined, BLE stack resident but adv off */ RADIO_MODE_SWITCHING_TO_BLE,     /* Wi-Fi disconnect in flight, BLE re-arming */ RADIO_MODE_WIFI_FAILED,          /* fallback transient — re-arms BLE next */ } radio_mode_t; typedef enum { /* User-locked default: try Wi-Fi if creds saved, else BLE. */ RADIO_BOOT_AUTO = 0, /* User forced BLE permanently — survives reboot. */ RADIO_BOOT_FORCE_BLE = 1, /* User forced Wi-Fi — boot path tries Wi-Fi even if connect previously failed (escape from the 3-fail-fallback loop on a router move). */ RADIO_BOOT_FORCE_WIFI = 2, } radio_boot_mode_t; /* Explicit struct tag so headers that want a callback signature can forward-declare `struct radio_status_s` without pulling this whole header (nus_commands.h does exactly that). */ typedef struct radio_status_s { radio_mode_t  mode; /* Optional fields, populated when relevant. NULL/zero when not. */ const char   *ssid;          /* current Wi-Fi SSID (UTF-8, NUL-terminated) */ uint32_t      ipv4;          /* current IPv4 in network byte order, 0 if none */ bool          ble_paired;    /* NUS link up */ uint8_t       wifi_fail_count; } radio_status_t; /* Persisted across the radio_set_boot_mode call — written to LittleFS at /.radio_boot_mode (a one-byte file).

### `void`

```c
typedef void (*radio_scheduler_on_state_fn_t)(const radio_status_t *st);
```

Event hook — called from the scheduler whenever mode changes. The project wires this to emit a NUS event (bento.radio.state) and to push an IPC frame to CM55 so the LCD page redraws.
