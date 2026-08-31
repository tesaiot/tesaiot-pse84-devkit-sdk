#include "smartwatch_round_embed.h"
#include "ui.h"
#include "ui_events.h"
#include "ui_embed_port.h"
#include "cy_rtc.h"
#include <math.h>
#include <string.h>
#include <time.h>

/* Brightness state used by original generated callbacks (round GUI). */
volatile uint8_t ui_demo_watch_brightness = 80U;

typedef struct {
    bool     mounted;
    bool     has_last_norm;
    float    last_norm;
    uint32_t step_count;
    uint32_t tick;
    uint32_t step_ui_tick;
    uint32_t step_ui_value;
    time_t   last_time_second;
    time_t   clock_ts;
    uint32_t clock_ms_ref;
} watch_embed_ctx_t;

static watch_embed_ctx_t s_ctx;

static time_t watch_default_seed(void)
{
    /* 2026-10-30 00:00:00 UTC */
    return (time_t)1793318400;
}

static void watch_update_live_clock_widgets(const struct tm *tm_now)
{
    if (tm_now == NULL) return;

    if (ui_DigiScreenClockDots) {
        lv_obj_set_style_opa(ui_DigiScreenClockDots,
                             (tm_now->tm_sec % 2) ? LV_OPA_30 : LV_OPA_100,
                             LV_PART_MAIN);
    }

    if (ui_AnalogScreenSecHand) {
        int16_t sec_angle = (int16_t)((tm_now->tm_sec % 60) * 60);
        lv_image_set_rotation(ui_AnalogScreenSecHand, sec_angle);
    }

    if (ui_AnalogScreenMinHand) {
        int16_t min_angle = (int16_t)(((tm_now->tm_min % 60) * 60) + (tm_now->tm_sec % 60));
        lv_image_set_rotation(ui_AnalogScreenMinHand, min_angle);
    }

    if (ui_AnalogScreenHourHand) {
        int hour12 = tm_now->tm_hour % 12;
        int16_t hour_angle = (int16_t)((hour12 * 300) + ((tm_now->tm_min % 60) * 5));
        lv_image_set_rotation(ui_AnalogScreenHourHand, hour_angle);
    }
}

static lv_obj_t *watch_target_for_gesture(lv_obj_t *active, lv_dir_t dir)
{
    if (active == ui_DigitalScreen) {
        if (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT) return ui_AnalogScreen;
        if (dir == LV_DIR_TOP) return ui_HealthScreen;
        if (dir == LV_DIR_BOTTOM) return ui_WeatherScreen1;
        return NULL;
    }

    if (active == ui_AnalogScreen) {
        if (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT) return ui_DigitalScreen;
        if (dir == LV_DIR_TOP) return ui_HealthScreen;
        if (dir == LV_DIR_BOTTOM) return ui_WeatherScreen1;
        return NULL;
    }

    if (active == ui_HealthScreen) {
        if (dir == LV_DIR_TOP) return ui_MusicScreen;
        if (dir == LV_DIR_BOTTOM) return ui_DigitalScreen;
        if (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT) return ui_AnalogScreen;
        return NULL;
    }

    if (active == ui_MusicScreen) {
        if (dir == LV_DIR_BOTTOM) return ui_HealthScreen;
        if (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT || dir == LV_DIR_TOP) return ui_WeatherScreen1;
        return NULL;
    }

    if (active == ui_WeatherScreen1) {
        if (dir == LV_DIR_LEFT) return ui_WeatherScreen2;
        if (dir == LV_DIR_RIGHT || dir == LV_DIR_TOP || dir == LV_DIR_BOTTOM) return ui_DigitalScreen;
        return NULL;
    }

    if (active == ui_WeatherScreen2) {
        if (dir == LV_DIR_RIGHT) return ui_WeatherScreen1;
        if (dir == LV_DIR_LEFT || dir == LV_DIR_TOP || dir == LV_DIR_BOTTOM) return ui_DigitalScreen;
        return NULL;
    }

    return NULL;
}

void smartwatch_round_embed_swipe(lv_dir_t dir)
{
    lv_obj_t *active = ui_embed_port_get_active_root();
    lv_obj_t *target = watch_target_for_gesture(active, dir);
    if (target == NULL) return;
    ui_embed_port_show(target);
    lv_obj_invalidate(target);
}

static uint32_t watch_estimate_steps(sensorhub_snapshot_t *snap)
{
    if (snap && snap->has_bmi270 && snap->bmi270_changed) {
        float ax = (float)snap->bmi270.ax / IPC_BMI270_ACCEL_LSB_PER_G;
        float ay = (float)snap->bmi270.ay / IPC_BMI270_ACCEL_LSB_PER_G;
        float az = (float)snap->bmi270.az / IPC_BMI270_ACCEL_LSB_PER_G;
        float norm = sqrtf((ax * ax) + (ay * ay) + (az * az));

        if (!s_ctx.has_last_norm) {
            s_ctx.last_norm = norm;
            s_ctx.has_last_norm = true;
            return 0U;
        }

        if (fabsf(norm - s_ctx.last_norm) > 0.08f) {
            s_ctx.last_norm = norm;
            return 1U;
        }

        s_ctx.last_norm = norm;
        return 0U;
    }

    if (snap && snap->has_pot && (snap->pot.percent_x10 > 800U)) {
        return 1U;
    }

    return 0U;
}

static void watch_publish_time(time_t now_ts)
{
    struct tm *tm_now = localtime(&now_ts);
    if (tm_now == NULL) return;

    int full_year = tm_now->tm_year + 1900;
    if (full_year < 2000 || full_year > 2099) {
        full_year = 2026;
    }

    cy_stc_rtc_config_t rtc = {0};
    rtc.sec = (uint32_t)tm_now->tm_sec;
    rtc.min = (uint32_t)tm_now->tm_min;
    rtc.hour = (uint32_t)tm_now->tm_hour;
    rtc.date = (uint32_t)tm_now->tm_mday;
    rtc.month = (uint32_t)(tm_now->tm_mon + 1);
    rtc.year = (uint32_t)(full_year - 2000);
    rtc.dayOfWeek = (uint32_t)(tm_now->tm_wday + 1);
    ui_time_cb(rtc);
    watch_update_live_clock_widgets(tm_now);
}

void smartwatch_round_embed_mount(lv_obj_t *host)
{
    if (host == NULL) return;

    memset(&s_ctx, 0, sizeof(s_ctx));
    lv_obj_add_flag(host, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(host, LV_OBJ_FLAG_SCROLLABLE);
    ui_embed_port_set_parent(host);
    ui_init();
    /* Try reading hardware RTC (set by NTP after WiFi connect).
     * Fall back to demo default if RTC hasn't been synced yet. */
    time_t seed = watch_default_seed();
    {
        cy_stc_rtc_config_t rtc;
        Cy_RTC_GetDateAndTime(&rtc);
        if (rtc.year >= 25 && rtc.month >= 1 && rtc.month <= 12) {
            struct tm t;
            memset(&t, 0, sizeof(t));
            t.tm_sec  = (int)rtc.sec;
            t.tm_min  = (int)rtc.min;
            t.tm_hour = (int)rtc.hour;
            t.tm_mday = (int)rtc.date;
            t.tm_mon  = (int)(rtc.month - 1);
            t.tm_year = (int)(rtc.year + 2000 - 1900);
            t.tm_isdst = -1;
            time_t real_ts = mktime(&t);
            if (real_ts > 1700000000) {  /* After Nov 2023 → valid */
                seed = real_ts;
            }
        }
    }
    s_ctx.clock_ts = seed;
    s_ctx.clock_ms_ref = lv_tick_get();
    s_ctx.last_time_second = (time_t)-1;
    s_ctx.mounted = true;
}

void smartwatch_round_embed_tick(sensorhub_snapshot_t *snap)
{
    if (!s_ctx.mounted) return;

    s_ctx.tick++;

    uint32_t now_ms = lv_tick_get();
    uint32_t elapsed_ms = now_ms - s_ctx.clock_ms_ref;
    if (elapsed_ms >= 1000U) {
        uint32_t advance = elapsed_ms / 1000U;
        s_ctx.clock_ms_ref += (advance * 1000U);
        s_ctx.clock_ts += (time_t)advance;
    }

    /* Re-sync from hardware RTC every 60 seconds (catches NTP updates) */
    if ((s_ctx.tick % (60 * 30)) == 0) {  /* ~60s at 30fps */
        cy_stc_rtc_config_t rtc;
        Cy_RTC_GetDateAndTime(&rtc);
        if (rtc.year >= 25 && rtc.month >= 1 && rtc.month <= 12) {
            struct tm t;
            memset(&t, 0, sizeof(t));
            t.tm_sec  = (int)rtc.sec;
            t.tm_min  = (int)rtc.min;
            t.tm_hour = (int)rtc.hour;
            t.tm_mday = (int)rtc.date;
            t.tm_mon  = (int)(rtc.month - 1);
            t.tm_year = (int)(rtc.year + 2000 - 1900);
            t.tm_isdst = -1;
            time_t real_ts = mktime(&t);
            if (real_ts > 1700000000) {
                s_ctx.clock_ts = real_ts;
                s_ctx.clock_ms_ref = now_ms;
            }
        }
    }

    if (s_ctx.clock_ts != s_ctx.last_time_second) {
        s_ctx.last_time_second = s_ctx.clock_ts;
        watch_publish_time(s_ctx.clock_ts);
        if ((s_ctx.clock_ts % 4) == 0) {
            ui_health_screen_cb();
        }
    }

    s_ctx.step_count += watch_estimate_steps(snap);
    if (s_ctx.step_count != s_ctx.step_ui_value &&
        (s_ctx.tick - s_ctx.step_ui_tick) >= 6U) {
        s_ctx.step_ui_value = s_ctx.step_count;
        s_ctx.step_ui_tick = s_ctx.tick;
        ui_step_cb(s_ctx.step_count);
    }
}

void smartwatch_round_embed_unmount(void)
{
    if (!s_ctx.mounted) return;

    ui_destroy();
    ui_embed_port_reset();
    memset(&s_ctx, 0, sizeof(s_ctx));
}
