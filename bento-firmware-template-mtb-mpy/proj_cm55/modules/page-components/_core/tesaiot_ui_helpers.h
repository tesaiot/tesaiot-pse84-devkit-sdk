/*******************************************************************************
 * File Name: tesaiot_ui_helpers.h
 *
 * Description: TESAIoT UI layout helper functions.
 *              Card, row, column, arc gauge, status bar creators.
 *              All follow Golden Ratio proportions from tesaiot_ui_theme.h.
 *
 *******************************************************************************/

#ifndef TESAIOT_UI_HELPERS_H
#define TESAIOT_UI_HELPERS_H

#include "tesaiot_ui_theme.h"
#include "lvgl.h"

/**
 * Initialize shared LVGL style objects (card, surface).
 * Call once at startup from sensorhub_ui_init().
 */
void tesaiot_ui_styles_init(void);

/** Get shared card style (bg + radius + shadow). */
const lv_style_t *tesaiot_style_card(void);

/** Get shared surface style (elevated bg, no border). */
const lv_style_t *tesaiot_style_surface(void);

/**
 * Create a styled card container (Golden Rectangle).
 * Dark bg, rounded corners, thin border, non-scrollable.
 *
 * @param parent   Parent LVGL object.
 * @param w        Card width in pixels.
 * @param h        Card height in pixels (use w*UI_PHI_INV for golden rect).
 * @param title    Optional title text (NULL to skip).
 * @param title_color  Title text color hex.
 * @param out_value_label  Output: value label inside card (NULL to skip).
 * @return Card container object.
 */
lv_obj_t *tesaiot_card_create(lv_obj_t *parent, int w, int h,
                               const char *title, uint32_t title_color,
                               lv_obj_t **out_value_label);

/**
 * Create a transparent flex-row container.
 *
 * @param parent  Parent object.
 * @param gap     Column gap between children (px).
 * @return Row container object.
 */
lv_obj_t *tesaiot_row_create(lv_obj_t *parent, int gap);

/**
 * Create a transparent flex-column container.
 *
 * @param parent  Parent object.
 * @param gap     Row gap between children (px).
 * @return Column container object.
 */
lv_obj_t *tesaiot_col_create(lv_obj_t *parent, int gap);

/**
 * Create an arc gauge (potentiometer/slider/pressure style).
 * 270-degree sweep, no knob, center value label.
 *
 * @param parent      Parent object.
 * @param size        Arc diameter (px).
 * @param range_min   Minimum value.
 * @param range_max   Maximum value.
 * @param color       Indicator color hex.
 * @param out_arc     Output: arc widget.
 * @param out_label   Output: center value label.
 * @return Wrapper container.
 */
lv_obj_t *tesaiot_arc_gauge_create(lv_obj_t *parent, int size,
                                    int range_min, int range_max,
                                    uint32_t color,
                                    lv_obj_t **out_arc,
                                    lv_obj_t **out_label);

/**
 * Create a status bar at the top of the screen.
 *
 * @param parent       Parent object (screen).
 * @param out_status   Output: status text label (right side).
 * @return Status bar container.
 */
lv_obj_t *tesaiot_status_bar_create(lv_obj_t *parent,
                                     lv_obj_t **out_status);

/**
 * Create a compass widget (circle + cardinal labels + needle).
 *
 * @param parent       Parent object.
 * @param size         Compass diameter (px).
 * @param out_needle   Output: line widget for needle.
 * @param out_pts      Output: needle point array (must be static, 2 elements).
 * @param out_heading  Output: heading label below compass.
 * @return Compass container.
 */
lv_obj_t *tesaiot_compass_create(lv_obj_t *parent, int size,
                                  lv_obj_t **out_needle,
                                  lv_point_precise_t *out_pts,
                                  lv_obj_t **out_heading);

/**
 * Update simple compass needle heading.
 *
 * @param needle         Needle line object.
 * @param needle_pts     Mutable point array (2 elements).
 * @param compass_size   Compass diameter in pixels.
 * @param heading_deg    Heading in degrees (0-359, 0=North).
 */
void tesaiot_compass_set_heading(lv_obj_t *needle,
                                  lv_point_precise_t *needle_pts,
                                  int32_t compass_size,
                                  int32_t heading_deg);

/**
 * Create a heading gauge (lv_scale round gauge — compass style).
 * Based on lv_example_scale_4 pattern with 360deg sweep,
 * cardinal labels (N/E/S/W), red North section, needle + arrowhead.
 *
 * @param parent       Parent object.
 * @param size         Gauge diameter (px).
 * @param out_scale    Output: scale widget (for needle value updates).
 * @param out_needle   Output: needle line widget.
 * @param out_arrow    Output: arrowhead line widget (V-shape at tip).
 * @param out_label    Output: center heading label.
 * @return The scale widget (same as *out_scale).
 */
lv_obj_t *tesaiot_heading_gauge_create(lv_obj_t *parent, int size,
                                        lv_obj_t **out_scale,
                                        lv_obj_t **out_needle,
                                        lv_obj_t **out_arrow,
                                        lv_obj_t **out_label);

/**
 * Update heading gauge: moves needle shaft + arrowhead barbs.
 *
 * @param scale        The lv_scale widget.
 * @param needle       Needle line widget.
 * @param needle_len   Needle length (center to tip).
 * @param arrow        Arrowhead line widget (3-point V-shape).
 * @param arrow_pts    Mutable point array (3 elements, caller-owned).
 * @param heading_deg  Heading in degrees (0-360, 0=North).
 */
void tesaiot_heading_gauge_set_heading(lv_obj_t *scale, lv_obj_t *needle,
                                        int32_t needle_len,
                                        lv_obj_t *arrow,
                                        lv_point_precise_t *arrow_pts,
                                        int32_t heading_deg);

/**
 * Create an Apple-style compass (rotating dial with fixed pointer at top).
 * Black bg, white tick marks every 10°, labels every 30°,
 * red North section. The entire dial rotates based on heading.
 *
 * @param parent          Parent object.
 * @param size            Compass diameter (px).
 * @param out_scale       Output: scale widget (for rotation updates).
 * @param out_heading     Output: heading label below compass.
 * @return Container wrapping scale + pointer + label.
 */
lv_obj_t *tesaiot_compass_apple_create(lv_obj_t *parent, int size,
                                        lv_obj_t **out_scale,
                                        lv_obj_t **out_heading);

/**
 * Update Apple-style compass: rotates dial so heading faces top pointer.
 *
 * @param scale        The lv_scale widget.
 * @param heading_lbl  Heading label widget.
 * @param heading_deg  Heading in degrees (0-360, 0=North).
 */
void tesaiot_compass_apple_set_heading(lv_obj_t *scale,
                                        lv_obj_t *heading_lbl,
                                        int32_t heading_deg);

/**
 * Create a label using Noto Sans Thai font.
 * Supports Thai + English + symbols in one string (Montserrat fallback).
 *
 * Usage: thai_label(parent, "อุณหภูมิ 28.5°C", 20, lv_color_white());
 *
 * @param parent  Parent LVGL object.
 * @param text    UTF-8 text (Thai + English mixed OK).
 * @param size    Font size: 14, 16, 20, or 28.
 * @param color   Text color.
 * @return Label object.
 */
lv_obj_t *thai_label(lv_obj_t *parent, const char *text,
                     int size, lv_color_t color);

#endif /* TESAIOT_UI_HELPERS_H */
