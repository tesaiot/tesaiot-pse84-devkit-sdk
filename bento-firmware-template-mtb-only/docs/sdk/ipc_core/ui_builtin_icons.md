# ui_builtin_icons.h

Built-in 24x24 monochrome icon bitmaps for ui.Image() widget. Each icon is 72 bytes (3 bytes per row x 24 rows). Bit=1 means foreground (user color), Bit=0 means transparent. MSB = leftmost pixel. Usage on CM55: int idx = ui_icon_lookup("heart"); if (idx >= 0) render ui_builtin_icons[idx] to lv_canvas

> Configuration header — constants and macros only, no functions.

## Constants

| Name | Value |
|---|---|
| `UI_BUILTIN_ICONS_H` | `#include` |
| `UI_ICON_W` | `24` |
| `UI_ICON_H` | `24` |
| `UI_ICON_STRIDE` | `3` |
| `UI_ICON_SIZE` | `(UI_ICON_STRIDE` |
| `UI_ICON_COUNT` | `16` |
| `UI_ICON_HEART` | `0` |
| `UI_ICON_STAR` | `1` |
| `UI_ICON_FLAG` | `2` |
| `UI_ICON_TROPHY` | `3` |
| `UI_ICON_SKULL` | `4` |
| `UI_ICON_ARROW_UP` | `5` |
| `UI_ICON_ARROW_DOWN` | `6` |
| `UI_ICON_ARROW_LEFT` | `7` |
| `UI_ICON_ARROW_RIGHT` | `8` |
| `UI_ICON_CHECK` | `9` |
| `UI_ICON_CROSS` | `10` |
| `UI_ICON_SMILEY` | `11` |
| `UI_ICON_CAR` | `12` |
| `UI_ICON_BOAT` | `13` |
| `UI_ICON_PLANE` | `14` |
| `UI_ICON_HOME` | `15` |
