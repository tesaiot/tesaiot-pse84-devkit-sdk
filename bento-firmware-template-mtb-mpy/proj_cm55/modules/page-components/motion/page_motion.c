/*******************************************************************************
 * File Name: page_motion.c
 *
 * Description: Motion page — BMI270 accel/gyro charts + BMM350 compass.
 *              Page-based conversion from tab_motion.c.
 *              Uses module-static context, screen-based lifecycle.
 *
 *              Layout: Golden Ratio phi split (480px left / 290px right).
 *
 *******************************************************************************/

#include "page_motion.h"
#include "page_manager.h"
#include "tesaiot_ui_theme.h"
#include "tesaiot_ui_helpers.h"
#include "ipc_sensorhub.h"
#include "bsp_feature_flags.h"
#include "lvgl.h"
#include <stdio.h>
#include <math.h>
#include <limits.h>

/*******************************************************************************
 * Axis color definitions
 *******************************************************************************/
#define COLOR_AX    UI_COLOR_ACCENT_RED
#define COLOR_AY    UI_COLOR_ACCENT_GREEN
#define COLOR_AZ    UI_COLOR_ACCENT_BLUE
#define COLOR_GX    UI_COLOR_ACCENT_ORANGE
#define COLOR_GY    UI_COLOR_ACCENT_PURPLE
#define COLOR_GZ    UI_COLOR_SENSOR_TOUCH

/*******************************************************************************
 * Module-Static Context
 *******************************************************************************/
typedef struct {
    lv_obj_t             *chart_accel;
    lv_obj_t             *chart_gyro;
    lv_chart_series_t    *ax_series;
    lv_chart_series_t    *ay_series;
    lv_chart_series_t    *az_series;
    lv_chart_series_t    *gx_series;
    lv_chart_series_t    *gy_series;
    lv_chart_series_t    *gz_series;
    lv_obj_t             *compass_scale;
    lv_obj_t             *heading_label;
    lv_obj_t             *tilt_label;
} page_motion_ctx_t;

static page_motion_ctx_t s_ctx;

/*******************************************************************************
 * Helper: auto-scale chart Y-axis
 *******************************************************************************/
static void autoscale_chart(lv_obj_t *chart,
                            lv_chart_series_t *s1,
                            lv_chart_series_t *s2,
                            lv_chart_series_t *s3,
                            int32_t min_half_range)
{
    uint32_t cnt = lv_chart_get_point_count(chart);
    int32_t *d1 = lv_chart_get_y_array(chart, s1);
    int32_t *d2 = lv_chart_get_y_array(chart, s2);
    int32_t *d3 = lv_chart_get_y_array(chart, s3);

    int32_t lo = INT32_MAX, hi = INT32_MIN;
    for (uint32_t i = 0; i < cnt; i++) {
        if (d1[i] != LV_CHART_POINT_NONE) { if (d1[i] < lo) lo = d1[i]; if (d1[i] > hi) hi = d1[i]; }
        if (d2[i] != LV_CHART_POINT_NONE) { if (d2[i] < lo) lo = d2[i]; if (d2[i] > hi) hi = d2[i]; }
        if (d3[i] != LV_CHART_POINT_NONE) { if (d3[i] < lo) lo = d3[i]; if (d3[i] > hi) hi = d3[i]; }
    }
    if (lo == INT32_MAX) return;

    int32_t mid = (hi + lo) / 2;
    if ((hi - lo) < min_half_range * 2) {
        lo = mid - min_half_range;
        hi = mid + min_half_range;
    }

    int32_t pad = (hi - lo) / 10;
    if (pad < 1) pad = 1;

    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, lo - pad, hi + pad);
}

/*******************************************************************************
 * Helper: style a chart with dark surface background
 *******************************************************************************/
static void style_chart(lv_obj_t *chart, int height)
{
    lv_obj_set_width(chart, LV_PCT(100));
    lv_obj_set_height(chart, height);
    lv_obj_set_style_bg_color(chart, lv_color_hex(UI_COLOR_BG_SURFACE), 0);
    lv_obj_set_style_bg_opa(chart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(chart, 0, 0);
    lv_obj_set_style_pad_all(chart, UI_SPACE_XS, 0);
    lv_obj_set_style_radius(chart, UI_CARD_RADIUS, 0);
    lv_obj_set_style_line_width(chart, UI_CHART_LINE_W, LV_PART_ITEMS);
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);
    lv_obj_clear_flag(chart, LV_OBJ_FLAG_SCROLLABLE);
}

/*******************************************************************************
 * page_motion_create
 *******************************************************************************/
lv_obj_t *page_motion_create(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));

    lv_obj_t *scr = lv_obj_create(NULL);
    page_manager_t *pm = pm_get_instance();
    lv_obj_t *content = pm_create_page_with_header(scr, pm,
                            "Motion", UI_COLOR_SENSOR_IMU);

    /* Root container: flex row */
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(content, UI_SPACE_MD, 0);
    lv_obj_set_style_pad_all(content, UI_SPACE_SM, 0);

    /* LEFT PANEL (480px) - Accel & Gyro charts */
    lv_obj_t *left_col = tesaiot_col_create(content, UI_SPACE_SM);
    lv_obj_set_width(left_col, 480);
    lv_obj_set_height(left_col, LV_PCT(100));
    lv_obj_set_style_pad_all(left_col, 0, 0);

    /* Acceleration section */
    lv_obj_t *accel_lbl = lv_label_create(left_col);
    lv_label_set_text(accel_lbl, "Acceleration (m/s^2)");
    lv_obj_set_style_text_font(accel_lbl, UI_FONT_H3, 0);
    lv_obj_set_style_text_color(accel_lbl,
                                lv_color_hex(UI_COLOR_SENSOR_IMU), 0);

    s_ctx.chart_accel = lv_chart_create(left_col);
    lv_chart_set_type(s_ctx.chart_accel, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_ctx.chart_accel, UI_CHART_POINTS);
    lv_chart_set_range(s_ctx.chart_accel, LV_CHART_AXIS_PRIMARY_Y, 0, 200);
    style_chart(s_ctx.chart_accel, UI_CHART_HEIGHT);

    s_ctx.ax_series = lv_chart_add_series(s_ctx.chart_accel,
                         lv_color_hex(COLOR_AX), LV_CHART_AXIS_PRIMARY_Y);
    s_ctx.ay_series = lv_chart_add_series(s_ctx.chart_accel,
                         lv_color_hex(COLOR_AY), LV_CHART_AXIS_PRIMARY_Y);
    s_ctx.az_series = lv_chart_add_series(s_ctx.chart_accel,
                         lv_color_hex(COLOR_AZ), LV_CHART_AXIS_PRIMARY_Y);

    /* Gyroscope section */
    lv_obj_t *gyro_lbl = lv_label_create(left_col);
    lv_label_set_text(gyro_lbl, "Gyroscope (dps)");
    lv_obj_set_style_text_font(gyro_lbl, UI_FONT_H3, 0);
    lv_obj_set_style_text_color(gyro_lbl,
                                lv_color_hex(UI_COLOR_SENSOR_IMU), 0);

    s_ctx.chart_gyro = lv_chart_create(left_col);
    lv_chart_set_type(s_ctx.chart_gyro, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_ctx.chart_gyro, UI_CHART_POINTS);
    lv_chart_set_range(s_ctx.chart_gyro, LV_CHART_AXIS_PRIMARY_Y, 0, 200);
    style_chart(s_ctx.chart_gyro, UI_CHART_HEIGHT);

    s_ctx.gx_series = lv_chart_add_series(s_ctx.chart_gyro,
                         lv_color_hex(COLOR_GX), LV_CHART_AXIS_PRIMARY_Y);
    s_ctx.gy_series = lv_chart_add_series(s_ctx.chart_gyro,
                         lv_color_hex(COLOR_GY), LV_CHART_AXIS_PRIMARY_Y);
    s_ctx.gz_series = lv_chart_add_series(s_ctx.chart_gyro,
                         lv_color_hex(COLOR_GZ), LV_CHART_AXIS_PRIMARY_Y);

    /* RIGHT PANEL (290px) - Compass + Tilt */
    lv_obj_t *right_col = tesaiot_col_create(content, UI_SPACE_MD);
    lv_obj_set_width(right_col, 290);
    lv_obj_set_height(right_col, LV_PCT(100));
    lv_obj_set_style_pad_all(right_col, 0, 0);
    lv_obj_set_flex_align(right_col, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* Compass heading — Apple-style rotating dial */
#if BSP_HAS_BMM350
    {
        tesaiot_compass_apple_create(right_col, UI_COMPASS_APPLE_SIZE,
                                     &s_ctx.compass_scale,
                                     &s_ctx.heading_label);
    }
#else
    {
        lv_obj_t *placeholder = lv_label_create(right_col);
        lv_label_set_text(placeholder, "Compass\nnot available\non this BSP");
        lv_obj_set_style_text_font(placeholder, UI_FONT_BODY, 0);
        lv_obj_set_style_text_color(placeholder,
                                    lv_color_hex(UI_COLOR_TEXT_DISABLED), 0);
        lv_obj_set_style_text_align(placeholder, LV_TEXT_ALIGN_CENTER, 0);
        s_ctx.compass_scale  = NULL;
        s_ctx.heading_label  = NULL;
    }
#endif

    /* Tilt readout */
    s_ctx.tilt_label = lv_label_create(right_col);
    lv_label_set_text(s_ctx.tilt_label, "Roll: ---  Pitch: ---");
    lv_obj_set_style_text_font(s_ctx.tilt_label, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(s_ctx.tilt_label,
                                lv_color_hex(UI_COLOR_TEXT_PRIMARY), 0);

    return scr;
}

/*******************************************************************************
 * page_motion_render
 *******************************************************************************/
void page_motion_render(sensorhub_snapshot_t *snap)
{
    if (snap == NULL) return;

    /* BMI270: Accelerometer + Gyroscope */
    if (snap->has_bmi270 && snap->bmi270_changed) {
        int32_t ax_val = (int32_t)((float)snap->bmi270.ax / IPC_BMI270_ACCEL_LSB_PER_G * 981.0f);
        int32_t ay_val = (int32_t)((float)snap->bmi270.ay / IPC_BMI270_ACCEL_LSB_PER_G * 981.0f);
        int32_t az_val = (int32_t)((float)snap->bmi270.az / IPC_BMI270_ACCEL_LSB_PER_G * 981.0f);

        lv_chart_set_next_value(s_ctx.chart_accel, s_ctx.ax_series, ax_val);
        lv_chart_set_next_value(s_ctx.chart_accel, s_ctx.ay_series, ay_val);
        lv_chart_set_next_value(s_ctx.chart_accel, s_ctx.az_series, az_val);

        autoscale_chart(s_ctx.chart_accel,
                        s_ctx.ax_series, s_ctx.ay_series, s_ctx.az_series, 200);

        int32_t gx_val = (int32_t)((float)snap->bmi270.gx / 16.4f * 10.0f);
        int32_t gy_val = (int32_t)((float)snap->bmi270.gy / 16.4f * 10.0f);
        int32_t gz_val = (int32_t)((float)snap->bmi270.gz / 16.4f * 10.0f);

        lv_chart_set_next_value(s_ctx.chart_gyro, s_ctx.gx_series, gx_val);
        lv_chart_set_next_value(s_ctx.chart_gyro, s_ctx.gy_series, gy_val);
        lv_chart_set_next_value(s_ctx.chart_gyro, s_ctx.gz_series, gz_val);

        autoscale_chart(s_ctx.chart_gyro,
                        s_ctx.gx_series, s_ctx.gy_series, s_ctx.gz_series, 100);

        /* Tilt angles */
        float ay_f = (float)ay_val / 100.0f;
        float az_f = (float)az_val / 100.0f;
        float ax_f = (float)ax_val / 100.0f;
        float roll  = atan2f(ay_f, az_f) * 57.2958f;
        float pitch = atan2f(-ax_f, sqrtf(ay_f * ay_f +
                                           az_f * az_f)) * 57.2958f;

        char tilt_buf[64];
        snprintf(tilt_buf, sizeof(tilt_buf),
                 "Roll: %.1f\xC2\xB0  Pitch: %.1f\xC2\xB0",
                 (double)roll, (double)pitch);
        lv_label_set_text(s_ctx.tilt_label, tilt_buf);
    }

    /* BMM350: Compass heading — Apple-style rotating dial */
#if BSP_HAS_BMM350
    if (snap->has_bmm350 && snap->bmm350_changed && s_ctx.compass_scale) {
        int heading_deg = (int)(snap->bmm350.heading_x10 / 10);
        tesaiot_compass_apple_set_heading(s_ctx.compass_scale,
                                           s_ctx.heading_label,
                                           heading_deg);
    }
#endif
}

/*******************************************************************************
 * page_motion_destroy
 *******************************************************************************/
void page_motion_destroy(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));
}
