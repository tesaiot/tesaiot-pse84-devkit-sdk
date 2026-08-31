/*******************************************************************************
 * File Name: page_playground.c
 *
 * Description: Playground page — merged UX/UI sandbox + Console/LCD terminal.
 *              Page-based replacement for tab_uxui + terminal tab.
 *
 *              Provides scrollable container for MicroPython-created widgets
 *              (via IPC ui.* commands) and emergency controls (DELETE/RESTART).
 *
 *              Terminal is lazy-created by ipc_lcd module on first lcd.print().
 *              Console Log button toggles terminal panel visibility via
 *              ipc_lcd_toggle_panel() API.
 *
 *              IPC container init: deferred to first visit (singleton pattern).
 *              On destroy: marks containers invalid so IPC handlers don't write
 *              to destroyed LVGL objects.
 *
 *******************************************************************************/

#include "page_playground.h"
#include "page_manager.h"
#include "tesaiot_ui_theme.h"
#include "tesaiot_ui_helpers.h"
#include "ui_widget_mgr.h"
#include "ipc_ui.h"
#include "ipc_lcd.h"
#include "ipc_communication.h"
#include "cy_ipc_pipe.h"
#include "cy_syslib.h"
#include "lvgl.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

/*******************************************************************************
 * Module-Static State
 *******************************************************************************/
static lv_obj_t *s_uxui_container     = NULL;
static lv_obj_t *s_terminal_container = NULL;

/* Console/UI toggle state */
static lv_obj_t *s_console_btn_lbl    = NULL;   /* Label inside console toggle button */
static lv_obj_t *s_console_badge      = NULL;   /* Red notification dot on console button */
static bool      s_console_mode       = false;   /* false=UI view (default), true=Console */

/* Delete confirmation overlay (on screen, outside widget manager) */
static lv_obj_t  *s_delete_overlay    = NULL;
static lv_timer_t *s_delete_timer     = NULL;

/*******************************************************************************
 * Emergency Control Buttons
 *******************************************************************************/
enum {
    UXUI_CTRL_DELETE_MAIN = 1,
    UXUI_CTRL_RESTART = 2,
};

static void send_ipc_cmd(uint32_t ipc_cmd)
{
    memset(&g_pm_ipc_msg_shared, 0, sizeof(g_pm_ipc_msg_shared));
    g_pm_ipc_msg_shared.client_id = CM33_IPC_SENSOR_CTRL_CLIENT_ID;
    g_pm_ipc_msg_shared.intr_mask = CY_IPC_CYPIPE_INTR_MASK_EP1;
    g_pm_ipc_msg_shared.cmd = ipc_cmd;

    for (int i = 0; i < 8; i++) {
        cy_en_ipc_pipe_status_t st = Cy_IPC_Pipe_SendMessage(
            CM33_IPC_PIPE_EP_ADDR, CM55_IPC_PIPE_EP_ADDR,
            (void *)&g_pm_ipc_msg_shared, NULL);
        if (st == CY_IPC_PIPE_SUCCESS) break;
        Cy_SysLib_DelayUs(200);
    }
}

/*******************************************************************************
 * Delete Confirmation: hard reset after showing overlay message.
 *
 * IPC_CMD_DELETE_MAIN_PY was sent 3 seconds ago.  CM33_NS has had time to
 * consume s_delete_pending via tacp_poll_uart(), trigger a soft reset, and
 * delete main.py.  NVIC_SystemReset() now reboots all cores cleanly —
 * the board boots to Home with main.py removed.
 *
 * This replaces the previous pm_navigate(HOME) approach which could freeze
 * if pm_is_animating() never cleared or the page state was inconsistent.
 *******************************************************************************/
static void delete_home_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    NVIC_SystemReset();
    /* Unreachable */
}

static void uxui_ctrl_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    uintptr_t cmd = (uintptr_t)lv_event_get_user_data(e);
    if (cmd == UXUI_CTRL_DELETE_MAIN) {
        /* Guard: ignore if already deleting */
        if (s_delete_overlay != NULL) return;

        /* Send delete request to CM33_NS — IPC handler sets s_delete_pending.
         * CM33_NS will consume it via tacp_poll_uart() and soft-reset to
         * delete main.py during boot. */
        ui_widget_mgr_clear_all();
        send_ipc_cmd(IPC_CMD_DELETE_MAIN_PY);

        /* Show confirmation overlay while waiting for CM33_NS */
        lv_obj_t *scr = lv_obj_get_screen(lv_event_get_target(e));

        s_delete_overlay = lv_obj_create(scr);
        lv_obj_set_size(s_delete_overlay, UI_SCREEN_W, UI_SCREEN_H);
        lv_obj_set_pos(s_delete_overlay, 0, 0);
        lv_obj_set_style_bg_color(s_delete_overlay,
                                  lv_color_hex(UI_COLOR_BG_DEEP), 0);
        lv_obj_set_style_bg_opa(s_delete_overlay, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_delete_overlay, 0, 0);
        lv_obj_remove_flag(s_delete_overlay, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *icon = lv_label_create(s_delete_overlay);
        lv_label_set_text(icon, LV_SYMBOL_OK);
        lv_obj_set_style_text_font(icon, UI_FONT_DISPLAY, 0);
        lv_obj_set_style_text_color(icon,
                                    lv_color_hex(UI_COLOR_ACCENT_GREEN), 0);
        lv_obj_align(icon, LV_ALIGN_CENTER, 0, -40);

        lv_obj_t *title = lv_label_create(s_delete_overlay);
        lv_label_set_text(title, "Delete Requested");
        lv_obj_set_style_text_font(title, UI_FONT_H1, 0);
        lv_obj_set_style_text_color(title,
                                    lv_color_hex(UI_COLOR_TEXT_PRIMARY), 0);
        lv_obj_align(title, LV_ALIGN_CENTER, 0, 10);

        lv_obj_t *sub = lv_label_create(s_delete_overlay);
        lv_label_set_text(sub,
                          "Saved main.py will be removed if present.\n"
                          "Board will reboot in a moment.");
        lv_obj_set_style_text_font(sub, UI_FONT_BODY, 0);
        lv_obj_set_style_text_color(sub,
                                    lv_color_hex(UI_COLOR_TEXT_DISABLED), 0);
        lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(sub, LV_ALIGN_CENTER, 0, 44);

        /* Force LVGL to render the overlay to display, then block this
         * task for 3 seconds so CM33_NS has time to soft-reset and delete
         * main.py.  Finally, hard-reset all cores for a clean reboot. */
        lv_refr_now(NULL);
        vTaskDelay(pdMS_TO_TICKS(3000));
        NVIC_SystemReset();
        return;  /* unreachable */
    }

    //! [ipc_ui_widget_mgr_clear_all_restart]
    /* ...context: inside uxui_ctrl_event_cb() - GFX task context ... */
    if (cmd == UXUI_CTRL_RESTART) {
        /* Soft restart: clear widgets, then tell CM33_NS to re-run main.py.
         * No NVIC_SystemReset — CM55/WiFi/sensors stay alive. */
        ui_widget_mgr_clear_all();
        send_ipc_cmd(IPC_CMD_RESTART_SCRIPT);
    }
    //! [ipc_ui_widget_mgr_clear_all_restart]
}

/*******************************************************************************
 * Console / UI Toggle — mutual exclusion between widget view and console
 *
 * UI mode (default): widgets visible, console hidden, button shows LIST icon
 * Console mode:      widgets hidden, console visible, button shows EYE icon
 *******************************************************************************/
//! [ipc_lcd_console_widget_toggle]
static void console_toggle_cb(lv_event_t *e)
{
    (void)e;
    s_console_mode = !s_console_mode;

    if (s_console_mode) {
        /* Switch to Console view — clear unread badge */
        ipc_lcd_clear_unread();
        if (s_console_badge) {
            lv_obj_add_flag(s_console_badge, LV_OBJ_FLAG_HIDDEN);
        }
        ui_widget_mgr_set_all_visible(false);
        if (!ipc_lcd_is_panel_visible()) {
            ipc_lcd_toggle_panel();
        }
    } else {
        /* Switch to UI view */
        if (ipc_lcd_is_panel_visible()) {
            ipc_lcd_toggle_panel();
        }
        ui_widget_mgr_set_all_visible(true);
    }

    /* Update button icon */
    if (s_console_btn_lbl) {
        lv_label_set_text(s_console_btn_lbl,
                          s_console_mode ? LV_SYMBOL_EYE_OPEN : LV_SYMBOL_LIST);
    }
}
//! [ipc_lcd_console_widget_toggle]

/*******************************************************************************
 * Control Button Helper
 *******************************************************************************/
static lv_obj_t *create_ctrl_btn(lv_obj_t *parent, const char *icon,
                                 int x, uint32_t color, uintptr_t cmd)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 40, 30);
    lv_obj_set_pos(btn, x, 2);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(btn, uxui_ctrl_event_cb, LV_EVENT_CLICKED,
                        (void *)cmd);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, icon);
    lv_obj_set_style_text_color(lbl, lv_color_hex(UI_COLOR_TEXT_PRIMARY), 0);
    lv_obj_center(lbl);
    return btn;
}

/*******************************************************************************
 * page_playground_create
 *******************************************************************************/
lv_obj_t *page_playground_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    page_manager_t *pm = pm_get_instance();
    lv_obj_t *content = pm_create_page_with_header(scr, pm,
                            "BENTO Playground", UI_COLOR_ACCENT);

    /* Make content scrollable for MicroPython widgets */
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(content, LV_DIR_VER | LV_DIR_HOR);
    lv_obj_set_style_pad_all(content, 4, 0);

    //! [ipc_container_bind_on_create]
    /* ...context: inside page_playground_create() ... */
    /* Store as UX/UI container for IPC widget creation */
    s_uxui_container = content;

    /* Terminal container: same as content — ipc_lcd creates terminal inside */
    s_terminal_container = content;

    /* Bind containers to IPC handlers (deferred from boot) */
    ipc_ui_set_container(content);
    ipc_lcd_set_container(content);
    //! [ipc_container_bind_on_create]

    /* Control buttons — header level (right-aligned, where Console btn used to be) */
    {
        /* Play button (rightmost) — soft-restart main.py */
        lv_obj_t *rbtn = lv_button_create(scr);
        lv_obj_set_size(rbtn, 40, 32);
        lv_obj_set_pos(rbtn, UI_SCREEN_W - 40 - UI_SPACE_SM,
                        UI_STATUS_BAR_H + UI_SPACE_XS);
        lv_obj_set_style_bg_color(rbtn, lv_color_hex(0x28A745), 0);
        lv_obj_set_style_bg_opa(rbtn, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(rbtn, UI_CARD_RADIUS, 0);
        lv_obj_add_event_cb(rbtn, uxui_ctrl_event_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)UXUI_CTRL_RESTART);
        lv_obj_t *rlbl = lv_label_create(rbtn);
        lv_label_set_text(rlbl, LV_SYMBOL_PLAY);
        lv_obj_set_style_text_color(rlbl, lv_color_white(), 0);
        lv_obj_center(rlbl);

        /* Delete button (left of Restart) */
        lv_obj_t *dbtn = lv_button_create(scr);
        lv_obj_set_size(dbtn, 40, 32);
        lv_obj_set_pos(dbtn, UI_SCREEN_W - 40 - UI_SPACE_SM - 40 - UI_SPACE_SM,
                        UI_STATUS_BAR_H + UI_SPACE_XS);
        lv_obj_set_style_bg_color(dbtn, lv_color_hex(UI_COLOR_ACCENT_RED), 0);
        lv_obj_set_style_bg_opa(dbtn, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(dbtn, UI_CARD_RADIUS, 0);
        lv_obj_add_event_cb(dbtn, uxui_ctrl_event_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)UXUI_CTRL_DELETE_MAIN);
        lv_obj_t *dlbl = lv_label_create(dbtn);
        lv_label_set_text(dlbl, LV_SYMBOL_TRASH);
        lv_obj_set_style_text_color(dlbl, lv_color_hex(UI_COLOR_TEXT_PRIMARY), 0);
        lv_obj_center(dlbl);
    }

    /* Console/UI toggle button — bottom-right corner, large touch target.
     * Default = UI view (LIST icon). Press to toggle to Console view (EYE icon). */
    {
        s_console_mode = false;  /* Always start in UI view */

        lv_obj_t *cbtn = lv_button_create(scr);
        lv_obj_set_size(cbtn, 80, UI_TOUCH_MIN);   /* 80×48: easy to tap */
        lv_obj_set_pos(cbtn, UI_SCREEN_W - 80 - UI_SPACE_MD,
                        UI_SCREEN_H - UI_TOUCH_MIN - UI_SPACE_MD);
        lv_obj_set_style_bg_color(cbtn,
                                  lv_color_hex(UI_COLOR_ACCENT_GREEN), 0);
        lv_obj_set_style_bg_opa(cbtn, LV_OPA_90, 0);
        lv_obj_set_style_radius(cbtn, UI_CARD_RADIUS, 0);
        lv_obj_set_style_shadow_width(cbtn, 8, 0);
        lv_obj_set_style_shadow_opa(cbtn, LV_OPA_30, 0);
        lv_obj_add_event_cb(cbtn, console_toggle_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_set_style_clip_corner(cbtn, false, 0);
        lv_obj_add_flag(cbtn, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

        lv_obj_t *lbl = lv_label_create(cbtn);
        lv_label_set_text(lbl, LV_SYMBOL_LIST);
        lv_obj_set_style_text_color(lbl,
                                    lv_color_hex(UI_COLOR_TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(lbl, UI_FONT_BODY, 0);
        lv_obj_center(lbl);
        s_console_btn_lbl = lbl;

        /* Notification badge — red dot at top-right corner, half outside button */
        s_console_badge = lv_obj_create(scr);
        lv_obj_set_size(s_console_badge, 18, 18);
        lv_obj_set_style_radius(s_console_badge, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(s_console_badge,
                                  lv_color_hex(UI_COLOR_ACCENT_RED), 0);
        lv_obj_set_style_bg_opa(s_console_badge, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_console_badge, 0, 0);
        lv_obj_remove_flag(s_console_badge, LV_OBJ_FLAG_SCROLLABLE
                                          | LV_OBJ_FLAG_CLICKABLE);
        /* Position: top-right corner of cbtn, half-overlapping outside */
        lv_coord_t bx = UI_SCREEN_W - 80 - UI_SPACE_MD;   /* cbtn x */
        lv_coord_t by = UI_SCREEN_H - UI_TOUCH_MIN - UI_SPACE_MD; /* cbtn y */
        lv_obj_set_pos(s_console_badge, bx + 80 - 9, by - 9);
        lv_obj_add_flag(s_console_badge, LV_OBJ_FLAG_HIDDEN);  /* Hidden by default */
    }

    return scr;
}

/*******************************************************************************
 * page_playground_render (no-op — widgets are event-driven via IPC)
 *******************************************************************************/
//! [ipc_lcd_has_unread_render]
void page_playground_render(sensorhub_snapshot_t *snap)
{
    (void)snap;

    /* Show/hide notification badge when new console text arrives while hidden */
    if (s_console_badge && !s_console_mode) {
        if (ipc_lcd_has_unread()) {
            lv_obj_remove_flag(s_console_badge, LV_OBJ_FLAG_HIDDEN);
        }
    }
}
//! [ipc_lcd_has_unread_render]

/*******************************************************************************
 * page_playground_destroy
 *******************************************************************************/
//! [ipc_container_unbind_on_destroy]
void page_playground_destroy(void)
{
    /* Unbind containers from IPC handlers — prevents writing to stale objects.
     * The actual LVGL objects are destroyed by lv_screen_load_anim(auto_del). */
    ipc_ui_set_container(NULL);
    ipc_lcd_set_container(NULL);
    s_uxui_container     = NULL;
    s_terminal_container = NULL;
    //! [ipc_container_unbind_on_destroy]
    s_console_btn_lbl    = NULL;
    s_console_badge      = NULL;
    s_console_mode       = false;

    /* Cancel pending delete timer (screen about to be destroyed) */
    if (s_delete_timer) {
        lv_timer_delete(s_delete_timer);
        s_delete_timer = NULL;
    }
    s_delete_overlay = NULL;  /* Destroyed with screen by auto_del */
}

/*******************************************************************************
 * Container Getters (for IPC handlers)
 *******************************************************************************/
lv_obj_t *page_playground_get_uxui_container(void)
{
    return s_uxui_container;
}

lv_obj_t *page_playground_get_terminal_container(void)
{
    return s_terminal_container;
}
