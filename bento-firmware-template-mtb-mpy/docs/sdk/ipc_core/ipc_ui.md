# ipc_ui.h

CM55 IPC UI handler — receives widget commands from CM33_NS MicroPython 'ui' module via IPC Pipe. ISR callback -> FreeRTOS queue -> LVGL timer (50ms) processes commands in GFX task context.

## Functions (exported by the archive)

### `ipc_ui_ext_dispatch`

```c
bool ipc_ui_ext_dispatch(uint32_t cmd, const uint8_t *data);
```

> **Weak.** The archive provides a stub. Define your own and the linker prefers yours.

Project extension dispatch hook for UI-band IPC opcodes not handled by the built-in switch (weak default returns false / no-op). Called from process_ui_command() in GFX-task context, so implementations may touch the display / GFX resources (e.g. the QWA309 DFR0522 RGB matrix on display I2C). @param cmd   The IPC opcode (IPC_CMD_UI_* range). @param data  Pointer to the raw 128-byte IPC payload (ipc_msg_t.data). @return true if the opcode was handled.

### `ipc_ui_init`

```c
bool ipc_ui_init(lv_obj_t *parent);
```

File Name: ipc_ui.h Description: CM55 IPC UI handler — receives widget commands from CM33_NS MicroPython 'ui' module via IPC Pipe. ISR callback -> FreeRTOS queue -> LVGL timer (50ms) processes commands in GFX task context. / #ifndef IPC_UI_H #define IPC_UI_H #include "lvgl.h" #include <stdbool.h> /** Initialize the IPC UI handler. Registers IPC callback, creates FreeRTOS queue, and LVGL timer. Must be called after LVGL and IPC Pipe are initialized. @param parent  LVGL parent object for user-created widgets (UX/UI tab container). @return true on success.

### `ipc_ui_input_activity`

```c
void ipc_ui_input_activity(void);
```

Touch activity hook — arms the 5 ms fast drain for one FAST_TIMEOUT window. Called from ui_widget_mgr_event_push() (GFX task context) so the very first tap of a quiescent app is answered at fast-mode latency instead of waiting out the 200 ms idle tick. Deliberately NOT wired to POLL_EVENTS: polling is continuous, so arming there would pin fast mode on permanently and revert the GFX sleep-clamp tuning that protects ai_infer/radar.

### `ipc_ui_set_container`

```c
void ipc_ui_set_container(lv_obj_t *parent);
```

Update the widget parent container (for page-based navigation). Called when Playground page is created/destroyed. Pass NULL to invalidate (page destroyed), non-NULL to activate. @param parent  LVGL container or NULL.

## Constants

| Name | Value |
|---|---|
| `IPC_UI_H` | `#include` |
