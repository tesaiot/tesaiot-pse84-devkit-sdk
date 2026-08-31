# BENTO Secure Library — SDK reference

Generated from the shipped headers and cross-checked against the shipped
binary: a function appears here only if `nm` found it in the archive.
Regenerate with `./bento-release.sh docs`.

| Header | Archive API | Open-source fns | What it is |
|---|---|---|---|
| [cy_wcm_shim.h](cy_wcm_shim.md) | 0 | 1 |  |
| [ipc_lcd.h](ipc_lcd.md) | 7 | 0 | CM55 IPC receiver for LCD text commands from CM33_NS MicroPython. |
| [ipc_sensorhub.h](ipc_sensorhub.md) | 12 | 0 | CM55 IPC receiver for sensor data from CM33_NS MicroPython. Stores latest sensor samples from al... |
| [ipc_service.h](ipc_service.md) | 1 | 0 | CM55 IPC Service for bidirectional WiFi/MQTT/TESAIoT commands. Receives IPC commands from CM33_N... |
| [ipc_ui.h](ipc_ui.md) | 4 | 0 | CM55 IPC UI handler — receives widget commands from CM33_NS MicroPython 'ui' module via IPC Pipe... |
| [ui_builtin_icons.h](ui_builtin_icons.md) | 0 | 0 | Built-in 24x24 monochrome icon bitmaps for ui.Image() widget. Each icon is 72 bytes (3 bytes per... |
| [ui_widget_defaults.h](ui_widget_defaults.md) | 0 | 0 | Default style constants for MicroPython Playground widgets. Centralizes magic numbers so they're... |
| [ui_widget_mgr.h](ui_widget_mgr.md) | 30 | 0 | Widget handle table and LVGL widget lifecycle manager. Maps handle IDs (0-31) to lv_obj_t* point... |

**54 archive API functions across 8 headers**, plus 1 functions declared here whose implementation ships as open source in this package (yours to read and change). "Archive API" means `nm` found the symbol exported by the shipped binary; `api.txt` lists 59 exported symbols in total — the difference is data symbols and functions whose only declaration is in `bento_secure_undeclared.h`.
