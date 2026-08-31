# cy_wcm_shim.h

> This header declares **1 functions implemented in open source that ships in this package** — not in the closed archive. Read the .c next to it; you own that code.

## Structs

### `cy_wcm_scan_result_t`

```c
typedef struct {
    char    SSID[33];
    int16_t signal_strength;
    uint8_t security;
    uint8_t channel;} cy_wcm_scan_result_t;
```

### `param`

```c
typedef struct {
    uint8_t mode;
    union {
        int16_t rssi_range;} param;
```

## Constants

| Name | Value |
|---|---|
| `CY_WCM_SHIM_H` | `#include` |
| `CY_WCM_SCAN_INCOMPLETE` | `0` |
| `CY_WCM_SCAN_COMPLETE` | `1` |
| `CY_WCM_SECURITY_OPEN` | `0` |
| `CY_WCM_SECURITY_WEP_SHARED` | `2` |
| `CY_WCM_SECURITY_WPA_MIXED_PSK` | `4` |
| `CY_WCM_SECURITY_WPA2_AES_PSK` | `6` |
| `CY_WCM_SECURITY_WPA2_FBT_PSK` | `8` |
| `CY_WCM_SCAN_FILTER_TYPE_RSSI` | `1` |

## Functions (implemented in open source in this package)

### `void`

```c
typedef void (*cy_wcm_scan_cb_t)(cy_wcm_scan_result_t *, void *, cy_wcm_scan_status_t);
```

cy_wcm_shim.h — Minimal WCM type stubs for CM55 IPC service compilation. WiFi runs on CM33_NS only (SDHC0 PPC-protected). CM55 wifi_manager is a stub returning errors. This header provides the types and constants that ipc_service.c references so it compiles without the full WiFi stack. / #ifndef CY_WCM_SHIM_H #define CY_WCM_SHIM_H #include <stdint.h> #include "cy_result.h" /* Scan status */ typedef uint32_t cy_wcm_scan_status_t; #define CY_WCM_SCAN_INCOMPLETE   0 #define CY_WCM_SCAN_COMPLETE     1 /* Security types */ #define CY_WCM_SECURITY_OPEN             0 #define CY_WCM_SECURITY_WEP_SHARED       2 #define CY_WCM_SECURITY_WPA_MIXED_PSK    4 #define CY_WCM_SECURITY_WPA2_AES_PSK     6 #define CY_WCM_SECURITY_WPA2_FBT_PSK     8 /* Scan result */ typedef struct { char    SSID[33]; int16_t signal_strength; uint8_t security; uint8_t channel; } cy_wcm_scan_result_t; /* Scan filter */ #define CY_WCM_SCAN_FILTER_TYPE_RSSI  1 typedef struct { uint8_t mode; union { int16_t rssi_range; } param; } cy_wcm_scan_filter_t; /* Scan callback type
