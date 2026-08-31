/*******************************************************************************
 * File Name: page_dashboard.c
 *
 * Description: Dashboard page — all-sensors overview in card grid layout.
 *              Page-based conversion from tab_dashboard.c.
 *              Uses module-static context, screen-based lifecycle.
 *
 *              Cards are conditionally compiled based on BSP feature flags.
 *              Row 1: _DASH_CARD_COUNT sensor cards, DASH_SENSOR_CARD_W x
 *              DASH_SENSOR_CARD_H (count is BSP-conditional).
 *              Row 2: joystick + radar cards (half-width, 80px tall).
 *              Status label at bottom shows update count.
 *
 *******************************************************************************/

#include "page_dashboard.h"
#include "page_manager.h"
#include "tesaiot_ui_theme.h"
#include "tesaiot_ui_helpers.h"
#include "ipc_sensorhub.h"
#include "bsp_feature_flags.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <limits.h>

#if BSP_HAS_RADAR
extern volatile bool  tesaiot_radar_initialized;
extern volatile bool  tesaiot_radar_presence_detected;
extern volatile float tesaiot_radar_current_energy;
#endif

#include "usb_hid_joystick.h"

/*******************************************************************************
 * Module-Static Context (replaces passed-in ctx)
 *******************************************************************************/
typedef struct {
    lv_obj_t *imu_label;
    lv_obj_t *baro_label;
    lv_obj_t *env_label;
    lv_obj_t *controls_label;
    lv_obj_t *joy_label;
    lv_obj_t *radar_label;
    lv_obj_t *status_label;
    lv_obj_t *motion_accel_chart;
    lv_obj_t *motion_gyro_chart;
    lv_chart_series_t *ax_series;
    lv_chart_series_t *ay_series;
    lv_chart_series_t *az_series;
    lv_chart_series_t *gx_series;
    lv_chart_series_t *gy_series;
    lv_chart_series_t *gz_series;
#if BSP_HAS_BMM350
    lv_obj_t *compass_scale;
    lv_obj_t *heading_label;
#endif
    uint32_t  update_count;
} page_dashboard_ctx_t;

static page_dashboard_ctx_t s_ctx;

/*******************************************************************************
 * Static text buffers
 *******************************************************************************/
static char s_imu_buf[128];
#if BSP_HAS_DPS368
static char s_baro_buf[196];
#endif
#if BSP_HAS_SHT40
static char s_env_buf[96];
#endif
#if BSP_HAS_CAPSENSE
static char s_controls_buf[96];
#endif
static char s_joy_buf[96];
#if BSP_HAS_RADAR
static char s_radar_buf[128];
#endif
static char s_status_buf[64];

/* Previous-frame caches for change detection (avoid redundant redraws) */
static char s_joy_prev[96];
#if BSP_HAS_RADAR
static char s_radar_prev[128];
#endif
#if BSP_HAS_BMM350
static int  s_heading_prev = -1;
#endif
/* Autoscale hysteresis — previous range bounds */
static int32_t s_accel_range_lo, s_accel_range_hi;
static int32_t s_gyro_range_lo, s_gyro_range_hi;

/* 2-column layout: left (sensors+charts) | right (compass+joystick)
 * Screen 800x480, content area ~770x390 after header+padding. */
#define DASH_COMPASS_SIZE    200
#define DASH_RIGHT_COL_W     260
#define DASH_LEFT_COL_W      (770 - DASH_RIGHT_COL_W - 8)  /* 502 */
/* Count sensor cards at compile time */
#define _DASH_CARD_COUNT  (1 /* BMI270 always */ \
    + (BSP_HAS_DPS368)  \
    + (BSP_HAS_SHT40)   \
    + (BSP_HAS_CAPSENSE))
#define DASH_SENSOR_CARD_W   ((DASH_LEFT_COL_W - (_DASH_CARD_COUNT - 1) * 8) / _DASH_CARD_COUNT)
#define DASH_SENSOR_CARD_H   80
#define DASH_MOTION_CHART_H  100

static void autoscale_chart(lv_obj_t *chart,
                            lv_chart_series_t *s1,
                            lv_chart_series_t *s2,
                            lv_chart_series_t *s3,
                            int32_t min_half_range,
                            int32_t *prev_lo, int32_t *prev_hi)
{
    if (!chart || !s1 || !s2 || !s3) return;
    uint32_t cnt = lv_chart_get_point_count(chart);
    int32_t *d1 = lv_chart_get_y_array(chart, s1);
    int32_t *d2 = lv_chart_get_y_array(chart, s2);
    int32_t *d3 = lv_chart_get_y_array(chart, s3);
    if (!d1 || !d2 || !d3) return;

    int32_t lo = INT32_MAX;
    int32_t hi = INT32_MIN;
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
    int32_t new_lo = lo - pad;
    int32_t new_hi = hi + pad;

    /* Hysteresis: skip lv_chart_set_range if bounds changed < 5% — avoids
       full chart redraw on stable data */
    int32_t prev_span = *prev_hi - *prev_lo;
    if (prev_span < 1) prev_span = 1;
    int32_t threshold = prev_span / 20;  /* 5% */
    if (threshold < 1) threshold = 1;

    if (abs(new_lo - *prev_lo) > threshold || abs(new_hi - *prev_hi) > threshold) {
        lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, new_lo, new_hi);
        *prev_lo = new_lo;
        *prev_hi = new_hi;
    }
}

static void style_compact_chart(lv_obj_t *chart)
{
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, UI_CHART_POINTS);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, -200, 200);
    lv_obj_set_width(chart, LV_PCT(100));
    lv_obj_set_height(chart, DASH_MOTION_CHART_H);
    lv_obj_set_style_bg_color(chart, lv_color_hex(UI_COLOR_BG_SURFACE), 0);
    lv_obj_set_style_bg_opa(chart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(chart, 0, 0);
    lv_obj_set_style_pad_all(chart, UI_SPACE_XS, 0);
    lv_obj_set_style_radius(chart, UI_RADIUS_MD, 0);
    lv_obj_set_style_line_width(chart, UI_CHART_LINE_W, LV_PART_ITEMS);
    lv_obj_set_style_line_rounded(chart, true, LV_PART_ITEMS);
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);
    lv_obj_clear_flag(chart, LV_OBJ_FLAG_SCROLLABLE);
}

/*******************************************************************************
 * page_dashboard_create
 *******************************************************************************/
lv_obj_t *page_dashboard_create(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_joy_prev[0] = '\0';
#if BSP_HAS_RADAR
    s_radar_prev[0] = '\0';
#endif
#if BSP_HAS_BMM350
    s_heading_prev = -1;
#endif
    s_accel_range_lo = -200; s_accel_range_hi = 200;
    s_gyro_range_lo  = -200; s_gyro_range_hi  = 200;

    lv_obj_t *scr = lv_obj_create(NULL);
    page_manager_t *pm = pm_get_instance();
    lv_obj_t *content = pm_create_page_with_header(scr, pm,
                            "Sensor Dashboard", UI_COLOR_SENSOR_IMU);

    /***************************************************************************
     * Main 2-column layout: left (sensors+charts) | right (compass+joystick)
     ***************************************************************************/
    lv_obj_t *main_row = tesaiot_row_create(content, UI_SPACE_SM);

    /* ===== LEFT COLUMN: sensor cards + motion charts ===== */
    lv_obj_t *left_col = tesaiot_col_create(main_row, UI_SPACE_SM);
    lv_obj_set_width(left_col, DASH_LEFT_COL_W);
    lv_obj_set_height(left_col, LV_SIZE_CONTENT);

    /* Sensor cards row */
    lv_obj_t *sensor_row = tesaiot_row_create(left_col, UI_SPACE_SM);

    tesaiot_card_create(sensor_row, DASH_SENSOR_CARD_W, DASH_SENSOR_CARD_H,
                        "BMI270", UI_COLOR_SENSOR_IMU,
                        &s_ctx.imu_label);

#if BSP_HAS_DPS368
    tesaiot_card_create(sensor_row, DASH_SENSOR_CARD_W, DASH_SENSOR_CARD_H,
                        "DPS368", UI_COLOR_SENSOR_BARO,
                        &s_ctx.baro_label);
#endif

#if BSP_HAS_SHT40
    tesaiot_card_create(sensor_row, DASH_SENSOR_CARD_W, DASH_SENSOR_CARD_H,
                        "SHT40", UI_COLOR_SENSOR_ENV,
                        &s_ctx.env_label);
#endif

#if BSP_HAS_CAPSENSE
    tesaiot_card_create(sensor_row, DASH_SENSOR_CARD_W, DASH_SENSOR_CARD_H,
                        "Controls", UI_COLOR_SENSOR_TOUCH,
                        &s_ctx.controls_label);
#endif

    /* Motion Trends card */
    lv_obj_t *motion_card = lv_obj_create(left_col);
    lv_obj_set_size(motion_card, DASH_LEFT_COL_W, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(motion_card, lv_color_hex(UI_COLOR_BG_CARD), 0);
    lv_obj_set_style_bg_opa(motion_card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(motion_card, UI_CARD_RADIUS, 0);
    lv_obj_set_style_border_width(motion_card, UI_CARD_BORDER, 0);
    lv_obj_set_style_pad_all(motion_card, UI_SPACE_SM, 0);
    lv_obj_set_style_pad_gap(motion_card, UI_SPACE_XS, 0);
    lv_obj_set_layout(motion_card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(motion_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(motion_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(motion_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *accel_title = lv_label_create(motion_card);
    lv_label_set_text(accel_title, "Accel (m/s^2)  X/Y/Z");
    lv_obj_set_style_text_font(accel_title, UI_FONT_CAPTION, 0);
    lv_obj_set_style_text_color(accel_title, lv_color_hex(UI_COLOR_TEXT_SECONDARY), 0);

    s_ctx.motion_accel_chart = lv_chart_create(motion_card);
    style_compact_chart(s_ctx.motion_accel_chart);
    s_ctx.ax_series = lv_chart_add_series(s_ctx.motion_accel_chart, lv_color_hex(UI_COLOR_ACCENT_RED), LV_CHART_AXIS_PRIMARY_Y);
    s_ctx.ay_series = lv_chart_add_series(s_ctx.motion_accel_chart, lv_color_hex(UI_COLOR_ACCENT_GREEN), LV_CHART_AXIS_PRIMARY_Y);
    s_ctx.az_series = lv_chart_add_series(s_ctx.motion_accel_chart, lv_color_hex(UI_COLOR_ACCENT_BLUE), LV_CHART_AXIS_PRIMARY_Y);

    lv_obj_t *gyro_title = lv_label_create(motion_card);
    lv_label_set_text(gyro_title, "Gyro (dps)  X/Y/Z");
    lv_obj_set_style_text_font(gyro_title, UI_FONT_CAPTION, 0);
    lv_obj_set_style_text_color(gyro_title, lv_color_hex(UI_COLOR_TEXT_SECONDARY), 0);

    s_ctx.motion_gyro_chart = lv_chart_create(motion_card);
    style_compact_chart(s_ctx.motion_gyro_chart);
    s_ctx.gx_series = lv_chart_add_series(s_ctx.motion_gyro_chart, lv_color_hex(UI_COLOR_ACCENT_ORANGE), LV_CHART_AXIS_PRIMARY_Y);
    s_ctx.gy_series = lv_chart_add_series(s_ctx.motion_gyro_chart, lv_color_hex(UI_COLOR_ACCENT_PURPLE), LV_CHART_AXIS_PRIMARY_Y);
    s_ctx.gz_series = lv_chart_add_series(s_ctx.motion_gyro_chart, lv_color_hex(UI_COLOR_SENSOR_TOUCH), LV_CHART_AXIS_PRIMARY_Y);

    /* Status label */
    s_ctx.status_label = lv_label_create(left_col);
    lv_label_set_text(s_ctx.status_label, "Updates: 0");
    lv_obj_set_style_text_color(s_ctx.status_label,
                                lv_color_hex(UI_COLOR_ACCENT_GREEN), 0);
    lv_obj_set_style_text_font(s_ctx.status_label, UI_FONT_CAPTION, 0);

    /* ===== RIGHT COLUMN: compass + joystick ===== */
    lv_obj_t *right_col = tesaiot_col_create(main_row, UI_SPACE_SM);
    lv_obj_set_width(right_col, DASH_RIGHT_COL_W);
    lv_obj_set_height(right_col, LV_SIZE_CONTENT);
    lv_obj_set_flex_align(right_col, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

#if BSP_HAS_BMM350
    tesaiot_compass_apple_create(right_col, DASH_COMPASS_SIZE,
                                  &s_ctx.compass_scale,
                                  &s_ctx.heading_label);
#endif

#if BSP_HAS_RADAR
    {
        lv_obj_t *radar_card = tesaiot_card_create(right_col,
                                                   LV_PCT(100), 72,
                                                   "Radar", UI_COLOR_SENSOR_RADAR,
                                                   &s_ctx.radar_label);
        lv_obj_set_style_pad_all(radar_card, UI_SPACE_SM, 0);
        if (s_ctx.radar_label) {
            lv_obj_set_width(s_ctx.radar_label, LV_PCT(100));
            lv_label_set_long_mode(s_ctx.radar_label, LV_LABEL_LONG_WRAP);
        }
    }
#endif

    {
        lv_obj_t *joy_card = tesaiot_card_create(right_col,
                                                 LV_PCT(100), 62,
                                                 "Joystick", UI_COLOR_SENSOR_JOY,
                                                 &s_ctx.joy_label);
        lv_obj_set_style_pad_all(joy_card, UI_SPACE_SM, 0);
        if (s_ctx.joy_label) {
            lv_obj_set_width(s_ctx.joy_label, LV_PCT(100));
            lv_label_set_long_mode(s_ctx.joy_label, LV_LABEL_LONG_DOT);
        }
    }

    return scr;
}

/*******************************************************************************
 * page_dashboard_render
 *******************************************************************************/
void page_dashboard_render(sensorhub_snapshot_t *snap)
{
    if (snap == NULL) return;

    bool any_update = false;

    /* BMI270 — Accelerometer */
    if (snap->has_bmi270 && snap->bmi270_changed && s_ctx.imu_label) {
        double ax = (double)snap->bmi270.ax / IPC_BMI270_ACCEL_LSB_PER_G * 9.81;
        double ay = (double)snap->bmi270.ay / IPC_BMI270_ACCEL_LSB_PER_G * 9.81;
        double az = (double)snap->bmi270.az / IPC_BMI270_ACCEL_LSB_PER_G * 9.81;
        snprintf(s_imu_buf, sizeof(s_imu_buf),
                 "Accel (m/s^2)\n"
                 "X:%+5.2f Y:%+5.2f Z:%+5.2f",
                 ax, ay, az);
        lv_label_set_text(s_ctx.imu_label, s_imu_buf);

        if (s_ctx.motion_accel_chart && s_ctx.ax_series && s_ctx.ay_series && s_ctx.az_series) {
            int32_t ax_val = (int32_t)((float)snap->bmi270.ax / IPC_BMI270_ACCEL_LSB_PER_G * 981.0f);
            int32_t ay_val = (int32_t)((float)snap->bmi270.ay / IPC_BMI270_ACCEL_LSB_PER_G * 981.0f);
            int32_t az_val = (int32_t)((float)snap->bmi270.az / IPC_BMI270_ACCEL_LSB_PER_G * 981.0f);
            lv_chart_set_next_value(s_ctx.motion_accel_chart, s_ctx.ax_series, ax_val);
            lv_chart_set_next_value(s_ctx.motion_accel_chart, s_ctx.ay_series, ay_val);
            lv_chart_set_next_value(s_ctx.motion_accel_chart, s_ctx.az_series, az_val);
            autoscale_chart(s_ctx.motion_accel_chart, s_ctx.ax_series, s_ctx.ay_series, s_ctx.az_series, 180, &s_accel_range_lo, &s_accel_range_hi);
        }

        if (s_ctx.motion_gyro_chart && s_ctx.gx_series && s_ctx.gy_series && s_ctx.gz_series) {
            int32_t gx_val = (int32_t)((float)snap->bmi270.gx / 16.4f * 10.0f);
            int32_t gy_val = (int32_t)((float)snap->bmi270.gy / 16.4f * 10.0f);
            int32_t gz_val = (int32_t)((float)snap->bmi270.gz / 16.4f * 10.0f);
            lv_chart_set_next_value(s_ctx.motion_gyro_chart, s_ctx.gx_series, gx_val);
            lv_chart_set_next_value(s_ctx.motion_gyro_chart, s_ctx.gy_series, gy_val);
            lv_chart_set_next_value(s_ctx.motion_gyro_chart, s_ctx.gz_series, gz_val);
            autoscale_chart(s_ctx.motion_gyro_chart, s_ctx.gx_series, s_ctx.gy_series, s_ctx.gz_series, 100, &s_gyro_range_lo, &s_gyro_range_hi);
        }
        any_update = true;
    }

#if BSP_HAS_DPS368
    if (snap->has_dps368 && snap->dps368_changed && s_ctx.baro_label) {
        double pressure = (double)snap->dps368.pressure_x100 / 100.0;
        double temp     = (double)snap->dps368.temperature_x100 / 100.0;
        double rh       = 50.0;
#if BSP_HAS_SHT40
        if (snap->has_sht40) {
            rh = (double)snap->sht40.humidity_x100 / 100.0;
        }
#endif

        double altitude = 44330.0 * (1.0 - pow(pressure / 1013.25, 0.190295));
        if (rh < 1.0) rh = 1.0;
        const double a = 17.27;
        const double b = 237.7;
        double gamma = (a * temp) / (b + temp) + log(rh / 100.0);
        double dew_point = (b * gamma) / (a - gamma);

        double heat_index_c = temp;
        double tf = temp * 9.0 / 5.0 + 32.0;
        if (tf >= 80.0 && rh >= 40.0) {
            double hi_f = -42.379
                        + 2.04901523  * tf
                        + 10.14333127 * rh
                        - 0.22475541  * tf * rh
                        - 0.00683783  * tf * tf
                        - 0.05481717  * rh * rh
                        + 0.00122874  * tf * tf * rh
                        + 0.00085282  * tf * rh * rh
                        - 0.00000199  * tf * tf * rh * rh;
            heat_index_c = (hi_f - 32.0) * 5.0 / 9.0;
        }

        const char *comfort;
        if (temp < 18.0) comfort = "Cold";
        else if (temp > 27.0) comfort = "Hot";
        else if (rh < 30.0) comfort = "Dry";
        else if (rh > 70.0) comfort = "Humid";
        else if (temp >= 20.0 && temp <= 25.0 && rh >= 30.0 && rh <= 60.0) comfort = "Comfort";
        else comfort = "OK";

        snprintf(s_baro_buf, sizeof(s_baro_buf),
                 "P: %.1f hPa  T: %.1f C\n"
                 "Alt: %.0f m  Dew: %.1f C\n"
                 "HI: %.1f C  %s",
                 pressure, temp, altitude, dew_point, heat_index_c, comfort);
        lv_label_set_text(s_ctx.baro_label, s_baro_buf);
        any_update = true;
    }
#endif

#if BSP_HAS_SHT40
    if (snap->has_sht40 && snap->sht40_changed && s_ctx.env_label) {
        double temp = (double)snap->sht40.temperature_x100 / 100.0;
        double hum  = (double)snap->sht40.humidity_x100 / 100.0;
        snprintf(s_env_buf, sizeof(s_env_buf),
                 "Temp:     %.1f C\n"
                 "Humidity: %.1f %%RH",
                 temp, hum);
        lv_label_set_text(s_ctx.env_label, s_env_buf);
        any_update = true;
    }
#endif

#if BSP_HAS_BMM350
    if (snap->has_bmm350 && snap->bmm350_changed && s_ctx.compass_scale) {
        int heading_deg = (int)(snap->bmm350.heading_x10 / 10);
        heading_deg %= 360;
        if (heading_deg < 0) heading_deg += 360;

        /* Skip rotation if heading changed < 2° — avoids full dial redraw */
        int delta = abs(heading_deg - s_heading_prev);
        if (delta > 180) delta = 360 - delta;  /* wrap-around */
        if (s_heading_prev < 0 || delta >= 2) {
            tesaiot_compass_apple_set_heading(s_ctx.compass_scale,
                                               s_ctx.heading_label,
                                               heading_deg);
            s_heading_prev = heading_deg;
            any_update = true;
        }
    }
#endif

#if BSP_HAS_CAPSENSE
    if (snap->has_capsense && snap->capsense_changed && s_ctx.controls_label) {
        snprintf(s_controls_buf, sizeof(s_controls_buf),
                 "Btn0: %s  Btn1: %s\n"
                 "Slider: %u%%",
                 snap->capsense.btn0_pressed ? "ON" : "OFF",
                 snap->capsense.btn1_pressed ? "ON" : "OFF",
                 (unsigned)snap->capsense.slider);
        lv_label_set_text(s_ctx.controls_label, s_controls_buf);
        any_update = true;
    }
#endif

    /* Joystick — USB HID (CM55 local), string-cached to avoid redundant redraws */
    if (s_ctx.joy_label) {
        const joystick_state_t *joy = usb_hid_joystick_get_state();
        if (joy->connected) {
            snprintf(s_joy_buf, sizeof(s_joy_buf),
                     "Connected  LX:%u LY:%u",
                     (unsigned)joy->report.left_x,
                     (unsigned)joy->report.left_y);
        } else if (joy->usb_init_done) {
            snprintf(s_joy_buf, sizeof(s_joy_buf), "Waiting for device...");
        } else {
            snprintf(s_joy_buf, sizeof(s_joy_buf), "USB Host: NOT init");
        }
        if (strcmp(s_joy_buf, s_joy_prev) != 0) {
            lv_label_set_text(s_ctx.joy_label, s_joy_buf);
            memcpy(s_joy_prev, s_joy_buf, sizeof(s_joy_prev));
            any_update = true;
        }
    }

#if BSP_HAS_RADAR
    /* Radar — global volatiles, string-cached to avoid redundant redraws */
    if (tesaiot_radar_initialized && s_ctx.radar_label) {
        snprintf(s_radar_buf, sizeof(s_radar_buf),
                 "Presence: %s\nEnergy: %.0f",
                 tesaiot_radar_presence_detected ? "DETECTED" : "ABSENT",
                 (double)tesaiot_radar_current_energy);
        if (strcmp(s_radar_buf, s_radar_prev) != 0) {
            lv_label_set_text(s_ctx.radar_label, s_radar_buf);
            memcpy(s_radar_prev, s_radar_buf, sizeof(s_radar_prev));
            any_update = true;
        }
    }
#endif

    if (any_update) {
        s_ctx.update_count++;
        snprintf(s_status_buf, sizeof(s_status_buf),
                 "Updates: %lu", (unsigned long)s_ctx.update_count);
        lv_label_set_text(s_ctx.status_label, s_status_buf);
    }
}

/*******************************************************************************
 * page_dashboard_destroy
 *******************************************************************************/
void page_dashboard_destroy(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));
}
