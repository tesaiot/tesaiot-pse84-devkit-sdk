/*******************************************************************************
 * File Name: page_manager.c
 *
 * Description: Page-based navigation manager implementation.
 *              Handles screen create/destroy lifecycle, navigation stack,
 *              and animated transitions using lv_screen_load_anim().
 *
 *******************************************************************************/

#include "page_manager.h"
#include "tesaiot_ui_theme.h"
#include "tesaiot_ui_helpers.h"
#include "ipc_communication.h"
#include <string.h>

/*******************************************************************************
 * IPC: Resume sensor_auto_task on CM33 when leaving Playground.
 * Safety net — if MicroPython script finished without soft reset,
 * sensor_auto stays paused. This IPC tells CM33 to resume so
 * Dashboard/Motion pages get live data again.
 *******************************************************************************/
CY_SECTION_SHAREDMEM ipc_msg_t g_pm_ipc_msg_shared;

static void pm_notify_sensor_resume(void)
{
    memset(&g_pm_ipc_msg_shared, 0, sizeof(g_pm_ipc_msg_shared));
    g_pm_ipc_msg_shared.client_id = CM33_IPC_SENSOR_CTRL_CLIENT_ID;
    g_pm_ipc_msg_shared.intr_mask = CY_IPC_CYPIPE_INTR_MASK_EP1;
    g_pm_ipc_msg_shared.cmd       = IPC_CMD_SENSOR_AUTO_CTRL;
    g_pm_ipc_msg_shared.data[0]   = 1;  /* 1 = resume */

    for (int i = 0; i < 4; i++) {
        cy_en_ipc_pipe_status_t st = Cy_IPC_Pipe_SendMessage(
            CM33_IPC_PIPE_EP_ADDR, CM55_IPC_PIPE_EP_ADDR,
            (void *)&g_pm_ipc_msg_shared, NULL);
        if (st == CY_IPC_PIPE_SUCCESS) break;
        Cy_SysLib_DelayUs(200);
    }
}

/*******************************************************************************
 * Singleton Instance
 *******************************************************************************/
static page_manager_t *s_instance = NULL;

page_manager_t *pm_get_instance(void)
{
    return s_instance;
}

void pm_set_instance(page_manager_t *pm)
{
    s_instance = pm;
}

/*******************************************************************************
 * Forward Declarations
 *******************************************************************************/
static void pm_loaded_cb(lv_event_t *e);
static void pm_unloaded_cb(lv_event_t *e);
static void back_btn_cb(lv_event_t *e);
static void swipe_back_cb(lv_event_t *e);

/*******************************************************************************
 * Core API
 *******************************************************************************/

void pm_init(page_manager_t *pm)
{
    memset(pm, 0, sizeof(page_manager_t));
    pm->nav_top = -1;
    pm->current_page = PAGE_ID_HOME;
    pm->animating = false;
}

void pm_register(page_manager_t *pm, page_id_t id, const page_def_t *def)
{
    if (id >= PM_MAX_PAGES) return;
    pm->pages[id] = *def;
    if ((uint8_t)(id + 1) > pm->page_count) {
        pm->page_count = (uint8_t)(id + 1);
    }
}

//! [j7_pm_navigate_lifecycle]
void pm_navigate(page_manager_t *pm, page_id_t target)
{
    /* M1: Guard against double-tap during animation */
    if (pm->animating) return;

    /* Bounds check */
    if (target >= pm->page_count) return;
    if (pm->pages[target].create_cb == NULL) return;

    /* M4: Self-navigation guard */
    if (target == pm->current_page) return;

    page_id_t leaving = pm->current_page;
    bool cache_leaving = pm->pages[leaving].cacheable;

    /* Resume sensor_auto_task when leaving Playground (safety net) */
    if (leaving == PAGE_ID_PLAYGROUND) {
        pm_notify_sensor_resume();
    }

    /* For cacheable pages, skip destroy — keep screen alive off-screen.
     * For normal pages, notify about impending destruction. */
    if (cache_leaving) {
        pm->cached_screens[leaving] = lv_screen_active();
    } else {
        if (pm->pages[leaving].destroy_cb) {
            pm->pages[leaving].destroy_cb();
        }
    }

    /* Push current page to nav stack */
    if (pm->nav_top < PM_NAV_STACK_DEPTH - 1) {
        pm->nav_top++;
        pm->nav_stack[pm->nav_top] = leaving;
    }

    /* Restore cached screen or create new one */
    lv_obj_t *new_scr;
    if (pm->pages[target].cacheable && pm->cached_screens[target] != NULL) {
        new_scr = pm->cached_screens[target];
        pm->cached_screens[target] = NULL;
    } else {
        new_scr = pm->pages[target].create_cb();
    }
    if (new_scr == NULL) return;

    /* Register unload hook on leaving screen (cleanup safety net) */
    lv_obj_t *leaving_scr = lv_screen_active();
    lv_obj_remove_event_cb(leaving_scr, pm_unloaded_cb);
    lv_obj_add_event_cb(leaving_scr, pm_unloaded_cb,
                        LV_EVENT_SCREEN_UNLOADED, pm);

    /* Animate transition. auto_del=false for cacheable leaving pages. */
    pm->animating = true;
    lv_screen_load_anim(new_scr, LV_SCR_LOAD_ANIM_MOVE_LEFT,
                        PM_ANIM_TIME_MS, 0, !cache_leaving);
                        //! [j7_pm_navigate_lifecycle]

    /* Clear animation flag when new screen is fully loaded.
     * Remove stale callback first to prevent accumulation on cached screens. */
    lv_obj_remove_event_cb(new_scr, pm_loaded_cb);
    lv_obj_add_event_cb(new_scr, pm_loaded_cb, LV_EVENT_SCREEN_LOADED, pm);

    pm->current_page = target;
}

void pm_back(page_manager_t *pm)
{
    /* M1: Guard against double-tap */
    if (pm->animating) return;

    /* Nothing to go back to */
    if (pm->nav_top < 0) return;

    page_id_t leaving = pm->current_page;
    bool cache_leaving = pm->pages[leaving].cacheable;

    /* Resume sensor_auto_task when leaving Playground (safety net) */
    if (leaving == PAGE_ID_PLAYGROUND) {
        pm_notify_sensor_resume();
    }

    /* For cacheable pages, skip destroy — keep screen alive off-screen. */
    if (cache_leaving) {
        pm->cached_screens[leaving] = lv_screen_active();
    } else {
        if (pm->pages[leaving].destroy_cb) {
            pm->pages[leaving].destroy_cb();
        }
    }

    /* Pop previous page from stack */
    page_id_t prev_id = pm->nav_stack[pm->nav_top];
    pm->nav_top--;

    /* Restore cached screen or create new one */
    lv_obj_t *prev_scr;
    if (pm->pages[prev_id].cacheable && pm->cached_screens[prev_id] != NULL) {
        prev_scr = pm->cached_screens[prev_id];
        pm->cached_screens[prev_id] = NULL;
    } else {
        if (pm->pages[prev_id].create_cb == NULL) return;
        prev_scr = pm->pages[prev_id].create_cb();
    }
    if (prev_scr == NULL) return;

    /* Register unload hook on leaving screen (cleanup safety net) */
    lv_obj_t *leaving_scr = lv_screen_active();
    lv_obj_remove_event_cb(leaving_scr, pm_unloaded_cb);
    lv_obj_add_event_cb(leaving_scr, pm_unloaded_cb,
                        LV_EVENT_SCREEN_UNLOADED, pm);

    /* Reverse animation direction. auto_del=false for cacheable leaving pages. */
    pm->animating = true;
    lv_screen_load_anim(prev_scr, LV_SCR_LOAD_ANIM_MOVE_RIGHT,
                        PM_ANIM_TIME_MS, 0, !cache_leaving);

    lv_obj_remove_event_cb(prev_scr, pm_loaded_cb);
    lv_obj_add_event_cb(prev_scr, pm_loaded_cb, LV_EVENT_SCREEN_LOADED, pm);

    pm->current_page = prev_id;
}

void pm_render(page_manager_t *pm, sensorhub_snapshot_t *snap)
{
    /* Skip render during a screen transition -- but never forever. The
     * animating flag is cleared by an LV_EVENT_SCREEN_LOADED callback, and if
     * that event is ever missed the flag sticks true and the page stops
     * updating live until the user navigates away and back (which is exactly
     * the "not live" symptom on the Edge AI page). Force it clear after the
     * transition should have finished so a missed event can never freeze the
     * live render. */
    static uint16_t s_stall = 0u;
    if (pm->animating) {
        if (++s_stall < ((PM_ANIM_TIME_MS / 33u) + 8u)) {
            return;
        }
        pm->animating = false;   /* transition overran -- unblock rendering */
    }
    s_stall = 0u;

    page_id_t cur = pm->current_page;
    if (cur < pm->page_count && pm->pages[cur].render_cb) {
        pm->pages[cur].render_cb(snap);
    }
}

page_id_t pm_current(const page_manager_t *pm)
{
    return pm->current_page;
}

bool pm_is_animating(const page_manager_t *pm)
{
    return pm->animating;
}

/*******************************************************************************
 * Animation Callback
 *******************************************************************************/

static void pm_loaded_cb(lv_event_t *e)
{
    page_manager_t *pm = lv_event_get_user_data(e);
    if (pm) {
        pm->animating = false;
    }
}

/* Leaving screen finished its exit animation: detach this one-shot hook so
 * it does not accumulate on cacheable screens. NOTE: does NOT run
 * destroy_cb — pm_navigate/pm_back already did. */
static void pm_unloaded_cb(lv_event_t *e)
{
    lv_obj_t *scr = lv_event_get_target(e);
    lv_obj_remove_event_cb(scr, pm_unloaded_cb);
}

/*******************************************************************************
 * Helper: Status Bar (re-created per page)
 *******************************************************************************/

lv_obj_t *pm_create_status_bar(lv_obj_t *screen, lv_obj_t **out_status)
{
    return tesaiot_status_bar_create(screen, out_status);
}

/*******************************************************************************
 * Helper: Back Button
 *******************************************************************************/

static void back_btn_cb(lv_event_t *e)
{
    page_manager_t *pm = lv_event_get_user_data(e);
    if (pm) {
        pm_back(pm);
    }
}

lv_obj_t *pm_create_back_button(lv_obj_t *screen, page_manager_t *pm)
{
    lv_obj_t *btn = lv_button_create(screen);
    lv_obj_set_size(btn, 80, 36);
    lv_obj_set_pos(btn, UI_SPACE_MD, UI_STATUS_BAR_H + UI_SPACE_XS);
    lv_obj_set_style_bg_color(btn, lv_color_hex(UI_COLOR_BG_CARD), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, UI_CARD_RADIUS, 0);
    lv_obj_set_style_border_width(btn, 0, 0);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, LV_SYMBOL_LEFT " Home");
    lv_obj_set_style_text_color(lbl, lv_color_hex(UI_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(lbl, UI_FONT_CAPTION, 0);
    lv_obj_center(lbl);

    lv_obj_add_event_cb(btn, back_btn_cb, LV_EVENT_CLICKED, pm);

    return btn;
}

/*******************************************************************************
 * Swipe-Right Gesture Callback (back navigation on inner pages)
 *******************************************************************************/

static void swipe_back_cb(lv_event_t *e)
{
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    if (dir == LV_DIR_RIGHT) {
        /* Skip back-nav if the user is interacting with a child widget
         * (e.g., dragging a volume slider knob).  Only trigger when the
         * swipe starts on the screen background / empty area. */
        lv_obj_t *pressed = lv_indev_get_active_obj();
        lv_obj_t *screen  = lv_event_get_target(e);
        if (pressed != NULL && pressed != screen) return;

        page_manager_t *pm = lv_event_get_user_data(e);
        if (pm && pm->nav_top >= 0) {
            pm_back(pm);
        }
    }
}

/*******************************************************************************
 * Helper: Full Page Header (status bar + back button + title + content area)
 *******************************************************************************/

/* Header height: status bar (32) + back button row (40) = 72px */
#define PM_HEADER_H  (UI_STATUS_BAR_H + 40)

lv_obj_t *pm_create_page_with_header(lv_obj_t *screen, page_manager_t *pm,
                                      const char *title, uint32_t title_color)
{
    /* Screen bg */
    lv_obj_set_style_bg_color(screen, lv_color_hex(UI_COLOR_BG_DEEP), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    /* Status bar at top (32px) */
    pm_create_status_bar(screen, &pm->status_lbl);

    /* Back button (below status bar) */
    pm_create_back_button(screen, pm);

    /* Title label (right of back button) */
    lv_obj_t *title_lbl = lv_label_create(screen);
    lv_label_set_text(title_lbl, title);
    lv_obj_set_style_text_color(title_lbl, lv_color_hex(title_color), 0);
    lv_obj_set_style_text_font(title_lbl, UI_FONT_H2, 0);
    lv_obj_set_pos(title_lbl, 100, UI_STATUS_BAR_H + UI_SPACE_SM);

    /* Time + WiFi icon (right-aligned, vertically centered in status bar) */
    {
        int topbar_y = (UI_STATUS_BAR_H - 14) / 2;  /* Center 14px text in 32px bar */
        int rx = UI_SCREEN_W - UI_SPACE_MD;

        /* Connectivity icon — Bluetooth when the Bento Desktop Buddy
         * variant is compiled in (BLE exclusive, WiFi is guarded off),
         * WiFi otherwise. Same label slot, same name (kept as wifi_lbl
         * for backwards-compat with code that toggles visibility). */
        rx -= 24;
        pm->wifi_lbl = lv_label_create(screen);
#if defined(ENABLE_PAGE_BENTO_BUDDY) && (ENABLE_PAGE_BENTO_BUDDY == 1)
        lv_label_set_text(pm->wifi_lbl, LV_SYMBOL_BLUETOOTH);
        lv_obj_set_style_text_color(pm->wifi_lbl,
                                     lv_color_hex(UI_COLOR_ACCENT_CYAN), 0);
#else
        lv_label_set_text(pm->wifi_lbl, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_color(pm->wifi_lbl,
                                     lv_color_hex(UI_COLOR_ACCENT_GREEN), 0);
#endif
        lv_obj_set_style_text_font(pm->wifi_lbl, UI_FONT_BODY, 0);
        lv_obj_set_pos(pm->wifi_lbl, rx, topbar_y);
        lv_obj_add_flag(pm->wifi_lbl, LV_OBJ_FLAG_HIDDEN);

        /* Time label */
        rx -= 12;
        pm->time_lbl = lv_label_create(screen);
        lv_label_set_text(pm->time_lbl, "");
        lv_obj_set_style_text_font(pm->time_lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(pm->time_lbl,
                                     lv_color_hex(UI_COLOR_TEXT_SECONDARY), 0);
        lv_obj_set_style_text_align(pm->time_lbl, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(pm->time_lbl, rx - 140, topbar_y);
        lv_obj_set_width(pm->time_lbl, 140);
        lv_obj_add_flag(pm->time_lbl, LV_OBJ_FLAG_HIDDEN);
    }

    /* Content area (below header, fills remaining height) */
    lv_obj_t *content = lv_obj_create(screen);
    lv_obj_set_pos(content, 0, PM_HEADER_H);
    lv_obj_set_size(content, UI_SCREEN_W, UI_SCREEN_H - PM_HEADER_H);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    /* Enable swipe-right gesture for back navigation */
    lv_obj_add_event_cb(screen, swipe_back_cb, LV_EVENT_GESTURE, pm);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);

    return content;
}
