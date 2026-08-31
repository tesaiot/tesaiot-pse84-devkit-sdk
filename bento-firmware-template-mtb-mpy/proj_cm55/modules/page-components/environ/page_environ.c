/*******************************************************************************
 * File Name: page_environ.c
 *
 * Description: Environment page — DPS368 + SHT40 arc gauges + derived metrics.
 *              AI Kit only (BSP_HAS_DPS368 || BSP_HAS_SHT40).
 *              Page-based conversion from tab_environ.c.
 *
 *              Left panel: DPS368 barometric pressure arc gauge with
 *                          computed altitude, dew point, heat index.
 *              Right panel: SHT40 temperature + humidity arc gauges.
 *
 *******************************************************************************/

#include "page_environ.h"

#if BSP_HAS_DPS368 || BSP_HAS_SHT40

#include "page_manager.h"
#include "tesaiot_ui_theme.h"
#include "tesaiot_ui_helpers.h"
#include "ipc_sensorhub.h"
#include "lvgl.h"
#include <stdio.h>
#include <math.h>

/*******************************************************************************
 * Module-Static Context
 *******************************************************************************/
typedef struct {
    lv_obj_t *press_arc;
    lv_obj_t *press_label;
    lv_obj_t *temp_arc;
    lv_obj_t *temp_label;
    lv_obj_t *hum_arc;
    lv_obj_t *hum_label;
    lv_obj_t *detail_label;
} page_environ_ctx_t;

static page_environ_ctx_t s_ctx;

/*******************************************************************************
 * Static text buffers
 *******************************************************************************/
static char s_press_buf[32];
static char s_temp_buf[32];
static char s_hum_buf[32];
static char s_detail_buf[256];

/*******************************************************************************
 * page_environ_create
 *******************************************************************************/
lv_obj_t *page_environ_create(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));

    lv_obj_t *scr = lv_obj_create(NULL);
    page_manager_t *pm = pm_get_instance();
    lv_obj_t *content = pm_create_page_with_header(scr, pm,
                            "Environment", UI_COLOR_SENSOR_BARO);

    /* Main flex row: phi split 480 / 290 */
    lv_obj_t *row = tesaiot_row_create(content, UI_SPACE_MD);

    /***************************************************************************
     * Left panel — DPS368 Barometric (phi major ~480px)
     ***************************************************************************/
#if BSP_HAS_DPS368
    {
        lv_obj_t *left = tesaiot_col_create(row, UI_SPACE_SM);
        lv_obj_set_width(left, 480);

        lv_obj_t *title_baro = lv_label_create(left);
        lv_label_set_text(title_baro, "DPS368 Barometric");
        lv_obj_set_style_text_color(title_baro,
                                    lv_color_hex(UI_COLOR_SENSOR_BARO),
                                    LV_PART_MAIN);
        lv_obj_set_style_text_font(title_baro, UI_FONT_H3, LV_PART_MAIN);

        lv_obj_t *gauge_row = tesaiot_row_create(left, UI_SPACE_MD);
        tesaiot_arc_gauge_create(gauge_row, UI_ARC_SIZE,
                                 900, 1100,
                                 UI_COLOR_SENSOR_BARO,
                                 &s_ctx.press_arc,
                                 &s_ctx.press_label);

        lv_obj_t *detail_col = tesaiot_col_create(gauge_row, UI_SPACE_XS);
        lv_obj_set_flex_grow(detail_col, 1);

        s_ctx.detail_label = lv_label_create(detail_col);
        lv_label_set_text(s_ctx.detail_label,
                          "Temp:       --- C\n"
                          "Altitude:   --- m\n"
                          "Dew Point:  --- C\n"
                          "Heat Index: --- C\n"
                          "Comfort:    ---");
        lv_obj_set_style_text_color(s_ctx.detail_label,
                                    lv_color_hex(UI_COLOR_TEXT_SECONDARY),
                                    LV_PART_MAIN);
        lv_obj_set_style_text_font(s_ctx.detail_label, UI_FONT_CAPTION,
                                   LV_PART_MAIN);
    }
#endif

    /***************************************************************************
     * Right panel — SHT40 Climate (phi minor ~290px)
     ***************************************************************************/
#if BSP_HAS_SHT40
    {
        lv_obj_t *right = tesaiot_col_create(row, UI_SPACE_SM);
        lv_obj_set_width(right, 290);

        lv_obj_t *title_env = lv_label_create(right);
        lv_label_set_text(title_env, "SHT40 Climate");
        lv_obj_set_style_text_color(title_env,
                                    lv_color_hex(UI_COLOR_SENSOR_ENV),
                                    LV_PART_MAIN);
        lv_obj_set_style_text_font(title_env, UI_FONT_H3, LV_PART_MAIN);

        tesaiot_arc_gauge_create(right, UI_ARC_SIZE,
                                 -10, 50,
                                 UI_COLOR_SENSOR_ENV,
                                 &s_ctx.temp_arc,
                                 &s_ctx.temp_label);

        tesaiot_arc_gauge_create(right, UI_ARC_SIZE,
                                 0, 100,
                                 UI_COLOR_ACCENT_BLUE,
                                 &s_ctx.hum_arc,
                                 &s_ctx.hum_label);
    }
#endif

    return scr;
}

/*******************************************************************************
 * page_environ_render
 *******************************************************************************/
void page_environ_render(sensorhub_snapshot_t *snap)
{
    if (snap == NULL) return;

    float dps_temp  = 0.0f;
    float dps_press = 0.0f;
    float sht_temp  = 0.0f;
    float sht_hum   = 0.0f;
    bool  have_dps  = false;
    bool  have_sht  = false;

#if BSP_HAS_DPS368
    if (snap->has_dps368 && snap->dps368_changed && s_ctx.press_arc) {
        dps_press = (float)snap->dps368.pressure_x100 / 100.0f;
        dps_temp  = (float)snap->dps368.temperature_x100 / 100.0f;
        have_dps  = true;

        lv_arc_set_value(s_ctx.press_arc, (int)(dps_press * 1));

        snprintf(s_press_buf, sizeof(s_press_buf),
                 "%.1f hPa", (double)dps_press);
        lv_label_set_text(s_ctx.press_label, s_press_buf);
    }
#endif

#if BSP_HAS_SHT40
    if (snap->has_sht40 && snap->sht40_changed) {
        sht_temp = (float)snap->sht40.temperature_x100 / 100.0f;
        sht_hum  = (float)snap->sht40.humidity_x100 / 100.0f;
        have_sht = true;

        if (s_ctx.temp_arc) {
            lv_arc_set_value(s_ctx.temp_arc, (int)sht_temp);
            snprintf(s_temp_buf, sizeof(s_temp_buf),
                     "%.1f C", (double)sht_temp);
            lv_label_set_text(s_ctx.temp_label, s_temp_buf);
        }

        if (s_ctx.hum_arc) {
            lv_arc_set_value(s_ctx.hum_arc, (int)sht_hum);
            snprintf(s_hum_buf, sizeof(s_hum_buf),
                     "%.1f%%", (double)sht_hum);
            lv_label_set_text(s_ctx.hum_label, s_hum_buf);
        }
    }
#endif

#if BSP_HAS_DPS368
    if (have_dps && s_ctx.detail_label) {
        float t  = have_sht ? sht_temp : dps_temp;
        float rh = have_sht ? sht_hum  : 50.0f;

        float altitude = 44330.0f * (1.0f - powf(dps_press / 1013.25f,
                                                  0.190295f));

        float dew_point = 0.0f;
        {
            const float a = 17.27f;
            const float b = 237.7f;
            float rh_clamped = rh;
            if (rh_clamped < 1.0f) rh_clamped = 1.0f;
            float gamma = (a * t) / (b + t) + logf(rh_clamped / 100.0f);
            dew_point = (b * gamma) / (a - gamma);
        }

        float heat_index_c = t;
        {
            float tf = t * 9.0f / 5.0f + 32.0f;
            if (tf >= 80.0f && rh >= 40.0f) {
                float hi_f = -42.379f
                           +  2.04901523f  * tf
                           + 10.14333127f  * rh
                           -  0.22475541f  * tf * rh
                           -  0.00683783f  * tf * tf
                           -  0.05481717f  * rh * rh
                           +  0.00122874f  * tf * tf * rh
                           +  0.00085282f  * tf * rh * rh
                           -  0.00000199f  * tf * tf * rh * rh;
                heat_index_c = (hi_f - 32.0f) * 5.0f / 9.0f;
            }
        }

        const char *comfort;
        if (t < 18.0f)       comfort = "Cold";
        else if (t > 27.0f)  comfort = "Hot";
        else if (rh < 30.0f) comfort = "Dry";
        else if (rh > 70.0f) comfort = "Humid";
        else if (t >= 20.0f && t <= 25.0f && rh >= 30.0f && rh <= 60.0f)
            comfort = "Comfortable";
        else comfort = "Acceptable";

        snprintf(s_detail_buf, sizeof(s_detail_buf),
                 "Temp:       %.1f C\n"
                 "Altitude:   %.0f m\n"
                 "Dew Point:  %.1f C\n"
                 "Heat Index: %.1f C\n"
                 "Comfort:    %s",
                 (double)dps_temp, (double)altitude,
                 (double)dew_point, (double)heat_index_c, comfort);
        lv_label_set_text(s_ctx.detail_label, s_detail_buf);
    }
#endif

    (void)dps_temp;
    (void)dps_press;
    (void)sht_temp;
    (void)sht_hum;
    (void)have_dps;
    (void)have_sht;
}

/*******************************************************************************
 * page_environ_destroy
 *******************************************************************************/
void page_environ_destroy(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));
}

#endif /* BSP_HAS_DPS368 || BSP_HAS_SHT40 */
