/*******************************************************************************
 * File Name: page_home.c
 *
 * Description: Home screen v2 — Premium warm dark shell layout.
 *              Shell with visible vertical gradient, hero section (brand +
 *              multi-sensor live hub), horizontally scrollable card row with
 *              LVGL symbol icons (left-aligned) on colored badges, and
 *              status icon bar (IDE/WiFi/Joystick) at top-right.
 *
 *              Layout: 770x460 shell centered on 800x480 screen.
 *              Cards CARD_W x CARD_H landscape rectangles with scroll gimmick.
 *
 *              Background uses single screen-level gradient (no seam) to
 *              prevent VG-Lite GPU asynchronous rendering artifacts.
 *              Sensor Live updates throttled to ~6fps.
 *
 *******************************************************************************/

#include "page_home.h"
#include "page_manager.h"
#include "icons/home_icons.h"
#include "tesaiot_ui_theme.h"
#include "bentoclaw_version.h"   /* BENTOCLAW_VERSION — hero kit/version label */
#include "bsp_feature_flags.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* Custom font: ◉ (U+25C9) and ⦿ (U+29BF) for BENTO brand — 20px */
extern const lv_font_t bento_symbols_20;

/* Custom font: FontAwesome security icons for HSM card — 28px */
#include "font_hsm_icons.h"

/* Welcome Lottie animation for Home hero */
#include "lottie_welcome.h"

/* Set label text only if changed — prevents unnecessary LVGL invalidation
 * that causes full-screen redraws (card row flicker in FULL render mode). */
static inline void label_set_text_if_changed(lv_obj_t *lbl, const char *text)
{
    if (strcmp(lv_label_get_text(lbl), text) != 0) {
        lv_label_set_text(lbl, text);
    }
}

/* Welcome Lottie render buffer dimensions (aspect ratio 428:123) */
#define WELCOME_LOTTIE_W   300
#define WELCOME_LOTTIE_H   86

/*******************************************************************************
 * Shell Dimensions
 *******************************************************************************/
#define SHELL_MARGIN_X   15
#define SHELL_MARGIN_Y   10
#define SHELL_W          (UI_SCREEN_W - 2 * SHELL_MARGIN_X)   /* 770 */
#define SHELL_H          (UI_SCREEN_H - 2 * SHELL_MARGIN_Y)   /* 460 */
#define SHELL_PAD        UI_CARD_PAD                           /* 16 */
#define INNER_W          (SHELL_W - 2 * SHELL_PAD)             /* 738 */
#define INNER_H          (SHELL_H - 2 * SHELL_PAD)             /* 428 */

/* Screen bg: dark olive brown (subtle warm frame around shell) */
#define BG_DARK_COLOR    0x3E2116

/*******************************************************************************
 * Card Row — CARD_W x CARD_H landscape rectangles, horizontally scrollable
 *******************************************************************************/
#define CARD_W           214
#define CARD_H           150
#define CARD_GAP         8
#define CARD_ROW_Y       250    /* Y within inner area — below hero */
#define BADGE_SIZE       48
#define BADGE_OPA        LV_OPA_40

/*******************************************************************************
 * Sensor Live Throttle — update every N frames (~6fps at 30fps render)
 *******************************************************************************/
#define SENSOR_UPDATE_SKIP  5

/*******************************************************************************
 * Hero Font (conditional — AI Kit has larger fonts)
 *******************************************************************************/
#if BSP_HAS_DPS368
    #define HOME_HERO_FONT     &lv_font_montserrat_36
    #define HOME_HERO_Y_BOARD  36
    #define HOME_HERO_Y_VAR    86
#else
    #define HOME_HERO_FONT     UI_FONT_H1   /* 28px */
    #define HOME_HERO_Y_BOARD  32
    #define HOME_HERO_Y_VAR    66
#endif

/*******************************************************************************
 * Sensor Live Row Indices
 *******************************************************************************/
enum {
    SENSOR_ROW_IMU = 0,
    SENSOR_ROW_COMP,
#if BSP_HAS_DPS368
    SENSOR_ROW_TEMP,
#endif
#if BSP_HAS_SHT40
    SENSOR_ROW_HUMID,
#endif
#if BSP_HAS_CAPSENSE
    SENSOR_ROW_TOUCH,
#endif
#if BSP_HAS_POTENTIOMETER
    SENSOR_ROW_POT,
#endif
    SENSOR_ROW_COUNT
};

/*******************************************************************************
 * Status Icon Indices
 *******************************************************************************/
enum {
    STATUS_IDE = 0,
    STATUS_WIFI,
    STATUS_COUNT
};

/*******************************************************************************
 * Card Definitions (BSP-conditional)
 *******************************************************************************/
//! [j7_home_card_defs]
typedef struct {
    page_id_t   id;
    const char *title;
    const char *icon;     /* LV_SYMBOL_* (FontAwesome subset) */
    uint32_t    color;
    const lv_font_t *icon_font;  /* NULL = default UI_FONT_H2, else custom */
    const lv_image_dsc_t *icon_img;  /* NULL = draw the glyph above; else this
                                      * hand-drawn asset, for concepts the
                                      * FontAwesome subset has no glyph for */
} home_card_def_t;

static const home_card_def_t s_card_defs[] = {
    /* ---- Primary order (top of the Home grid) ---- */
    { PAGE_ID_DASHBOARD,  "Sensor Dashboard",  NULL, UI_COLOR_SENSOR_IMU, NULL, &icon_dashboard },
#if ENABLE_PAGE_GPIO_RGB
    { PAGE_ID_GPIO_RGB,   "GPIO & RGB Matrix", NULL, UI_COLOR_ACCENT_GREEN, NULL, &icon_touch_rgb },
#endif
#if ENABLE_PAGE_MOTOR_CTRL
    { PAGE_ID_MOTOR_CTRL, "Motor Controller",  LV_SYMBOL_REFRESH, UI_COLOR_ACCENT_ORANGE, NULL, NULL },
#endif
#if defined(BENTO_HAS_EDGE_AI) && (BENTO_HAS_EDGE_AI == 1)
    { PAGE_ID_EDGE_AI,    "Edge AI",           NULL, UI_COLOR_ACCENT_PURPLE, NULL, &icon_ai_chip },
#endif
//! [j7_home_card_defs]
#if ENABLE_PAGE_HSM
    { PAGE_ID_HSM, "HSM Security", HSM_ICON_LOCK, UI_COLOR_ACCENT_PURPLE, &font_hsm_icons_28 },
#endif
#if ENABLE_PAGE_SMART_WATCH
    { PAGE_ID_SMART_WATCH, "Smart Watch", NULL, UI_COLOR_ACCENT_CYAN, NULL, &icon_smartwatch },
#endif
#if ENABLE_PAGE_ANIMATION
    { PAGE_ID_ANIMATION, "Animation", NULL, UI_COLOR_ACCENT_PURPLE, NULL, &icon_animation },
#endif
    { PAGE_ID_PLAYGROUND, "BENTO Playground", NULL, UI_COLOR_ACCENT, NULL, &icon_bento },
#if ENABLE_PAGE_JOYSTICK
    { PAGE_ID_JOYSTICK,   "Joystick",   NULL, UI_COLOR_SENSOR_JOY, NULL, &icon_joystick },
#endif
#if ENABLE_PAGE_BENTOCLAW
    { PAGE_ID_BENTOCLAW, "BENTO Claw", NULL, UI_COLOR_ACCENT_CYAN, NULL, &icon_claw },
#endif
/* Hide the Wi-Fi Setting card when the Bento Desktop Buddy variant is
 * compiled in — CYW55500 is a single-radio combo chip, so WiFi APIs are
 * blocked (modwifi raises OSError) whenever BLE is active. Showing the
 * card would let the user enter a menu that cannot do anything. */
#if ENABLE_PAGE_WIFI_CONNECT && !(defined(ENABLE_PAGE_BENTO_BUDDY) && ENABLE_PAGE_BENTO_BUDDY == 1)
    { PAGE_ID_WIFI_CONNECT, "Wi-Fi Setting", LV_SYMBOL_WIFI, UI_COLOR_ACCENT_GREEN },
#endif
    /* ---- Secondary cards (kept after the primary order, #if-gated) ---- */
#if ENABLE_PAGE_CONTROLS
    { PAGE_ID_CONTROLS,   "Controls",          LV_SYMBOL_SETTINGS, UI_COLOR_SENSOR_TOUCH },
#endif
#if ENABLE_PAGE_SMART_CARD
    { PAGE_ID_SMART_CARD, "Smart Card", LV_SYMBOL_SD_CARD, UI_COLOR_ACCENT_BLUE },
#endif
#if ENABLE_PAGE_BENTO_BUDDY
    { PAGE_ID_BENTO_BUDDY, "Bento Buddy", LV_SYMBOL_BLUETOOTH, UI_COLOR_ACCENT_CYAN },
#endif
/* TESAIoT Connectivity relies on WiFi + MQTT — also hide when the
 * Bento Buddy variant is active (WiFi APIs are guarded off). */
#if ENABLE_PAGE_TESAIOT_CONNECT && !(defined(ENABLE_PAGE_BENTO_BUDDY) && ENABLE_PAGE_BENTO_BUDDY == 1)
    { PAGE_ID_TESAIOT_CONNECT, "TESAIoT Connectivity", LV_SYMBOL_WIFI, 0x00897B },
#endif
#if ENABLE_PAGE_GAME_SNAKE
    { PAGE_ID_GAME_SNAKE,   "Snake",         LV_SYMBOL_RIGHT,   0x4CAF50 },
#endif
#if ENABLE_PAGE_GAME_FLAPPY
    { PAGE_ID_GAME_FLAPPY,  "Flappy Bird",   LV_SYMBOL_UP,      0xFFC107 },
#endif
#if ENABLE_PAGE_GAME_PONG
    { PAGE_ID_GAME_PONG,    "Pong",          LV_SYMBOL_SHUFFLE, 0x00BCD4 },
#endif
#if ENABLE_PAGE_GAME_SHOOTER
    { PAGE_ID_GAME_SHOOTER, "Space Shooter", LV_SYMBOL_CHARGE,  0xF44336 },
#endif
};

#define NUM_CARDS  (sizeof(s_card_defs) / sizeof(s_card_defs[0]))

/*******************************************************************************
 * Module-Static Context
 *******************************************************************************/
typedef struct {
    lv_obj_t *sensor_val_lbls[SENSOR_ROW_COUNT];
    lv_obj_t *status_icons[STATUS_COUNT];
    lv_obj_t *time_lbl;
    lv_obj_t *welcome_lottie;
    lv_obj_t *card_row;
    page_id_t card_page_ids[PM_MAX_PAGES];
} page_home_ctx_t;

static page_home_ctx_t s_ctx;
static lv_draw_buf_t *s_welcome_draw_buf = NULL;
static int32_t s_saved_scroll_x = 0;   /* Preserve card row scroll across nav */

/*******************************************************************************
 * Lottie "write → erase → write → stop" via timer + pause
 *
 * LVGL Lottie internally creates ONE animation with REPEAT_INFINITE.
 * Calling lv_anim_start() creates a SECOND animation on the same ThorVG
 * canvas → concurrent rendering → HardFault. NEVER call lv_anim_start().
 *
 * Safe approach: let the internal animation run, pause it after 1.5 cycles
 * using a one-shot lv_timer. For tap replay: reset act_time + resume.
 *******************************************************************************/
static lv_timer_t *s_lottie_timer = NULL;

/* Helper: cancel any pending Lottie timer */
static void lottie_timer_cancel(void)
{
    if (s_lottie_timer) {
        lv_timer_delete(s_lottie_timer);
        s_lottie_timer = NULL;
    }
}

/* Pause the Lottie animation (used by boot stop + return re-pause) */
static void lottie_pause_cb(lv_timer_t *timer)
{
    (void)timer;
    s_lottie_timer = NULL;
    if (!s_ctx.welcome_lottie) return;
    lv_anim_t *a = lv_lottie_get_anim(s_ctx.welcome_lottie);
    if (a) lv_anim_pause(a);
}

/* Resume at midpoint to render "Welcome" frame, then re-pause after 1 tick.
 * Called ~100ms after page_home_create() when screen is visible. */
static void lottie_show_welcome_cb(lv_timer_t *timer)
{
    (void)timer;
    s_lottie_timer = NULL;
    if (!s_ctx.welcome_lottie) return;

    lv_anim_t *a = lv_lottie_get_anim(s_ctx.welcome_lottie);
    if (!a) return;

    /* Jump to midpoint and resume so exec_cb renders the frame */
    a->act_time = a->duration / 2;
    lv_anim_resume(a);

    /* Re-pause after one render tick */
    s_lottie_timer = lv_timer_create(lottie_pause_cb, 35, NULL);
    lv_timer_set_repeat_count(s_lottie_timer, 1);
}

/* Schedule animation pause after 1.5 × duration (write → erase → write) */
static void lottie_schedule_stop(void)
{
    lv_anim_t *a = lv_lottie_get_anim(s_ctx.welcome_lottie);
    if (!a) return;

    lottie_timer_cancel();
    uint32_t stop_ms = (uint32_t)a->duration + (uint32_t)a->duration / 2;
    s_lottie_timer = lv_timer_create(lottie_pause_cb, stop_ms, NULL);
    lv_timer_set_repeat_count(s_lottie_timer, 1);
}

/* Return visit: pause immediately, then show "Welcome" once screen is visible */
static void lottie_show_static_welcome(void)
{
    lv_anim_t *a = lv_lottie_get_anim(s_ctx.welcome_lottie);
    if (!a) return;

    /* Pause now to prevent act_time reset by invisible-tick handler */
    a->act_time = a->duration / 2;
    lv_anim_pause(a);

    /* After screen loads (~100ms), resume briefly to render midpoint frame */
    lottie_timer_cancel();
    s_lottie_timer = lv_timer_create(lottie_show_welcome_cb, 100, NULL);
    lv_timer_set_repeat_count(s_lottie_timer, 1);
}

/* Tap: restart the 1.5-cycle sequence from frame 0 */
static void lottie_tap_cb(lv_event_t *e)
{
    (void)e;
    if (!s_ctx.welcome_lottie) return;

    lv_anim_t *a = lv_lottie_get_anim(s_ctx.welcome_lottie);
    if (!a) return;

    /* Reset to frame 0 and resume (NO lv_anim_start!) */
    a->act_time = 0;
    a->reverse_play_in_progress = false;
    lv_anim_resume(a);

    /* Schedule pause after 1.5 cycles */
    lottie_schedule_stop();
}

/*******************************************************************************
 * Card Click Handler
 *******************************************************************************/
static void card_click_cb(lv_event_t *e)
{
    page_id_t *p_id = lv_event_get_user_data(e);
    if (p_id == NULL) return;

    page_manager_t *pm = pm_get_instance();
    if (pm) {
        pm_navigate(pm, *p_id);
    }
}

/*******************************************************************************
 * Helper: Create one sensor row in the hero right panel
 *******************************************************************************/
static void create_sensor_row(lv_obj_t *parent, int x, int *y,
                              const char *icon, const char *name,
                              int ctx_idx)
{
    lv_obj_t *name_lbl = lv_label_create(parent);
    char name_buf[32];
    snprintf(name_buf, sizeof(name_buf), "%s %s", icon, name);
    lv_label_set_text(name_lbl, name_buf);
    lv_obj_set_style_text_font(name_lbl, UI_FONT_CAPTION, 0);
    lv_obj_set_style_text_color(name_lbl,
                                lv_color_hex(UI_COLOR_TEXT_SUBTLE), 0);
    lv_obj_set_pos(name_lbl, x, *y);

    s_ctx.sensor_val_lbls[ctx_idx] = lv_label_create(parent);
    lv_label_set_text(s_ctx.sensor_val_lbls[ctx_idx], "---");
    lv_obj_set_style_text_font(s_ctx.sensor_val_lbls[ctx_idx],
                                UI_FONT_CAPTION, 0);
    lv_obj_set_style_text_color(s_ctx.sensor_val_lbls[ctx_idx],
                                lv_color_hex(UI_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_pos(s_ctx.sensor_val_lbls[ctx_idx], x + 68, *y);

    *y += 20;
}

/*******************************************************************************
 * page_home_create
 *******************************************************************************/
lv_obj_t *page_home_create(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));
    page_manager_t *pm = pm_get_instance();

    /* ── Screen — Solid dark bg (no gradient = zero banding at margins) ── */
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(BG_DARK_COLOR), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* ── Shell — Solid color (no gradient = no banding, no scroll flash) ─ */
    lv_obj_t *shell = lv_obj_create(scr);
    lv_obj_set_pos(shell, SHELL_MARGIN_X, SHELL_MARGIN_Y);
    lv_obj_set_size(shell, SHELL_W, SHELL_H);
    lv_obj_set_style_bg_color(shell, lv_color_hex(UI_COLOR_BG_CARD), 0);
    lv_obj_set_style_bg_opa(shell, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(shell, UI_RADIUS_XL, 0);
    lv_obj_set_style_border_width(shell, 0, 0);
    lv_obj_set_style_pad_all(shell, SHELL_PAD, 0);
    lv_obj_clear_flag(shell, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Time + Status Icons (top-right) ─────────────────────────────── */
    {
        /* Home status bar shows connectivity icon. Flip to Bluetooth when
         * the Bento Desktop Buddy variant is compiled in — the top-bar in
         * page_manager.c already does the same switch, so the two stay in
         * sync. */
        static const char * const icons[STATUS_COUNT] = {
#if defined(ENABLE_PAGE_BENTO_BUDDY) && ENABLE_PAGE_BENTO_BUDDY == 1
            LV_SYMBOL_USB, LV_SYMBOL_BLUETOOTH
#else
            LV_SYMBOL_USB, LV_SYMBOL_WIFI
#endif
        };
        int sx = SHELL_W - SHELL_PAD;

        for (int i = STATUS_COUNT - 1; i >= 0; i--) {
            sx -= 24;
            s_ctx.status_icons[i] = lv_label_create(shell);
            lv_label_set_text(s_ctx.status_icons[i], icons[i]);
            lv_obj_set_style_text_font(s_ctx.status_icons[i],
                                        UI_FONT_BODY, 0);
            lv_obj_set_style_text_color(s_ctx.status_icons[i],
                                        lv_color_hex(UI_COLOR_ACCENT_GREEN), 0);
            lv_obj_set_pos(s_ctx.status_icons[i], sx, -5);
            lv_obj_add_flag(s_ctx.status_icons[i], LV_OBJ_FLAG_HIDDEN);
        }

        /* Time label to the left of status icons */
        sx -= 12;
        s_ctx.time_lbl = lv_label_create(shell);
        lv_label_set_text(s_ctx.time_lbl, "");
        lv_obj_add_flag(s_ctx.time_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_font(s_ctx.time_lbl,
                                    &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(s_ctx.time_lbl,
                                    lv_color_hex(UI_COLOR_TEXT_SECONDARY), 0);
        lv_obj_set_style_text_align(s_ctx.time_lbl,
                                    LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(s_ctx.time_lbl, sx - 140, -4);
        lv_obj_set_width(s_ctx.time_lbl, 140);
    }

    if (pm) {
        pm->status_lbl = s_ctx.status_icons[STATUS_IDE];
        pm->time_lbl   = s_ctx.time_lbl;
        pm->wifi_lbl   = s_ctx.status_icons[STATUS_WIFI];
    }

    /* ── Hero Left — Brand Identity ────────────────────────────────────── */
    /* "BENTO" in accent color, "◉⦿" in white (20px fallback, no space) */
    static lv_font_t brand_sym_font;
    brand_sym_font = *UI_FONT_BODY;
    brand_sym_font.fallback = &bento_symbols_20;

    lv_obj_t *brand_accent = lv_label_create(shell);
    lv_label_set_text(brand_accent, "BENTO");
    lv_obj_set_style_text_font(brand_accent, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(brand_accent,
                                lv_color_hex(UI_COLOR_ACCENT), 0);
    lv_obj_set_pos(brand_accent, SHELL_PAD, SHELL_PAD);

    lv_obj_t *brand_sym = lv_label_create(shell);
    lv_label_set_text(brand_sym, "\xE2\x97\x89\xE2\xA6\xBF");
    lv_obj_set_style_text_font(brand_sym, &brand_sym_font, 0);
    lv_obj_set_style_text_color(brand_sym,
                                lv_color_hex(UI_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_pos(brand_sym, SHELL_PAD + 60, SHELL_PAD);

    lv_obj_t *brand_suffix = lv_label_create(shell);
    lv_label_set_text(brand_suffix, HOME_BRAND_TAGLINE);
    lv_obj_set_style_text_font(brand_suffix, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(brand_suffix,
                                lv_color_hex(UI_COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_pos(brand_suffix, SHELL_PAD + 96, SHELL_PAD + 6);

    lv_obj_t *board_lbl = lv_label_create(shell);
    lv_label_set_text(board_lbl, "TESAIoT");
    lv_obj_set_style_text_font(board_lbl, HOME_HERO_FONT, 0);
    lv_obj_set_style_text_color(board_lbl,
                                lv_color_hex(UI_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_pos(board_lbl, SHELL_PAD, SHELL_PAD + HOME_HERO_Y_BOARD);

    lv_obj_t *var_lbl = lv_label_create(shell);
#if BSP_HAS_DPS368
    lv_label_set_text(var_lbl, "Secure Edge AI Kit");
#elif BSP_HAS_CAPSENSE
    lv_label_set_text(var_lbl, "Eva Kit");
#else
    lv_label_set_text(var_lbl, "E84");
#endif
    lv_obj_set_style_text_font(var_lbl, UI_FONT_H1, 0);
    lv_obj_set_style_text_color(var_lbl,
                                lv_color_hex(UI_COLOR_ACCENT), 0);
    lv_obj_set_pos(var_lbl, SHELL_PAD, SHELL_PAD + HOME_HERO_Y_VAR);

    /* Firmware version — smaller, warm-white, baseline-aligned to the
     * right of the kit name. Separate label so the kit keeps its H1
     * accent while the version reads as quiet meta. */
    lv_obj_t *ver_lbl = lv_label_create(shell);
    /* The variant suffix was hardcoded "-mtb_mpy" once, and the two variants'
     * UIs are otherwise pixel-identical — so an mtb-only board introduced
     * itself on screen as mtb_mpy, and the only way to tell the truth was a
     * serial console. This label is the one place a person looks. */
#if defined(BENTO_HAS_MPY) && !BENTO_HAS_MPY
    lv_label_set_text(ver_lbl, "v" BENTOCLAW_VERSION "-mtb_only");
#else
    lv_label_set_text(ver_lbl, "v" BENTOCLAW_VERSION "-mtb_mpy");
#endif
    lv_obj_set_style_text_font(ver_lbl, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(ver_lbl,
                                lv_color_hex(UI_COLOR_TEXT_PRIMARY), 0);
    lv_obj_align_to(ver_lbl, var_lbl, LV_ALIGN_OUT_RIGHT_BOTTOM, 8, -3);

    /* ── Welcome Lottie — below "AI Kit", near center ────────────────── */
    s_welcome_draw_buf = lv_draw_buf_create(
        WELCOME_LOTTIE_W, WELCOME_LOTTIE_H,
        LV_COLOR_FORMAT_ARGB8888, 0);
    if (s_welcome_draw_buf) {
        s_ctx.welcome_lottie = lv_lottie_create(shell);
        lv_lottie_set_draw_buf(s_ctx.welcome_lottie, s_welcome_draw_buf);
        lv_lottie_set_src_data(s_ctx.welcome_lottie,
                               lottie_welcome_json,
                               sizeof(lottie_welcome_json));
        lv_obj_set_pos(s_ctx.welcome_lottie,
                       SHELL_PAD + 120, SHELL_PAD + HOME_HERO_Y_VAR + 36);
        /* First boot: write → erase → write → stop (1.5 cycles).
         * Return visits: show "Welcome" text at midpoint frame, no anim. */
        static bool s_first_boot = true;
        if (s_first_boot) {
            s_first_boot = false;
            lottie_schedule_stop();
        } else {
            lottie_show_static_welcome();
        }
        /* Tap to replay the same 1.5-cycle sequence */
        lv_obj_add_flag(s_ctx.welcome_lottie, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(s_ctx.welcome_lottie, lottie_tap_cb,
                            LV_EVENT_CLICKED, NULL);
    } else {
        LV_LOG_WARN("Lottie draw buffer allocation failed — animation disabled");
    }

    /* ── Hero Right — Sensor Live Panel ────────────────────────────────── */
    int hero_x = SHELL_PAD + 550;
    int hero_y = SHELL_PAD + 40;  /* +40px = 2 lines below top to clear time label */

    lv_obj_t *hub_title = lv_label_create(shell);
    lv_label_set_text(hub_title, "Sensor Live");
    lv_obj_set_style_text_font(hub_title, UI_FONT_H3, 0);
    lv_obj_set_style_text_color(hub_title,
                                lv_color_hex(UI_COLOR_ACCENT), 0);
    lv_obj_set_pos(hub_title, hero_x, hero_y);
    hero_y += 28;

    create_sensor_row(shell, hero_x, &hero_y,
                      LV_SYMBOL_GPS, "IMU", SENSOR_ROW_IMU);
    create_sensor_row(shell, hero_x, &hero_y,
                      LV_SYMBOL_LOOP, "Comp", SENSOR_ROW_COMP);
#if BSP_HAS_DPS368
    create_sensor_row(shell, hero_x, &hero_y,
                      LV_SYMBOL_CHARGE, "Temp", SENSOR_ROW_TEMP);
#endif
#if BSP_HAS_SHT40
    create_sensor_row(shell, hero_x, &hero_y,
                      LV_SYMBOL_TINT, "Humid", SENSOR_ROW_HUMID);
#endif
#if BSP_HAS_CAPSENSE
    create_sensor_row(shell, hero_x, &hero_y,
                      LV_SYMBOL_SETTINGS, "Touch", SENSOR_ROW_TOUCH);
#endif
#if BSP_HAS_POTENTIOMETER
    create_sensor_row(shell, hero_x, &hero_y,
                      LV_SYMBOL_SHUFFLE, "Pot", SENSOR_ROW_POT);
#endif

    /* ── Scrollable Card Row (180x120 landscape, scroll gimmick) ──────── */
    s_ctx.card_row = lv_obj_create(shell);
    lv_obj_t *card_row = s_ctx.card_row;
    lv_obj_set_pos(card_row, SHELL_PAD, SHELL_PAD + CARD_ROW_Y);
    lv_obj_set_size(card_row, INNER_W, CARD_H);
    lv_obj_set_style_bg_opa(card_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(card_row, 0, 0);
    lv_obj_set_style_pad_all(card_row, 0, 0);
    lv_obj_set_style_pad_column(card_row, CARD_GAP, 0);
    lv_obj_set_flex_flow(card_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_scroll_dir(card_row, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(card_row, LV_SCROLL_SNAP_START);
    lv_obj_set_scrollbar_mode(card_row, LV_SCROLLBAR_MODE_OFF);

    for (int i = 0; i < (int)NUM_CARDS; i++) {
        const home_card_def_t *def = &s_card_defs[i];

        lv_obj_t *card = lv_obj_create(card_row);
        lv_obj_set_size(card, CARD_W, CARD_H);
        lv_obj_set_style_bg_color(card,
                                  lv_color_hex(UI_COLOR_BG_SURFACE), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(card, UI_CARD_RADIUS, 0);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_style_pad_all(card, 0, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

        /* Colored circle badge (left-aligned, vertically centered) */
        lv_obj_t *badge = lv_obj_create(card);
        lv_obj_set_size(badge, BADGE_SIZE, BADGE_SIZE);
        lv_obj_set_style_bg_color(badge, lv_color_hex(def->color), 0);
        lv_obj_set_style_bg_opa(badge, BADGE_OPA, 0);
        lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(badge, 0, 0);
        lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(badge, LV_ALIGN_TOP_LEFT, 12, 12);

        /* Icon: a hand-drawn asset when the card supplies one, otherwise the
         * FontAwesome glyph. Both sit centred inside the coloured badge. */
        if (def->icon_img != NULL) {
            lv_obj_t *icon_img = lv_image_create(badge);
            lv_image_set_src(icon_img, def->icon_img);
            /* +30% larger icons across the whole menu (256 = 1.0x). Antialiased
             * so the 32px white-alpha assets stay crisp scaled up. */
            lv_image_set_scale(icon_img, 333);
            lv_image_set_antialias(icon_img, true);
            lv_obj_center(icon_img);
        } else {
            lv_obj_t *icon_lbl = lv_label_create(badge);
            lv_label_set_text(icon_lbl, def->icon);
            lv_obj_set_style_text_font(icon_lbl,
                                        def->icon_font ? def->icon_font : UI_FONT_H1, 0);
            lv_obj_set_style_text_color(icon_lbl,
                                        lv_color_hex(UI_COLOR_TEXT_PRIMARY), 0);
            lv_obj_center(icon_lbl);
        }

        /* Title (bottom-left, below badge) */
        lv_obj_t *title = lv_label_create(card);
        lv_label_set_text(title, def->title);
        lv_obj_set_style_text_font(title, UI_FONT_BODY, 0);
        lv_obj_set_style_text_color(title,
                                    lv_color_hex(UI_COLOR_TEXT_PRIMARY), 0);
        lv_obj_align(title, LV_ALIGN_BOTTOM_LEFT, UI_SPACE_MD, -14);

        //! [j7_card_tap_navigate]
        /* ...context: inside the home card build loop ... */
        s_ctx.card_page_ids[i] = def->id;
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, card_click_cb, LV_EVENT_CLICKED,
                            &s_ctx.card_page_ids[i]);
                            //! [j7_card_tap_navigate]

        /* Pressed state — subtle fade for tactile feedback */
        lv_obj_set_style_bg_opa(card, LV_OPA_70,
                                LV_PART_MAIN | LV_STATE_PRESSED);
    }

    /* Restore card row scroll position from previous session */
    if (s_saved_scroll_x != 0) {
        lv_obj_scroll_to_x(card_row, s_saved_scroll_x, LV_ANIM_OFF);
    }

    return scr;
}

/*******************************************************************************
 * page_home_render — Update Sensor Live Values + Status Icons
 *******************************************************************************/
void page_home_render(sensorhub_snapshot_t *snap)
{
    if (snap == NULL) return;

    /* Throttle sensor value updates to ~6fps (every SENSOR_UPDATE_SKIP frames).
     * Reduces LVGL partial redraws and eliminates VG-Lite gradient artifacts.
     * Status icon polling remains at full rate (lightweight bool checks). */
    static uint8_t s_sensor_skip = 0;
    bool do_sensor = (++s_sensor_skip >= SENSOR_UPDATE_SKIP);
    if (do_sensor) s_sensor_skip = 0;

    char buf[32];

    if (do_sensor && snap->has_bmi270 && snap->bmi270_changed &&
        s_ctx.sensor_val_lbls[SENSOR_ROW_IMU]) {
        float ax = (float)snap->bmi270.ax / IPC_BMI270_ACCEL_LSB_PER_G * 9.81f;
        float ay = (float)snap->bmi270.ay / IPC_BMI270_ACCEL_LSB_PER_G * 9.81f;
        float az = (float)snap->bmi270.az / IPC_BMI270_ACCEL_LSB_PER_G * 9.81f;
        snprintf(buf, sizeof(buf), "%+.0f %+.0f %+.0f",
                 (double)ax, (double)ay, (double)az);
        label_set_text_if_changed(s_ctx.sensor_val_lbls[SENSOR_ROW_IMU], buf);
    }

    if (do_sensor && snap->has_bmm350 && snap->bmm350_changed &&
        s_ctx.sensor_val_lbls[SENSOR_ROW_COMP]) {
        static const char * const cardinal[] = {
            "N", "NE", "E", "SE", "S", "SW", "W", "NW"
        };
        int deg = (int)(snap->bmm350.heading_x10 / 10);
        int idx = ((deg + 22) / 45) % 8;
        snprintf(buf, sizeof(buf), "%d\xC2\xB0 %s", deg, cardinal[idx]);
        label_set_text_if_changed(s_ctx.sensor_val_lbls[SENSOR_ROW_COMP], buf);
    }

#if BSP_HAS_DPS368
    if (do_sensor && snap->has_dps368 && snap->dps368_changed &&
        s_ctx.sensor_val_lbls[SENSOR_ROW_TEMP]) {
        float temp = snap->dps368.temperature_x100 / 100.0f;
        snprintf(buf, sizeof(buf), "%.0f\xC2\xB0" "C", (double)temp);
        label_set_text_if_changed(s_ctx.sensor_val_lbls[SENSOR_ROW_TEMP], buf);
    }
#endif

#if BSP_HAS_SHT40
    if (do_sensor && snap->has_sht40 && snap->sht40_changed &&
        s_ctx.sensor_val_lbls[SENSOR_ROW_HUMID]) {
        float hum = snap->sht40.humidity_x100 / 100.0f;
        snprintf(buf, sizeof(buf), "%.0f%%", (double)hum);
        label_set_text_if_changed(s_ctx.sensor_val_lbls[SENSOR_ROW_HUMID], buf);
    }
#endif

#if BSP_HAS_CAPSENSE
    if (do_sensor && snap->has_capsense && snap->capsense_changed &&
        s_ctx.sensor_val_lbls[SENSOR_ROW_TOUCH]) {
        snprintf(buf, sizeof(buf), "B0:%s B1:%s S:%d",
                 snap->capsense.btn0_pressed ? "ON" : "--",
                 snap->capsense.btn1_pressed ? "ON" : "--",
                 snap->capsense.slider);
        label_set_text_if_changed(s_ctx.sensor_val_lbls[SENSOR_ROW_TOUCH], buf);
    }
#endif

#if BSP_HAS_POTENTIOMETER
    if (do_sensor && snap->has_pot && snap->pot_changed &&
        s_ctx.sensor_val_lbls[SENSOR_ROW_POT]) {
        float pct = snap->pot.percent_x10 / 10.0f;
        snprintf(buf, sizeof(buf), "%.0f%%", (double)pct);
        label_set_text_if_changed(s_ctx.sensor_val_lbls[SENSOR_ROW_POT], buf);
    }
#endif

    /* Time + WiFi icon updated globally by pm_update_topbar() in sensorhub_ui.c */
}

/*******************************************************************************
 * page_home_destroy
 *******************************************************************************/
void page_home_destroy(void)
{
    /* Save card row scroll position before destroy */
    if (s_ctx.card_row) {
        s_saved_scroll_x = lv_obj_get_scroll_x(s_ctx.card_row);
    }

    lottie_timer_cancel();

    /* Destroy lottie widget before draw buffer (ThorVG callback safety) */
    if (s_ctx.welcome_lottie) {
        lv_obj_delete(s_ctx.welcome_lottie);
        s_ctx.welcome_lottie = NULL;
    }
    if (s_welcome_draw_buf) {
        lv_draw_buf_destroy(s_welcome_draw_buf);
        s_welcome_draw_buf = NULL;
    }

    page_manager_t *pm = pm_get_instance();
    if (pm) {
        pm->status_lbl = NULL;
        pm->time_lbl   = NULL;
        pm->wifi_lbl   = NULL;
    }
    memset(&s_ctx, 0, sizeof(s_ctx));
}
