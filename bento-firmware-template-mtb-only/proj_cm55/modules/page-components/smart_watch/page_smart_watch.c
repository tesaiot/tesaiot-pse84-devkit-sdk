/*******************************************************************************
 * File Name: page_smart_watch.c
 *
 * Description: Smart Watch page — Smartwatch Demo embedded in page manager.
 *              Layout motifs adapted from Smartwatch Demo Health screen.
 *
 *******************************************************************************/

#include "page_smart_watch.h"
#include "page_manager.h"
#include "tesaiot_ui_theme.h"
#include "demo/smartwatch_round/smartwatch_round_embed.h"
#include "lvgl.h"
#include <stdint.h>

static lv_obj_t *s_watch_host = NULL;

static void watch_back_cb(lv_event_t *e)
{
    page_manager_t *pm = lv_event_get_user_data(e);
    if (pm) {
        pm_back(pm);
    }
}

static void watch_nav_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_dir_t dir = (lv_dir_t)(uintptr_t)lv_event_get_user_data(e);
    smartwatch_round_embed_swipe(dir);
}

static void watch_swipe_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_GESTURE) return;
    lv_indev_t *indev = lv_indev_active();
    if (indev == NULL) return;
    smartwatch_round_embed_swipe(lv_indev_get_gesture_dir(indev));
}

static lv_obj_t *watch_create_nav_button(lv_obj_t *parent, const char *txt, lv_dir_t dir)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 46, 46);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x0F1118), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_70, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(UI_COLOR_ACCENT_CYAN), 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, txt);
    lv_obj_set_style_text_font(lbl, UI_FONT_H3, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(UI_COLOR_TEXT_PRIMARY), 0);
    lv_obj_center(lbl);

    lv_obj_add_event_cb(btn, watch_nav_btn_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)dir);
    return btn;
}

lv_obj_t *page_smart_watch_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(UI_COLOR_BG_DEEP), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, watch_swipe_cb, LV_EVENT_GESTURE, NULL);

    page_manager_t *pm = pm_get_instance();

    lv_obj_t *back_btn = lv_button_create(scr);
    lv_obj_set_size(back_btn, 90, 36);
    lv_obj_set_pos(back_btn, 10, 8);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(UI_COLOR_BG_CARD), 0);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(back_btn, UI_CARD_RADIUS, 0);
    lv_obj_set_style_border_width(back_btn, 0, 0);
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " Home");
    lv_obj_set_style_text_font(back_lbl, UI_FONT_CAPTION, 0);
    lv_obj_set_style_text_color(back_lbl, lv_color_hex(UI_COLOR_TEXT_PRIMARY), 0);
    lv_obj_center(back_lbl);
    lv_obj_add_event_cb(back_btn, watch_back_cb, LV_EVENT_CLICKED, pm);

    lv_obj_t *frame = lv_obj_create(scr);
    lv_obj_remove_style_all(frame);
    lv_obj_set_size(frame, 470, 470);
    lv_obj_center(frame);
    lv_obj_set_style_bg_color(frame, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(frame, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(frame, 0, 0);
    lv_obj_set_style_radius(frame, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(frame, 2, 0);
    lv_obj_set_style_clip_corner(frame, true, 0);
    lv_obj_add_flag(frame, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_clear_flag(frame, LV_OBJ_FLAG_SCROLLABLE);

    s_watch_host = lv_obj_create(frame);
    lv_obj_remove_style_all(s_watch_host);
    lv_obj_set_size(s_watch_host, 466, 466);
    lv_obj_center(s_watch_host);
    lv_obj_set_style_bg_color(s_watch_host, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_watch_host, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_watch_host, 0, 0);
    lv_obj_set_style_clip_corner(s_watch_host, false, 0);
    lv_obj_set_style_border_width(s_watch_host, 0, 0);
    lv_obj_set_style_pad_all(s_watch_host, 0, 0);
    lv_obj_add_flag(s_watch_host, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_watch_host, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn_left = watch_create_nav_button(scr, LV_SYMBOL_LEFT, LV_DIR_LEFT);
    lv_obj_align_to(btn_left, frame, LV_ALIGN_OUT_LEFT_MID, -20, 0);

    lv_obj_t *btn_right = watch_create_nav_button(scr, LV_SYMBOL_RIGHT, LV_DIR_RIGHT);
    lv_obj_align_to(btn_right, frame, LV_ALIGN_OUT_RIGHT_MID, 20, 0);

    lv_obj_t *btn_up = watch_create_nav_button(scr, LV_SYMBOL_UP, LV_DIR_TOP);
    lv_obj_align_to(btn_up, frame, LV_ALIGN_TOP_MID, 0, 14);

    lv_obj_t *btn_down = watch_create_nav_button(scr, LV_SYMBOL_DOWN, LV_DIR_BOTTOM);
    lv_obj_align_to(btn_down, frame, LV_ALIGN_BOTTOM_MID, 0, -14);

    smartwatch_round_embed_mount(s_watch_host);
    return scr;
}

void page_smart_watch_render(sensorhub_snapshot_t *snap)
{
    smartwatch_round_embed_tick(snap);
}

void page_smart_watch_destroy(void)
{
    smartwatch_round_embed_unmount();
    s_watch_host = NULL;
}
