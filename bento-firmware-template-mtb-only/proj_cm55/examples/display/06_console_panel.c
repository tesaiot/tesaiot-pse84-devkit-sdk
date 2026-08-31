/* sdk-example: core=cm55 variant=both group=display
 * id:      cm55/display/06_console_panel
 * title:   Show the MicroPython console over your page
 * teaches: bind the lcd.print() terminal to a container, flip between console and widgets, and badge output that arrived while it was hidden
 * apis:    ipc_lcd_set_container, ipc_lcd_toggle_panel, ipc_lcd_is_panel_visible, ipc_lcd_has_unread, ipc_lcd_clear_unread, ipc_lcd_reset_auto_nav, ui_widget_mgr_set_all_visible, ui_widget_mgr_create, ipc_ui_set_container
 * entry:   example_ipc_core_console_panel
 */
/*******************************************************************************
 * ipc_core/06_console_panel — the other half of the Playground page.
 *
 * ipc_lcd receives lcd.print() text from CM33_NS and renders it into a
 * terminal-like panel. Like ipc_ui it is initialised at boot with a NULL
 * container and binds to a page later, so the whole of this API is about one
 * question: WHERE does the console draw, and is it in front right now.
 *
 * WHAT THE PANEL ACTUALLY IS
 * --------------------------
 * A single lv_obj sized 100% x 100% of the container you bind, created lazily
 * the first time ipc_lcd_toggle_panel() is called, with 42 px of top padding
 * reserved so a header-level control can sit above it. It is NOT a widget in
 * the ui_widget_mgr handle table — ui_widget_mgr_clear_all() does not touch
 * it, and ui_widget_mgr_set_all_visible() does not hide it. That separation is
 * the whole design: one call hides all the widgets, the console stays, and you
 * have a console/UI toggle in two lines.
 *
 * SO PUT YOUR TOGGLE OUTSIDE THE MANAGED SET
 * ------------------------------------------
 * The button below is a plain lv_button_create() and is deliberately NOT a
 * managed widget: a toggle that hides itself is a page you cannot leave. The
 * shipped Playground page solves this by putting its console button on the
 * screen above the container; an example only gets the container, so this one
 * stays reachable with lv_obj_move_foreground() after every toggle.
 *
 * THE UNREAD FLAG
 * ---------------
 * ipc_lcd_has_unread() goes true when text arrives while the panel is hidden,
 * and only ipc_lcd_clear_unread() clears it. That is a notification badge, and
 * it is the reason a hidden console is not a lost console.
 *
 * ipc_lcd_reset_auto_nav() re-arms the ONE-SHOT jump to the Playground page
 * that the first lcd.print() of a session performs. Call it when a new
 * MicroPython program starts; otherwise the second program of the session
 * prints into a page nobody switched to.
 *******************************************************************************/

#include "../sdk_examples.h"

#include <string.h>

#include "ipc_lcd.h"
#include "ipc_ui.h"
#include "ipc_ui_protocol.h"
#include "ui_widget_mgr.h"

#define BADGE_POLL_MS   (250)

static lv_obj_t   *s_toggle;
static lv_obj_t   *s_toggle_lbl;
static lv_obj_t   *s_badge;
static lv_timer_t *s_timer;

static void container_deleted_cb(lv_event_t *e)
{
    (void)e;
    /* BOTH receivers hold a raw pointer to this container. Leaving either
     * bound to a freed object is how ipc_lcd ends up writing spans into
     * deleted LVGL memory on the next lcd.print(). */
    ipc_lcd_set_container(NULL);
    ipc_ui_set_container(NULL);
}

static void toggle_deleted_cb(lv_event_t *e)
{
    (void)e;
    if (s_timer != NULL) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    s_toggle = NULL;
    s_toggle_lbl = NULL;
    s_badge = NULL;
}

/* Put the console in front, or the widgets, never both. */
static void apply_view(bool console_visible)
{
    /* One call covers every widget in the handle table, whatever page or
     * program created them. This is the Console/UI switch. */
    ui_widget_mgr_set_all_visible(!console_visible);

    if (console_visible) {
        /* Opening the console is what "reading" means, so the badge clears. */
        ipc_lcd_clear_unread();
        if (s_badge != NULL) {
            lv_obj_add_flag(s_badge, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_toggle_lbl != NULL) {
        lv_label_set_text(s_toggle_lbl,
                          console_visible ? "Show widgets" : "Show console");
    }
    /* The terminal panel is 100% x 100% of this container and was created
     * after our button, so it is later in the child list and draws on top.
     * Lift the controls back over it. */
    if (s_toggle != NULL) {
        lv_obj_move_foreground(s_toggle);
    }
    if (s_badge != NULL) {
        lv_obj_move_foreground(s_badge);
    }
}

static void toggle_cb(lv_event_t *e)
{
    (void)e;
    /* toggle_panel() force-creates the terminal on first use, then flips
     * LV_OBJ_FLAG_HIDDEN. It does not tell you which way it went — ask. */
    ipc_lcd_toggle_panel();
    apply_view(ipc_lcd_is_panel_visible());
}

static void badge_cb(lv_timer_t *t)
{
    (void)t;
    if (s_badge == NULL) {
        return;
    }
    const bool show = ipc_lcd_has_unread() && !ipc_lcd_is_panel_visible();
    if (show) {
        lv_obj_remove_flag(s_badge, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_badge, LV_OBJ_FLAG_HIDDEN);
    }
}

int example_ipc_core_console_panel(lv_obj_t *parent)
{
    /* Bind both receivers to this page. ipc_ui first, because it clears the
     * previous tenant's widgets and we want something of our own to hide. */
    ipc_ui_set_container(parent);
    ipc_lcd_set_container(parent);
    lv_obj_remove_event_cb(parent, container_deleted_cb);
    lv_obj_add_event_cb(parent, container_deleted_cb, LV_EVENT_DELETE, NULL);

    /* A new program is starting as far as the console is concerned, so let the
     * next lcd.print() navigate here again. */
    ipc_lcd_reset_auto_nav();

    if (s_timer != NULL) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }

    /* Three managed widgets, purely so set_all_visible() has something to act
     * on and you can see it work. */
    ipc_ui_create_t cfg;
    static const char *const demo[3] = {
        "widget layer, line 1", "widget layer, line 2", "widget layer, line 3"
    };
    for (int i = 0; i < 3; i++) {
        memset(&cfg, 0, sizeof(cfg));
        cfg.widget_type = UI_WIDGET_LABEL;
        cfg.x = 12; cfg.y = (int16_t)(56 + i * 30);
        cfg.color = 0xD8E0F0;
        cfg.init_val = 18;
        lv_snprintf(cfg.text, sizeof(cfg.text), "%s", demo[i]);
        if (ui_widget_mgr_create(&cfg) < 0) {
            sdk_example_logf("widget create failed - no container bound?");
            return SDK_EX_REFUSED;
        }
    }

    /* The toggle. Plain LVGL on purpose — see the header block. */
    s_toggle = lv_button_create(parent);
    lv_obj_set_size(s_toggle, 150, 34);
    lv_obj_set_pos(s_toggle, 8, 4);
    lv_obj_add_flag(s_toggle, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_event_cb(s_toggle, toggle_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_toggle, toggle_deleted_cb, LV_EVENT_DELETE, NULL);
    s_toggle_lbl = lv_label_create(s_toggle);
    lv_label_set_text(s_toggle_lbl, "Show console");
    lv_obj_center(s_toggle_lbl);

    s_badge = lv_label_create(parent);
    lv_label_set_text(s_badge, "new output");
    lv_obj_set_style_text_color(s_badge, lv_color_hex(0xFFB300), 0);
    lv_obj_set_pos(s_badge, 168, 12);
    lv_obj_add_flag(s_badge, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(s_badge, LV_OBJ_FLAG_HIDDEN);

    /* Open the console once so the panel actually exists and the developer
     * sees it without hunting for the button. */
    ipc_lcd_toggle_panel();
    const bool visible = ipc_lcd_is_panel_visible();
    apply_view(visible);

    s_timer = lv_timer_create(badge_cb, BADGE_POLL_MS, NULL);
    if (s_timer == NULL) {
        sdk_example_logf("lv_timer_create failed - unread badge will not update");
        return SDK_EX_REFUSED;
    }

    sdk_example_logf("console bound to this page; panel %s",
                     visible ? "created and shown" : "COULD NOT BE CREATED");
    sdk_example_logf("  widgets hidden by ui_widget_mgr_set_all_visible(false)");
    sdk_example_logf("  unread flag polled every %d ms", BADGE_POLL_MS);
    sdk_example_logf("run lcd.print() from MicroPython to fill it");

    if (!visible) {
        /* Honest: the only failure this API can report is that the panel did
         * not appear, and it appears only if ipc_lcd has a container and LVGL
         * had memory for it. */
        return SDK_EX_UNAVAILABLE;
    }
    return SDK_EX_STARTED;
}
