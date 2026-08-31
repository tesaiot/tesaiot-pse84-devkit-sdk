# wifi_creds_types.h

WiFi credential entry type and constants shared between: - lfs_wifi_creds.c (LittleFS-based, primary boot store) - OPTIGA Trust M (secure backup, future) Entry format: 100 bytes per network (SSID + password + security + flags) Max 6 saved networks.

> Configuration header — constants and macros only, no functions.

## Constants

| Name | Value |
|---|---|
| `WIFI_CREDS_TYPES_H` | `#include` |
| `QSPI_WIFI_CREDS_MAGIC` | `0x57494649U` |
| `QSPI_WIFI_CREDS_VERSION` | `1` |
| `QSPI_WIFI_CREDS_MAX` | `6` |
