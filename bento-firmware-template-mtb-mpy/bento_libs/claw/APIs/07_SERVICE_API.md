# 07 -- Service API

BENTO PSoC Edge E84 Firmware SDK -- TACP Protocol, MicroPython Boot System, Filesystem, and Runtime Services

---

## Architecture Overview

The service layer manages the MicroPython runtime lifecycle, file transfer from the IDE, persistent storage, and background sensor tasks. Everything runs on CM33_NS as a FreeRTOS task.

```
BENTO IDE (Host)                   CM33_NS (FreeRTOS)
  |                                  |
  |-- UART (TACP protocol) -------->| tacp_poll_uart()
  |   AA 55 CMD / AA 55 20 frame   |   |
  |                                  |   +--> TACP state machine
  |                                  |   +--> Ring buffer --> REPL
  |                                  |
  |                                  | mpy_task_entry()
  |                                  |   +-- gc_init(112KB static heap)
  |                                  |   +-- VFS mount (LittleFS2 on QSPI)
  |                                  |   +-- WiFi creds load
  |                                  |   +-- boot.py / main.py
  |                                  |   +-- REPL loop
  |                                  |
  |                                  | sensor_auto_task
  |                                  |   +-- periodic sensor reads
  |                                  |   +-- IPC push to CM55 dashboard
  |                                  |   +-- NTP sync + time push
```

---

## 1. TACP Protocol (TESAIoT Control Protocol)

**Header:** `common/mpy/tacp.h`

TACP is the binary protocol between the BENTO IDE and the device over UART. It handles control commands and file transfers.

### Frame Format

**Control commands (3 bytes):**
```
[0xAA] [0x55] [CMD]
```

**File transfer (variable length):**
```
[0xAA] [0x55] [0x20] [path_len:1B] [path:NB] [file_len:4B LE] [file_data:MB] [crc16:2B LE]
```

### Control Commands

| Command | Code | Description |
|---------|------|-------------|
| `TACP_CMD_TERMINATE` | `0x01` | Ctrl-C: raise `KeyboardInterrupt` in MicroPython |
| `TACP_CMD_PROGRAM_MODE` | `0x02` | Safe boot + soft reset for code upload |
| `TACP_CMD_CONNECT` | `0x03` | IDE connection ping (no side effects) |
| `TACP_CMD_STATUS` | `0x04` | Show USB icon on LCD |
| `TACP_CMD_FILE_XFER` | `0x20` | Start binary file upload (followed by frame data) |

### Binary File Transfer Sub-Protocol

After receiving `AA 55 20`, the device reads the following binary frame:

| Field | Size | Format | Description |
|-------|------|--------|-------------|
| `path_len` | 1 byte | uint8 | Path length (max 127) |
| `path` | N bytes | UTF-8 | File path (e.g., `/main.py`) |
| `file_len` | 4 bytes | uint32 LE | File size in bytes |
| `file_data` | M bytes | raw | File content |
| `crc16` | 2 bytes | uint16 LE | CRC-CCITT over file_data |

**Flow control:**
1. Device receives header (path_len + path + file_len)
2. Device sends `TACP:FILE_RDY\r\n` when ready for data
3. Host streams `file_data` bytes
4. Device verifies CRC-CCITT and writes to filesystem

**Responses (ASCII over UART):**
```
TACP:FILE_OK <path> <size>\r\n      # Success
TACP:FILE_ERR <code> <msg>\r\n      # Failure
```

### Error Codes

| Code | Description |
|------|-------------|
| 1 | Path too long (exceeds 127 bytes) |
| 2 | File too large (exceeds 512KB) |
| 3 | Timeout (>5s between bytes) |
| 4 | CRC mismatch |
| 5 | Filesystem write error |
| 6 | Out of memory |

### Transfer Limits

| Constant | Value | Description |
|----------|-------|-------------|
| `TACP_FILE_PATH_MAX` | 127 | Max path length in bytes |
| `TACP_FILE_SIZE_MAX` | 512 KB | Max file size |
| `TACP_FILE_CHUNK_SIZE` | 512 | Internal write chunk size |
| `TACP_FILE_TIMEOUT_MS` | 5000 | Byte-level timeout |

### Ring Buffer

TACP uses a 256-byte ring buffer for REPL byte forwarding. Non-TACP bytes received on UART are placed in the ring buffer for MicroPython REPL consumption.

```c
#define TACP_RING_BUF_SIZE  256  /* Must be power of 2 */

typedef struct {
    uint8_t buf[TACP_RING_BUF_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
} tacp_ring_buf_t;
```

### C API

| Function | Signature | Description |
|----------|-----------|-------------|
| `tacp_init` | `void tacp_init(void)` | Initialize TACP state machine and ring buffer. Called on every soft reset. |
| `tacp_poll_uart` | `bool tacp_poll_uart(void)` | Poll UART for TACP commands and REPL bytes. Returns `true` if a command was detected. |
| `tacp_ring_buf_read` | `int tacp_ring_buf_read(void)` | Read one byte from ring buffer. Returns -1 if empty. |
| `tacp_ring_buf_readable` | `bool tacp_ring_buf_readable(void)` | Check if ring buffer has data. |

### Memory Allocation for File Transfer

File transfer allocates `file_len + 2` bytes from the MicroPython GC heap (calls `gc_collect()` first to maximize available memory). The largest example file is ~42KB. Safe minimum GC heap for TACP + runtime: 96KB (with 64KB absolute minimum).

---

## 2. MicroPython Boot System

**Source:** `common/mpy/mpy_main.c`

### Boot Modes

| Mode | Trigger | Behavior |
|------|---------|----------|
| `BOOT_MODE_NORMAL` | Default | Execute `/boot.py`, then `/main.py`, then REPL |
| `BOOT_MODE_SAFE` | User button held at boot | Skip `/boot.py` and `/main.py`, go straight to REPL. LED blinks twice. |
| `BOOT_MODE_SAFE_DEPLOY` | `TACP_CMD_PROGRAM_MODE` (0x02) | Software-requested safe boot. Shows deploy screen on LCD. Used by IDE during code upload. |

### Boot Sequence

```
mpy_task_entry()
  |
  +-- gc_init(mpy_gc_heap, 112KB)
  +-- mp_cstack_init_with_top(stack, 8KB - 512)
  +-- time_init()
  |
soft_reset:
  +-- tacp_init()                          # Clear stale UART bytes
  +-- pyexec_mode_kind = FRIENDLY_REPL     # Ensure friendly mode
  +-- mp_init()
  +-- readline_init0()
  +-- sys.path = ['/', '/lib']
  |
  +-- exec_python_str(vfs_mount_script)    # Mount QSPI filesystem
  +-- lfs_wifi_creds_init()                # Initialize credential store
  +-- lfs_wifi_creds_read()                # Load saved WiFi credentials
  +-- CRC32 migration check                # Upgrade old checksum format
  |
  +-- (if delete_main_flag) os.remove('/main.py')
  |
  +-- check_boot_mode()
  |     |
  |     +-- NORMAL:
  |     |     +-- ui_auto_clear_on_reset()    # Clear leftover UI widgets
  |     |     +-- pyexec_file_if_exists("/boot.py")
  |     |     +-- pyexec_file_if_exists("/main.py")
  |     |
  |     +-- SAFE: skip boot.py/main.py
  |     |
  |     +-- SAFE_DEPLOY: ui_show_deploy_screen()
  |
  +-- REPL loop:
        +-- wifi_creds_flush_if_dirty()    # Between REPL iterations
        +-- pyexec_friendly_repl() / pyexec_raw_repl()
        |
        (on exit)
        +-- wifi_creds_flush_if_dirty()
        +-- lfs_wifi_creds_deinit()
        +-- machine_pin_irq_deinit_all()
        +-- gc_sweep_all()
        +-- mp_deinit()
        +-- goto soft_reset
```

### Safe Boot Magic Values

| Constant | Value | Description |
|----------|-------|-------------|
| `MPY_SAFE_BOOT_MAGIC` | `0x53424654` (`"SBFT"`) | Stored in `.noinit` section. Set by `mpy_request_safe_boot_once()`. |
| `MPY_DELETE_MAIN_MAGIC` | `0x444D5059` (`"DMPY"`) | Regular SRAM variable. Set by CM55 delete button via `mpy_request_delete_main_py()`. |

### Safe Boot API

| Function | Signature | Description |
|----------|-----------|-------------|
| `mpy_request_safe_boot_once` | `void mpy_request_safe_boot_once(void)` | Set flag to enter safe boot on next soft reset. Cleared after use. |
| `mpy_request_delete_main_py` | `void mpy_request_delete_main_py(void)` | Set flag to delete `/main.py` on next soft reset. Used by CM55 UI delete button. |

### VFS Mount Script

The VFS mount is performed via `exec_python_str()` rather than a frozen module, to avoid the `mpy-cross` dependency:

```python
import os, psoc_edge
bdev = psoc_edge.QSPI_Flash()
try:
    vfs = os.VfsLfs2(bdev, progsize=0x200, readsize=0x1000)
    os.mount(vfs, '/')
except:
    os.VfsLfs2.mkfs(bdev, progsize=0x200, readsize=0x1000)
    vfs = os.VfsLfs2(bdev, progsize=0x200, readsize=0x1000)
    os.mount(vfs, '/')
```

On first boot (or corrupted filesystem), `mkfs` formats the partition automatically.

---

## 3. Filesystem

### LittleFS2 on QSPI Flash

| Parameter | Value |
|-----------|-------|
| Filesystem | LittleFS2 |
| Storage medium | External QSPI NOR flash |
| Total capacity | 64 MB |
| Partition offset | 0x900000 |
| Partition size | ~55 MB (remainder after firmware) |
| Program size | 0x200 (512 bytes) |
| Read size | 0x1000 (4096 bytes) |
| Mount point | `/` |

### Key Files

| Path | Description |
|------|-------------|
| `/boot.py` | Executed first on normal boot (before `/main.py`) |
| `/main.py` | Executed second on normal boot. Deleted only by the CM55 "Delete main.py" button; a TACP upload never deletes it (it stages to `/.tacp.tmp` and renames on success). |
| `/.wifi_creds` | Binary credential store (magic + version + entries + CRC32) |
| `/lib/` | MicroPython library path (in `sys.path`) |

### Filesystem Operations (MicroPython)

Standard MicroPython `os` module operations work:
```python
import os
os.listdir('/')          # List root directory
os.stat('/main.py')      # File metadata
os.remove('/main.py')    # Delete file
os.mkdir('/data')        # Create directory
os.rename('/a.py', '/b.py')
```

---

## 4. GC Heap Configuration

**Source:** `common/mpy/mpy_main.c`, line 56

| Parameter | Value | Description |
|-----------|-------|-------------|
| `MPY_GC_HEAP_SIZE` | 112 KB (default) | Static array, 4-byte aligned |
| Allocation | `static uint8_t mpy_gc_heap[MPY_GC_HEAP_SIZE]` | Not from FreeRTOS heap |
| Safe minimum | 96 KB | For TACP file transfer + runtime |
| Absolute minimum | 64 KB | Bare REPL + small scripts |

The GC heap is a static array to avoid contention with FreeRTOS `heap_3` (malloc wrapper). This provides a dedicated, non-fragmented region for MicroPython objects.

### CM33_NS RAM Budget

| Section | Size | Description |
|---------|------|-------------|
| `.bss` | 186 KB | Includes 112KB GC heap |
| `.heap` (FreeRTOS) | 63 KB | For FreeRTOS tasks, cy_wcm, cy_mqtt |
| `.data` | 2 KB | Initialized globals |
| **Free** | **~4 KB** | Remaining unallocated |
| **Total** | **256 KB** | CM33_NS RAM |

---

## 5. FreeRTOS Task Configuration

### MicroPython Task

| Parameter | Value |
|-----------|-------|
| Entry point | `mpy_task_entry(void *arg)` |
| Stack size | 8 KB (`MPY_TASK_STACK_SIZE`) |
| Stack control | MicroPython uses 8KB - 512 bytes (512 reserved for FreeRTOS overhead) |
| Priority | Standard (not elevated) |

### Auto-Sensor Background Task

**Header:** `common/mpy/sensor_auto_task.h`

Reads all enabled sensors periodically and pushes data via IPC to CM55 for LVGL dashboard display. Runs independently from MicroPython.

| Function | Signature | Description |
|----------|-----------|-------------|
| `sensor_auto_task_create` | `void sensor_auto_task_create(void)` | Create the FreeRTOS task. Call from `main()` before scheduler starts. |
| `sensor_auto_start` | `void sensor_auto_start(void)` | Start/resume auto-push. |
| `sensor_auto_stop` | `void sensor_auto_stop(void)` | Pause auto-push (task suspended, zero CPU). |
| `sensor_auto_is_running` | `bool sensor_auto_is_running(void)` | Check if auto-push is active. |
| `sensor_auto_set_rate` | `void sensor_auto_set_rate(uint32_t interval_ms)` | Set push interval (50-5000 ms). |
| `sensor_auto_get_rate` | `uint32_t sensor_auto_get_rate(void)` | Get current push interval. |
| `sensor_auto_set_mask` | `void sensor_auto_set_mask(uint32_t mask)` | Set which sensors are enabled (bitmask). |
| `sensor_auto_get_mask` | `uint32_t sensor_auto_get_mask(void)` | Get current sensor enable mask. |
| `sensor_auto_enable` | `void sensor_auto_enable(uint32_t flag)` | Enable specific sensor(s). |
| `sensor_auto_disable` | `void sensor_auto_disable(uint32_t flag)` | Disable specific sensor(s). |
| `sensor_auto_get_push_count` | `uint32_t sensor_auto_get_push_count(void)` | Total push cycles since boot. |
| `sensor_auto_is_delete_pending` | `bool sensor_auto_is_delete_pending(void)` | Check and consume delete-main.py request from IPC ISR. |

### Sensor Enable Flags

```c
#define SENSOR_AUTO_BMI270      (1 << 0)   /* IMU (accel + gyro) */
#define SENSOR_AUTO_DPS368      (1 << 1)   /* Barometric pressure */
#define SENSOR_AUTO_SHT40       (1 << 2)   /* Temperature + humidity */
#define SENSOR_AUTO_BMM350      (1 << 3)   /* Magnetometer */
#define SENSOR_AUTO_CAPSENSE    (1 << 4)   /* Capacitive touch (Eva Kit) */
#define SENSOR_AUTO_POT         (1 << 5)   /* Potentiometer (Eva Kit) */
#define SENSOR_AUTO_ALL         (0x3F)     /* All sensors */
```

### WiFi State and NTP Push

| Function | Signature | Description |
|----------|-----------|-------------|
| `sensor_auto_push_wifi_state` | `void sensor_auto_push_wifi_state(bool connected)` | Push WiFi connected/disconnected state to CM55 topbar. Non-blocking IPC. |
| `sensor_auto_ntp_and_push_time` | `void sensor_auto_ntp_and_push_time(void)` | Try NTP sync + push formatted time to CM55. Call after WiFi connect succeeds. |

---

## 6. Error Handlers

### MicroPython Fatal Errors

```c
void nlr_jump_fail(void *val) {
    printf("FATAL: nlr_jump_fail\r\n");
    for (;;) { }  /* Infinite loop -- requires hardware reset */
}
```

### GC Collection

```c
void gc_collect(void) {
    gc_collect_start();
    gc_helper_collect_regs_and_stack();  /* Scan ARM registers + task stack */
    gc_collect_end();
}
```

---

## 7. Deferred WiFi Credential Flush

WiFi credentials are staged by the WiFi IPC worker task (separate FreeRTOS task) and flushed to QSPI LittleFS from the MicroPython task context. This is necessary because `lfs_wifi_creds_write()` uses `exec_python_str()` which requires the MicroPython runtime (GC heap, globals dict, lexer/parser/compiler).

```c
extern qspi_wifi_entry_t g_boot_wifi_creds[];
extern volatile int      g_boot_wifi_creds_count;
extern volatile bool     g_boot_wifi_creds_dirty;

void wifi_creds_flush_if_dirty(void);  /* Called between REPL iterations */
```

**Flush points:**
1. Between REPL command executions (main loop)
2. Before MicroPython soft reset (before `gc_sweep_all()`)

---

## 8. Board Support

| Feature | AI Kit | Eva Kit | Game Console |
|---------|--------|---------|--------------|
| TACP protocol | Yes | Yes | Yes |
| MicroPython runtime | Yes | Yes | Yes |
| LittleFS on QSPI | Yes | Yes | Yes |
| GC heap (112KB) | Yes | Yes | Yes |
| Auto-sensor task | Yes | Yes | Partial |
| NTP time sync | Yes | Yes | Yes |
| WiFi credential storage | Yes | Yes | Yes |
| Safe boot (button) | Yes | Yes | Yes |
| Safe boot (TACP) | Yes | Yes | Yes |
| Deploy screen overlay | Yes | Yes | Yes |

---

## Source Files

| File | Path |
|------|------|
| TACP protocol header | `common/mpy/tacp.h` |
| MicroPython task entry | `common/mpy/mpy_main.c` |
| Auto-sensor task header | `common/mpy/sensor_auto_task.h` |
| WiFi credential types | `common/mpy/wifi_creds_types.h` |
| LittleFS credential store | `common/mpy/lfs_wifi_creds.h` |
