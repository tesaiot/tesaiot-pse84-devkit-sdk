# ui_widget_defaults.h

Default style constants for MicroPython Playground widgets. Centralizes magic numbers so they're easy to tweak without hunting through ui_widget_mgr.c. These apply ONLY to widgets created from MicroPython via IPC. SensorHub / BentoClaw native pages are not affected.

> Configuration header — constants and macros only, no functions.

## Constants

| Name | Value |
|---|---|
| `UI_WIDGET_DEFAULTS_H` | `/*******************************************************************************` |
| `UI_DEFAULT_FONT_SIZE` | `20` |
| `UI_DEF_BUTTON_FONT_SIZE` | `20` |
| `UI_DEF_LABEL_FONT_SIZE` | `20` |
| `UI_DEF_CHECKBOX_FONT_SIZE` | `16` |
| `UI_DEF_DROPDOWN_FONT_SIZE` | `16` |
| `UI_DEF_TEXTAREA_FONT_SIZE` | `16` |
| `UI_DEF_SCREEN_W` | `792` |
| `UI_DEF_SCREEN_H` | `398` |
| `UI_DEF_SLIDER_W` | `200` |
| `UI_DEF_BAR_W` | `200` |
| `UI_DEF_TEXTAREA_W` | `200` |
| `UI_DEF_ARC_SIZE` | `150` |
| `UI_DEF_CHART_W` | `250` |
| `UI_DEF_CHART_H` | `150` |
| `UI_DEF_CHART_POINTS` | `50` |
| `UI_DEF_IMAGE_SIZE` | `48` |
| `UI_AUTO_ROW_H` | `60` |
| `UI_AUTO_COL_W` | `200` |
| `UI_DEF_RANGE_MAX` | `100` |
| `UI_DEF_SEG7_COLOR` | `0x00FF00` |
| `UI_DEF_CHART_LINE_COLOR` | `0x00BFFF` |
| `UI_DEF_ICON_COLOR` | `0xFFFFFF` |
| `UI_DEF_PANEL_BG_COLOR` | `0x1a1a2e` |
| `UI_DEF_PANEL_BORDER_COLOR` | `0x16213e` |
| `UI_DEF_DOTMATRIX_ON_COLOR` | `0x00FF00` |
| `UI_DEF_DOTMATRIX_OFF_COLOR` | `0x003300` |
| `UI_DEF_DOTMATRIX_BG_COLOR` | `0x111111` |
| `UI_DEF_DOTMATRIX_GRID_COLOR` | `0x333333` |
| `UI_DEF_COMPASS_NEEDLE_COLOR` | `0xFF4444` |
| `UI_DEF_COMPASS_LABEL_COLOR` | `0xCCCCCC` |
| `UI_DEF_COMPASS_HDG_COLOR` | `0xFFFFFF` |
