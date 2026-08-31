# 08 -- IPC API Reference
<!-- //! [doc-drift-fix] — see docs/template_local_deltas.list; a sync that reverts this file must be refused -->

**BENTO PSoC Edge E84 Inter-Processor Communication Framework**

| Item | Detail |
|------|--------|
| Header | `ipc_communication.h`, `ipc_ui_protocol.h` |
| Source | `cm33_ipc_communication.c` (CM33), `cm55_ipc_communication.c` (CM55) |
| Static Library | `libtesaiot_ipc_cm33.a` (CM33), `libtesaiot_ipc_cm55.a` (CM55) |
| Dependencies | Infineon PDL (`cy_ipc_pipe.h`, `cy_pdl.h`, `cy_sysint.h`), `cybsp.h` |
| Targets | PSoC Edge E84 -- CM33_NS (Non-Secure) and CM55 cores |

---

## 1. Architecture Overview

The IPC framework provides bidirectional communication between the **CM33_NS** (Non-Secure) and **CM55** cores on the PSoC Edge E84 SoC. It is built on top of the Infineon `Cy_IPC_Pipe` driver, which uses hardware IPC channels and interrupt-driven notification.

### Physical Layer

```
  CM33_NS (WiFi, MicroPython, Sensors)           CM55 (LVGL UI, Camera, AI)
  =========================================       ============================
  Endpoint 1 (EP1)  <--- IPC Channel 4 --->  Endpoint 2 (EP2)
       IPC Interrupt 4                            IPC Interrupt 5
       IPC Channel   4                            IPC Channel  15
       EP Address    1                            EP Address    2
```

- **IPC Channel 4** (`CY_IPC_CHAN_CYPIPE_EP1`): CM33 receiver channel
- **IPC Channel 15** (`CY_IPC_CHAN_CYPIPE_EP2`): CM55 receiver channel
- **Interrupt 4** (`CY_IPC_INTR_CYPIPE_EP1`): Notifies CM33 of incoming messages
- **Interrupt 5** (`CY_IPC_INTR_CYPIPE_EP2`): Notifies CM55 of incoming messages

### Client Routing

The pipe supports up to **10 clients** (`CY_IPC_CYPIPE_CLIENT_CNT = 10`) and **5 endpoints** (`CY_IPC_MAX_ENDPOINTS = 5`). Each message carries a `client_id` field that routes it to the correct callback on the receiving side.

| Client ID | Macro | Direction | Purpose |
|-----------|-------|-----------|---------|
| 2 | `CM33_IPC_HSM_CLIENT_ID` | CM55 -> CM33 | HSM/OPTIGA Trust M requests |
| 3 | `CM33_IPC_PIPE_CLIENT_ID` | CM55 -> CM33 | General CM33 receiver |
| 4 | `CM33_IPC_LED_CLIENT_ID` | CM55 -> CM33 | LED toggle from UI |
| 5 | `CM55_IPC_PIPE_CLIENT_ID` | CM33 -> CM55 | LCD terminal commands |
| 6 | `CM55_IPC_SENSOR_CLIENT_ID` | CM33 -> CM55 | Sensor data push |
| 7 | `CM55_IPC_SERVICE_CLIENT_ID` | Bidirectional | WiFi + MQTT + TESAIoT |
| 8 | `CM55_IPC_UI_CLIENT_ID` | Bidirectional | MicroPython UI widgets |
| 9 | `CM33_IPC_SENSOR_CTRL_CLIENT_ID` | CM55 -> CM33 | Sensor auto-control |

### Endpoint Configuration

| Parameter | EP1 (CM33) | EP2 (CM55) |
|-----------|-----------|-----------|
| `epAddress` | `1` | `2` |
| `ipcNotifierNumber` | `4` | `5` |
| `ipcNotifierPriority` | `1` | `1` |
| `epChannel` | `4` | `15` |
| `epIntr` | `4` | `5` |
| `epIntrmask` | Combined mask (CH4 \| CH15) | Combined mask (CH4 \| CH15) |

---

## 2. Core IPC Framework Functions

### `cm33_ipc_communication_setup`

```c
void cm33_ipc_communication_setup(void);
```

Initializes the IPC pipe infrastructure on the CM33_NS core. Performs:

1. `Cy_IPC_Sema_Init()` -- Initializes IPC semaphore array (`ipc_sema_array`)
2. `Cy_IPC_Pipe_Config()` -- Configures endpoint array
3. `Cy_IPC_Pipe_Init()` -- Initializes the pipe with EP1/EP2 configuration

Idempotent -- subsequent calls are no-ops unless `cm33_ipc_communication_recover()` has been called.

**Call site**: Early in CM33_NS `main()`, before any `Cy_IPC_Pipe_RegisterCallback()` or `Cy_IPC_Pipe_SendMessage()`.

---

### `cm33_ipc_communication_recover`

```c
void cm33_ipc_communication_recover(void);
```

Forces re-initialization of the IPC pipe and semaphore on CM33_NS. Resets the internal `s_cm33_ipc_initialized` flag and calls the full init sequence again.

**Use case**: Recovery from stale IPC state after MicroPython soft-reset (`Ctrl+D`) or rapid Run/REPL transitions that leave the pipe in an inconsistent state.

---

### `cm33_ipc_pipe_isr`

```c
void cm33_ipc_pipe_isr(void);
```

CM33 IPC pipe interrupt service routine. Calls `Cy_IPC_Pipe_ExecuteCallback(CM33_IPC_PIPE_EP_ADDR)` to dispatch received messages to registered client callbacks.

Installed automatically by `cm33_ipc_communication_setup()` via the pipe config's `userPipeIsrHandler` field.

---

### `cm55_ipc_communication_setup`

```c
void cm55_ipc_communication_setup(void);
```

Initializes the IPC pipe infrastructure on the CM55 core. Performs:

1. `Cy_IPC_Pipe_Config()` -- Configures endpoint array
2. `Cy_IPC_Pipe_Init()` -- Initializes the pipe with mirrored EP2/EP1 configuration
3. `Cy_SysInt_Init()` + `NVIC_EnableIRQ()` -- Enables the EP2 interrupt on CM55's NVIC

**Call site**: At the top of the CM55 GFX task, **before** any `ipc_sensorhub_init()`, `ipc_lcd_init()`, or other IPC module initialization.

---

### `cm55_ipc_pipe_isr` / `Cy_SysIpcPipeIsrCm55`

```c
void cm55_ipc_pipe_isr(void);       /* declared in header */
void Cy_SysIpcPipeIsrCm55(void);    /* actual implementation in CM55 source */
```

CM55 IPC pipe interrupt service routine. Calls `Cy_IPC_Pipe_ExecuteCallback(CM55_IPC_PIPE_EP_ADDR)`.

---

## 3. Message Structures

### `ipc_msg_t` -- Outgoing Message

```c
typedef struct {
    uint16_t client_id;          /* Bits 0-7: Client ID for routing */
    uint16_t intr_mask;          /* Bits 16-31: Release mask (MANDATORY for Pipe Driver) */
    uint32_t cmd;                /* Command code (IPC_CMD_*) */
    uint32_t value;              /* Command argument, flags, or pointer (cast) */
    char     data[128];          /* Payload buffer (IPC_DATA_MAX_LEN) */
} ipc_msg_t;
```

| Field | Size | Description |
|-------|------|-------------|
| `client_id` | 2 bytes | Selects the receiving callback. Must match a registered client ID on the target endpoint. |
| `intr_mask` | 2 bytes | Release interrupt mask. **Must be set** -- the Cy_IPC_Pipe driver uses this to notify the receiver. Typically `CY_IPC_CYPIPE_INTR_MASK`. |
| `cmd` | 4 bytes | One of the `IPC_CMD_*` command codes (see Section 4). |
| `value` | 4 bytes | Overloaded: widget handle, flags, or `(uint32_t)&response` pointer for bidirectional commands. |
| `data` | 128 bytes | Packed payload. Contents depend on `cmd`. |

### `ipc_response_t` -- Bidirectional Response

```c
typedef struct __attribute__((packed)) {
    volatile uint8_t ready;     /* 0=pending, 1=ready (written by responder) */
    uint8_t  cmd;               /* Echo of the request command */
    uint8_t  status;            /* 0=success, nonzero=error code */
    uint8_t  reserved;
    uint16_t data_len;          /* Bytes of valid data in data[] */
    uint16_t reserved2;
    uint8_t  data[240];         /* Response payload (IPC_RESPONSE_DATA_MAX) */
} ipc_response_t;
```

**Bidirectional pattern**:

1. Requester allocates `ipc_response_t` on stack, sets `ready = 0`
2. Requester stores `(uint32_t)&response` in `ipc_msg_t.value`
3. Requester sends IPC message via `Cy_IPC_Pipe_SendMessage()`
4. Responder processes command, writes result into the response struct, sets `ready = 1`
5. Requester busy-waits on `response.ready` with a timeout

**Important**: The response struct must remain valid (on stack or static) until `ready == 1`. Do not free or reuse it while the responder may still be writing.

---

## 4. IPC Command Reference

### 4.1 UI Commands (0x50--0x63)

Defined in `ipc_ui_protocol.h`. Direction: CM33_NS -> CM55 (some bidirectional).

| Code | Macro | Direction | Description |
|------|-------|-----------|-------------|
| `0x50` | `IPC_CMD_UI_CREATE` | Bidir | Create LVGL widget. Payload: `ipc_ui_create_t`. Response: handle ID. |
| `0x51` | `IPC_CMD_UI_DELETE` | Fire-forget | Delete widget by handle (`msg.value`). |
| `0x52` | `IPC_CMD_UI_SET_TEXT` | Fire-forget | Set text on widget. Handle in `msg.value`, text in `msg.data[]`. |
| `0x53` | `IPC_CMD_UI_SET_VALUE` | Fire-forget | Set numeric value. Handle in `msg.value`, value in `msg.data[]`. |
| `0x54` | `IPC_CMD_UI_SET_POSITION` | Fire-forget | Set (x, y) position. |
| `0x55` | `IPC_CMD_UI_SET_SIZE` | Fire-forget | Set (width, height). |
| `0x56` | `IPC_CMD_UI_SET_COLOR` | Fire-forget | Set primary color (0xRRGGBB). |
| `0x57` | `IPC_CMD_UI_SET_VISIBLE` | Fire-forget | Show/hide widget. `msg.value`: 0=hide, 1=show. |
| `0x58` | `IPC_CMD_UI_POLL_EVENTS` | Bidir | Poll pending UI events. Response: array of `ipc_ui_event_t`. |
| `0x59` | `IPC_CMD_UI_CLEAR_ALL` | Fire-forget | Delete all widgets, reset UI state. |
| `0x5A` | `IPC_CMD_UI_SET_DOTMATRIX` | Fire-forget | Set dot matrix bitmap data. |
| `0x5B` | `IPC_CMD_UI_GET_VALUE` | Bidir | Read current widget value. |
| `0x5C` | `IPC_CMD_UI_LIST` | Bidir | List active widgets. Response: count + `ipc_ui_widget_info_t[]`. |
| `0x5D` | `IPC_CMD_UI_SET_IMAGE` | Fire-forget | Set image pixel data (chunked RGB565 transfer). |
| `0x5E` | `IPC_CMD_UI_SET_SCREEN` | Fire-forget | Set screen dimensions + reset layout. |
| `0x5F` | `IPC_CMD_UI_IDE_STATUS` | Fire-forget | Show/hide IDE connection status overlay. |
| `0x60` | `IPC_CMD_UI_CHART_ADD_SERIES` | Bidir | Add data series to chart widget. |
| `0x61` | `IPC_CMD_UI_CHART_SET_NEXT` | Fire-forget | Append next value to chart series. |
| `0x62` | `IPC_CMD_UI_DEPLOY_SCREEN` | Fire-forget | Show native deploy overlay screen. |

Range sentinel: `IPC_CMD_UI_FIRST = 0x50`, `IPC_CMD_UI_LAST = 0x63`.

### 4.2 GPIO / LED Commands (0x80--0x81)

| Code | Macro | Direction | Description |
|------|-------|-----------|-------------|
| `0x80` | `IPC_CMD_GPIO_LED_STATE` | CM33 -> CM55 | Push LED state bitmask to LVGL UI. |
| `0x81` | `IPC_CMD_LED_TOGGLE` | CM55 -> CM33 | Toggle physical LED. `data[0]` = LED index (0--2). |

### 4.3 Sensor Data Commands (0x91--0x99)

| Code | Macro | Direction | Description |
|------|-------|-----------|-------------|
| `0x91` | `IPC_CMD_SENSOR_BMI270` | CM33 -> CM55 | BMI270 IMU data (`ipc_sensor_bmi270_t`). |
| `0x92` | `IPC_CMD_SENSOR_DPS368` | CM33 -> CM55 | DPS368 pressure/temp (`ipc_sensor_dps368_t`). AI Kit only. |
| `0x93` | `IPC_CMD_SENSOR_SHT40` | CM33 -> CM55 | SHT40 humidity/temp (`ipc_sensor_sht40_t`). AI Kit only. |
| `0x94` | `IPC_CMD_SENSOR_BMM350` | CM33 -> CM55 | BMM350 magnetometer (`ipc_sensor_bmm350_t`). |
| `0x95` | `IPC_CMD_SENSOR_CAPSENSE` | CM33 -> CM55 | CapSense buttons + slider (`ipc_sensor_capsense_t`). Eva Kit only. |
| `0x96` | `IPC_CMD_SENSOR_POT` | CM33 -> CM55 | Potentiometer ADC (`ipc_sensor_pot_t`). Eva Kit only. |
| `0x97` | `IPC_CMD_SENSOR_AUTO_CTRL` | CM55 -> CM33 | Start/stop sensor auto-push. `data[0]`: 0=stop, 1=start. |
| `0x98` | `IPC_CMD_SYSTEM_SAFE_REBOOT` | CM55 -> CM33 | Request one-shot safe boot recovery. |
| `0x99` | `IPC_CMD_DELETE_MAIN_PY` | CM55 -> CM33 | Delete `/main.py` autorun script + reboot. |

### 4.4 TESAIoT Credential Commands (0xA0--0xAF)

Secure credential management via OPTIGA Trust M on CM33_NS.

| Code | Macro | Direction | Description |
|------|-------|-----------|-------------|
| `0xA0` | `IPC_CMD_TESAIOT_INIT` | CM55 -> CM33 | Initialize OPTIGA + credential subsystem. |
| `0xA1` | `IPC_CMD_TESAIOT_DEVICE_ID` | CM55 -> CM33 | Read device ID from slot 0. |
| `0xA2` | `IPC_CMD_TESAIOT_LICENSE` | CM55 -> CM33 | Read/verify license key from slot 1. |
| `0xA3` | `IPC_CMD_TESAIOT_CRED_READ` | CM55 -> CM33 | Read credential from slot N. |
| `0xA4` | `IPC_CMD_TESAIOT_CRED_WRITE` | CM55 -> CM33 | Write credential to slot N. |
| `0xA5` | `IPC_CMD_TESAIOT_RANDOM` | CM55 -> CM33 | Generate hardware random bytes. |
| `0xA6` | `IPC_CMD_TESAIOT_CRED_ERASE` | CM55 -> CM33 | Erase credential slot N. |
| `0xA7` | `IPC_CMD_TESAIOT_HASH` | CM55 -> CM33 | SHA-256 hash computation. |
| `0xA8` | `IPC_CMD_TESAIOT_HMAC` | CM55 -> CM33 | HMAC computation. |
| `0xA9` | `IPC_CMD_TESAIOT_AES_KEYGEN` | CM55 -> CM33 | AES key generation. |
| `0xAA` | `IPC_CMD_TESAIOT_AES_ENC` | CM55 -> CM33 | AES encryption. |
| `0xAB` | `IPC_CMD_TESAIOT_AES_DEC` | CM55 -> CM33 | AES decryption. |
| `0xAC` | `IPC_CMD_TESAIOT_SIGN` | CM55 -> CM33 | ECDSA signature. |
| `0xAD` | `IPC_CMD_TESAIOT_COUNTER_RD` | CM55 -> CM33 | Read monotonic counter. |
| `0xAE` | `IPC_CMD_TESAIOT_COUNTER_INC` | CM55 -> CM33 | Increment monotonic counter. |
| `0xAF` | `IPC_CMD_TESAIOT_HEALTH` | CM55 -> CM33 | Run OPTIGA self-test suite. |

#### TESAIoT Credential Slot Mapping

| Slot | Macro | OID | Max Size | Purpose |
|------|-------|-----|----------|---------|
| 0 | `TESAIOT_SLOT_DEVICE_ID` | 0xF1D0 | 140 B | Device identifier |
| 1 | `TESAIOT_SLOT_LICENSE_KEY` | 0xF1D1 | 140 B | License key |
| 2 | `TESAIOT_SLOT_MQTT_USER` | 0xF1D2 | 140 B | MQTT username |
| 3 | `TESAIOT_SLOT_MQTT_PASS` | 0xF1D3 | 140 B | MQTT password |
| 4 | **RESERVED** | 0xF1D4 | -- | Protected Update confidentiality key (do NOT use) |
| 5 | `TESAIOT_SLOT_WIFI_SSID` | 0xF1D5 | 140 B | WiFi SSID |
| 6 | `TESAIOT_SLOT_WIFI_PASS` | 0xF1D6 | 140 B | WiFi password |
| 7 | `TESAIOT_SLOT_API_KEY` | 0xF1D7 | 140 B | API key |
| 8--11 | `TESAIOT_SLOT_USER0`..`USER3` | 0xF1D8--0xF1DB | 140 B | User-defined |
| 12 | `TESAIOT_SLOT_LARGE0` | 0xF1E0 | 1500 B | Large data object 0 |
| 13 | `TESAIOT_SLOT_LARGE1` | 0xF1E1 | 1500 B | Large data object 1 |

Slot-to-OID lookup: `tesaiot_slot_to_oid(uint8_t slot)` (inline in header). Handles the gap at slot 4 and the Type 2 large slots at 12--13.

### 4.5 Radar Command (0xB0)

| Code | Macro | Direction | Description |
|------|-------|-----------|-------------|
| `0xB0` | `IPC_CMD_RADAR_STATUS` | Bidir | Query radar presence detection status (`ipc_radar_status_t`). |

### 4.6 HSM / Security Commands (0xB5--0xBC)

OPTIGA Trust M operations requested by CM55 UI (HSM page) and processed by CM33_NS.

| Code | Macro | Direction | Description |
|------|-------|-----------|-------------|
| `0xB5` | `IPC_CMD_HSM_REQUEST` | CM55 -> CM33 | Read chip data (UID, lifecycle state, certs, counters). |
| `0xB6` | `IPC_CMD_HSM_BENCHMARK` | CM55 -> CM33 | Run crypto benchmarks (ECC, SHA, RNG). |
| `0xB7` | `IPC_CMD_HSM_READ_CERT` | CM55 -> CM33 | Read + parse certificate DER (slot in `resp->cmd`). |
| `0xB8` | `IPC_CMD_HSM_PIN_CHECK` | CM55 -> CM33 | Check if PIN exists in DATA_3 object. |
| `0xB9` | `IPC_CMD_HSM_PIN_SET` | CM55 -> CM33 | Write SHA-256(digits) to DATA_3. |
| `0xBA` | `IPC_CMD_HSM_PIN_VERIFY` | CM55 -> CM33 | Verify PIN against stored hash. |
| `0xBB` | `IPC_CMD_HSM_HEALTH` | CM55 -> CM33 | Run 8 OPTIGA self-tests. |
| `0xBC` | `IPC_CMD_HSM_PIN_RESET` | CM55 -> CM33 | Erase PIN from DATA_3 (requires old PIN verification). |

### 4.7 Joystick Commands (0xC0--0xC1)

| Code | Macro | Direction | Description |
|------|-------|-----------|-------------|
| `0xC0` | `IPC_CMD_JOYSTICK_STATE` | Bidir | Read joystick state (`ipc_joystick_state_t`). |
| `0xC1` | `IPC_CMD_JOYSTICK_INIT` | CM33 -> CM55 | Initialize USB Host HID subsystem on CM55. |

### 4.8 WiFi Commands (0xD0--0xD9)

| Code | Macro | Direction | Description |
|------|-------|-----------|-------------|
| `0xD0` | `IPC_CMD_WIFI_SCAN` | Bidir | Scan for WiFi networks. Response: `ipc_wifi_scan_entry_t[]` (max 6). |
| `0xD1` | `IPC_CMD_WIFI_CONNECT` | Bidir | Connect to WiFi AP. SSID+password in `msg.data[]`. |
| `0xD2` | `IPC_CMD_WIFI_DISCONNECT` | Fire-forget | Disconnect from WiFi AP. |
| `0xD3` | `IPC_CMD_WIFI_STATUS` | Bidir | Query WiFi connection status. |
| `0xD4` | `IPC_CMD_WIFI_IP` | Bidir | Query assigned IP address. |
| `0xD5` | `IPC_CMD_WIFI_SOFTAP` | Fire-forget | Start SoftAP mode. |
| `0xD6` | `IPC_CMD_TOUCH_PAUSE` | CM33 -> CM55 | Pause touch I2C polling (for OPTIGA/CapSense bus sharing). |
| `0xD7` | `IPC_CMD_TOUCH_RESUME` | CM33 -> CM55 | Resume touch I2C + reinit touch controller. |
| `0xD8` | `IPC_CMD_WIFI_STATE_PUSH` | CM33 -> CM55 | Push WiFi connected state. `data[0]`: 0=disconnected, 1=connected. |
| `0xD9` | `IPC_CMD_TIME_PUSH` | CM33 -> CM55 | Push NTP-synced time string (null-terminated in `data[]`). |

### 4.9 LCD Terminal Commands (0xE0--0xE2)

| Code | Macro | Direction | Description |
|------|-------|-----------|-------------|
| `0xE0` | `IPC_CMD_LCD_PRINT` | CM33 -> CM55 | Print text to LVGL terminal. Text in `msg.data[]`. |
| `0xE1` | `IPC_CMD_LCD_CLEAR` | CM33 -> CM55 | Clear terminal output. |
| `0xE2` | `IPC_CMD_LCD_THEME` | CM33 -> CM55 | Set terminal color theme. |

### 4.10 MQTT Commands (0xF0--0xF6)

| Code | Macro | Direction | Description |
|------|-------|-----------|-------------|
| `0xF0` | `IPC_CMD_MQTT_CONNECT` | Bidir | Connect to MQTT broker. |
| `0xF1` | `IPC_CMD_MQTT_DISCONNECT` | Fire-forget | Disconnect from MQTT broker. |
| `0xF2` | `IPC_CMD_MQTT_PUBLISH` | Fire-forget | Publish message to topic. |
| `0xF3` | `IPC_CMD_MQTT_SUBSCRIBE` | Bidir | Subscribe to topic. |
| `0xF6` | `IPC_CMD_MQTT_POLL` | Bidir | Poll for incoming MQTT messages. |

---

## 5. Sensor Data Structures

All sensor structs are `__attribute__((packed))` and fit within `ipc_msg_t.data[]` (128 bytes max). Each includes a `sequence` counter for change detection.

### `ipc_sensor_bmi270_t` (14 bytes)

```c
typedef struct __attribute__((packed)) {
    int16_t  ax, ay, az;     /* Accel raw (divide by 16384 for g) */
    int16_t  gx, gy, gz;     /* Gyro raw (divide by 16.4 for dps) */
    uint16_t sequence;
} ipc_sensor_bmi270_t;
```

### `ipc_sensor_dps368_t` (8 bytes)

```c
typedef struct __attribute__((packed)) {
    int32_t  pressure_x100;     /* hPa * 100 */
    int16_t  temperature_x100;  /* Celsius * 100 */
    uint16_t sequence;
} ipc_sensor_dps368_t;
```

### `ipc_sensor_sht40_t` (6 bytes)

```c
typedef struct __attribute__((packed)) {
    int16_t  temperature_x100;  /* Celsius * 100 */
    uint16_t humidity_x100;     /* %RH * 100 */
    uint16_t sequence;
} ipc_sensor_sht40_t;
```

### `ipc_sensor_bmm350_t` (10 bytes)

```c
typedef struct __attribute__((packed)) {
    int16_t  mx_x100;          /* X micro-Tesla * 100 */
    int16_t  my_x100;          /* Y micro-Tesla * 100 */
    int16_t  mz_x100;          /* Z micro-Tesla * 100 */
    uint16_t heading_x10;      /* Compass heading * 10 (0-3600) */
    uint16_t sequence;
} ipc_sensor_bmm350_t;
```

### `ipc_sensor_capsense_t` (6 bytes)

```c
typedef struct __attribute__((packed)) {
    uint8_t  btn0_pressed;     /* 0 or 1 */
    uint8_t  btn1_pressed;     /* 0 or 1 */
    uint8_t  slider;           /* 0-100 (%) */
    uint8_t  reserved;
    uint16_t sequence;
} ipc_sensor_capsense_t;
```

### `ipc_sensor_pot_t` (6 bytes)

```c
typedef struct __attribute__((packed)) {
    uint16_t raw;              /* 0-65535 (16-bit scaled) */
    uint16_t percent_x10;     /* 0-1000 (0.0-100.0%) */
    uint16_t sequence;
} ipc_sensor_pot_t;
```

### `ipc_joystick_state_t` (32 bytes)

```c
typedef struct __attribute__((packed)) {
    uint8_t  connected;        /* 0=disconnected, 1=connected */
    uint8_t  sequence;         /* Increments on each new HID report */
    uint8_t  left_x, left_y;  /* 0x00-0xFF, center ~0x80 */
    uint8_t  right_x, right_y;
    uint8_t  buttons1;         /* hat[0:3] + X[4] A[5] B[6] Y[7] */
    uint8_t  buttons2;         /* LB[0] RB[1] LT[2] RT[3] Back[4] Start[5] L3[6] R3[7] */
    uint16_t vid, pid;         /* USB Vendor/Product ID */
    /* Debug fields */
    uint8_t  usb_init_done;
    uint8_t  init_stage;       /* USB_STAGE_* (0-7) */
    uint16_t add_event_cnt;
    uint16_t remove_event_cnt;
    uint16_t report_cnt;
    uint32_t isr_count;
    uint32_t port_power_cnt;
    uint8_t  usbh_running;
    uint8_t  num_devices;
    uint8_t  root_conns;
    uint8_t  usb_class;        /* 0x03=HID, 0xFF=vendor */
    uint16_t usb_vid;
    uint16_t usb_pid;
} ipc_joystick_state_t;
```

### `ipc_radar_status_t` (8 bytes)

```c
typedef struct __attribute__((packed)) {
    uint8_t initialized;       /* 1 if radar HW init succeeded */
    uint8_t presence;          /* 1 if person detected */
    uint8_t reserved[2];
    float   energy;            /* Smoothed signal energy delta */
} ipc_radar_status_t;
```

### `ipc_wifi_scan_entry_t` (36 bytes)

```c
typedef struct __attribute__((packed)) {
    char    ssid[33];          /* Null-terminated, max 32 chars */
    int8_t  rssi;              /* Signal strength dBm */
    uint8_t security;          /* 0=open, 1=WEP, 2=WPA, 3=WPA2, 4=WPA3 */
    uint8_t channel;
} ipc_wifi_scan_entry_t;
```

Maximum entries per scan response: `IPC_WIFI_SCAN_MAX_ENTRIES = 6` (limited by `IPC_RESPONSE_DATA_MAX / 36`).

---

## 6. UI Widget Protocol

### Widget Types

```c
typedef enum {
    UI_WIDGET_BUTTON    = 1,    UI_WIDGET_LABEL     = 2,
    UI_WIDGET_SLIDER    = 3,    UI_WIDGET_SWITCH    = 4,
    UI_WIDGET_CHECKBOX  = 5,    UI_WIDGET_ARC       = 6,
    UI_WIDGET_BAR       = 7,    UI_WIDGET_SPINNER   = 8,
    UI_WIDGET_DROPDOWN  = 9,    UI_WIDGET_TEXTAREA  = 10,
    UI_WIDGET_SEG7      = 11,   UI_WIDGET_DOTMATRIX = 12,
    UI_WIDGET_CHART     = 13,   UI_WIDGET_IMAGE     = 14,
    UI_WIDGET_PANEL     = 15,   UI_WIDGET_COMPASS   = 16,
} ui_widget_type_t;
```

### Create Payload (`ipc_ui_create_t`, 121 bytes)

```c
typedef struct __attribute__((packed)) {
    uint8_t  widget_type;      /* ui_widget_type_t (1-16) */
    int16_t  x, y;             /* Position (-1 = auto-layout) */
    int16_t  w, h;             /* Size (-1 = auto-size) */
    uint32_t color;            /* 0xRRGGBB (0 = theme default) */
    int32_t  min_val;          /* Range min / dot matrix cols */
    int32_t  max_val;          /* Range max / dot matrix rows */
    int32_t  init_val;         /* Initial value / font size */
    char     text[96];         /* Label text (UI_CREATE_TEXT_MAX) */
} ipc_ui_create_t;
```

### Event Structures

```c
/* Event types */
#define UI_EVENT_CLICKED        (1)
#define UI_EVENT_VALUE_CHANGED  (2)
#define UI_EVENT_TOGGLED        (3)

/* Single event entry in POLL_EVENTS response */
typedef struct __attribute__((packed)) {
    uint8_t  handle_id;        /* Widget that generated the event */
    uint8_t  event_type;       /* UI_EVENT_* */
    int32_t  value;            /* Event value (slider pos, toggle state, etc.) */
    uint16_t reserved;
} ipc_ui_event_t;              /* 8 bytes */

/* Widget info entry in LIST response */
typedef struct __attribute__((packed)) {
    uint8_t  handle_id;        /* Widget handle (0-31) */
    uint8_t  widget_type;      /* ui_widget_type_t */
} ipc_ui_widget_info_t;        /* 2 bytes */
```

### Response Status Codes

| Code | Macro | Meaning |
|------|-------|---------|
| 0 | `UI_STATUS_OK` | Success |
| 1 | `UI_STATUS_TABLE_FULL` | Widget table full (max 32) |
| 2 | `UI_STATUS_INVALID_TYPE` | Unknown widget type |
| 3 | `UI_STATUS_INVALID_HANDLE` | Handle ID not found |
| 4 | `UI_STATUS_ERROR` | Generic error |
| 5 | `UI_STATUS_ALLOC_FAILED` | LVGL memory allocation failed |

### Limits

| Constant | Value | Description |
|----------|-------|-------------|
| `UI_MAX_WIDGETS` | 32 | Maximum simultaneous widgets |
| `UI_EVENT_RING_SIZE` | 16 | Event ring buffer depth on CM55 |
| `UI_MAX_EVENTS_PER_POLL` | 8 | Max events returned per poll (8 * 8 = 64 bytes) |
| `UI_CREATE_TEXT_MAX` | 96 | Max text length in create payload |
| `UI_CHART_MAX_SERIES` | 4 | Max data series per chart widget |

---

## 7. IPC Sensorhub Receiver API (CM55 Side)

Header: `ipc_sensorhub.h`

### Initialization

```c
bool ipc_sensorhub_init(void);
```

Registers an IPC pipe callback on `CM55_IPC_SENSOR_CLIENT_ID` (6). Handles incoming `IPC_CMD_SENSOR_*` messages and stores the latest sample for each sensor type. Also handles `IPC_CMD_WIFI_STATE_PUSH` and `IPC_CMD_TIME_PUSH`.

**Prerequisite**: `cm55_ipc_communication_setup()` must have been called first.

Returns `true` on success.

### Snapshot API

```c
void ipc_sensorhub_snapshot(sensorhub_snapshot_t *snap);
```

Takes an atomic snapshot of all latest sensor data. Clears the per-sensor `*_changed` flags after reading, enabling change-detection patterns:

```c
sensorhub_snapshot_t snap;
ipc_sensorhub_snapshot(&snap);

if (snap.bmi270_changed) {
    float ax_g = snap.bmi270.ax / 16384.0f;
    // ... update UI
}
```

### Snapshot Structure

```c
typedef struct {
    ipc_sensor_bmi270_t   bmi270;     bool has_bmi270;   bool bmi270_changed;
    ipc_sensor_dps368_t   dps368;     bool has_dps368;   bool dps368_changed;
    ipc_sensor_sht40_t    sht40;      bool has_sht40;    bool sht40_changed;
    ipc_sensor_bmm350_t   bmm350;     bool has_bmm350;   bool bmm350_changed;
    ipc_sensor_capsense_t capsense;   bool has_capsense;  bool capsense_changed;
    ipc_sensor_pot_t      pot;        bool has_pot;       bool pot_changed;
    uint8_t led_state_bitmask;        bool has_led_state; bool led_state_changed;
} sensorhub_snapshot_t;
```

- `has_*` -- `true` once the first sample has ever been received
- `*_changed` -- `true` if a new sample arrived since the last `ipc_sensorhub_snapshot()` call

### WiFi and Time Queries

```c
bool ipc_sensorhub_wifi_connected(void);
```

Returns the last WiFi state pushed by CM33_NS via `IPC_CMD_WIFI_STATE_PUSH`. Non-blocking volatile read -- no IPC roundtrip. This replaces the old `wifi_manager_is_connected()` query which used a blocking IPC call with 5-second timeout.

```c
bool ipc_sensorhub_ntp_synced(void);
```

Returns `true` if CM33_NS has sent at least one `IPC_CMD_TIME_PUSH` (NTP sync complete).

```c
bool ipc_sensorhub_get_time_str(char *buf, size_t buf_size);
```

Copies the latest NTP time string (e.g., `"Mon 10 Mar 14:35"`) into `buf`. Returns `false` if NTP not yet synced. Buffer must be >= 32 bytes.

### Local Feed API (Eva Kit)

On Eva Kit, the CM55 reads some sensors directly (CM33_NS cannot access SCB0 I2C while display owns it). These functions inject data into the sensorhub as if it arrived via IPC:

```c
void ipc_sensorhub_feed_bmi270(const ipc_sensor_bmi270_t *data);
void ipc_sensorhub_feed_bmm350(const ipc_sensor_bmm350_t *data);
void ipc_sensorhub_feed_capsense(const ipc_sensor_capsense_t *data);
void ipc_sensorhub_feed_pot(const ipc_sensor_pot_t *data);
```

---

## 8. LCD Terminal API (CM55 Side)

Header: `ipc_lcd.h`

### Initialization

```c
bool ipc_lcd_init(lv_obj_t *parent);
```

Sets up the IPC pipe callback on `CM55_IPC_PIPE_CLIENT_ID` (5), creates an LVGL timer that polls the receive queue, and builds a terminal-like text renderer inside `parent` (typically the Playground tab container).

**Prerequisites**: LVGL initialized, display ready, `cm55_ipc_communication_setup()` called.

### Container Management

```c
void ipc_lcd_set_container(lv_obj_t *parent);
```

Updates the terminal's parent container. Called when navigating between pages:
- Pass the new container pointer when the Playground page is created
- Pass `NULL` when the page is destroyed

### Panel Visibility

```c
void ipc_lcd_toggle_panel(void);
bool ipc_lcd_is_panel_visible(void);
```

Toggle or query the console panel visibility. Used by the Playground page's Console Log button. If the terminal has not been created yet, `toggle_panel` force-creates it.

### Notification

```c
bool ipc_lcd_has_unread(void);
void ipc_lcd_clear_unread(void);
```

Track unread console output for badge notifications. `has_unread()` returns `true` if new text arrived while the console panel was hidden.

### Auto-Navigation

```c
void ipc_lcd_reset_auto_nav(void);
```

Resets the one-shot auto-navigate flag. Called on `IPC_CMD_UI_CLEAR_ALL` (new MicroPython session) so that the next `lcd.print()` call auto-navigates to the Playground page.

---

## 9. Critical Constraints

### Single-Owner Deadlock

**The most dangerous pitfall in this IPC framework.**

`Cy_IPC_Pipe_SendMessage()` must **never** be called from within an IPC pipe callback handler. The IPC pipe is single-owner: the handler holds the pipe lock, and `SendMessage` attempts to acquire the same lock, causing a **permanent deadlock**.

**Symptom**: Display permanently frozen, no response to touch, no error output.

**Correct pattern** -- deferred flag:

```c
static volatile bool s_need_reply = false;

/* IPC callback (runs in ISR context) */
static void my_ipc_handler(uint32_t *msg) {
    // Process incoming data...
    s_need_reply = true;  // Set flag, do NOT call SendMessage here
}

/* Task main loop */
void my_task(void *arg) {
    for (;;) {
        if (s_need_reply) {
            s_need_reply = false;
            Cy_IPC_Pipe_SendMessage(...);  // Safe: outside handler
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

### Init Order (CM55)

`cm55_ipc_communication_setup()` **must** be called before any `Cy_IPC_Pipe_RegisterCallback()`. Without this, `RegisterCallback` accesses uninitialized pipe endpoints, causing a HardFault at boot.

**Order in the CM55 GFX task** (`tesaiot_display_task`, as shipped):

```
1. cm55_ipc_communication_setup()   // FIRST — this is the only load-bearing rule
2. ipc_sensorhub_init()
3. ipc_service_init()               // WiFi, MQTT, TESAIoT
   ... display bring-up (may fail; IPC keeps working either way) ...
4. ipc_lcd_init(NULL)               // always, display or not
5. if (display_ok) ipc_ui_init(NULL) // only when the display came up
```

Steps 2-5 are the shipped sequence, not a requirement — only step 1 must come
first. Both containers are passed `NULL` on purpose (deferred binding): the
Playground page calls `ipc_lcd_set_container()` / `ipc_ui_set_container()`
when it is created and NULL-ifies them when it is destroyed, so the handlers
never hold a parent that navigation has already deleted.

### Init Order (CM33_NS)

`cm33_ipc_communication_setup()` must be called before any module that registers callbacks or sends messages.

### Thread Safety

- **Sensor data**: ISR writes volatile fields; `ipc_sensorhub_snapshot()` reads atomically. Safe from any task.
- **LVGL widgets**: All LVGL calls must happen in the GFX task. The IPC UI module queues commands via ring buffer and processes them in an LVGL timer (GFX task context).
- **`ipc_response_t`**: The response struct must remain valid on the requester's stack until `ready == 1`. Do not use across task boundaries without static allocation.

### Memory Bus Constraints

- **SDHC0 is PPC-protected for CM33_NS only**. CM55 cannot access WiFi hardware directly -- all WiFi operations go through IPC.
- **SCB0 I2C is shared** between CM55 touch controller and CM33_NS OPTIGA Trust M. Use `IPC_CMD_TOUCH_PAUSE`/`RESUME` to coordinate access.

### Message Size Limits

| Buffer | Max Size | Usage |
|--------|----------|-------|
| `ipc_msg_t.data[]` | 128 bytes | Outgoing payload |
| `ipc_response_t.data[]` | 240 bytes | Response payload |
| `IPC_WIFI_SCAN_MAX_ENTRIES` | 6 entries | WiFi scan results (6 * 36 = 216 bytes) |

---

## 10. Known Gotchas

### IPC Pipe Deadlock (Production Incident)

| Aspect | Detail |
|--------|--------|
| **Symptom** | Display permanently frozen after WiFi state change |
| **Root cause** | `Cy_IPC_Pipe_SendMessage()` called inside IPC request handler |
| **Fix** | Deferred flag pattern (see Section 9) |
| **Command involved** | `IPC_CMD_WIFI_STATE_PUSH (0xD8)` |
| **Resolution** | CM55 uses `ipc_sensorhub_wifi_connected()` (volatile read) instead of blocking IPC query |

### Stale IPC After MicroPython Soft-Reset

| Aspect | Detail |
|--------|--------|
| **Symptom** | `ui.screen()` and `ui.clear()` hang indefinitely after `Ctrl+C` interrupts a running script |
| **Root cause** | IPC channel left in inconsistent state after Python abort |
| **Fix** | `cm33_ipc_communication_recover()` or board soft reset (`Ctrl+D`) |

### CM55 HardFault on RegisterCallback

| Aspect | Detail |
|--------|--------|
| **Symptom** | HardFault at boot, crash in `Cy_IPC_Pipe_RegisterCallback` |
| **Root cause** | `RegisterCallback` called before `cm55_ipc_communication_setup()` |
| **Fix** | Always call `cm55_ipc_communication_setup()` first in GFX task |

### CM55 HardFault on WiFi Access

| Aspect | Detail |
|--------|--------|
| **Symptom** | BusFault kills calling task on CM55 |
| **Root cause** | CM55 tried to access SDHC0 (PPC-protected for CM33_NS) |
| **Fix** | All WiFi operations must go through IPC to CM33_NS |

### LVGL Calls from Wrong Task

| Aspect | Detail |
|--------|--------|
| **Symptom** | HardFault during widget creation |
| **Root cause** | LVGL API called from IPC callback (ISR context) or non-GFX task |
| **Fix** | Queue operations to GFX task. IPC modules use LVGL timers for deferred processing. |

### Response Struct Lifetime

| Aspect | Detail |
|--------|--------|
| **Symptom** | Corrupted response data, sporadic crashes |
| **Root cause** | `ipc_response_t` on stack went out of scope before responder wrote `ready=1` |
| **Fix** | Keep response struct alive until `ready==1`. Use static allocation if needed. |

---

## Appendix A: BSP Feature Flags

Sensor availability varies by board. Use these compile-time flags to guard sensor-related IPC code:

| Flag | AI Kit | Eva Kit | Game Console |
|------|--------|---------|--------------|
| `BSP_HAS_BMI270` | 1 | 1 | 1 |
| `BSP_HAS_BMM350` | 1 | 1 | 0 |
| `BSP_HAS_DPS368` | 1 | 0 | 0 |
| `BSP_HAS_SHT40` | 1 | 0 | 0 |
| `BSP_HAS_RADAR` | 1 | 0 | 0 |
| `BSP_HAS_CAPSENSE` | 0 | 1 | 0 |
| `BSP_HAS_POTENTIOMETER` | 0 | 1 | 0 |

Flags default to `0` in `bsp_feature_flags.h` and are set to `1` via `-DBSP_HAS_XXX=1` in the build system's `bsp_features.mk` per target.

## Appendix B: Constants Quick Reference

```c
/* Buffer sizes */
#define IPC_DATA_MAX_LEN         128    /* ipc_msg_t.data[] */
#define IPC_RESPONSE_DATA_MAX    240    /* ipc_response_t.data[] */

/* Pipe topology */
#define CY_IPC_MAX_ENDPOINTS       5
#define CY_IPC_CYPIPE_CLIENT_CNT  10

/* Channels and interrupts */
#define CY_IPC_CHAN_CYPIPE_EP1     4    /* CM33 channel */
#define CY_IPC_INTR_CYPIPE_EP1    4    /* CM33 interrupt */
#define CY_IPC_CHAN_CYPIPE_EP2    15    /* CM55 channel */
#define CY_IPC_INTR_CYPIPE_EP2    5    /* CM55 interrupt */

/* Endpoint addresses */
#define CM33_IPC_PIPE_EP_ADDR      1
#define CM55_IPC_PIPE_EP_ADDR      2

/* Physics */
#define GRAVITY_ACCEL          9.80665f
```
