# ipc_lcd.h

CM55 IPC receiver for LCD text commands from CM33_NS MicroPython.

## Functions (exported by the archive)

### `ipc_lcd_clear_unread`

```c
void ipc_lcd_clear_unread(void);
```

Clear the unread flag.  Called when the user opens the Console panel.

### `ipc_lcd_has_unread`

```c
bool ipc_lcd_has_unread(void);
```

Check if new text arrived while the console panel was hidden. Used by Playground to show a notification badge on the Console button. @return true if there is unread console output.

### `ipc_lcd_init`

```c
bool ipc_lcd_init(lv_obj_t *parent);
```

File Name: ipc_lcd.h Description: CM55 IPC receiver for LCD text commands from CM33_NS MicroPython. / #ifndef IPC_LCD_H #define IPC_LCD_H #include "lvgl.h" #include <stdbool.h> /** Initialize IPC LCD receiver. Call after LVGL and display are ready. Sets up IPC pipe, registers callback, and creates an LVGL timer that polls the receive queue and updates a terminal-like renderer. @param parent Pointer to LVGL parent container (Terminal tab). @return true on success.

### `ipc_lcd_is_panel_visible`

```c
bool ipc_lcd_is_panel_visible(void);
```

Check if the terminal panel is currently visible. @return true if visible, false if hidden or not yet created.

### `ipc_lcd_reset_auto_nav`

```c
void ipc_lcd_reset_auto_nav(void);
```

Reset the one-shot auto-navigate flag. Called on UI CLEAR_ALL (new MicroPython session) so lcd.print() can auto-navigate to Playground again.

### `ipc_lcd_set_container`

```c
void ipc_lcd_set_container(lv_obj_t *parent);
```

Update the terminal container (for page-based navigation). Called when Playground page is created/destroyed. Pass NULL to invalidate (page destroyed), non-NULL to activate. @param parent  LVGL container or NULL.

### `ipc_lcd_toggle_panel`

```c
void ipc_lcd_toggle_panel(void);
```

Toggle terminal panel visibility (show/hide). If the terminal has not been created yet, force-creates it first. Used by Playground Console Log button.

## Constants

| Name | Value |
|---|---|
| `IPC_LCD_H` | `#include` |
