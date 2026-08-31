/*******************************************************************************
 * File Name: ui_widget_defaults.h
 *
 * Description: Default style constants for MicroPython Playground widgets.
 *              Centralizes magic numbers so they're easy to tweak without
 *              hunting through ui_widget_mgr.c.
 *
 *              These apply ONLY to widgets created from MicroPython via IPC.
 *              SensorHub / BentoClaw native pages are not affected.
 *
 *******************************************************************************/

#ifndef UI_WIDGET_DEFAULTS_H
#define UI_WIDGET_DEFAULTS_H

/*******************************************************************************
 * Font Sizes
 *
 * Default font for widgets when the user does not specify `value=` (font size).
 * On the 4.3" 800x480 display, 16px is readable but small; 20px is comfortable.
 *
 * Available Montserrat:  14, 16, 20, 24, 28
 * Available Noto Thai:   14, 16, 20, 24, 28
 *
 * Per-widget defaults allow fine-grained control:
 *   Button   — 20px  (needs to be readable inside a tappable area)
 *   Label    — 20px  (body text, comfortable reading distance)
 *   Checkbox — 16px  (compact control, typically short text)
 *   Dropdown — 16px  (compact control with popup list)
 *   Textarea — 16px  (multi-line input, fits more text)
 *   Fallback — 20px  (anything else)
 ******************************************************************************/
#define UI_DEFAULT_FONT_SIZE        20  /* fallback for unlisted widget types  */
#define UI_DEF_BUTTON_FONT_SIZE     20
#define UI_DEF_LABEL_FONT_SIZE      20
#define UI_DEF_CHECKBOX_FONT_SIZE   16
#define UI_DEF_DROPDOWN_FONT_SIZE   16
#define UI_DEF_TEXTAREA_FONT_SIZE   16

/*******************************************************************************
 * Screen Defaults — ui.screen() without arguments
 *
 * Waveshare 4.3" DSI LCD: 800x480 physical.
 * Usable area for Playground widgets = 792x398 (after status bar + margins).
 ******************************************************************************/
#define UI_DEF_SCREEN_W             792
#define UI_DEF_SCREEN_H             398

/*******************************************************************************
 * Widget Dimensions (used when w= / h= not specified)
 ******************************************************************************/
#define UI_DEF_SLIDER_W             200
#define UI_DEF_BAR_W                200
#define UI_DEF_TEXTAREA_W           200
#define UI_DEF_ARC_SIZE             150   /* width = height */
#define UI_DEF_CHART_W              250
#define UI_DEF_CHART_H              150
#define UI_DEF_CHART_POINTS         50
#define UI_DEF_IMAGE_SIZE           48    /* icon canvas w/h (2x scale of 24x24) */

/*******************************************************************************
 * Auto-Layout Grid (when x=-1, y=-1)
 ******************************************************************************/
#define UI_AUTO_ROW_H               60
#define UI_AUTO_COL_W               200

/*******************************************************************************
 * Range Defaults (slider, arc, bar, chart)
 ******************************************************************************/
#define UI_DEF_RANGE_MAX            100

/*******************************************************************************
 * Color Defaults (hex 0xRRGGBB)
 ******************************************************************************/
#define UI_DEF_SEG7_COLOR           0x00FF00   /* 7-segment display */
#define UI_DEF_CHART_LINE_COLOR     0x00BFFF   /* chart series line */
#define UI_DEF_ICON_COLOR           0xFFFFFF   /* built-in icon fill */
#define UI_DEF_PANEL_BG_COLOR       0x1a1a2e   /* Panel background */
#define UI_DEF_PANEL_BORDER_COLOR   0x16213e   /* Panel border */
#define UI_DEF_DOTMATRIX_ON_COLOR   0x00FF00   /* DotMatrix lit pixel */
#define UI_DEF_DOTMATRIX_OFF_COLOR  0x003300   /* DotMatrix dim pixel */
#define UI_DEF_DOTMATRIX_BG_COLOR   0x111111   /* DotMatrix background */
#define UI_DEF_DOTMATRIX_GRID_COLOR 0x333333   /* DotMatrix grid lines */

/*******************************************************************************
 * Compass Defaults
 ******************************************************************************/
#define UI_DEF_COMPASS_NEEDLE_COLOR 0xFF4444   /* needle (North label + line) */
#define UI_DEF_COMPASS_LABEL_COLOR  0xCCCCCC   /* E/S/W labels */
#define UI_DEF_COMPASS_HDG_COLOR    0xFFFFFF   /* heading degrees text */

#endif /* UI_WIDGET_DEFAULTS_H */
