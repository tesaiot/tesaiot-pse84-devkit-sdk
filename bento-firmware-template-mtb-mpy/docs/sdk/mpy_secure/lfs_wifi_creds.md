# lfs_wifi_creds.h

LittleFS-based persistent storage for WiFi credentials. Primary credential store — survives firmware reflash. Stores up to 6 WiFi network entries in /.wifi_creds file on the MicroPython LittleFS2 filesystem (55MB QSPI partition at 0x900000). Uses the same binary format and entry type as qspi_wifi_creds.h for seamless migration from raw QSPI sector storage. Uses MicroPython's exec_python_str() for file I/O instead of direct LFS2 C API (avoids blockdev callback + LFS2_NO_MALLOC issues). MUST be called from MicroPython task context only.

## Functions (exported by the archive)

### `lfs_wifi_creds_deinit`

```c
void lfs_wifi_creds_deinit(void);
```

Invalidate LFS handle before MicroPython soft reset. Called from mpy_main.c before mp_deinit().

### `lfs_wifi_creds_init`

```c
void lfs_wifi_creds_init(void);
```

File: lfs_wifi_creds.h Description: LittleFS-based persistent storage for WiFi credentials. Primary credential store — survives firmware reflash. Stores up to 6 WiFi network entries in /.wifi_creds file on the MicroPython LittleFS2 filesystem (55MB QSPI partition at 0x900000). Uses the same binary format and entry type as qspi_wifi_creds.h for seamless migration from raw QSPI sector storage. Uses MicroPython's exec_python_str() for file I/O instead of direct LFS2 C API (avoids blockdev callback + LFS2_NO_MALLOC issues). MUST be called from MicroPython task context only. / #ifndef LFS_WIFI_CREDS_H #define LFS_WIFI_CREDS_H #include "wifi_creds_types.h"  /* qspi_wifi_entry_t type + constants */ /** Initialize LFS credential store. MUST be called from MicroPython task AFTER VFS mount succeeds.

### `lfs_wifi_creds_needs_resave`

```c
bool lfs_wifi_creds_needs_resave(void);
```

Check if credentials need re-saving (XOR-32 → CRC32 migration). Call after boot WiFi auto-connect; if true, re-read and re-write.

### `lfs_wifi_creds_read`

```c
int lfs_wifi_creds_read(qspi_wifi_entry_t *entries, int max_entries);
```

Read all saved WiFi credentials from LittleFS. Waits up to 10 seconds for LFS init (VFS mount). Validates magic, version, checksum before returning data. @param entries     Output array (must hold QSPI_WIFI_CREDS_MAX elements) @param max_entries Max entries to read @return Number of valid entries read (0 if LFS not ready or no data)

### `lfs_wifi_creds_ready`

```c
bool lfs_wifi_creds_ready(void);
```

Check if LFS credential store is ready for use.

### `lfs_wifi_creds_write`

```c
bool lfs_wifi_creds_write(const qspi_wifi_entry_t *entries, int count);
```

Write WiFi credentials to LittleFS. Creates/overwrites /.wifi_creds with magic + version + entries + checksum. @param entries Array of entries to write @param count   Number of entries (1..QSPI_WIFI_CREDS_MAX) @return true on success

## Constants

| Name | Value |
|---|---|
| `LFS_WIFI_CREDS_H` | `#include` |
