# bento_link.h

## Functions (exported by the archive)

### `bento_link_at`

```c
bento_link_t *bento_link_at(uint32_t index);
```

Enumerate registered backends: index 0..n-1, NULL past the end.

### `bento_link_get`

```c
bento_link_t *bento_link_get(const char *name);
```

Look up a backend by name; NULL if not compiled in / not registered.

### `bento_link_register`

```c
bool bento_link_register(bento_link_t *link);
```

File Name        : bento_link.h Description      : BENTO transport-agnostic link layer — ONE interface for the sensor/model data plane over pluggable backends: "ipc" (CM33<->CM55 pipe), "usb" (TACP serial), "ble" (NUS), "wifi" (TCP socket). v1 ships the ipc backend. Two planes: - CONTROL: u8 cmd + u32 value — DEEPCraft-compatible, so the vendored deepcraft_engine.c drives it through a ~40-line adapter (first-member vtable cast pattern, see common/deepcraft/deepcraft_interface.h). - DATA: framed byte streams (stream_id + seq), for bulk capture (mic PCM, IMU, model scores). v1: reserved. Backends self-register by name; availability follows the board persona (compile-time BSP flags), so Python-side deepcraft.links() reflects the real board automatically. Design reference : TESAIoT_PLAN/2026-7/MPY_Sensor_DEEPCraft_Integration/ / #ifndef BENTO_LINK_H #define BENTO_LINK_H #include <stdint.h> #include <stdbool.h> #ifdef __cplusplus extern "C" { #endif /* Capability flags */ #define BL_F_LOSSY      (1u << 0)  /* frames may drop (no retransmit)          */ #define BL_F_BULK_RING  (1u << 1)  /* supports negotiated shared-memory bulk   */ typedef struct bento_link_s bento_link_t; struct bento_link_s { /* CONTROL PLANE — u8 cmd + u32 value. Must be callable from task context; returns false if the transport is busy/unavailable. */ bool (*send_ctrl)(bento_link_t *self, uint8_t cmd, uint32_t value); /* DATA PLANE — framed stream chunk (<= mtu). v1: ipc backend returns false (bulk rides the negotiated SOCMEM ring in a later phase). */ bool (*send_data)(bento_link_t *self, uint8_t stream_id, const void *buf, uint16_t len); /* Receive callbacks. Backends MUST invoke these OFF interrupt context (each backend trampolines: ISR -> queue -> delivery task). */ void (*register_receive)(bento_link_t *self, void (*ctrl_cb)(uint8_t cmd, uint32_t value), void (*data_cb)(uint8_t stream_id, const void *buf, uint16_t len, uint16_t seq)); /* Capability advertisement — callers adapt, never assume. */ const char *name;          /* "ipc" | "usb" | "ble" | "wifi"              */ uint16_t    mtu;           /* max data-plane payload per frame            */ uint32_t    bandwidth_bps; /* honest sustained budget for the data plane  */ uint8_t     flags;         /* BL_F_*                                      */ }; /* ── Registry ────────────────────────────────────────────────────────────── */ #define BENTO_LINK_MAX_BACKENDS  (4u) /** Register a backend (called once from each backend's init).

## Constants

| Name | Value |
|---|---|
| `BENTO_LINK_H` | `#include` |
| `BL_F_LOSSY` | `(1u` |
| `BL_F_BULK_RING` | `(1u` |
| `BENTO_LINK_MAX_BACKENDS` | `(4u)` |
