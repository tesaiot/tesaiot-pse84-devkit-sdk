# BENTO PSoC Edge E84 — UI & Display API Reference

Version: 2026.03 | Platform: PSoC Edge E84 (PSE84-xxx) | LVGL 9.x | MicroPython 1.24

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [Widget Type Enum](#2-widget-type-enum)
3. [IPC Command Protocol](#3-ipc-command-protocol)
4. [Event System](#4-event-system)
5. [Screen Management](#5-screen-management)
6. [Widget Reference](#6-widget-reference)
7. [Widget Common Methods](#7-widget-common-methods)
8. [LCD Terminal API](#8-lcd-terminal-api)
9. [Markup Support](#9-markup-support)
10. [Built-in Icons](#10-built-in-icons)
11. [Limits and Constraints](#11-limits-and-constraints)
12. [MicroPython API Reference](#12-micropython-api-reference)
13. [C API Reference](#13-c-api-reference)

---

## 1. Architecture Overview

The BENTO UI system is an IPC-based widget framework that spans two processor cores:

```
 CM33_NS (MicroPython)              CM55 (LVGL / Display)
 ========================          ========================
 modui.c                           ipc_ui.c / ui_widget_mgr.c
   |                                  |
   |-- ui.Button("Click")            |-- Creates lv_btn + lv_label
   |      |                           |
   |      +-- IPC_CMD_UI_CREATE -->   +-- pm_register(), lv_obj_create()
   |                                  |
   |-- ui.poll()                      |
   |      |                           |
   |      +-- IPC_CMD_UI_POLL ----->  +-- Drain event ring buffer
   |      <-- [events] -----------    |
```

**Key design points:**

- **CM33_NS** (bare-metal, no FreeRTOS) sends IPC Pipe commands to CM55
- **CM55** (FreeRTOS + LVGL) creates and manages LVGL widget objects
- Communication uses `Cy_IPC_Pipe_SendMessage()` with shared-memory payloads
- Bidirectional commands (CREATE, POLL_EVENTS, GET_VALUE, LIST) use a response buffer
- Fire-and-forget commands (SET_TEXT, SET_VALUE, DELETE, etc.) return immediately
- Maximum 32 concurrent widgets, 16 event ring buffer, 8 events per poll
- All widget operations are lazy-initialized on first use

### Shared Memory Layout

```c
CY_SECTION_SHAREDMEM static ipc_msg_t      ui_ipc_msg;   // CM33 -> CM55
CY_SECTION_SHAREDMEM static ipc_response_t ui_ipc_resp;  // CM55 -> CM33
```

---

## 2. Widget Type Enum

Defined in `ipc_ui_protocol.h`:

```c
typedef enum {
    UI_WIDGET_BUTTON    = 1,   // Push button with text label
    UI_WIDGET_LABEL     = 2,   // Static or dynamic text label
    UI_WIDGET_SLIDER    = 3,   // Horizontal slider with range
    UI_WIDGET_SWITCH    = 4,   // Toggle on/off switch
    UI_WIDGET_CHECKBOX  = 5,   // Check/uncheck box with label
    UI_WIDGET_ARC       = 6,   // Circular arc indicator
    UI_WIDGET_BAR       = 7,   // Linear progress bar
    UI_WIDGET_SPINNER   = 8,   // Rotating spinner (busy indicator)
    UI_WIDGET_DROPDOWN  = 9,   // Dropdown list selector
    UI_WIDGET_TEXTAREA  = 10,  // Multi-line text input area
    UI_WIDGET_SEG7      = 11,  // 7-segment display (styled label)
    UI_WIDGET_DOTMATRIX = 12,  // Dot matrix (lv_canvas based)
    UI_WIDGET_CHART     = 13,  // Line/area chart with series
    UI_WIDGET_IMAGE     = 14,  // Image (built-in icon or dynamic RGB565)
    UI_WIDGET_PANEL     = 15,  // Dark rect container (dashboard card)
    UI_WIDGET_COMPASS   = 16,  // Heading compass (circle + needle + NESW)
} ui_widget_type_t;
```

---

## 3. IPC Command Protocol

### Command Codes (0x50-0x62)

| Code   | Name                      | Direction     | Description |
|--------|---------------------------|---------------|-------------|
| `0x50` | `IPC_CMD_UI_CREATE`       | Bidirectional | Create widget, returns handle |
| `0x51` | `IPC_CMD_UI_DELETE`       | Fire-forget   | Delete widget by handle |
| `0x52` | `IPC_CMD_UI_SET_TEXT`     | Fire-forget   | Set text content |
| `0x53` | `IPC_CMD_UI_SET_VALUE`    | Fire-forget   | Set numeric value |
| `0x54` | `IPC_CMD_UI_SET_POSITION` | Fire-forget   | Set x,y position |
| `0x55` | `IPC_CMD_UI_SET_SIZE`     | Fire-forget   | Set width, height |
| `0x56` | `IPC_CMD_UI_SET_COLOR`    | Fire-forget   | Set primary color (0xRRGGBB) |
| `0x57` | `IPC_CMD_UI_SET_VISIBLE`  | Fire-forget   | Show (1) or hide (0) widget |
| `0x58` | `IPC_CMD_UI_POLL_EVENTS`  | Bidirectional | Poll pending events |
| `0x59` | `IPC_CMD_UI_CLEAR_ALL`    | Fire-forget   | Delete all widgets |
| `0x5A` | `IPC_CMD_UI_SET_DOTMATRIX`| Fire-forget   | Set dot matrix bitmap |
| `0x5B` | `IPC_CMD_UI_GET_VALUE`    | Bidirectional | Read current widget value |
| `0x5C` | `IPC_CMD_UI_LIST`         | Bidirectional | List active widgets |
| `0x5D` | `IPC_CMD_UI_SET_IMAGE`    | Fire-forget   | Set image pixel data (chunked) |
| `0x5E` | `IPC_CMD_UI_SET_SCREEN`   | Fire-forget   | Set screen dimensions |
| `0x5F` | `IPC_CMD_UI_IDE_STATUS`   | Fire-forget   | Show/hide IDE connection icon |
| `0x60` | `IPC_CMD_UI_CHART_ADD_SERIES` | Bidirectional | Add series to chart |
| `0x61` | `IPC_CMD_UI_CHART_SET_NEXT`   | Fire-forget   | Append value to chart series |
| `0x62` | `IPC_CMD_UI_DEPLOY_SCREEN`    | Fire-forget   | Show native deploy overlay |

### CREATE Payload Structure

```c
typedef struct __attribute__((packed)) {
    uint8_t  widget_type;       // ui_widget_type_t (1-16)
    int16_t  x;                 // X position (-1 = auto-layout)
    int16_t  y;                 // Y position (-1 = auto-layout)
    int16_t  w;                 // Width (-1 = auto-size)
    int16_t  h;                 // Height (-1 = auto-size)
    uint32_t color;             // 0xRRGGBB (0 = theme default)
    int32_t  min_val;           // Range minimum / cols (DotMatrix)
    int32_t  max_val;           // Range maximum / rows (DotMatrix)
    int32_t  init_val;          // Initial value
    char     text[96];          // Label text (null-terminated)
} ipc_ui_create_t;              // Total: 121 bytes
```

### Response Status Codes

| Code | Name                    | Description |
|------|-------------------------|-------------|
| `0`  | `UI_STATUS_OK`          | Success |
| `1`  | `UI_STATUS_TABLE_FULL`  | Max 32 widgets reached |
| `2`  | `UI_STATUS_INVALID_TYPE`| Unknown widget type |
| `3`  | `UI_STATUS_INVALID_HANDLE` | Handle not found |
| `4`  | `UI_STATUS_ERROR`       | Generic error |
| `5`  | `UI_STATUS_ALLOC_FAILED`| LVGL memory allocation failed |

---

## 4. Event System

### Event Types

```c
#define UI_EVENT_CLICKED        (1)   // Button pressed, checkbox toggled
#define UI_EVENT_VALUE_CHANGED  (2)   // Slider/arc/dropdown value changed
#define UI_EVENT_TOGGLED        (3)   // Switch toggled on/off
```

### Event Structure

```c
typedef struct __attribute__((packed)) {
    uint8_t  handle_id;    // Widget that generated the event
    uint8_t  event_type;   // UI_EVENT_*
    int32_t  value;        // Current value (slider position, switch state, etc.)
    uint16_t reserved;
} ipc_ui_event_t;          // 8 bytes per event
```

### Polling Model

Events are stored in a ring buffer on CM55 (capacity: 16 events). The CM33 side polls events using `ui.poll()`, which returns up to 8 events per call as a list of dictionaries.

**MicroPython usage:**

```python
import ui, time

btn = ui.Button("Click Me")
while True:
    for ev in ui.poll():
        if ev['handle'] == btn.id() and ev['type'] == 'clicked':
            print("Button clicked!")
    time.sleep_ms(50)
```

### Event Matching

Each widget has a unique handle (0-31). Use `widget.id()` to get the handle and match it against `ev['handle']` in poll results. The `ev.get('handle')` pattern is the canonical way to identify which widget generated an event.

---

## 5. Screen Management

### ui.screen(width, height)

Configures the Playground canvas dimensions and resets the auto-layout engine.

```python
ui.screen(792, 398)  # Default: full Playground tab area
ui.screen(400, 300)  # Custom smaller canvas
ui.screen()          # Default dimensions (792x398)
```

**Behavior:**
- Automatically calls `ui.clear()` before setting dimensions (prevents GPU overload)
- Pauses background sensor auto-push to free IPC bandwidth
- The `height` parameter is currently ignored by firmware; only `width` matters
- Default screen area: 792x398 pixels

**C prototype:**
```c
// Fire-and-forget: sends IPC_CMD_UI_SET_SCREEN with [w:int16, h:int16]
static mp_obj_t ui_screen(size_t n_args, const mp_obj_t *args);
```

### ui.clear()

Deletes all widgets from the Playground canvas.

```python
ui.clear()  # Remove all widgets
```

**C prototype:**
```c
// Fire-and-forget: sends IPC_CMD_UI_CLEAR_ALL
static mp_obj_t ui_clear(void);
```

### ui.program(code)

Persist MicroPython code across board resets by writing to `/main.py` on the QSPI filesystem.

```python
ui.program("import ui\nui.Label('Hello')")  # Write /main.py
code = ui.program()                          # Read /main.py (returns str or None)
ui.program("")                               # Delete /main.py
```

---

## 6. Widget Reference

All widgets share a common constructor pattern:

```python
widget = ui.WidgetType(text, x=, y=, w=, h=, color=, min=, max=, value=)
```

**Common keyword arguments:**

| Arg     | Type  | Default | Description |
|---------|-------|---------|-------------|
| `text`  | str   | `""`    | Label text (first positional arg) |
| `x`     | int   | -1      | X position (-1 = auto-layout) |
| `y`     | int   | -1      | Y position (-1 = auto-layout) |
| `w`     | int   | -1      | Width (-1 = auto-size) |
| `h`     | int   | -1      | Height (-1 = auto-size) |
| `color` | int   | 0       | Color as 0xRRGGBB (0 = theme default) |
| `min`   | int   | 0       | Range minimum |
| `max`   | int   | 0       | Range maximum |
| `value` | int   | 0       | Initial value / font size for Button/Label |

---

### 6.1 Button

Interactive push button with text label.

```python
btn = ui.Button("Click Me")
btn = ui.Button("OK", x=100, y=50, w=120, h=40, color=0x4CAF50)
btn = ui.Button("X", value=28)  # value= sets font size (14,16,20,24,28)
```

**Events:** `clicked` (value = 0)

**Font sizes:** Use `value=` to set font size. Available sizes: 14, 16, 20, 24, 28. Default button text shows empty string if no text provided.

---

### 6.2 Label

Static or dynamic text display.

```python
lbl = ui.Label("Hello World")
lbl = ui.Label("Temperature", color=0xFF9800)
lbl = ui.Label("Title", value=24)  # value= sets font size
lbl.text("Updated text")
```

**Events:** None (labels are non-interactive)

**Font sizes:** Use `value=` to set font size. Available sizes: 14, 16, 20, 24, 28.

---

### 6.3 Slider

Horizontal slider with configurable range.

```python
slider = ui.Slider(min=0, max=100, value=50)
slider = ui.Slider("Volume", min=0, max=100, value=75, w=200)
current = slider.value()     # Read current value
slider.value(80)             # Set value programmatically
```

**Events:** `value_changed` (value = current slider position)

---

### 6.4 Switch

Toggle switch (on/off).

```python
sw = ui.Switch("LED Control")
sw = ui.Switch("Motor", value=1)  # Initially ON
state = sw.value()  # 0 or 1
```

**Events:** `toggled` (value = 0 or 1)

---

### 6.5 Checkbox

Check/uncheck box with text label.

```python
cb = ui.Checkbox("Enable WiFi")
cb = ui.Checkbox("Auto-connect", value=1)  # Initially checked
```

**Events:** `clicked` (value = checked state 0/1)

---

### 6.6 Arc

Circular arc indicator with range.

```python
arc = ui.Arc(min=0, max=360, value=90)
arc = ui.Arc("RPM", min=0, max=8000, value=3500, w=150, h=150, color=0xE040FB)
arc.value(180)
```

**Events:** `value_changed`

---

### 6.7 Bar (Progress Bar)

Linear progress bar.

```python
bar = ui.Bar(min=0, max=100, value=0)
bar = ui.Bar("Battery", min=0, max=100, value=85, w=200, color=0x4CAF50)
bar.value(50)
```

**Events:** None (non-interactive)

---

### 6.8 Spinner

Rotating busy indicator (no user value).

```python
spinner = ui.Spinner()
spinner = ui.Spinner(w=60, h=60, color=0x2196F3)
spinner.hide()   # Hide when operation complete
spinner.delete() # Or remove entirely
```

**Events:** None

---

### 6.9 Dropdown

Dropdown list with selectable options. Options are newline-separated in the text parameter.

```python
dd = ui.Dropdown("Option A\nOption B\nOption C")
dd = ui.Dropdown("Red\nGreen\nBlue", value=1)  # Pre-select index 1
selected = dd.value()  # Returns selected index (0-based)
```

**Events:** `value_changed` (value = selected index)

---

### 6.10 Textarea

Multi-line text input area.

```python
ta = ui.Textarea("Initial text", w=300, h=100)
ta.text("New content")
content = ta.text()  # Note: text getter not yet implemented
```

**Events:** `value_changed`

---

### 6.11 Seg7 (7-Segment Display)

Styled label that looks like a 7-segment LED display.

```python
seg = ui.Seg7("1234")
seg = ui.Seg7("00:00", color=0xFF0000)
seg.text("5678")
```

**Events:** None

---

### 6.12 DotMatrix

Pixel-addressable dot matrix display using `lv_canvas`. Dimensions set via `cols`/`rows`.

```python
dm = ui.DotMatrix(cols=8, rows=8)
# Set pixels with bytearray (1 byte per row, MSB = leftmost)
pattern = bytearray([0xFF, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0xFF])
dm.set_pixels(pattern)
```

**Constructor-specific args:**

| Arg    | Type | Description |
|--------|------|-------------|
| `cols` | int  | Number of columns |
| `rows` | int  | Number of rows |

**Methods:**
- `set_pixels(bytearray)` -- Send bitmap data. **Must be `bytearray`**, not a list.

**Events:** None

---

### 6.13 Chart

Line/area chart with up to 4 data series.

```python
chart = ui.Chart(min=0, max=100, w=300, h=200)
s0 = chart.add_series(0xFF0000)  # Red series, returns index
s1 = chart.add_series(0x00FF00)  # Green series
chart.set_next(s0, 42)           # Append value to series 0
chart.value(75)                  # Alternative: add to default series
```

**Methods:**
- `add_series(color)` -- Add a data series (max 4). Returns series index. Bidirectional IPC.
- `set_next(series_idx, value)` -- Append a value to a specific series.
- `value(val)` -- Append value to default series (uses `IPC_CMD_UI_SET_VALUE`).

**Events:** None

**Limit:** Maximum 4 series per chart (`UI_CHART_MAX_SERIES`).

---

### 6.14 Image

Image widget supporting built-in icons and dynamic RGB565 pixel data.

```python
# Built-in icon (24x24 mono bitmap, rendered at 48x48 default)
img = ui.Image("heart")
img = ui.Image("star", color=0xFFD700, w=96, h=96)
img.icon("check")  # Change to different icon

# Dynamic RGB565 pixel data
img = ui.Image(w=32, h=32)
pixels = bytearray(32 * 32 * 2)  # RGB565: 2 bytes per pixel
img.set_image(pixels)
```

**Methods:**
- `icon(name)` -- Change to a different built-in icon by name.
- `set_image(bytearray)` -- Send RGB565 pixel data. Automatically chunked over IPC (124 bytes per chunk, 200us inter-chunk delay).

**Events:** `clicked`

See [Section 10](#10-built-in-icons) for the full icon list.

---

### 6.15 Panel

Dark rectangular container for dashboard layouts.

```python
panel = ui.Panel(x=0, y=0, w=250, h=120, color=0x142240)
```

**Events:** None

**Usage:** Typically used as background cards in dashboard layouts. Default appearance: dark background, 1px border, 12px border radius.

---

### 6.16 Compass

Heading compass with circle, needle, and N/E/S/W labels.

```python
compass = ui.Compass(min=0, max=360, value=0, w=200, h=200)
compass.value(180)  # Point south
```

**Events:** None

---

## 7. Widget Common Methods

All widget objects returned by factory functions share these methods:

| Method | Signature | Description |
|--------|-----------|-------------|
| `id()` | `widget.id() -> int` | Return widget handle (0-31) for event matching |
| `text(str)` | `widget.text("new text")` | Set widget text content |
| `text()` | `widget.text() -> None` | Get text (not implemented) |
| `value(int)` | `widget.value(42)` | Set numeric value |
| `value()` | `widget.value() -> int` | Read current value (bidirectional IPC) |
| `pos(x, y)` | `widget.pos(100, 50)` | Set position |
| `size(w, h)` | `widget.size(200, 80)` | Set dimensions |
| `color(rgb)` | `widget.color(0xFF0000)` | Set primary color (0xRRGGBB) |
| `show()` | `widget.show()` | Make widget visible |
| `hide()` | `widget.hide()` | Make widget invisible |
| `delete()` | `widget.delete()` | Remove widget permanently |
| `set_pixels(ba)` | `widget.set_pixels(bytearray)` | Set dot matrix data (DotMatrix only) |
| `icon(name)` | `widget.icon("heart")` | Change built-in icon (Image only) |
| `set_image(ba)` | `widget.set_image(bytearray)` | Set RGB565 pixel data (Image only) |
| `add_series(c)` | `widget.add_series(0xFF0000) -> int` | Add chart series (Chart only) |
| `set_next(i, v)` | `widget.set_next(0, 42)` | Append value to chart series (Chart only) |

### Widget Print Representation

```python
>>> btn = ui.Button("Test")
>>> btn
<Widget handle=0 type=1>
```

---

## 8. LCD Terminal API

The `lcd` module provides a console-like text output to the CM55 display terminal panel.

### Module Functions

#### lcd.print(*args, sep=' ', end='\n', markup=True)

Print text to the LCD terminal. Alias: `lcd.console()`.

```python
import lcd
lcd.print("Hello World")
lcd.print("Temperature:", 25.5, "C")
lcd.print("Status", "OK", sep=" | ")
lcd.print("no newline", end="")
```

**Parameters:**

| Param    | Type | Default | Description |
|----------|------|---------|-------------|
| `*args`  | any  | --      | Values to print (converted to str) |
| `sep`    | str  | `' '`   | Separator between values |
| `end`    | str  | `'\n'`  | End-of-line character |
| `markup` | bool | `True`  | Enable HTML/CSS markup processing |

#### lcd.console(*args, sep=' ', end='\n', markup=True)

Identical to `lcd.print()`. Provided for API naming clarity.

#### lcd.clear()

Clear all text from the terminal.

```python
lcd.clear()
```

#### lcd.theme(name)

Set the terminal color theme.

```python
lcd.theme("dark")
lcd.theme("light")
```

### IPC Commands Used

| Function | IPC Command | Notes |
|----------|-------------|-------|
| `lcd.print()` / `lcd.console()` | `IPC_CMD_LCD_PRINT` | Text + length in value field |
| `lcd.clear()` | `IPC_CMD_LCD_CLEAR` | No payload |
| `lcd.theme()` | `IPC_CMD_LCD_THEME` | Theme name as text |

### Markup Flag

The markup flag is embedded in bit 31 of the `value` field:

```c
#define LCD_IPC_FLAG_MARKUP     (1UL << 31)
#define LCD_IPC_VALUE_LEN_MASK  (0x7FFFFFFFUL)
```

Text is truncated to 127 characters per IPC message (`IPC_DATA_MAX_LEN - 1`).

---

## 9. Markup Support

When `markup=True` (default), the LCD terminal supports:

- **HTML tags:** `<b>bold</b>`, `<i>italic</i>`, `<u>underline</u>`
- **CSS classes:** Applied via LVGL rich text rendering
- **FontAwesome icons:** Embedded icon characters in text
- **Color spans:** `<span style="color:#FF0000">red text</span>`

Example:

```python
lcd.print("<b>Warning:</b> <span style='color:#FF0000'>High Temperature</span>")
```

---

## 10. Built-in Icons

16 built-in 24x24 monochrome bitmap icons, rendered at configurable size (default 48x48). Each icon is 72 bytes (3 bytes/row x 24 rows, MSB = leftmost pixel).

| Index | Name           | Description |
|-------|----------------|-------------|
| 0     | `heart`        | Heart shape |
| 1     | `star`         | 5-pointed star |
| 2     | `flag`         | Flag on pole |
| 3     | `trophy`       | Trophy cup |
| 4     | `skull`        | Skull |
| 5     | `arrow_up`     | Up arrow |
| 6     | `arrow_down`   | Down arrow |
| 7     | `arrow_left`   | Left arrow |
| 8     | `arrow_right`  | Right arrow |
| 9     | `check`        | Checkmark |
| 10    | `cross`        | X mark |
| 11    | `smiley`       | Smiley face |
| 12    | `car`          | Car (side view) |
| 13    | `boat`         | Sailboat |
| 14    | `plane`        | Airplane (top view) |
| 15    | `home`         | House |

**Usage:**

```python
img = ui.Image("heart", color=0xFF0000)
img.icon("star")  # Change icon
```

**C lookup:**

```c
int idx = ui_icon_lookup("heart");  // Returns 0, or -1 if not found
```

---

## 11. Limits and Constraints

| Constraint | Value | Notes |
|------------|-------|-------|
| Max widgets | 32 | `UI_MAX_WIDGETS` |
| Event ring buffer | 16 events | `UI_EVENT_RING_SIZE` |
| Events per poll | 8 max | `UI_MAX_EVENTS_PER_POLL` |
| Create text length | 96 chars | `UI_CREATE_TEXT_MAX` (null-terminated) |
| Chart max series | 4 per chart | `UI_CHART_MAX_SERIES` |
| IPC data max | 128 bytes | `IPC_DATA_MAX_LEN` |
| IPC response max | 240 bytes | `ipc_response_t.data[]` |
| Image chunk size | 124 bytes | `IPC_DATA_MAX_LEN - 4` header bytes |
| Icon bitmap size | 72 bytes | 24x24 mono, 3 bytes/row |
| Icon count | 16 | `UI_ICON_COUNT` |
| LCD text per message | 127 chars | `IPC_DATA_MAX_LEN - 1` |
| Screen default size | 792x398 px | Playground tab area |
| Python GC heap | 128 KB | `mpy_main.c:94` |
| QSPI filesystem | 64 MB | For `/main.py` and user files |
| Available font sizes | 14, 16, 20, 24, 28 | Common across AI Kit and Eva Kit |
| IPC send retries | 100 | `UI_IPC_SEND_RETRIES` |
| IPC retry delay | 100 us | `UI_IPC_RETRY_DELAY_US` |
| IPC response timeout | 2000 ms | `UI_IPC_RESP_TIMEOUT_MS` |

### Thread Safety

- All `ui.*` functions must be called from the MicroPython task on CM33_NS.
- CM55 LVGL widget operations are serialized through the GFX task.
- The IPC pipe is single-owner: never call `Cy_IPC_Pipe_SendMessage()` from within an IPC handler.
- Background sensor auto-push is paused on first UI use and resumed on soft reset.

---

## 12. MicroPython API Reference

### Module: `ui`

#### Factory Functions

| Function | Returns | Description |
|----------|---------|-------------|
| `ui.Button(text, **kw)` | `Widget` | Create push button |
| `ui.Label(text, **kw)` | `Widget` | Create text label |
| `ui.Slider(**kw)` | `Widget` | Create horizontal slider |
| `ui.Switch(text, **kw)` | `Widget` | Create toggle switch |
| `ui.Checkbox(text, **kw)` | `Widget` | Create checkbox |
| `ui.Arc(**kw)` | `Widget` | Create circular arc |
| `ui.Bar(**kw)` | `Widget` | Create progress bar |
| `ui.Spinner(**kw)` | `Widget` | Create spinner |
| `ui.Dropdown(text, **kw)` | `Widget` | Create dropdown (options: `\n`-separated) |
| `ui.Textarea(text, **kw)` | `Widget` | Create text area |
| `ui.Seg7(text, **kw)` | `Widget` | Create 7-segment display |
| `ui.DotMatrix(cols=, rows=, **kw)` | `Widget` | Create dot matrix |
| `ui.Chart(**kw)` | `Widget` | Create chart |
| `ui.Image(text, **kw)` | `Widget` | Create image (icon name or empty) |
| `ui.Panel(**kw)` | `Widget` | Create panel container |
| `ui.Compass(**kw)` | `Widget` | Create compass |

#### Utility Functions

| Function | Returns | Description |
|----------|---------|-------------|
| `ui.poll()` | `list[dict]` | Poll events: `[{'handle':N, 'type':'clicked', 'value':V}, ...]` |
| `ui.clear()` | `None` | Delete all widgets |
| `ui.list()` | `list[dict]` | List active widgets: `[{'id':N, 'type':'Button'}, ...]` |
| `ui.get(id)` | `Widget` | Get Widget by handle ID (raises `ValueError` if not found) |
| `ui.screen(w, h)` | `None` | Set canvas dimensions (auto-clears) |
| `ui.program(code)` | `True/None` | Write/read/delete `/main.py` |

#### Internal Functions (used by IDE, not user-facing)

| Function | Description |
|----------|-------------|
| `ui._ide_status(bool)` | Show/hide IDE connection icon |
| `ui._deploy()` | Show "Programming Mode" overlay |

### Module: `lcd`

| Function | Returns | Description |
|----------|---------|-------------|
| `lcd.print(*args, sep=' ', end='\n', markup=True)` | `None` | Print to LCD terminal |
| `lcd.console(*args, sep=' ', end='\n', markup=True)` | `None` | Alias for `lcd.print()` |
| `lcd.clear()` | `None` | Clear terminal |
| `lcd.theme(name)` | `None` | Set color theme |

---

## 13. C API Reference

### CM55 LCD Module (ipc_lcd.h)

```c
/**
 * Initialize IPC LCD receiver. Call after LVGL and display are ready.
 * @param parent  LVGL parent container (Terminal tab)
 * @return true on success
 */
bool ipc_lcd_init(lv_obj_t *parent);

/**
 * Update the terminal container for page-based navigation.
 * @param parent  LVGL container or NULL (page destroyed)
 */
void ipc_lcd_set_container(lv_obj_t *parent);

/**
 * Toggle terminal panel visibility.
 */
void ipc_lcd_toggle_panel(void);

/**
 * Check if terminal panel is visible.
 * @return true if visible
 */
bool ipc_lcd_is_panel_visible(void);

/**
 * Reset auto-navigate flag. Called on UI CLEAR_ALL.
 */
void ipc_lcd_reset_auto_nav(void);

/**
 * Check if unread console output exists while panel is hidden.
 * @return true if unread text available
 */
bool ipc_lcd_has_unread(void);

/**
 * Clear the unread flag.
 */
void ipc_lcd_clear_unread(void);
```

### CM33_NS External C Functions (modui.c)

```c
/**
 * Notify CM55 of IDE connection state change.
 * Called from tacp.c on TACP_CMD_IDE_STATUS.
 * Non-blocking: skips silently if CM55 not ready.
 * @param connected  true if IDE connected, false if disconnected
 */
void ui_notify_ide_connected(bool connected);

/**
 * Clear all LVGL widgets on MicroPython soft reset.
 * Also resumes background sensor auto-push if it was paused.
 * Called from mpy_main.c on every soft reset.
 */
void ui_auto_clear_on_reset(void);

/**
 * Show native "Programming Mode" overlay on LCD.
 * Called from mpy_main.c in BOOT_MODE_SAFE_DEPLOY.
 * Fire-and-forget without blocking on CM55 probe.
 */
void ui_show_deploy_screen(void);
```

### C Usage Example: Creating a Widget via IPC

```c
#include "ipc_ui_protocol.h"
#include "ipc_communication.h"

// Example: Create a button at (50, 100) with text "Start"
void create_button_example(void) {
    ipc_ui_create_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.widget_type = UI_WIDGET_BUTTON;
    cfg.x = 50;
    cfg.y = 100;
    cfg.w = 120;
    cfg.h = 40;
    cfg.color = 0x4CAF50;  // Green
    cfg.min_val = 0;
    cfg.max_val = 0;
    cfg.init_val = 0;
    strncpy(cfg.text, "Start", UI_CREATE_TEXT_MAX - 1);

    // Send bidirectional IPC_CMD_UI_CREATE
    // Response: ui_ipc_resp.data[0] = assigned handle (0-31)
    if (ui_ipc_send_bidir(IPC_CMD_UI_CREATE, &cfg, sizeof(cfg))) {
        uint8_t handle = ui_ipc_resp.data[0];
        // handle is now usable for SET_TEXT, SET_VALUE, DELETE, etc.
    }
}

// Example: Poll events
void poll_events_example(void) {
    if (ui_ipc_send_bidir(IPC_CMD_UI_POLL_EVENTS, NULL, 0)) {
        int count = ui_ipc_resp.data_len / sizeof(ipc_ui_event_t);
        const ipc_ui_event_t *events = (const ipc_ui_event_t *)ui_ipc_resp.data;
        for (int i = 0; i < count; i++) {
            printf("Widget %d: event %d, value %d\n",
                   events[i].handle_id, events[i].event_type, events[i].value);
        }
    }
}

// Example: Set widget text
void set_text_example(uint8_t handle, const char *text) {
    uint8_t buf[IPC_DATA_MAX_LEN];
    memset(buf, 0, sizeof(buf));
    buf[0] = handle;
    size_t len = strlen(text);
    size_t copy = (len > IPC_DATA_MAX_LEN - 2) ? (IPC_DATA_MAX_LEN - 2) : len;
    memcpy(&buf[1], text, copy);
    buf[1 + copy] = '\0';
    ui_ipc_send_fire_forget(IPC_CMD_UI_SET_TEXT, buf, 2 + copy);
}

// Example: Send RGB565 image data in chunks
void send_image_example(uint8_t handle, const uint8_t *data, size_t len) {
    uint16_t offset = 0;
    #define IMAGE_CHUNK_MAX (IPC_DATA_MAX_LEN - 4)

    while (len > 0) {
        uint8_t chunk_len = (len > IMAGE_CHUNK_MAX) ? IMAGE_CHUNK_MAX : (uint8_t)len;
        uint8_t buf[IPC_DATA_MAX_LEN];
        buf[0] = handle;
        buf[1] = (uint8_t)(offset & 0xFF);
        buf[2] = (uint8_t)(offset >> 8);
        buf[3] = chunk_len;
        memcpy(&buf[4], data, chunk_len);
        ui_ipc_send_fire_forget(IPC_CMD_UI_SET_IMAGE, buf, 4 + chunk_len);
        data += chunk_len;
        offset += chunk_len;
        len -= chunk_len;
        if (len > 0) Cy_SysLib_DelayUs(200);
    }
}
```

---

## Source Files

| File | Location | Role |
|------|----------|------|
| `ipc_ui_protocol.h` | `common/shared/include/` | IPC protocol definitions (shared CM33/CM55) |
| `modui.c` | `common/mpy/` | MicroPython `ui` module (CM33_NS) |
| `modlcd.c` | `common/mpy/` | MicroPython `lcd` module (CM33_NS) |
| `ipc_lcd.h` | `common/modules/ipc_lcd/` | CM55 LCD terminal receiver |
| `ui_builtin_icons.h` | `common/modules/ipc_ui/` | Built-in 24x24 icon bitmaps |
| `ui_widget_mgr.c` | `common/modules/ipc_ui/` | CM55 widget manager (LVGL creation) |
