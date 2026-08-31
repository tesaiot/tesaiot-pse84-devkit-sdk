/*******************************************************************************
 * File Name: sensorhub_ui.c
 *
 * Description: TESAIoT Sensor Hub — Page Manager Orchestrator.
 *              Replaces TabView (7 tabs, ~140 widgets) with page-based
 *              navigation: Home card grid + per-page create/destroy.
 *
 *              Boots to Home card grid. Tap card -> full-screen page.
 *              Back button -> return to Home. Create-on-demand, destroy-
 *              on-leave for minimal LVGL heap usage (~15KB idle).
 *
 *              33ms LVGL timer polls IPC snapshot and renders active page
 *              with dirty-flag optimization (only update on data change).
 *
 *              Uses Golden Ratio design system from tesaiot_ui_theme.h.
 *
 *******************************************************************************/

#include "sensorhub_ui.h"
#include "page_manager.h"
#include "tesaiot_ui_theme.h"
#include "tesaiot_ui_helpers.h"
#include "ipc_sensorhub.h"
#include "bsp_feature_flags.h"
#include "cy_rtc.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

/* Core page headers (always compiled) */
#include "page_home.h"
#include "page_dashboard.h"
#include "page_playground.h"

/* Optional page headers (gated by ENABLE_PAGE_*) */
#if ENABLE_PAGE_MOTION
#include "page_motion.h"
#endif
#if ENABLE_PAGE_ENVIRON
#include "page_environ.h"
#endif
#if ENABLE_PAGE_CONTROLS
#include "page_controls.h"
#endif
#if ENABLE_PAGE_GPIO_RGB
#include "page_gpio_rgb.h"
#endif
#if ENABLE_PAGE_MOTOR_CTRL
#include "page_motor_ctrl.h"
#endif
#if ENABLE_PAGE_JOYSTICK
#include "page_joystick.h"
#endif
#if ENABLE_PAGE_FACE_ID
#include "page_face_identification.h"
#endif
#if ENABLE_PAGE_SMART_WATCH
#include "page_smart_watch.h"
#endif
#if ENABLE_PAGE_SPECTRUM
#include "page_spectrum_analyzer.h"
#endif
#if ENABLE_PAGE_WIFI_CONNECT
#include "page_wifi_connect.h"
#endif
#if ENABLE_PAGE_HSM
#include "page_hsm.h"
#endif
#if ENABLE_PAGE_BENTOCLAW
#include "page_bentoclaw.h"
#endif
#if ENABLE_PAGE_BENTO_BUDDY
#include "page_bento_buddy.h"
#endif
#if ENABLE_PAGE_SMART_CARD
#include "page_smart_card.h"
#endif
#if ENABLE_PAGE_ANIMATION
#include "page_animation.h"
#endif
#if ENABLE_PAGE_GAME_SNAKE
#include "page_game_snake.h"
#endif
#if ENABLE_PAGE_GAME_FLAPPY
#include "page_game_flappy.h"
#endif
#if ENABLE_PAGE_GAME_PONG
#include "page_game_pong.h"
#endif
#if ENABLE_PAGE_GAME_SHOOTER
#include "page_game_shooter.h"
#endif
#if ENABLE_PAGE_TESAIOT_CONNECT
#include "page_tesaiot_connect.h"
#endif
#if defined(BENTO_HAS_EDGE_AI) && (BENTO_HAS_EDGE_AI == 1)
#include "page_edge_ai.h"
#endif

/*******************************************************************************
 * Static Page Manager Instance
 *******************************************************************************/
static page_manager_t s_pm;

/*******************************************************************************
 * Global Topbar Update: Time + WiFi icon (runs every ~6fps = every 5 ticks)
 *******************************************************************************/
static void pm_update_topbar(page_manager_t *pm)
{
    static uint8_t skip = 0;
    if (++skip < 5) return;       /* ~6fps at 33ms tick */
    skip = 0;

    //! [ipc_sensorhub_wifi_edge_detect]
    /* ...context: inside pm_update_topbar() - GFX task context ... */
    /* WiFi icon — only toggle visibility when state actually changes
     * to prevent unnecessary LVGL invalidation (card row flicker). */
    if (pm->wifi_lbl) {
        bool connected = ipc_sensorhub_wifi_connected();
        bool hidden = lv_obj_has_flag(pm->wifi_lbl, LV_OBJ_FLAG_HIDDEN);
        if (connected && hidden) {
            lv_obj_clear_flag(pm->wifi_lbl, LV_OBJ_FLAG_HIDDEN);
        } else if (!connected && !hidden) {
            lv_obj_add_flag(pm->wifi_lbl, LV_OBJ_FLAG_HIDDEN);
        }
    }
    //! [ipc_sensorhub_wifi_edge_detect]

    //! [ipc_sensorhub_ntp_rtc_gate]
    /* ...context: inside pm_update_topbar() - GFX task context ... */
    /* RTC time display — only after NTP sync notification from CM33_NS */
    if (pm->time_lbl && ipc_sensorhub_ntp_synced()) {
        static const char * const dow[] = {
            "", "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
        };
        static const char * const mon[] = {
            "", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
        };
        cy_stc_rtc_config_t rtc;
        Cy_RTC_GetDateAndTime(&rtc);
        if (rtc.month >= 1 && rtc.month <= 12 &&
            rtc.dayOfWeek >= 1 && rtc.dayOfWeek <= 7) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%s %d %s %02d:%02d",
                     dow[rtc.dayOfWeek], (int)rtc.date,
                     mon[rtc.month], (int)rtc.hour, (int)rtc.min);
            /* Only invalidate if text actually changed (prevents card row flicker) */
            if (strcmp(lv_label_get_text(pm->time_lbl), buf) != 0) {
                lv_label_set_text(pm->time_lbl, buf);
            }
            //! [ipc_sensorhub_ntp_rtc_gate]
            if (lv_obj_has_flag(pm->time_lbl, LV_OBJ_FLAG_HIDDEN)) {
                lv_obj_clear_flag(pm->time_lbl, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

/*******************************************************************************
 * Timer Callback: Poll IPC snapshot and render active page (33ms)
 *******************************************************************************/
//! [ipc_sensorhub_snapshot_tick]
static void sensorhub_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    /* Take snapshot of all sensor data from IPC */
    sensorhub_snapshot_t snap;
    ipc_sensorhub_snapshot(&snap);

    /* Dispatch render to current page (skips if animating) */
    pm_render(&s_pm, &snap);

    /* Update global topbar (time + WiFi) on current page */
    if (!s_pm.animating) {
        pm_update_topbar(&s_pm);
    }
}
//! [ipc_sensorhub_snapshot_tick]

/*******************************************************************************
 * Public API
 *******************************************************************************/

bool sensorhub_ui_init(void *parent)
{
    (void)parent;

    /* Initialize shared styles + page manager */
    tesaiot_ui_styles_init();
    pm_init(&s_pm);
    pm_set_instance(&s_pm);

    /* Set bottom_layer bg to match dark theme — prevents white flash
       during screen transition animations (MOVE_LEFT exposes the gap) */
    lv_obj_t *bottom_layer = lv_display_get_layer_bottom(NULL);
    lv_obj_set_style_bg_color(bottom_layer, lv_color_hex(UI_COLOR_BG_DEEP), 0);

    /***************************************************************************
     * Register all pages (BSP-conditional)
     ***************************************************************************/

    /* Home card grid (always present, boot screen) */
    {
        page_def_t def = {
            .name         = "Home",
            .subtitle     = "Smart Menu",
            .accent_color = UI_COLOR_ACCENT_CYAN,
            .create_cb    = page_home_create,
            .render_cb    = page_home_render,
            .destroy_cb   = page_home_destroy,
        };
        pm_register(&s_pm, PAGE_ID_HOME, &def);
    }

    /* Dashboard overview (all sensors) */
    {
        page_def_t def = {
            .name         = "Sensor Dashboard",
            .subtitle     = "All Sensors + Motion Trends",
            .accent_color = UI_COLOR_SENSOR_IMU,
            .create_cb    = page_dashboard_create,
            .render_cb    = page_dashboard_render,
            .destroy_cb   = page_dashboard_destroy,
        };
        pm_register(&s_pm, PAGE_ID_DASHBOARD, &def);
    }

    /* ===== Optional pages (gated by ENABLE_PAGE_*) ===== */

#if ENABLE_PAGE_MOTION
    /* Motion (BMI270 + BMM350) */
    {
        page_def_t def = {
            .name         = "Motion",
            .subtitle     = "Accel + Gyro + Compass",
            .accent_color = UI_COLOR_SENSOR_IMU,
            .create_cb    = page_motion_create,
            .render_cb    = page_motion_render,
            .destroy_cb   = page_motion_destroy,
        };
        pm_register(&s_pm, PAGE_ID_MOTION, &def);
    }
#endif

#if ENABLE_PAGE_ENVIRON
    /* Environment (DPS368 + SHT40 — AI Kit only) */
    {
        page_def_t def = {
            .name         = "Environ",
            .subtitle     = "Pressure + Temp + Humidity",
            .accent_color = UI_COLOR_SENSOR_BARO,
            .create_cb    = page_environ_create,
            .render_cb    = page_environ_render,
            .destroy_cb   = page_environ_destroy,
        };
        pm_register(&s_pm, PAGE_ID_ENVIRON, &def);
    }
#endif

//! [cm55_page_edge_ai_register]
/* ...context: inside sensorhub_ui_init() page registration ... */
#if defined(BENTO_HAS_EDGE_AI) && (BENTO_HAS_EDGE_AI == 1)
    /* Edge AI hub — ONE page hosting every compiled-in DEEPCRAFT model. */
    {
        page_def_t def = {
            .name         = "Edge AI",
            .subtitle     = "On-device inference",
            .accent_color = UI_COLOR_ACCENT_PURPLE,
            .create_cb    = page_edge_ai_create,
            .render_cb    = page_edge_ai_render,
            .destroy_cb   = page_edge_ai_destroy,
        };
        pm_register(&s_pm, PAGE_ID_EDGE_AI, &def);
    }
    //! [cm55_page_edge_ai_register]
#endif

#if ENABLE_PAGE_CONTROLS
    /* Controls (CapSense + Pot — Eva Kit only) */
    {
        page_def_t def = {
            .name         = "Controls",
            .subtitle     = "CapSense + Pot + LEDs",
            .accent_color = UI_COLOR_SENSOR_TOUCH,
            .create_cb    = page_controls_create,
            .render_cb    = page_controls_render,
            .destroy_cb   = page_controls_destroy,
        };
        pm_register(&s_pm, PAGE_ID_CONTROLS, &def);
    }
#endif

#if ENABLE_PAGE_GPIO_RGB
    /* GPIO & RGB Matrix (QWA309 pots + CapSense + SW5/SW6 + DFR0522 —
     * TESAIoT Dev Kit). Merges the former Potentiometers and Touch & RGB
     * pages into one I/O hub. */
    {
        page_def_t def = {
            .name         = "GPIO & RGB Matrix",
            .subtitle     = "Pots + Buttons + RGB",
            .accent_color = UI_COLOR_ACCENT_GREEN,
            .create_cb    = page_gpio_rgb_create,
            .render_cb    = page_gpio_rgb_render,
            .destroy_cb   = page_gpio_rgb_destroy,
        };
        pm_register(&s_pm, PAGE_ID_GPIO_RGB, &def);
    }
#endif

#if ENABLE_PAGE_MOTOR_CTRL
    /* Motor Controller (Adafruit Motor Shield v2 on the QWA309 Arduino
     * header). Four DC terminals or two stepper ports, all over I2C at 0x60.
     * destroy_cb stops every motor: this shield has no hardware standby and
     * reports neither stall nor thermal trip, so leaving the page is the only
     * moment we can guarantee a stop. */
    {
        page_def_t def = {
            .name         = "Motor Controller",
            .subtitle     = "DC + Stepper via Shield v2",
            .accent_color = UI_COLOR_ACCENT_ORANGE,
            .create_cb    = page_motor_ctrl_create,
            .render_cb    = page_motor_ctrl_render,
            .destroy_cb   = page_motor_ctrl_destroy,
        };
        pm_register(&s_pm, PAGE_ID_MOTOR_CTRL, &def);
    }
#endif

#if ENABLE_PAGE_JOYSTICK
    /* Joystick (F310 USB HID) */
    {
        page_def_t def = {
            .name         = "Joystick",
            .subtitle     = "USB HID Gamepad",
            .accent_color = UI_COLOR_SENSOR_JOY,
            .create_cb    = page_joystick_create,
            .render_cb    = page_joystick_render,
            .destroy_cb   = page_joystick_destroy,
        };
        pm_register(&s_pm, PAGE_ID_JOYSTICK, &def);
    }
#endif

    /* Playground (MicroPython widget sandbox + Console) */
    {
        page_def_t def = {
            .name         = "BENTO Playground",
            .subtitle     = "MicroPython + Console",
            .accent_color = UI_COLOR_ACCENT_CYAN,
            .create_cb    = page_playground_create,
            .render_cb    = page_playground_render,
            .destroy_cb   = page_playground_destroy,
            .cacheable    = true,
        };
        pm_register(&s_pm, PAGE_ID_PLAYGROUND, &def);
    }

#if ENABLE_PAGE_FACE_ID
    {
        page_def_t def = {
            .name         = "Face Identification",
            .subtitle     = "Face-ID Native Demo",
            .accent_color = UI_COLOR_ACCENT_PURPLE,
            .create_cb    = page_face_identification_create,
            .render_cb    = page_face_identification_render,
            .destroy_cb   = page_face_identification_destroy,
        };
        pm_register(&s_pm, PAGE_ID_FACE_IDENTIFICATION, &def);
    }
#endif

#if ENABLE_PAGE_SMART_WATCH
    {
        page_def_t def = {
            .name         = "Smart Watch",
            .subtitle     = "Smartwatch Native Demo",
            .accent_color = UI_COLOR_ACCENT_CYAN,
            .create_cb    = page_smart_watch_create,
            .render_cb    = page_smart_watch_render,
            .destroy_cb   = page_smart_watch_destroy,
        };
        pm_register(&s_pm, PAGE_ID_SMART_WATCH, &def);
    }
#endif

#if ENABLE_PAGE_SPECTRUM
    {
        page_def_t def = {
            .name         = "Spectrum Analyzer",
            .subtitle     = "Scope/FFT Native Demo",
            .accent_color = UI_COLOR_ACCENT_BLUE,
            .create_cb    = page_spectrum_analyzer_create,
            .render_cb    = page_spectrum_analyzer_render,
            .destroy_cb   = page_spectrum_analyzer_destroy,
        };
        pm_register(&s_pm, PAGE_ID_SPECTRUM_ANALYZER, &def);
    }
#endif

#if ENABLE_PAGE_WIFI_CONNECT
    {
        page_def_t def = {
            .name         = "Wi-Fi",
            .subtitle     = "Wi-Fi Connect",
            .accent_color = UI_COLOR_ACCENT_GREEN,
            .create_cb    = page_wifi_connect_create,
            .render_cb    = page_wifi_connect_render,
            .destroy_cb   = page_wifi_connect_destroy,
        };
        pm_register(&s_pm, PAGE_ID_WIFI_CONNECT, &def);
    }
#endif

#if ENABLE_PAGE_HSM
    /* HSM — OPTIGA Trust M Security Dashboard */
    {
        page_def_t def = {
            .name         = "HSM Security",
            .subtitle     = "OPTIGA Trust M",
            .accent_color = UI_COLOR_ACCENT_PURPLE,
            .create_cb    = page_hsm_create,
            .render_cb    = page_hsm_render,
            .destroy_cb   = page_hsm_destroy,
        };
        pm_register(&s_pm, PAGE_ID_HSM, &def);
    }
#endif

#if ENABLE_PAGE_BENTOCLAW
    /* BentoClaw Agent Status */
    {
        page_def_t def = {
            .name         = "BentoClaw",
            .subtitle     = "AI Agent Status",
            .accent_color = UI_COLOR_ACCENT_CYAN,
            .create_cb    = page_bentoclaw_create,
            .render_cb    = page_bentoclaw_render,
            .destroy_cb   = page_bentoclaw_destroy,
        };
        pm_register(&s_pm, PAGE_ID_BENTOCLAW, &def);
    }
#endif

#if ENABLE_PAGE_BENTO_BUDDY
    /* Bento Desktop Buddy — BLE NUS bridge to Bento Desktop for macOS/Windows. */
    {
        page_def_t def = {
            .name         = "Bento Buddy",
            .subtitle     = "Bento Desktop bridge",
            .accent_color = UI_COLOR_ACCENT_CYAN,
            .create_cb    = page_bento_buddy_create,
            .render_cb    = page_bento_buddy_render,
            .destroy_cb   = page_bento_buddy_destroy,
        };
        pm_register(&s_pm, PAGE_ID_BENTO_BUDDY, &def);
    }
#endif

#if ENABLE_PAGE_SMART_CARD
    /* Smart Card Reader (Thai ID) */
    {
        page_def_t def = {
            .name         = "Smart Card",
            .subtitle     = "Thai ID Reader",
            .accent_color = UI_COLOR_ACCENT_BLUE,
            .create_cb    = page_smart_card_create,
            .render_cb    = page_smart_card_render,
            .destroy_cb   = page_smart_card_destroy,
        };
        pm_register(&s_pm, PAGE_ID_SMART_CARD, &def);
    }
#endif

#if ENABLE_PAGE_ANIMATION
    /* Lottie Animation Demo */
    {
        page_def_t def = {
            .name         = "Animation",
            .subtitle     = "Lottie Demo",
            .accent_color = UI_COLOR_ACCENT_PURPLE,
            .create_cb    = page_animation_create,
            .render_cb    = page_animation_render,
            .destroy_cb   = page_animation_destroy,
        };
        pm_register(&s_pm, PAGE_ID_ANIMATION, &def);
    }
#endif

#if ENABLE_PAGE_TESAIOT_CONNECT
    /* TESAIoT Connectivity — 3-channel connectivity dashboard */
    {
        page_def_t def = {
            .name         = "TESAIoT Connectivity",
            .subtitle     = "3-Channel Secure Connection",
            .accent_color = 0x00897B,
            .create_cb    = page_tesaiot_connect_create,
            .render_cb    = page_tesaiot_connect_render,
            .destroy_cb   = page_tesaiot_connect_destroy,
            .cacheable    = false,
        };
        pm_register(&s_pm, PAGE_ID_TESAIOT_CONNECT, &def);
    }
#endif

    /* ===== Game pages (gated by ENABLE_PAGE_GAME_*) ===== */

#if ENABLE_PAGE_GAME_SNAKE
    {
        page_def_t def = {
            .name         = "Snake",
            .subtitle     = "Classic Snake Game",
            .accent_color = 0x4CAF50,
            .create_cb    = page_game_snake_create,
            .render_cb    = page_game_snake_render,
            .destroy_cb   = page_game_snake_destroy,
        };
        pm_register(&s_pm, PAGE_ID_GAME_SNAKE, &def);
    }
#endif

#if ENABLE_PAGE_GAME_FLAPPY
    {
        page_def_t def = {
            .name         = "Flappy Bird",
            .subtitle     = "Tap to Flap",
            .accent_color = 0xFFC107,
            .create_cb    = page_game_flappy_create,
            .render_cb    = page_game_flappy_render,
            .destroy_cb   = page_game_flappy_destroy,
        };
        pm_register(&s_pm, PAGE_ID_GAME_FLAPPY, &def);
    }
#endif

#if ENABLE_PAGE_GAME_PONG
    {
        page_def_t def = {
            .name         = "Pong",
            .subtitle     = "Classic Pong vs AI",
            .accent_color = 0x00BCD4,
            .create_cb    = page_game_pong_create,
            .render_cb    = page_game_pong_render,
            .destroy_cb   = page_game_pong_destroy,
        };
        pm_register(&s_pm, PAGE_ID_GAME_PONG, &def);
    }
#endif

#if ENABLE_PAGE_GAME_SHOOTER
    {
        page_def_t def = {
            .name         = "Space Shooter",
            .subtitle     = "Defend the Galaxy",
            .accent_color = 0xF44336,
            .create_cb    = page_game_shooter_create,
            .render_cb    = page_game_shooter_render,
            .destroy_cb   = page_game_shooter_destroy,
        };
        pm_register(&s_pm, PAGE_ID_GAME_SHOOTER, &def);
    }
#endif

    /***************************************************************************
     * Create Home screen and load it as initial screen
     ***************************************************************************/
    lv_obj_t *home_scr = page_home_create();
    if (home_scr) {
        lv_screen_load(home_scr);
    }

    /***************************************************************************
     * 33ms Render Timer (LVGL timer — runs in GFX task context)
     ***************************************************************************/
    lv_timer_create(sensorhub_timer_cb, 33, NULL);

    return true;
}

/*******************************************************************************
 * Container Getters (for IPC handlers — delegate to Playground page)
 *******************************************************************************/

void *sensorhub_ui_get_uxui_container(void)
{
    return (void *)page_playground_get_uxui_container();
}

void *sensorhub_ui_get_terminal_container(void)
{
    return (void *)page_playground_get_terminal_container();
}

/*******************************************************************************
 * Navigation API (backward-compatible with existing callers)
 *******************************************************************************/

void sensorhub_ui_switch_to_uxui(void)
{
    /* Navigate to Playground page (replaces tabview switch) */
    pm_navigate(&s_pm, PAGE_ID_PLAYGROUND);
}

void sensorhub_ui_switch_to_terminal(void)
{
    /* Terminal is merged into Playground — same navigation */
    pm_navigate(&s_pm, PAGE_ID_PLAYGROUND);
}

void sensorhub_ui_set_ide_connected(bool connected)
{
    if (s_pm.status_lbl == NULL) return;
    if (connected) {
        /* Set USB icon text (safe for both Home icon and inner-page label) */
        lv_label_set_text(s_pm.status_lbl, LV_SYMBOL_USB);
        lv_obj_remove_flag(s_pm.status_lbl, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_pm.status_lbl, LV_OBJ_FLAG_HIDDEN);
    }
}
