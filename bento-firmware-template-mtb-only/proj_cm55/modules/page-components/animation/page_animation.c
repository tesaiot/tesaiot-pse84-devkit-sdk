/*******************************************************************************
 * File Name: page_animation.c
 *
 * Description: Lottie Animation demo page for BENTO PSoC Edge E84.
 *              Showcases ThorVG-powered vector animations with play/pause
 *              and animation selection controls.
 *
 *              Buffer: 150x150 ARGB8888 = 90 KB in SOCMEM heap.
 *              Animations: embedded as const C arrays in flash.
 *
 *******************************************************************************/

#include "page_animation.h"
#include "page_manager.h"
#include "tesaiot_ui_theme.h"
#include "tesaiot_ui_helpers.h"
#include "lv_fonts_thai.h"
#include "lvgl.h"
#include <string.h>

/*******************************************************************************
 * Lottie Animation Assets (embedded JSON → C arrays)
 *******************************************************************************/
#include "lottie_assets.h"

/*******************************************************************************
 * Animation Dimensions
 *******************************************************************************/
#define ANIM_W   150
#define ANIM_H   150

/*******************************************************************************
 * Animation Catalog
 *******************************************************************************/
typedef struct {
    const char     *name;
    const char     *description;
    const uint8_t  *data;
    size_t          data_len;
    uint32_t        color;       /* accent color for card border */
} anim_entry_t;

static const anim_entry_t s_anim_catalog[] = {
    {
        .name        = "Approve",
        .description = "Success checkmark",
        .data        = lottie_success_checkmark_json,
        .data_len    = sizeof(lottie_success_checkmark_json),
        .color       = UI_COLOR_ACCENT_GREEN,
    },
    {
        .name        = "Spinner",
        .description = "Loading spinner",
        .data        = lottie_loading_spinner_json,
        .data_len    = sizeof(lottie_loading_spinner_json),
        .color       = UI_COLOR_ACCENT_BLUE,
    },
    {
        .name        = "Ring",
        .description = "Multi-ring loading",
        .data        = lottie_loading_ring_json,
        .data_len    = sizeof(lottie_loading_ring_json),
        .color       = UI_COLOR_ACCENT_CYAN,
    },
    {
        .name        = "Bell",
        .description = "Notification bell",
        .data        = lottie_notification_bell_json,
        .data_len    = sizeof(lottie_notification_bell_json),
        .color       = UI_COLOR_ACCENT_ORANGE,
    },
    {
        .name        = "Heartbeat",
        .description = "Heart pulse monitor",
        .data        = lottie_heartbeat_pulse_json,
        .data_len    = sizeof(lottie_heartbeat_pulse_json),
        .color       = UI_COLOR_ACCENT_PURPLE,
    },
    {
        .name        = "WiFi",
        .description = "WiFi radar signal",
        .data        = lottie_wifi_radar_json,
        .data_len    = sizeof(lottie_wifi_radar_json),
        .color       = UI_COLOR_ACCENT_GREEN,
    },
};

#define ANIM_COUNT  (sizeof(s_anim_catalog) / sizeof(s_anim_catalog[0]))

/*******************************************************************************
 * Page Context
 *******************************************************************************/
static struct {
    lv_obj_t    *screen;
    lv_obj_t    *anim_card;
    lv_obj_t    *lottie_obj;
    lv_obj_t    *title_lbl;
    lv_obj_t    *desc_lbl;
    lv_obj_t    *status_lbl;
    lv_obj_t    *btn_play;
    lv_obj_t    *btn_pause;
    lv_obj_t    *btn_restart;
    lv_obj_t    *btn_prev;
    lv_obj_t    *btn_next;
    int          current_idx;
    bool         is_paused;
} s_ctx;

/*******************************************************************************
 * Draw buffer for Lottie rendering (ARGB8888, allocated in SOCMEM heap)
 *******************************************************************************/
static lv_draw_buf_t *s_draw_buf;

/*******************************************************************************
 * Forward Declarations
 *******************************************************************************/
static void load_animation(int idx);
static void btn_play_cb(lv_event_t *e);
static void btn_pause_cb(lv_event_t *e);
static void btn_restart_cb(lv_event_t *e);
static void btn_prev_cb(lv_event_t *e);
static void btn_next_cb(lv_event_t *e);
static lv_obj_t *create_ctrl_btn(lv_obj_t *parent, const char *icon,
                                  lv_event_cb_t cb, uint32_t color);

/*******************************************************************************
 * page_animation_create
 *******************************************************************************/
lv_obj_t *page_animation_create(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));

    /* Use page_manager header for back button + swipe gesture */
    lv_obj_t *scr = lv_obj_create(NULL);
    page_manager_t *pm = pm_get_instance();
    lv_obj_t *content = pm_create_page_with_header(scr, pm,
                            "Lottie Animation", UI_COLOR_ACCENT_PURPLE);
    s_ctx.screen = scr;

    /***************************************************************************
     * Main layout: left = animation area, right = info + controls
     ***************************************************************************/

    /* Animation display card (left side) */
    lv_obj_t *anim_card = lv_obj_create(content);
    lv_obj_set_size(anim_card, ANIM_W + 40, ANIM_H + 40);
    lv_obj_align(anim_card, LV_ALIGN_LEFT_MID, UI_SPACE_LG, 0);
    lv_obj_set_style_bg_color(anim_card, lv_color_hex(UI_COLOR_BG_CARD), 0);
    lv_obj_set_style_radius(anim_card, UI_RADIUS_LG, 0);
    lv_obj_set_style_border_width(anim_card, 2, 0);
    lv_obj_set_style_border_color(anim_card, lv_color_hex(UI_COLOR_ACCENT_PURPLE), 0);
    lv_obj_set_style_pad_all(anim_card, 0, 0);
    lv_obj_set_scrollbar_mode(anim_card, LV_SCROLLBAR_MODE_OFF);
    s_ctx.anim_card = anim_card;

    /* Allocate draw buffer for Lottie (ARGB8888) */
    s_draw_buf = lv_draw_buf_create(ANIM_W, ANIM_H,
                                     LV_COLOR_FORMAT_ARGB8888, 0);

    /* Right panel */
    lv_obj_t *right_panel = lv_obj_create(content);
    lv_obj_set_size(right_panel, 440, 380);
    lv_obj_align(right_panel, LV_ALIGN_RIGHT_MID, -UI_SPACE_MD, 0);
    lv_obj_set_style_bg_opa(right_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_panel, 0, 0);
    lv_obj_set_style_pad_all(right_panel, 0, 0);
    lv_obj_clear_flag(right_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(right_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(right_panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(right_panel, UI_SPACE_MD, 0);

    /* Animation name */
    s_ctx.title_lbl = lv_label_create(right_panel);
    lv_obj_set_style_text_font(s_ctx.title_lbl, UI_FONT_H1, 0);
    lv_obj_set_style_text_color(s_ctx.title_lbl, lv_color_hex(UI_COLOR_ACCENT_PURPLE), 0);

    /* Description */
    s_ctx.desc_lbl = lv_label_create(right_panel);
    lv_obj_set_style_text_font(s_ctx.desc_lbl, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(s_ctx.desc_lbl, lv_color_hex(UI_COLOR_TEXT_SECONDARY), 0);

    /* Status label */
    s_ctx.status_lbl = lv_label_create(right_panel);
    lv_obj_set_style_text_font(s_ctx.status_lbl, UI_FONT_CAPTION, 0);
    lv_obj_set_style_text_color(s_ctx.status_lbl, lv_color_hex(UI_COLOR_ACCENT_GREEN), 0);
    lv_label_set_text(s_ctx.status_lbl, LV_SYMBOL_PLAY " Playing");

    /* Info card */
    lv_obj_t *info_card = lv_obj_create(right_panel);
    lv_obj_set_size(info_card, 420, 80);
    lv_obj_set_style_bg_color(info_card, lv_color_hex(UI_COLOR_BG_CARD), 0);
    lv_obj_set_style_radius(info_card, UI_RADIUS_MD, 0);
    lv_obj_set_style_border_width(info_card, 0, 0);
    lv_obj_set_style_pad_all(info_card, UI_SPACE_MD, 0);
    lv_obj_set_scrollbar_mode(info_card, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *info_lbl = lv_label_create(info_card);
    lv_label_set_text(info_lbl,
        "ThorVG v0.15 " LV_SYMBOL_RIGHT " LVGL 9.4\n"
        "Buffer: 150x150 ARGB8888 (90 KB)\n"
        "Renderer: Software (Cortex-M55)");
    lv_obj_set_style_text_font(info_lbl, UI_FONT_CAPTION, 0);
    lv_obj_set_style_text_color(info_lbl, lv_color_hex(UI_COLOR_TEXT_DISABLED), 0);
    lv_obj_set_width(info_lbl, 400);

    /* Control buttons row */
    lv_obj_t *ctrl_row = lv_obj_create(right_panel);
    lv_obj_set_size(ctrl_row, 420, 60);
    lv_obj_set_style_bg_opa(ctrl_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ctrl_row, 0, 0);
    lv_obj_set_style_pad_all(ctrl_row, 0, 0);
    lv_obj_clear_flag(ctrl_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(ctrl_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctrl_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ctrl_row, UI_SPACE_MD, 0);

    s_ctx.btn_prev    = create_ctrl_btn(ctrl_row, LV_SYMBOL_PREV,    btn_prev_cb,    UI_COLOR_ACCENT_BLUE);
    s_ctx.btn_play    = create_ctrl_btn(ctrl_row, LV_SYMBOL_PLAY,    btn_play_cb,    UI_COLOR_ACCENT_GREEN);
    s_ctx.btn_pause   = create_ctrl_btn(ctrl_row, LV_SYMBOL_PAUSE,   btn_pause_cb,   UI_COLOR_ACCENT_ORANGE);
    s_ctx.btn_restart = create_ctrl_btn(ctrl_row, LV_SYMBOL_REFRESH, btn_restart_cb, UI_COLOR_ACCENT_CYAN);
    s_ctx.btn_next    = create_ctrl_btn(ctrl_row, LV_SYMBOL_NEXT,    btn_next_cb,    UI_COLOR_ACCENT_BLUE);

    /* Load first animation */
    load_animation(0);

    return scr;
}

/*******************************************************************************
 * page_animation_render — called every 33ms by LVGL timer
 *******************************************************************************/
void page_animation_render(sensorhub_snapshot_t *snap)
{
    LV_UNUSED(snap);
    /* Lottie widget auto-renders via LVGL animation system — no manual update needed */
}

/*******************************************************************************
 * page_animation_destroy
 *******************************************************************************/
void page_animation_destroy(void)
{
    /* Delete lottie widget FIRST — its animation callback accesses
     * the draw buffer every frame.  Deleting it stops the ThorVG
     * animation and triggers the lottie destructor (frees tvg objects).
     * Only then is it safe to destroy the shared draw buffer. */
    if(s_ctx.lottie_obj) {
        lv_obj_delete(s_ctx.lottie_obj);
        s_ctx.lottie_obj = NULL;
    }
    if(s_draw_buf) {
        lv_draw_buf_destroy(s_draw_buf);
        s_draw_buf = NULL;
    }
    s_ctx.anim_card = NULL;
    s_ctx.screen = NULL;
}

/*******************************************************************************
 * Static Helpers
 *******************************************************************************/
static void load_animation(int idx)
{
    if(idx < 0 || idx >= (int)ANIM_COUNT) return;

    s_ctx.current_idx = idx;
    const anim_entry_t *entry = &s_anim_catalog[idx];

    /* Update labels */
    char title_buf[64];
    snprintf(title_buf, sizeof(title_buf), "%s (%d/%d)",
             entry->name, idx + 1, (int)ANIM_COUNT);
    lv_label_set_text(s_ctx.title_lbl, title_buf);
    lv_label_set_text(s_ctx.desc_lbl, entry->description);

    /* Recreate lottie widget — ThorVG needs a fresh canvas push when
     * switching animation source data.  lv_lottie_set_src_data() alone
     * does not re-push the paint to the canvas, so the old animation
     * keeps rendering.  Destroy + create is the reliable path. */
    if(s_ctx.lottie_obj) {
        lv_obj_delete(s_ctx.lottie_obj);
        s_ctx.lottie_obj = NULL;
    }

    s_ctx.lottie_obj = lv_lottie_create(s_ctx.anim_card);
    if(s_draw_buf) {
        lv_lottie_set_draw_buf(s_ctx.lottie_obj, s_draw_buf);
    }
    lv_lottie_set_src_data(s_ctx.lottie_obj, entry->data, entry->data_len);
    lv_obj_center(s_ctx.lottie_obj);

    /* Reset state */
    s_ctx.is_paused = false;
    lv_label_set_text(s_ctx.status_lbl, LV_SYMBOL_PLAY " Playing");
    lv_obj_set_style_text_color(s_ctx.status_lbl,
                                 lv_color_hex(UI_COLOR_ACCENT_GREEN), 0);
}

static lv_obj_t *create_ctrl_btn(lv_obj_t *parent, const char *icon,
                                  lv_event_cb_t cb, uint32_t color)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, UI_TOUCH_PRIMARY, UI_TOUCH_MIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(UI_COLOR_BG_SURFACE), 0);
    lv_obj_set_style_radius(btn, UI_RADIUS_MD, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, icon);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(lbl, UI_FONT_H3, 0);
    lv_obj_center(lbl);

    return btn;
}

static void btn_play_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if(!s_ctx.lottie_obj) return;
    lv_anim_t *anim = lv_lottie_get_anim(s_ctx.lottie_obj);
    if(anim) {
        lv_anim_resume(anim);
        s_ctx.is_paused = false;
        lv_label_set_text(s_ctx.status_lbl, LV_SYMBOL_PLAY " Playing");
        lv_obj_set_style_text_color(s_ctx.status_lbl,
                                     lv_color_hex(UI_COLOR_ACCENT_GREEN), 0);
    }
}

static void btn_pause_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if(!s_ctx.lottie_obj) return;
    lv_anim_t *anim = lv_lottie_get_anim(s_ctx.lottie_obj);
    if(anim) {
        lv_anim_pause(anim);
        s_ctx.is_paused = true;
        lv_label_set_text(s_ctx.status_lbl, LV_SYMBOL_PAUSE " Paused");
        lv_obj_set_style_text_color(s_ctx.status_lbl,
                                     lv_color_hex(UI_COLOR_ACCENT_ORANGE), 0);
    }
}

static void btn_restart_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    load_animation(s_ctx.current_idx);
}

static void btn_prev_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    int idx = s_ctx.current_idx - 1;
    if(idx < 0) idx = (int)ANIM_COUNT - 1;
    load_animation(idx);
}

static void btn_next_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    int idx = s_ctx.current_idx + 1;
    if(idx >= (int)ANIM_COUNT) idx = 0;
    load_animation(idx);
}
