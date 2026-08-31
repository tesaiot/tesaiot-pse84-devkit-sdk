/*******************************************************************************
 * File Name: tesaiot_ui_helpers.c
 *
 * Description: TESAIoT UI layout helper functions.
 *              Card, row, column, arc gauge, status bar, compass creators.
 *              All follow Golden Ratio proportions from tesaiot_ui_theme.h.
 *
 *******************************************************************************/

#include "tesaiot_ui_helpers.h"
#include "lv_fonts_thai.h"
#include <math.h>
#include <stdio.h>

/*******************************************************************************
 * Shared Style Objects (initialized once, applied via lv_obj_add_style)
 *
 * Eliminates repeated lv_obj_set_style_* calls across pages.
 * Call tesaiot_ui_styles_init() once at startup (from sensorhub_ui_init).
 *******************************************************************************/
static lv_style_t s_style_card;
static lv_style_t s_style_surface;
static bool s_styles_inited = false;

void tesaiot_ui_styles_init(void)
{
    if (s_styles_inited) return;

    /* Card style: bg + radius + shadow + no scroll */
    lv_style_init(&s_style_card);
    lv_style_set_bg_color(&s_style_card, lv_color_hex(UI_COLOR_BG_CARD));
    lv_style_set_bg_opa(&s_style_card, LV_OPA_COVER);
    lv_style_set_radius(&s_style_card, UI_CARD_RADIUS);
    lv_style_set_border_color(&s_style_card, lv_color_hex(UI_COLOR_BG_HOVER));
    lv_style_set_border_width(&s_style_card, UI_CARD_BORDER);
    lv_style_set_pad_all(&s_style_card, UI_CARD_PAD);
    lv_style_set_shadow_width(&s_style_card, 12);
    lv_style_set_shadow_color(&s_style_card, lv_color_hex(0x000000));
    lv_style_set_shadow_opa(&s_style_card, LV_OPA_30);
    lv_style_set_shadow_offset_y(&s_style_card, 4);

    /* Surface style: elevated bg, no border, smaller radius */
    lv_style_init(&s_style_surface);
    lv_style_set_bg_color(&s_style_surface, lv_color_hex(UI_COLOR_BG_SURFACE));
    lv_style_set_bg_opa(&s_style_surface, LV_OPA_COVER);
    lv_style_set_radius(&s_style_surface, UI_RADIUS_MD);
    lv_style_set_border_width(&s_style_surface, 0);
    lv_style_set_pad_all(&s_style_surface, UI_SPACE_SM);

    s_styles_inited = true;
}

const lv_style_t *tesaiot_style_card(void)    { return &s_style_card; }
const lv_style_t *tesaiot_style_surface(void) { return &s_style_surface; }

/*******************************************************************************
 * tesaiot_card_create
 *******************************************************************************/
lv_obj_t *tesaiot_card_create(lv_obj_t *parent, int w, int h,
                               const char *title, uint32_t title_color,
                               lv_obj_t **out_value_label)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_add_style(card, (lv_style_t *)&s_style_card, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    if (title != NULL) {
        lv_obj_t *lbl_title = lv_label_create(card);
        lv_label_set_text(lbl_title, title);
        lv_obj_set_style_text_color(lbl_title, lv_color_hex(title_color), LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl_title, UI_FONT_H3, LV_PART_MAIN);
        lv_obj_align(lbl_title, LV_ALIGN_TOP_LEFT, 0, 0);
    }

    if (out_value_label != NULL) {
        lv_obj_t *lbl_value = lv_label_create(card);
        lv_label_set_text(lbl_value, "---");
        lv_obj_set_style_text_color(lbl_value, lv_color_hex(UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl_value, UI_FONT_BODY, LV_PART_MAIN);
        lv_obj_align(lbl_value, LV_ALIGN_TOP_LEFT, 0, 28);
        *out_value_label = lbl_value;
    }

    return card;
}

/*******************************************************************************
 * tesaiot_row_create
 *******************************************************************************/
lv_obj_t *tesaiot_row_create(lv_obj_t *parent, int gap)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, gap, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

/*******************************************************************************
 * tesaiot_col_create
 *******************************************************************************/
lv_obj_t *tesaiot_col_create(lv_obj_t *parent, int gap)
{
    lv_obj_t *col = lv_obj_create(parent);
    lv_obj_set_width(col, LV_PCT(100));
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_set_layout(col, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, gap, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(col, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(col, 0, LV_PART_MAIN);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    return col;
}

/*******************************************************************************
 * tesaiot_arc_gauge_create
 *******************************************************************************/
lv_obj_t *tesaiot_arc_gauge_create(lv_obj_t *parent, int size,
                                    int range_min, int range_max,
                                    uint32_t color,
                                    lv_obj_t **out_arc,
                                    lv_obj_t **out_label)
{
    /* Transparent wrapper for centering */
    lv_obj_t *wrap = lv_obj_create(parent);
    lv_obj_set_size(wrap, size, size);
    lv_obj_set_style_bg_opa(wrap, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(wrap, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(wrap, 0, LV_PART_MAIN);
    lv_obj_clear_flag(wrap, LV_OBJ_FLAG_SCROLLABLE);

    /* Arc */
    lv_obj_t *arc = lv_arc_create(wrap);
    lv_obj_set_size(arc, size, size);
    lv_arc_set_range(arc, range_min, range_max);
    lv_arc_set_rotation(arc, UI_ARC_START_ANGLE);
    lv_arc_set_bg_angles(arc, 0, UI_ARC_SWEEP);
    lv_arc_set_value(arc, range_min);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_width(arc, UI_ARC_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, UI_ARC_WIDTH, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(UI_COLOR_BG_HOVER), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(color), LV_PART_INDICATOR);
    lv_obj_center(arc);

    /* Center value label */
    lv_obj_t *lbl = lv_label_create(wrap);
    lv_label_set_text(lbl, "--");
    lv_obj_set_style_text_font(lbl, UI_FONT_H1, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, lv_color_hex(UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_center(lbl);

    if (out_arc)   *out_arc = arc;
    if (out_label) *out_label = lbl;
    return wrap;
}

/*******************************************************************************
 * tesaiot_status_bar_create
 *******************************************************************************/
lv_obj_t *tesaiot_status_bar_create(lv_obj_t *parent,
                                     lv_obj_t **out_status)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, UI_SCREEN_W, UI_STATUS_BAR_H);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(UI_COLOR_BG_CARD), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(bar, UI_SPACE_MD, LV_PART_MAIN);
    lv_obj_set_style_pad_right(bar, UI_SPACE_MD, LV_PART_MAIN);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    /* Title (left) */
    lv_obj_t *title = lv_label_create(bar);
    lv_label_set_text(title, "Copyright by BDH & TESAIoT");
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, UI_FONT_STATUS, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);

    /* BSP info (center) */
    lv_obj_t *bsp_lbl = lv_label_create(bar);
#if BSP_HAS_DPS368
    lv_label_set_text(bsp_lbl, "PSoC Edge AI Kit via BENTO IDE");
#elif BSP_HAS_CAPSENSE
    lv_label_set_text(bsp_lbl, "PSoC Edge Eva Kit via BENTO IDE");
#else
    lv_label_set_text(bsp_lbl, "PSoC Edge E84 via BENTO IDE");
#endif
    lv_obj_set_style_text_color(bsp_lbl, lv_color_hex(UI_COLOR_TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_set_style_text_font(bsp_lbl, UI_FONT_CAPTION, LV_PART_MAIN);
    lv_obj_align(bsp_lbl, LV_ALIGN_CENTER, 0, 0);

    /* Status (right) — hidden until IDE connects */
    if (out_status) {
        lv_obj_t *status = lv_label_create(bar);
        lv_label_set_text(status, "");
        lv_obj_set_style_text_color(status, lv_color_hex(UI_COLOR_ACCENT_GREEN), LV_PART_MAIN);
        lv_obj_set_style_text_font(status, UI_FONT_CAPTION, LV_PART_MAIN);
        lv_obj_align(status, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_flag(status, LV_OBJ_FLAG_HIDDEN);
        *out_status = status;
    }

    return bar;
}

/*******************************************************************************
 * tesaiot_heading_gauge_create
 *
 * lv_scale round gauge (360deg compass) — based on lv_example_scale_4.
 * Creates: scale + cardinal labels + red North section + needle + heading label.
 *******************************************************************************/
lv_obj_t *tesaiot_heading_gauge_create(lv_obj_t *parent, int size,
                                        lv_obj_t **out_scale,
                                        lv_obj_t **out_needle,
                                        lv_obj_t **out_arrow,
                                        lv_obj_t **out_label)
{
    /*=========================================================================
     * Scale widget: full 360° compass
     *=========================================================================*/
    lv_obj_t *scale = lv_scale_create(parent);
    lv_obj_set_size(scale, size, size);
    lv_scale_set_mode(scale, LV_SCALE_MODE_ROUND_OUTER);
    lv_scale_set_label_show(scale, true);

    /* Full 360° sweep, North at top (12-o'clock) */
    lv_scale_set_range(scale, 0, 360);
    lv_scale_set_angle_range(scale, 360);
    lv_scale_set_rotation(scale, 270);

    /* 13 ticks = 12 intervals of 30°; major every 3rd → N,E,S,W labels */
    lv_scale_set_total_tick_count(scale, 13);
    lv_scale_set_major_tick_every(scale, 3);

    /* Cardinal direction labels (first "N" at 0° and last "N" at 360° overlap) */
    static const char *compass_labels[] = {"N", "E", "S", "W", "N", NULL};
    lv_scale_set_text_src(scale, compass_labels);

    /* Tick lengths */
    lv_obj_set_style_length(scale, 5, LV_PART_ITEMS);
    lv_obj_set_style_length(scale, 10, LV_PART_INDICATOR);

    /*=========================================================================
     * Styles (static — initialized once, shared across all instances)
     *=========================================================================*/
    static lv_style_t st_indicator, st_minor, st_main, st_needle;
    static lv_style_t st_north_main, st_north_ind, st_north_items;
    static bool inited = false;

    if (!inited) {
        /* Major ticks + labels: purple (BMM350 color) */
        lv_style_init(&st_indicator);
        lv_style_set_text_font(&st_indicator, UI_FONT_CAPTION);
        lv_style_set_text_color(&st_indicator,
                                lv_color_hex(UI_COLOR_SENSOR_MAG));
        lv_style_set_line_color(&st_indicator,
                                lv_color_hex(UI_COLOR_SENSOR_MAG));
        lv_style_set_width(&st_indicator, 10);
        lv_style_set_line_width(&st_indicator, 2);

        /* Minor ticks: muted purple */
        lv_style_init(&st_minor);
        lv_style_set_line_color(&st_minor, lv_color_hex(UI_COLOR_BG_HOVER));
        lv_style_set_width(&st_minor, 5);
        lv_style_set_line_width(&st_minor, 2);

        /* Main arc: purple */
        lv_style_init(&st_main);
        lv_style_set_arc_color(&st_main,
                               lv_color_hex(UI_COLOR_SENSOR_MAG));
        lv_style_set_arc_width(&st_main, 3);

        /* Needle: orange for high visibility */
        lv_style_init(&st_needle);
        lv_style_set_line_width(&st_needle, 3);
        lv_style_set_line_color(&st_needle,
                                lv_color_hex(UI_COLOR_ACCENT_ORANGE));
        lv_style_set_line_rounded(&st_needle, true);

        /* North section styles: red accent */
        lv_style_init(&st_north_main);
        lv_style_set_arc_color(&st_north_main,
                               lv_color_hex(UI_COLOR_ACCENT_RED));
        lv_style_set_arc_width(&st_north_main, 3);

        lv_style_init(&st_north_ind);
        lv_style_set_line_color(&st_north_ind,
                                lv_color_hex(UI_COLOR_ACCENT_RED));
        lv_style_set_text_color(&st_north_ind,
                                lv_color_hex(UI_COLOR_ACCENT_RED));

        lv_style_init(&st_north_items);
        lv_style_set_line_color(&st_north_items,
                                lv_color_hex(UI_COLOR_ACCENT_RED));

        inited = true;
    }

    lv_obj_add_style(scale, &st_indicator, LV_PART_INDICATOR);
    lv_obj_add_style(scale, &st_minor, LV_PART_ITEMS);
    lv_obj_add_style(scale, &st_main, LV_PART_MAIN);

    /*=========================================================================
     * North section (red highlight around 0°/360°)
     * Two sections to cover the wrap-around: 330°-360° and 0°-30°
     *=========================================================================*/
    lv_scale_section_t *north1 = lv_scale_add_section(scale);
    lv_scale_section_set_range(north1, 330, 360);
    lv_scale_section_set_style(north1, LV_PART_MAIN, &st_north_main);
    lv_scale_section_set_style(north1, LV_PART_INDICATOR, &st_north_ind);
    lv_scale_section_set_style(north1, LV_PART_ITEMS, &st_north_items);

    lv_scale_section_t *north2 = lv_scale_add_section(scale);
    lv_scale_section_set_range(north2, 0, 30);
    lv_scale_section_set_style(north2, LV_PART_MAIN, &st_north_main);
    lv_scale_section_set_style(north2, LV_PART_INDICATOR, &st_north_ind);
    lv_scale_section_set_style(north2, LV_PART_ITEMS, &st_north_items);

    /*=========================================================================
     * Needle line (inside scale)
     *=========================================================================*/
    lv_obj_t *needle = lv_line_create(scale);
    lv_obj_add_style(needle, &st_needle, 0);

    /* Needle length: from center toward rim, leaving room for ticks/labels */
    int needle_len = size / 2 - 20;
    lv_scale_set_line_needle_value(scale, needle, needle_len, 0);

    /*=========================================================================
     * Arrowhead line (V-shape barbs at needle tip)
     *=========================================================================*/
    lv_obj_t *arrow = lv_line_create(scale);
    lv_obj_add_style(arrow, &st_needle, 0);

    /*=========================================================================
     * Center heading label (positioned at scale center)
     *=========================================================================*/
    lv_obj_t *lbl = lv_label_create(scale);
    lv_label_set_text(lbl, "---");
    lv_obj_set_style_text_font(lbl,
                                (size >= 180) ? UI_FONT_H2 : UI_FONT_BODY,
                                LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl,
                                lv_color_hex(UI_COLOR_TEXT_PRIMARY),
                                LV_PART_MAIN);
    lv_obj_center(lbl);

    /* Output pointers */
    if (out_scale)  *out_scale  = scale;
    if (out_needle) *out_needle = needle;
    if (out_arrow)  *out_arrow  = arrow;
    if (out_label)  *out_label  = lbl;

    return scale;
}

/*******************************************************************************
 * tesaiot_heading_gauge_set_heading
 *
 * Update needle shaft + arrowhead barbs for the given heading.
 * The arrowhead is a V-shape (3-point lv_line) at the needle tip.
 *******************************************************************************/
void tesaiot_heading_gauge_set_heading(lv_obj_t *scale, lv_obj_t *needle,
                                        int32_t needle_len,
                                        lv_obj_t *arrow,
                                        lv_point_precise_t *arrow_pts,
                                        int32_t heading_deg)
{
    /* 1. Update shaft via lv_scale API */
    lv_scale_set_line_needle_value(scale, needle, needle_len, heading_deg);

    /* 2. Compute arrowhead barb positions */
    if (arrow == NULL || arrow_pts == NULL) return;

    int32_t cx = lv_obj_get_width(scale) / 2;
    int32_t cy = lv_obj_get_height(scale) / 2;

    float theta = (float)heading_deg * 3.14159265f / 180.0f;
    float dx = sinf(theta);
    float dy = -cosf(theta);

    /* Tip of the needle */
    float tip_x = (float)cx + (float)needle_len * dx;
    float tip_y = (float)cy + (float)needle_len * dy;

    /* Barb parameters: length proportional to gauge, angle 25deg from shaft */
    int32_t barb_len = lv_obj_get_width(scale) / 12;
    float barb_a = 25.0f * 3.14159265f / 180.0f;

    /* Left barb: reverse shaft rotated +barb_a */
    float la = theta + 3.14159265f - barb_a;
    /* Right barb: reverse shaft rotated -barb_a */
    float ra = theta + 3.14159265f + barb_a;

    arrow_pts[0].x = (lv_value_precise_t)(tip_x + (float)barb_len * sinf(la));
    arrow_pts[0].y = (lv_value_precise_t)(tip_y - (float)barb_len * cosf(la));
    arrow_pts[1].x = (lv_value_precise_t)tip_x;
    arrow_pts[1].y = (lv_value_precise_t)tip_y;
    arrow_pts[2].x = (lv_value_precise_t)(tip_x + (float)barb_len * sinf(ra));
    arrow_pts[2].y = (lv_value_precise_t)(tip_y - (float)barb_len * cosf(ra));

    lv_line_set_points_mutable(arrow, arrow_pts, 3);
}

/*******************************************************************************
 * tesaiot_compass_create
 *******************************************************************************/
lv_obj_t *tesaiot_compass_create(lv_obj_t *parent, int size,
                                  lv_obj_t **out_needle,
                                  lv_point_precise_t *out_pts,
                                  lv_obj_t **out_heading)
{
    int center = size / 2;
    int needle_len = center - 20;

    /* Circular container */
    lv_obj_t *compass = lv_obj_create(parent);
    lv_obj_set_size(compass, size, size);
    lv_obj_set_style_radius(compass, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_pad_all(compass, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(compass, lv_color_hex(UI_COLOR_BG_SURFACE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(compass, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(compass, lv_color_hex(UI_COLOR_SENSOR_MAG), LV_PART_MAIN);
    lv_obj_set_style_border_width(compass, 2, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(compass, true, LV_PART_MAIN);
    lv_obj_clear_flag(compass, LV_OBJ_FLAG_SCROLLABLE);

    /* Cardinal direction labels */
    static const char *dirs[] = {"N", "E", "S", "W"};
    static const lv_align_t aligns[] = {LV_ALIGN_TOP_MID, LV_ALIGN_RIGHT_MID,
                                         LV_ALIGN_BOTTOM_MID, LV_ALIGN_LEFT_MID};
    static const int offx[] = {0, -4, 0, 4};
    static const int ofy[] = {4, 0, -4, 0};

    for (int i = 0; i < 4; i++) {
        lv_obj_t *lbl = lv_label_create(compass);
        lv_label_set_text(lbl, dirs[i]);
        lv_obj_set_style_text_color(lbl,
            (i == 0) ? lv_color_hex(UI_COLOR_ACCENT_RED)
                     : lv_color_hex(UI_COLOR_TEXT_SECONDARY),
            LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl, UI_FONT_CAPTION, LV_PART_MAIN);
        lv_obj_align(lbl, aligns[i], offx[i], ofy[i]);
    }

    /* Needle line */
    lv_obj_t *needle = lv_line_create(compass);
    lv_obj_set_size(needle, size, size);
    lv_obj_center(needle);
    lv_obj_set_style_line_width(needle, UI_COMPASS_NEEDLE_W, LV_PART_MAIN);
    lv_obj_set_style_line_color(needle, lv_color_hex(UI_COLOR_ACCENT_RED), LV_PART_MAIN);
    lv_obj_set_style_line_rounded(needle, true, LV_PART_MAIN);

    /* Initialize needle pointing North */
    out_pts[0].x = center;
    out_pts[0].y = center;
    out_pts[1].x = center;
    out_pts[1].y = center - needle_len;
    lv_line_set_points_mutable(needle, out_pts, 2);

    if (out_needle) *out_needle = needle;

    /* Heading label at compass center */
    if (out_heading) {
        lv_obj_t *hdg = lv_label_create(compass);
        lv_label_set_text(hdg, "---");
        lv_obj_set_style_text_color(hdg, lv_color_hex(UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
        lv_obj_set_style_text_font(hdg, UI_FONT_BODY, LV_PART_MAIN);
        lv_obj_center(hdg);
        *out_heading = hdg;
    }

    return compass;
}

/*******************************************************************************
 * tesaiot_compass_set_heading
 *******************************************************************************/
void tesaiot_compass_set_heading(lv_obj_t *needle,
                                  lv_point_precise_t *needle_pts,
                                  int32_t compass_size,
                                  int32_t heading_deg)
{
    if (needle == NULL || needle_pts == NULL || compass_size <= 0) {
        return;
    }

    heading_deg %= 360;
    if (heading_deg < 0) {
        heading_deg += 360;
    }

    int32_t center = compass_size / 2;
    int32_t needle_len = center - 20;

    float theta = (float)heading_deg * 3.14159265f / 180.0f;

    needle_pts[0].x = (lv_value_precise_t)center;
    needle_pts[0].y = (lv_value_precise_t)center;
    needle_pts[1].x = (lv_value_precise_t)((float)center + (float)needle_len * sinf(theta));
    needle_pts[1].y = (lv_value_precise_t)((float)center - (float)needle_len * cosf(theta));

    lv_line_set_points_mutable(needle, needle_pts, 2);
}

/*******************************************************************************
 * Apple-Style Compass (rotating dial)
 *
 * Black background, lv_scale ROUND_INNER with 360° sweep.
 * Tick marks every 10°, labels every 30° (N, 30, 60, E, ...).
 * Red North section. Fixed red pointer (▼) at top.
 * The ENTIRE dial rotates via lv_scale_set_rotation() so that the
 * current heading always aligns with the fixed top pointer.
 *******************************************************************************/

lv_obj_t *tesaiot_compass_apple_create(lv_obj_t *parent, int size,
                                        lv_obj_t **out_scale,
                                        lv_obj_t **out_heading)
{
    /* Container: black background, centered layout */
    lv_obj_t *container = lv_obj_create(parent);
    lv_obj_set_size(container, size, size + 40);
    lv_obj_set_style_bg_color(container, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_set_style_radius(container, UI_CARD_RADIUS, 0);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);

    /* Scale widget: full 360° circle */
    lv_obj_t *scale = lv_scale_create(container);
    lv_obj_set_size(scale, size, size);
    lv_scale_set_mode(scale, LV_SCALE_MODE_ROUND_INNER);
    lv_obj_set_style_bg_opa(scale, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(scale, LV_RADIUS_CIRCLE, 0);
    lv_obj_align(scale, LV_ALIGN_TOP_MID, 0, 0);

    lv_scale_set_label_show(scale, true);

    /* 37 ticks = 36 segments of 10° each; major every 3 = labels every 30° */
    lv_scale_set_total_tick_count(scale, 37);
    lv_scale_set_major_tick_every(scale, 3);
    lv_scale_set_range(scale, 0, 360);
    lv_scale_set_angle_range(scale, 360);
    lv_scale_set_rotation(scale, 270);  /* N at 12 o'clock */

    /* Labels every 30°: N, 30, 60, E, 120, 150, S, 210, 240, W, 300, 330 */
    static const char *compass_labels[] = {
        "N", "30", "60", "E", "120", "150",
        "S", "210", "240", "W", "300", "330", "", NULL
    };
    lv_scale_set_text_src(scale, compass_labels);

    /* Styles (static — initialized once) */
    static lv_style_t st_major, st_minor, st_arc;
    static lv_style_t st_north_arc, st_north_ind;
    static bool inited = false;

    if (!inited) {
        /* Major ticks + labels: white */
        lv_style_init(&st_major);
        lv_style_set_text_font(&st_major, UI_FONT_CAPTION);
        lv_style_set_text_color(&st_major, lv_color_white());
        lv_style_set_line_color(&st_major, lv_color_white());
        lv_style_set_length(&st_major, 15);
        lv_style_set_line_width(&st_major, 2);

        /* Minor ticks: dim grey */
        lv_style_init(&st_minor);
        lv_style_set_line_color(&st_minor, lv_color_hex(UI_COLOR_TEXT_MUTED));
        lv_style_set_length(&st_minor, 8);
        lv_style_set_line_width(&st_minor, 1);

        /* Arc ring: subtle dark */
        lv_style_init(&st_arc);
        lv_style_set_arc_color(&st_arc, lv_color_hex(0x444444));
        lv_style_set_arc_width(&st_arc, 2);

        /* North section: red */
        lv_style_init(&st_north_arc);
        lv_style_set_arc_color(&st_north_arc,
                               lv_color_hex(UI_COLOR_ACCENT_RED));
        lv_style_set_arc_width(&st_north_arc, 3);

        lv_style_init(&st_north_ind);
        lv_style_set_line_color(&st_north_ind,
                                lv_color_hex(UI_COLOR_ACCENT_RED));
        lv_style_set_text_color(&st_north_ind,
                                lv_color_hex(UI_COLOR_ACCENT_RED));
        inited = true;
    }

    lv_obj_add_style(scale, &st_major, LV_PART_INDICATOR);
    lv_obj_add_style(scale, &st_minor, LV_PART_ITEMS);
    lv_obj_add_style(scale, &st_arc, LV_PART_MAIN);

    /* North section: 0-10° */
    lv_scale_section_t *n1 = lv_scale_add_section(scale);
    lv_scale_section_set_range(n1, 0, 10);
    lv_scale_section_set_style(n1, LV_PART_MAIN, &st_north_arc);
    lv_scale_section_set_style(n1, LV_PART_INDICATOR, &st_north_ind);

    /* North section: 350-360° (wrap-around) */
    lv_scale_section_t *n2 = lv_scale_add_section(scale);
    lv_scale_section_set_range(n2, 350, 360);
    lv_scale_section_set_style(n2, LV_PART_MAIN, &st_north_arc);
    lv_scale_section_set_style(n2, LV_PART_INDICATOR, &st_north_ind);

    /* Fixed red pointer at top (does NOT rotate with dial) */
    lv_obj_t *pointer = lv_label_create(container);
    lv_label_set_text(pointer, LV_SYMBOL_DOWN);
    lv_obj_set_style_text_color(pointer,
                                lv_color_hex(UI_COLOR_ACCENT_RED), 0);
    lv_obj_set_style_text_font(pointer, UI_FONT_H2, 0);
    lv_obj_align_to(pointer, scale, LV_ALIGN_OUT_TOP_MID, 0, 12);

    /* Heading label below compass */
    lv_obj_t *hdg_lbl = lv_label_create(container);
    lv_label_set_text(hdg_lbl, "0\xC2\xB0 N");
    lv_obj_set_style_text_color(hdg_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(hdg_lbl, UI_FONT_H2, 0);
    lv_obj_align_to(hdg_lbl, scale, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);

    /* Output */
    if (out_scale)   *out_scale   = scale;
    if (out_heading) *out_heading = hdg_lbl;

    return container;
}

void tesaiot_compass_apple_set_heading(lv_obj_t *scale,
                                        lv_obj_t *heading_lbl,
                                        int32_t heading_deg)
{
    if (scale == NULL) return;

    heading_deg %= 360;
    if (heading_deg < 0) heading_deg += 360;

    /* Rotate dial so heading° is at 12 o'clock */
    lv_scale_set_rotation(scale, 270 - heading_deg);

    /* Update heading label */
    if (heading_lbl) {
        static const char * const cardinal[] = {
            "N", "NE", "E", "SE", "S", "SW", "W", "NW"
        };
        int idx = ((heading_deg + 22) / 45) % 8;

        char buf[32];
        snprintf(buf, sizeof(buf), "%d\xC2\xB0 %s",
                 (int)heading_deg, cardinal[idx]);
        lv_label_set_text(heading_lbl, buf);
    }
}

/*******************************************************************************
 * thai_label — Create a label with Noto Sans Thai font.
 *
 * Substitutes Thai 2-tier clusters in the text with PUA codepoints before
 * lv_label_set_text — LVGL has no shaper, so without this, tone marks
 * overlap above-vowels on the LCD. See thai_text/ for the pipeline.
 ******************************************************************************/
#include "thai_lvgl_adapter.h"

lv_obj_t *thai_label(lv_obj_t *parent, const char *text,
                     int size, lv_color_t color)
{
    const lv_font_t *font;
    switch (size) {
        case 28: font = &lv_font_noto_thai_28; break;
        case 24: font = &lv_font_noto_thai_24; break;
        case 20: font = &lv_font_noto_thai_20; break;
        case 16: font = &lv_font_noto_thai_16; break;
        default: font = &lv_font_noto_thai_14; break;
    }
    lv_obj_t *lbl = lv_label_create(parent);
    thai_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    return lbl;
}
