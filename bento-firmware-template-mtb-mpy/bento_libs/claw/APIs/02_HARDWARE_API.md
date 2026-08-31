# BENTO PSoC Edge E84 — Hardware Peripheral API Reference

Version: 2026.03 | Platform: PSoC Edge E84 (PSE84-xxx) | MicroPython 1.24 | ModusToolbox 3.6

---

## Table of Contents

1. [GPIO API](#1-gpio-api)
2. [RTC API](#2-rtc-api)
3. [Camera HAL](#3-camera-hal)
4. [USB HID Joystick](#4-usb-hid-joystick)
5. [PDM Microphone](#5-pdm-microphone)
6. [Machine Pin](#6-machine-pin)
7. [I2C Target (Slave Mode)](#7-i2c-target-slave-mode)
8. [Board Support Matrix](#8-board-support-matrix)

---

## 1. GPIO API

Module: `gpio` (CM33_NS MicroPython module)
Source: `common/mpy/modgpio.c`

The GPIO module provides BSP-aware LED control and button reading with an object-oriented API. LED state changes are automatically mirrored to the CM55 display via IPC.

### 1.1 Module Functions

#### gpio.led(index) -> LED

Get an LED object by index.

```python
import gpio
led0 = gpio.led(0)  # LED1
led2 = gpio.led(2)  # RGB_RED (AI Kit only)
```

**C prototype:**
```c
static mp_obj_t mod_gpio_led(mp_obj_t index_obj);
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `index` | int | LED index (0 to `num_leds()-1`) |

**Returns:** `LED` object. Raises `ValueError` if index out of range.

**Thread safety:** Safe. GPIO operations use PDL `Cy_GPIO_*` functions which are register-level atomic.

**Board support:**

| Index | AI Kit (5 LEDs) | Eva Kit (2 LEDs) | Game Console (2 LEDs) |
|-------|-----------------|-------------------|----------------------|
| 0 | LED1 | LED1 | LED1 |
| 1 | LED2 | LED2 | LED2 |
| 2 | RGB_RED | -- | -- |
| 3 | RGB_BLUE | -- | -- |
| 4 | RGB_GREEN | -- | -- |

---

#### gpio.button(index) -> Button

Get a Button object by index.

```python
btn = gpio.button(0)  # SW1
```

**C prototype:**
```c
static mp_obj_t mod_gpio_button(mp_obj_t index_obj);
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `index` | int | Button index (0 to `num_buttons()-1`) |

**Returns:** `Button` object. Raises `ValueError` if index out of range.

All boards have 1 user button (SW1).

---

#### gpio.num_leds() -> int

Return the number of available LEDs on the current board.

```python
n = gpio.num_leds()  # 5 on AI Kit, 2 on Eva Kit
```

---

#### gpio.num_buttons() -> int

Return the number of available buttons on the current board.

```python
n = gpio.num_buttons()  # 1 on all boards
```

---

#### gpio.board_info() -> dict

Return board information including name, LED/button count, and pin names.

```python
info = gpio.board_info()
# {'name': 'PSoC Edge AI Dev Kit',
#  'leds': 5,
#  'buttons': 1,
#  'led_names': ['LED1', 'LED2', 'RGB_RED', 'RGB_BLUE', 'RGB_GREEN'],
#  'btn_names': ['SW1']}
```

**C prototype:**
```c
static mp_obj_t mod_gpio_board_info(void);
```

**Return value dict keys:**

| Key | Type | Description |
|-----|------|-------------|
| `name` | str | Board name (`"PSoC Edge AI Dev Kit"` or `"PSoC Edge Eval Kit"`) |
| `leds` | int | Number of LEDs |
| `buttons` | int | Number of buttons |
| `led_names` | list[str] | LED name strings |
| `btn_names` | list[str] | Button name strings |

---

### 1.2 LED Object Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `on()` | `led.on()` | Turn LED on (GPIO high). Sends IPC to CM55. |
| `off()` | `led.off()` | Turn LED off (GPIO low). Sends IPC to CM55. |
| `toggle()` | `led.toggle()` | Invert LED state. Sends IPC to CM55. |
| `value([v])` | `led.value()` / `led.value(1)` | Read or set LED state (0/1). |
| `name()` | `led.name() -> str` | Return LED name string. |

**C prototypes:**
```c
static mp_obj_t gpio_led_on(mp_obj_t self_in);
static mp_obj_t gpio_led_off(mp_obj_t self_in);
static mp_obj_t gpio_led_toggle(mp_obj_t self_in);
static mp_obj_t gpio_led_value(size_t n_args, const mp_obj_t *args);
static mp_obj_t gpio_led_name(mp_obj_t self_in);
```

**IPC notification:** Every LED state change sends `IPC_CMD_GPIO_LED_STATE` to CM55 with a bitmask of all LED states in `data[0]`. This allows the LVGL UI to reflect LED states in real time.

**Hardware:** Uses `Cy_GPIO_Set()`, `Cy_GPIO_Clr()`, `Cy_GPIO_Inv()`, `Cy_GPIO_Read()`. LEDs are initialized as `CY_GPIO_DM_STRONG` output, active high.

**Print representation:**
```python
>>> gpio.led(0)
LED(0, name='LED1')
```

**C example:**
```c
// Initialize LED as output
Cy_GPIO_Pin_FastInit(CYBSP_USER_LED1_PORT, CYBSP_USER_LED1_NUM,
                     CY_GPIO_DM_STRONG, 0, HSIOM_SEL_GPIO);

// Turn LED on
Cy_GPIO_Set(CYBSP_USER_LED1_PORT, CYBSP_USER_LED1_NUM);

// Toggle LED
Cy_GPIO_Inv(CYBSP_USER_LED1_PORT, CYBSP_USER_LED1_NUM);

// Read LED state
uint32_t state = Cy_GPIO_Read(CYBSP_USER_LED1_PORT, CYBSP_USER_LED1_NUM);
```

---

### 1.3 Button Object Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `is_pressed()` | `btn.is_pressed() -> bool` | Check if button is currently pressed. |
| `value()` | `btn.value() -> int` | Read raw GPIO value (0 = pressed, active-low). |
| `name()` | `btn.name() -> str` | Return button name string. |

**C prototypes:**
```c
static mp_obj_t gpio_btn_is_pressed(mp_obj_t self_in);
static mp_obj_t gpio_btn_value(mp_obj_t self_in);
static mp_obj_t gpio_btn_name(mp_obj_t self_in);
```

**Note:** Buttons are active-low with internal pull-up (`CY_GPIO_DM_PULLUP`). `is_pressed()` returns `True` when pin reads `CYBSP_BTN_PRESSED` (0). `value()` returns the raw pin level (0 = pressed, 1 = released).

**Print representation:**
```python
>>> gpio.button(0)
Button(0, name='SW1')
```

**MicroPython example:**
```python
import gpio, time
btn = gpio.button(0)
led = gpio.led(0)

while True:
    if btn.is_pressed():
        led.toggle()
        while btn.is_pressed():
            time.sleep_ms(10)  # Debounce
    time.sleep_ms(50)
```

---

## 2. RTC API

Class: `machine.RTC` (singleton)
Source: `micropython-psoc-edge-psoc-edge-main/ports/psoc-edge/machine_rtc.c`

Hardware RTC backed by the PSoC Edge SRSS RTC block with ALARM1 interrupt support and 28-byte backup register memory that persists across resets.

### 2.1 Constructor

```python
from machine import RTC
rtc = RTC()  # Singleton, no arguments
```

**C prototype:**
```c
static mp_obj_t machine_rtc_make_new(const mp_obj_type_t *type,
                                      size_t n_args, size_t n_kw,
                                      const mp_obj_t *args);
```

---

### 2.2 Methods

#### rtc.init(datetime)

Initialize the RTC with a specific date and time. Resets alarm state.

```python
rtc.init((2026, 3, 14, 5, 10, 30, 0, 0))
# Format: (year, month, day, weekday, hour, minute, second, subseconds)
```

**C prototype:**
```c
static mp_obj_t machine_rtc_init(mp_obj_t self_in, mp_obj_t datetime);
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `datetime` | tuple(8) | `(year, month, day, weekday, hour, min, sec, subsec)` |

**Notes:**
- Year range: 2000-2099 (RTC hardware stores 0-99, century offset = 2000)
- Weekday is auto-calculated by hardware regardless of input value
- Validates all fields; raises `ValueError` on invalid date/time

---

#### rtc.datetime([tuple])

Get or set the current date and time.

```python
# Get current datetime
dt = rtc.datetime()
# Returns: (2026, 3, 14, 5, 10, 30, 0, 0)
#          (year, month, day, weekday, hour, min, sec, subsec)

# Set datetime
rtc.datetime((2026, 3, 14, 5, 10, 30, 0, 0))
```

**C prototype:**
```c
static mp_obj_t machine_rtc_datetime(mp_uint_t n_args, const mp_obj_t *datetime);
```

---

#### rtc.now()

Get current date and time (alias for `datetime()` with no arguments).

```python
dt = rtc.now()
```

---

#### rtc.alarm(time, repeat=False)

Set an alarm. Time can be a datetime tuple (absolute) or milliseconds (relative).

```python
# Relative alarm: 5 seconds from now
rtc.alarm(5000)

# Repeating alarm: every 10 seconds
rtc.alarm(10000, repeat=True)

# Absolute alarm: specific datetime (repeat not allowed)
rtc.alarm((2026, 3, 14, 5, 11, 0, 0, 0))
```

**C prototype:**
```c
static mp_obj_t machine_rtc_alarm(size_t n_args, const mp_obj_t *pos_args,
                                   mp_map_t *kw_args);
```

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `time` | int/tuple | required | Milliseconds (int) or datetime tuple |
| `repeat` | bool | `False` | Repeat alarm periodically (int time only) |

**Notes:**
- Only 1 alarm is supported (ALARM0 / `CY_RTC_ALARM_1`)
- Relative time is rounded up to nearest second: `(ms + 999) / 1000`
- Cannot use `repeat=True` with datetime tuple
- Raises `ValueError` if alarm time is in the past

**Hardware:** Uses `Cy_RTC_SetAlarmDateAndTimeDirect()` with ALARM1 interrupt via NVIC.

---

#### rtc.alarm_left([alarm_id])

Get remaining time until alarm fires, in milliseconds.

```python
remaining_ms = rtc.alarm_left()
```

**C prototype:**
```c
static mp_obj_t machine_rtc_alarm_left(size_t n_args, const mp_obj_t *args);
```

**Returns:** Remaining time in milliseconds. Raises `ValueError` if no alarm is set.

---

#### rtc.alarm_cancel([alarm_id]) / rtc.cancel([alarm_id])

Cancel the active alarm.

```python
rtc.alarm_cancel()
# or equivalently:
rtc.cancel()
```

**C prototype:**
```c
static mp_obj_t machine_rtc_alarm_cancel(size_t n_args, const mp_obj_t *args);
```

**Hardware actions:**
- Disables RTC interrupt mask (`Cy_RTC_SetInterruptMask(0)`)
- Clears pending ALARM1 interrupt
- Disables NVIC IRQ for RTC

---

#### rtc.irq(handler=, trigger=0, wake=-1)

Register a callback function for the RTC alarm interrupt.

```python
def alarm_handler(rtc):
    print("Alarm fired!")

rtc.irq(handler=alarm_handler)
rtc.alarm(3000)  # Fire in 3 seconds
```

**C prototype:**
```c
static mp_obj_t machine_rtc_irq(size_t n_args, const mp_obj_t *pos_args,
                                 mp_map_t *kw_args);
```

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `handler` | callable/None | required | Callback function `fn(rtc)` or `None` to disable |
| `trigger` | int | 0 | Must be 0 (only alarm trigger supported) |
| `wake` | int | -1 | Not implemented, raises `NotImplementedError` |

**Callback execution:** Scheduled via `mp_sched_schedule()` -- runs in MicroPython context, not ISR.

---

#### rtc.memory([data])

Read or write backup register memory. Data persists across soft resets and power cycles (backed by VBAT).

```python
# Write data (max 28 bytes)
rtc.memory(b"Hello RTC!")

# Read data
data = rtc.memory()  # Returns bytes object
```

**C prototype:**
```c
static mp_obj_t machine_rtc_memory(size_t n_args, const mp_obj_t *args);
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `data` | bytes/bytearray | Data to store (max 28 bytes) |

**Returns:** When reading: `bytes` object. Empty bytes if no valid data stored.

**Implementation details:**
- Uses `BACKUP_BREG_SET2[]` registers (8 x 32-bit words = 32 bytes)
- Word 7 stores metadata: magic `0x5254` (16-bit) + length (16-bit)
- Usable data: 7 words x 4 bytes = 28 bytes (`MICROPY_HW_RTC_USER_MEM_MAX`)
- Requires `Cy_RTC_WriteEnable()` / `Cy_RTC_WriteEnable(DISABLED)` bracket

**C example:**
```c
// Write to backup registers
Cy_RTC_WriteEnable(CY_RTC_WRITE_ENABLED);
BACKUP_BREG_SET2[0] = 0xDEADBEEF;
uint32_t meta = (RTC_BREG_MAGIC << 16) | 4;  // 4 bytes stored
BACKUP_BREG_SET2[RTC_BREG_META_INDEX] = meta;
Cy_RTC_WriteEnable(CY_RTC_WRITE_DISABLED);

// Read from backup registers
Cy_RTC_SyncFromRtc();
uint32_t val = BACKUP_BREG_SET2[0];
```

---

#### rtc.deinit()

Reset RTC to January 1, 2015 00:00:00.

```python
rtc.deinit()
```

---

### 2.3 Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `RTC.ALARM0` | 0 | Alarm identifier (only one supported) |

---

## 3. Camera HAL

Source: `kit-pse84-ai/libraries/tesaiot-camera-hal/camera_hal.h`

The Camera HAL provides a unified vtable-based interface for both DVP (OV7675) and USB UVC cameras. Inspired by the Zephyr Video API pattern.

### 3.1 Data Structures

#### camera_error_t

```c
typedef enum {
    CAM_OK = 0,              // Success
    CAM_ERR_NO_FRAME,        // No frame available (try again)
    CAM_ERR_NOT_INIT,        // Backend not initialized
    CAM_ERR_DISCONNECTED,    // Camera disconnected
    CAM_ERR_INVALID_ARG,     // NULL pointer or invalid argument
    CAM_ERR_BAD_DATA,        // Frame data validation failed
    CAM_ERR_NOT_SUPPORTED,   // Unknown VID/PID
    CAM_ERR_INIT_FAILED,     // Backend init failed
} camera_error_t;
```

#### camera_source_t

```c
typedef enum {
    CAM_SOURCE_UVC = 0,      // USB Video Class camera
    CAM_SOURCE_DVP = 1,      // Digital Video Port (OV7675)
    CAM_SOURCE_COUNT
} camera_source_t;
```

#### camera_pixel_format_t

```c
typedef enum {
    CAM_FORMAT_YUYV422,      // YUYV 4:2:2 (USB native)
    CAM_FORMAT_RGB565,       // RGB565 (DVP native)
    CAM_FORMAT_RGB888,       // RGB888 (after conversion)
} camera_pixel_format_t;
```

#### camera_frame_t

```c
typedef struct {
    void                 *data;          // Pointer to raw pixel data
    uint32_t              width;         // Frame width in pixels
    uint32_t              height;        // Frame height in pixels
    uint32_t              stride_bytes;  // Bytes per row (may include padding)
    camera_pixel_format_t format;        // Pixel format
    uint8_t               buffer_index;  // Double-buffer index (0 or 1)
    void                 *_backend_buf;  // Backend-specific (e.g., vg_lite_buffer_t*)
} camera_frame_t;
```

#### camera_status_t

```c
typedef struct {
    bool connected;    // Device physically connected
    bool streaming;    // Producing frames actively
} camera_status_t;
```

#### camera_device_t

```c
typedef struct {
    const camera_ops_t *ops;          // Active backend vtable
    camera_source_t     source;       // Current camera source
    bool                initialized;  // HAL initialized
} camera_device_t;
```

---

### 3.2 Public API Functions

#### camera_hal_init

```c
camera_error_t camera_hal_init(camera_device_t *dev, camera_source_t source);
```

Initialize the camera HAL with a specific source.

| Parameter | Type | Description |
|-----------|------|-------------|
| `dev` | `camera_device_t*` | Caller-allocated device instance |
| `source` | `camera_source_t` | `CAM_SOURCE_DVP` or `CAM_SOURCE_UVC` |

**Returns:** `CAM_OK` on success.

**Prerequisites:** VGLite buffers (`dvp_image_frames[]`, `usb_yuv_frames[]`) must be allocated before calling.

**Thread safety:** Must be called from the GFX task on CM55.

**Board support:** All boards with camera connector (AI Kit, Eva Kit).

---

#### camera_hal_get_frame

```c
camera_error_t camera_hal_get_frame(camera_device_t *dev, camera_frame_t *frame);
```

Try to acquire a frame. Non-blocking.

| Parameter | Type | Description |
|-----------|------|-------------|
| `dev` | `camera_device_t*` | Initialized device |
| `frame` | `camera_frame_t*` | Output: filled on success |

**Returns:** `CAM_OK` if frame available, `CAM_ERR_NO_FRAME` if not ready.

**Contract:** Caller MUST call `camera_hal_release_frame()` after processing.

**Backend behavior:**
- DVP: Clears `dvp_frame_ready` flag immediately (ISR writes to other buffer)
- UVC: Does NOT clear `BufReady` -- `release_frame()` does that

---

#### camera_hal_release_frame

```c
camera_error_t camera_hal_release_frame(camera_device_t *dev, camera_frame_t *frame);
```

Release a previously acquired frame.

| Parameter | Type | Description |
|-----------|------|-------------|
| `dev` | `camera_device_t*` | Initialized device |
| `frame` | `camera_frame_t*` | Frame from `get_frame()` |

**Returns:** `CAM_OK` on success.

**Backend behavior:**
- DVP: No-op (flag already cleared in `get_frame`)
- UVC: Clears `BufReady` so USB callback can reuse the buffer

---

#### camera_hal_get_status

```c
camera_error_t camera_hal_get_status(camera_device_t *dev, camera_status_t *status);
```

Get current camera status.

| Parameter | Type | Description |
|-----------|------|-------------|
| `dev` | `camera_device_t*` | Initialized device |
| `status` | `camera_status_t*` | Output |

**Returns:** `CAM_OK` on success.

---

#### camera_hal_switch

```c
camera_error_t camera_hal_switch(camera_device_t *dev, camera_source_t new_source);
```

Switch between camera sources. Both backends stay initialized; switching just changes which vtable is active. Instant, no re-initialization needed.

| Parameter | Type | Description |
|-----------|------|-------------|
| `dev` | `camera_device_t*` | Initialized device |
| `new_source` | `camera_source_t` | Source to switch to |

**Returns:** `CAM_OK` on success.

---

#### camera_hal_get_source

```c
camera_source_t camera_hal_get_source(const camera_device_t *dev);
```

Get the currently active camera source.

**Returns:** Current `camera_source_t` value.

---

#### camera_hal_needs_mirror

```c
bool camera_hal_needs_mirror(const camera_device_t *dev);
```

Check if the active camera requires software horizontal mirroring.

**Returns:**
- USB cameras: `true` (most need SW mirror, except HBVCAM 0.3MP)
- DVP (OV7675): `false` (mirrors via MVFP register)

---

### 3.3 Backend vtable

Each camera backend implements:

```c
typedef struct camera_ops {
    camera_error_t (*get_frame)(void *ctx, camera_frame_t *frame);
    camera_error_t (*release_frame)(void *ctx, camera_frame_t *frame);
    camera_error_t (*get_status)(void *ctx, camera_status_t *status);
} camera_ops_t;
```

Backend registration:
```c
const camera_ops_t *camera_backend_dvp_get_ops(void);
const camera_ops_t *camera_backend_uvc_get_ops(void);
```

### 3.4 C Usage Example

```c
#include "camera_hal.h"

static camera_device_t cam_dev;

void camera_task(void) {
    // Initialize with DVP camera
    camera_error_t err = camera_hal_init(&cam_dev, CAM_SOURCE_DVP);
    if (err != CAM_OK) return;

    camera_frame_t frame;
    while (1) {
        err = camera_hal_get_frame(&cam_dev, &frame);
        if (err == CAM_OK) {
            // Process frame.data (RGB565, frame.width x frame.height)
            process_frame(frame.data, frame.width, frame.height,
                          frame.stride_bytes, frame.format);
            camera_hal_release_frame(&cam_dev, &frame);
        }
        vTaskDelay(pdMS_TO_TICKS(33));  // ~30 FPS
    }
}
```

---

## 4. USB HID Joystick

Module: `joystick` (CM33_NS MicroPython module)
Source: `common/mpy/modjoystick.c`, `common/modules/usb_hid_joystick/usb_hid_joystick.h`

Reads Logitech F310 DirectInput HID reports via IPC bridge from CM55. The USB Host stack (SEGGER emUSB-Host) runs on CM55, and MicroPython on CM33_NS queries state via bidirectional IPC.

### 4.1 Architecture

```
  CM33_NS (MicroPython)          CM55 (USB Host / FreeRTOS)
  ========================      ============================
  joystick.read()               USBH_HID_Task
    |                              |
    +-- IPC_CMD_JOYSTICK_STATE --> +-- Read joystick_state_t
    <-- [state data] ------------  |   (volatile, updated by
                                   |    HID report callback)
```

### 4.2 F310 DirectInput HID Report Format

8-byte report structure:

```c
typedef struct __attribute__((packed)) {
    uint8_t left_x;     // Byte 0: Left Stick X  (0x00-0xFF, center=0x80)
    uint8_t left_y;     // Byte 1: Left Stick Y  (0x00-0xFF, center=0x7F)
    uint8_t right_x;    // Byte 2: Right Stick X (0x00-0xFF, center=0x80)
    uint8_t right_y;    // Byte 3: Right Stick Y (0x00-0xFF, center=0x7F)
    uint8_t buttons1;   // Byte 4: hat[0:3] + X[4] A[5] B[6] Y[7]
    uint8_t buttons2;   // Byte 5: LB[0] RB[1] LT[2] RT[3] Back[4] Start[5] L3[6] R3[7]
    uint8_t mode;       // Byte 6: Mode switch
    uint8_t status;     // Byte 7: Status
} f310_report_t;
```

**USB identification:**
- VID: `0x046D` (Logitech)
- PID: `0xC216` (F310 DirectInput)

### 4.3 Button Masks

**Byte 4 (face buttons + hat):**

| Mask | Button |
|------|--------|
| `0x0F` | Hat/D-pad direction (0-8) |
| `0x10` | X (blue) |
| `0x20` | A (green) |
| `0x40` | B (red) |
| `0x80` | Y (yellow) |

**Byte 5 (shoulders + system):**

| Mask | Button |
|------|--------|
| `0x01` | LB (left bumper) |
| `0x02` | RB (right bumper) |
| `0x04` | LT (left trigger) |
| `0x08` | RT (right trigger) |
| `0x10` | Back |
| `0x20` | Start |
| `0x40` | L3 (left stick click) |
| `0x80` | R3 (right stick click) |

**Hat switch values:**

| Value | Direction |
|-------|-----------|
| 0 | Up |
| 1 | Up-Right |
| 2 | Right |
| 3 | Down-Right |
| 4 | Down |
| 5 | Down-Left |
| 6 | Left |
| 7 | Up-Left |
| 8 | Neutral (centered) |

---

### 4.4 Module Functions

#### joystick.init()

Initialize USB Host HID on CM55 via IPC.

```python
import joystick
joystick.init()
```

**C prototype:**
```c
static mp_obj_t joystick_init(void);
```

**IPC command:** `IPC_CMD_JOYSTICK_INIT`

---

#### joystick.connected() -> bool

Check if a gamepad is currently connected.

```python
if joystick.connected():
    print("Gamepad ready!")
```

---

#### joystick.read() -> dict

Read raw button and axis state. Scenario 1: Raw Reading.

```python
state = joystick.read()
# Returns dict with keys:
#   left_x, left_y, right_x, right_y (int 0-255)
#   a, b, x, y (bool) - face buttons
#   lb, rb, lt, rt (bool) - shoulders/triggers
#   back, start, l3, r3 (bool) - system buttons
#   dpad (int 0-8) - hat switch raw value
#   dpad_up, dpad_down, dpad_left, dpad_right (bool)
```

---

#### joystick.on_foot() -> dict

Scenario 2: On-Foot Character Control. Axes are normalized to float -1.0 to +1.0 with deadzone applied.

```python
state = joystick.on_foot()
# Keys: walk_x, walk_y, camera_x, camera_y (float -1.0..1.0)
#        jump (B), run (A), enter_exit (Y),
#        change_camera (Back), pause (Start)
```

---

#### joystick.vehicle() -> dict

Scenario 3: Land Vehicle Control. Left stick X = steering, triggers = accel/brake.

```python
state = joystick.vehicle()
# Keys: steering (float), accelerate (RT), brake (LT),
#        clutch (LB), parking_brake (RB),
#        shift_up (X), shift_down (B),
#        auto_shift_up (D-up), auto_shift_down (D-down),
#        toggle_lights (D-left), horn (L3),
#        starter (A), ignition (R3),
#        camera_x, camera_y (float),
#        enter_exit (Y), change_camera (Back), pause (Start)
```

---

#### joystick.plane() -> dict

Scenario 4: Aircraft Control. Left stick = aileron/elevator.

```python
state = joystick.plane()
# Keys: aileron (float), elevator (float),
#        rudder_left (LT), rudder_right (RT),
#        reverse (LB), parking_brake (RB),
#        throttle_inc (D-up), throttle_dec (D-down),
#        full_throttle (B), zero_throttle (X),
#        start_engines (A),
#        camera_x, camera_y (float),
#        enter_exit (Y), change_camera (Back), pause (Start)
```

---

#### joystick.boat() -> dict

Scenario 5: Watercraft Control. Left stick X = steering.

```python
state = joystick.boat()
# Keys: steering (float),
#        throttle_up (RT), throttle_down (LT),
#        center_rudder (X), reverse (B),
#        camera_x, camera_y (float),
#        enter_exit (Y), change_camera (Back), pause (Start)
```

---

#### joystick.info() -> dict

Device information and debug diagnostics.

```python
info = joystick.info()
# Keys: connected (bool), vid (int), pid (int), sequence (int)
#        init_stage (int), usb_init (bool), add_events (int),
#        rm_events (int), reports (int), isr_count (int),
#        port_power (int), running (bool), num_dev (int),
#        root_conns (int), usb_class (int), usb_vid (int), usb_pid (int)
```

---

#### joystick.deadzone(value) -> int

Set stick axis deadzone (percentage, 0-50).

```python
joystick.deadzone(15)  # 15% deadzone
```

Default: 10. Clamped to range 0-50.

---

#### joystick.wait_connect(timeout_ms=5000) -> bool

Block until a gamepad is plugged in or timeout expires.

```python
if joystick.wait_connect(10000):  # Wait up to 10 seconds
    print("Gamepad connected!")
else:
    print("No gamepad detected")
```

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `timeout_ms` | int | 5000 | Wait timeout (100-30000 ms) |

---

### 4.5 Axis Normalization

Stick axes are normalized from raw 0-255 to float -1.0 to +1.0:

```
normalized = (raw - center) / range
```

Where center is 0x80 (128) for X-axes and 0x7F (127) for Y-axes. Deadzone is applied symmetrically around zero.

### 4.6 C API

```c
// Initialize USB Host HID (creates USBH tasks on CM55)
void usb_hid_joystick_init(void);

// Request non-blocking init from worker task
bool usb_hid_joystick_request_init(void);

// Get pointer to current state (volatile reads, thread-safe)
const joystick_state_t* usb_hid_joystick_get_state(void);

// Check connection status
bool usb_hid_joystick_is_connected(void);
```

**Board support:** All boards with USB Host connector. Tested with Logitech F310 DirectInput (VID:046D PID:C216).

---

## 5. PDM Microphone

Class: `machine.PDM_PCM`
Source: `micropython-psoc-edge-psoc-edge-main/ports/psoc-edge/machine_pdm_pcm.h`

PDM (Pulse Density Modulation) to PCM digital microphone interface using the PSoC Edge PDM/PCM v2 hardware block with DMA-based double-buffered audio capture.

### 5.1 Constructor

```python
from machine import PDM_PCM

mic = PDM_PCM(
    sck=pin_sck,          # PDM clock pin
    data=pin_data,        # PDM data pin
    sample_rate=16000,    # Sample rate in Hz
    decimation_rate=64,   # PDM oversampling / decimation
    bits=16,              # Bit depth per sample
    format=PDM_PCM.MONO_LEFT,  # Channel format
    left_gain=0,          # Left channel gain (dB)
    right_gain=0          # Right channel gain (dB)
)
```

### 5.2 Constructor Parameters

| Parameter | Type | Default | Values | Description |
|-----------|------|---------|--------|-------------|
| `sck` | Pin | required | -- | PDM clock output pin |
| `data` | Pin | required | -- | PDM data input pin |
| `sample_rate` | int | -- | 8000, 16000, 22050, 44100, 48000 | Audio sample rate |
| `decimation_rate` | int | -- | 8-255 | PDM oversampling factor |
| `bits` | int | -- | 16, 18, 20, 24 | Bits per PCM sample |
| `format` | int | -- | `MONO_LEFT`, `MONO_RIGHT`, `STEREO` | Channel format |
| `left_gain` | int | 0 | dB | Left channel gain |
| `right_gain` | int | 0 | dB | Right channel gain |

### 5.3 Supported Sample Rates

| Rate (Hz) | DPLL Output Clock | Notes |
|-----------|-------------------|-------|
| 8000 | 73.728 MHz | Narrowband speech |
| 16000 | 73.728 MHz | Wideband speech |
| 22050 | 169.344 MHz | CD quality (half) |
| 44100 | 169.344 MHz | CD quality |
| 48000 | 73.728 MHz | Professional audio |

### 5.4 Audio Format Enum

```c
typedef enum {
    MONO_LEFT,    // Left channel only
    MONO_RIGHT,   // Right channel only
    STEREO        // Both channels interleaved
} format_t;
```

### 5.5 DMA Configuration

The PDM_PCM driver uses interrupt-driven ping-pong buffering:

| Parameter | Value | Description |
|-----------|-------|-------------|
| Frame size | 1024 samples | `FRAME_SIZE` |
| DMA buffer | 128 x 32-bit words | `SIZEOF_DMA_BUFFER` |
| Half-DMA buffer | 64 x 32-bit words | Double-buffered |
| RX FIFO trigger | 31 entries | `RX_FIFO_TRIG_LEVEL` |
| ISR priority | 2 | `PDM_PCM_ISR_PRIORITY` |
| Non-blocking copy | 256 samples | `SIZEOF_NON_BLOCKING_COPY_IN_BYTES` |

### 5.6 C Internal API

```c
// Initialize PDM_PCM hardware and DMA
static void mp_machine_pdm_pcm_init(machine_pdm_pcm_obj_t *self);

// Deinitialize and release resources
static void mp_machine_pdm_pcm_deinit(machine_pdm_pcm_obj_t *self);

// Set channel gains
static void mp_machine_pdm_pcm_set_gain(machine_pdm_pcm_obj_t *self,
                                         int16_t left_gain,
                                         int16_t right_gain);

// Process IRQ (swap ping-pong buffers)
static void mp_machine_pdm_pcm_irq_update(machine_pdm_pcm_obj_t *self);
```

### 5.7 Usage Example

```python
from machine import PDM_PCM, Pin

mic = PDM_PCM(
    sck=Pin('P21_2'),
    data=Pin('P21_3'),
    sample_rate=16000,
    decimation_rate=64,
    bits=16,
    format=PDM_PCM.MONO_LEFT
)

# Read audio samples into buffer
buf = bytearray(1024)
num_read = mic.readinto(buf)
```

**Board support:**

| Board | PDM Support | Notes |
|-------|-------------|-------|
| AI Kit | Yes | On-board MEMS microphone |
| Eva Kit | Yes | On-board MEMS microphone |
| Game Console | Yes | On-board MEMS microphone |

---

## 6. Machine Pin

Class: `machine.Pin`
Source: `micropython-psoc-edge-psoc-edge-main/ports/psoc-edge/machine_pin.c`

Standard MicroPython `machine.Pin` implementation for PSoC Edge GPIO. Supports board-named pins, CPU-named pins, and all standard digital I/O modes.

### 6.1 Constructor

```python
from machine import Pin

# By board name
led = Pin('USER_LED1', Pin.OUT)

# By CPU name
p = Pin(Pin.cpu.P0_0, Pin.IN, Pin.PULL_UP)

# Full constructor
pin = Pin(id, mode=None, pull=None, drive=None, value=None)
```

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `id` | str/Pin | required | Pin name (board or CPU) or Pin object |
| `mode` | int | None | `Pin.IN`, `Pin.OUT`, `Pin.OPEN_DRAIN` |
| `pull` | int | None | `Pin.PULL_UP`, `Pin.PULL_DOWN`, `Pin.PULL_UP_DOWN` |
| `drive` | int | None | Drive strength (0-3, maps to `CY_GPIO_DRIVE_SEL_*`) |
| `value` | int | None | Initial value (0 or 1) |

### 6.2 Pin Modes

```c
enum { GPIO_MODE_NONE = 0, GPIO_MODE_IN, GPIO_MODE_OUT, GPIO_MODE_OPEN_DRAIN };
```

| Mode | Constant | PDL Drive Mode |
|------|----------|----------------|
| Input | `Pin.IN` | `CY_GPIO_DM_HIGHZ` (default) |
| Input + Pull-up | `Pin.IN` + `Pin.PULL_UP` | `CY_GPIO_DM_PULLUP` |
| Input + Pull-down | `Pin.IN` + `Pin.PULL_DOWN` | `CY_GPIO_DM_PULLDOWN` |
| Output | `Pin.OUT` | `CY_GPIO_DM_STRONG_IN_OFF` |
| Open Drain | `Pin.OPEN_DRAIN` | `CY_GPIO_DM_OD_DRIVESLOW_IN_OFF` |

### 6.3 Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `init(mode, pull, drive, value)` | -- | Reconfigure pin |
| `value([v])` | `pin.value()` / `pin.value(1)` | Read or write pin level |
| `on()` | `pin.on()` | Set pin high |
| `off()` | `pin.off()` | Set pin low |
| `toggle()` | `pin.toggle()` | Invert pin level |
| `mode([m])` | `pin.mode()` / `pin.mode(Pin.OUT)` | Get or set mode |

**C prototypes:**
```c
mp_obj_t mp_pin_make_new(const mp_obj_type_t *type, size_t n_args,
                          size_t n_kw, const mp_obj_t *args);

static mp_obj_t machine_pin_value(size_t n_args, const mp_obj_t *args);
static mp_obj_t machine_pin_on(mp_obj_t self_in);
static mp_obj_t machine_pin_off(mp_obj_t self_in);
static mp_obj_t machine_pin_toggle(mp_obj_t self_in);
```

**Callable:** Pin objects are callable. `pin()` reads the value, `pin(1)` sets it.

```python
p = Pin('P0_0', Pin.OUT)
p(1)        # Set high
val = p()   # Read level
```

### 6.4 Pin Object Structure

```c
typedef struct _machine_pin_obj_t {
    mp_obj_base_t base;
    qstr name;       // Pin name (e.g., MP_QSTR_P0_0)
    uint8_t port;    // GPIO port number
    uint8_t pin;     // Pin number within port
    uint8_t af_num;  // Alternate function count
    const machine_pin_af_obj_t *af;  // Alternate function table
} machine_pin_obj_t;
```

### 6.5 Print Representation

```python
>>> Pin('P0_0', Pin.OUT)
Pin(Pin.cpu.P0_0, mode=Pin.OUT)
```

### 6.6 C Example

```c
#include "cy_gpio.h"

// Configure P0_0 as output, drive high
Cy_GPIO_Pin_FastInit(GPIO_PRT0, 0, CY_GPIO_DM_STRONG_IN_OFF, 1, HSIOM_SEL_GPIO);

// Read pin
uint32_t val = Cy_GPIO_Read(GPIO_PRT0, 0);

// Toggle
if (Cy_GPIO_Read(GPIO_PRT0, 0)) {
    Cy_GPIO_Clr(GPIO_PRT0, 0);
} else {
    Cy_GPIO_Set(GPIO_PRT0, 0);
}
```

---

## 7. I2C Target (Slave Mode)

Class: `machine.I2CTarget`
Source: `micropython-psoc-edge-psoc-edge-main/ports/psoc-edge/machine_i2c_target.c`

I2C slave/target mode using the PSoC Edge SCB (Serial Communication Block) I2C hardware with PDL event-driven callbacks.

### 7.1 Object Structure

```c
typedef struct _machine_i2c_target_obj_t {
    mp_obj_base_t base;
    uint8_t id;              // SCB instance ID
    uint32_t scl_pin;        // SCL pin
    uint32_t sda_pin;        // SDA pin
    uint32_t slave_addr;     // 7-bit slave address
    uint8_t addrsize;        // Address size
    cy_stc_scb_i2c_config_t cfg;    // PDL config
    cy_stc_scb_i2c_context_t ctx;   // PDL context
    size_t tx_index;         // TX buffer index
    size_t rx_index;         // RX buffer index
} machine_i2c_target_obj_t;
```

### 7.2 PDL Event Callback

The driver implements the PDL Slave Operation event pattern:

| Event | PDL Constant | Action |
|-------|-------------|--------|
| Read request | `CY_SCB_I2C_SLAVE_READ_EVENT` | Address matched for read |
| Write request | `CY_SCB_I2C_SLAVE_WRITE_EVENT` | Address matched for write |
| Read buffer empty | `CY_SCB_I2C_SLAVE_RD_BUF_EMPTY_EVENT` | Refill TX buffer |
| Read complete | `CY_SCB_I2C_SLAVE_RD_CMPLT_EVENT` | Transaction finished |
| Write complete | `CY_SCB_I2C_SLAVE_WR_CMPLT_EVENT` | Transaction finished |

**Critical PDL requirement:** Buffers must be reconfigured after each transaction. Without reconfiguration, the next transaction continues from where the previous one stopped.

### 7.3 C ISR Handler

```c
// NVIC interrupt handler -- calls PDL dispatcher
static void machine_i2c_target_isr(void) {
    Cy_SCB_I2C_SlaveInterrupt(MICROPY_HW_I2C0_SCB, &self->ctx);
}
```

### 7.4 Usage

```python
from machine import I2CTarget

# Create I2C target at address 0x42
target = I2CTarget(0, addr=0x42, sda='P6_1', scl='P6_0')
```

**Board support:** Requires available SCB instance. I2C SCB0 is shared with CM55 touch on Eva Kit (requires IPC touch pause/resume mechanism).

---

## 8. Board Support Matrix

### 8.1 Hardware Feature Matrix

| Feature | AI Kit | Eva Kit | Game Console |
|---------|--------|---------|--------------|
| **BSP define** | `APP_KIT_PSE84_AI` | `APP_KIT_PSE84_EVAL_EPC2` | `APP_KIT_PSE84_AI` (game) |
| **LEDs** | 5 (2 user + RGB) | 2 (user) | 2 (user) |
| **Buttons** | 1 (SW1) | 1 (SW1) | 1 (SW1) |
| **BMI270 IMU** | Yes | Yes | Yes |
| **DPS368 Barometer** | Yes | No | No |
| **SHT40 Temp/Humidity** | Yes | No | No |
| **BMM350 Magnetometer** | Yes | Yes | No |
| **CapSense** | No | Yes | No |
| **Potentiometer** | No | Yes | No |
| **Radar (BGT60LTR11)** | Yes | Yes | No |
| **Joystick (USB Host)** | Yes | Yes | Yes |
| **Camera (DVP OV7675)** | Yes | Yes | No |
| **Camera (USB UVC)** | Yes | Yes | No |
| **PDM Microphone** | Yes | Yes | Yes |
| **Display (800x480)** | Yes | Yes | Yes |
| **WiFi (CYW55513)** | Yes | Yes | Yes |
| **QSPI Flash (64MB)** | Yes | Yes | Yes |
| **OPTIGA Trust M** | No | Yes | No |

### 8.2 BSP Feature Flags

```c
// Set in each project's Makefile as DEFINES+=
#define BSP_HAS_DPS368        1  // AI Kit only
#define BSP_HAS_SHT40         1  // AI Kit only
#define BSP_HAS_CAPSENSE      1  // Eva Kit only
#define BSP_HAS_POTENTIOMETER 1  // Eva Kit only
#define BSP_HAS_BMI270        1  // All boards
#define BSP_HAS_BMM350        1  // AI Kit + Eva Kit
```

### 8.3 Processor Core Assignment

| Core | RTOS | Responsibility |
|------|------|----------------|
| CM33_S | Bare-metal | Secure boot, TFM, PPC/SAU config |
| CM33_NS | Bare-metal | MicroPython, WiFi (WCM/WHD), RTSP, sensors |
| CM55 | FreeRTOS | LVGL display, USB Host, camera, AI inference |

**Critical constraint:** SDHC0 (WiFi chip interface) is PPC-protected for CM33_NS only. CM55 cannot access it -- BusFault kills the calling task.

### 8.4 Memory Layout (CM33_NS)

| Region | Size | Usage |
|--------|------|-------|
| Total RAM | 256 KB | CM33_NS allocation |
| .bss | 186 KB | Static data (includes 128 KB MicroPython GC heap) |
| .heap | 63 KB | C heap (malloc) |
| .data | 2 KB | Initialized data |
| Free | ~4 KB | Remaining |

### 8.5 LVGL Font Availability

| Font Size | AI Kit | Eva Kit |
|-----------|--------|---------|
| 12 | No | Yes |
| 14 | Yes | Yes |
| 16 | Yes | Yes |
| 18 | No | Yes |
| 20 | Yes | Yes |
| 22 | No | Yes |
| 24 | Yes | Yes |
| 28 | Yes | Yes |
| 36 | Yes | No |
| 40 | Yes | No |

Common sizes (safe for both boards): **14, 16, 20, 24, 28**.

---

## Source Files

| File | Location | Role |
|------|----------|------|
| `modgpio.c` | `common/mpy/` | MicroPython `gpio` module (LED/Button) |
| `machine_rtc.c` | `micropython/.../ports/psoc-edge/` | MicroPython `machine.RTC` class |
| `camera_hal.h` | `kit-pse84-ai/libraries/tesaiot-camera-hal/` | Camera HAL header (vtable API) |
| `usb_hid_joystick.h` | `common/modules/usb_hid_joystick/` | USB HID joystick driver header |
| `modjoystick.c` | `common/mpy/` | MicroPython `joystick` module |
| `machine_pdm_pcm.h` | `micropython/.../ports/psoc-edge/` | PDM/PCM microphone driver header |
| `machine_pin.c` | `micropython/.../ports/psoc-edge/` | MicroPython `machine.Pin` class |
| `machine_pin.h` | `micropython/.../ports/psoc-edge/` | Pin object structure definitions |
| `machine_i2c_target.c` | `micropython/.../ports/psoc-edge/` | I2C target/slave mode driver |
