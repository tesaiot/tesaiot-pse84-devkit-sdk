/*******************************************************************************
 * File Name: page_examples.c
 *
 * Description: SDK Examples page — a scrollable list of every example in the
 *              SDK, grouped by library, with a detail view that runs the one
 *              you tapped and shows what the SDK actually returned.
 *
 *              Two views live in this page, swapped by hiding one and showing
 *              the other:
 *
 *                LIST    every example, grouped by module, newest question
 *                        first: which library, what does it teach, which
 *                        functions does it call.
 *                DETAIL  one example: what it teaches, the APIs it exercises,
 *                        a Run button, a drawing area the example may use, and
 *                        the report the example wrote.
 *
 *              The Run button is at the TOP of the detail view, above the
 *              drawing area, because the primary action must never be below the
 *              fold — the drawing area is what grows, so putting Run under it
 *              would push Run off-screen for exactly the examples that draw
 *              most.
 *
 * WHY A CM33 ROW IS STILL LISTED
 * ------------------------------
 * ble_nus, mpy_secure and tesaiot_hsm are v8-M soft-float archives; this core
 * is cortex-m55 hard-float. Linking one into this image fails at the link step,
 * which is the good outcome. Their rows carry run == NULL and the detail view
 * shows the console command that does run them instead. The product owner's
 * requirement is that a developer can see which libraries and functions they
 * may call — that is a question about the SDK, not about this core.
 *
 *******************************************************************************/

#include "page_examples.h"

#include <stdbool.h>
#include <stddef.h>

#include "page_manager.h"
#include "tesaiot_ui_theme.h"
#include "sdk_examples.h"

/* The examples may leave widgets behind in the ipc_core widget manager. The
 * page owns putting that right on the way out — see page_examples_destroy(). */
#include "ui_widget_mgr.h"

/*******************************************************************************
 * State
 *******************************************************************************/
typedef struct {
    lv_obj_t *screen;
    lv_obj_t *content;      /* from pm_create_page_with_header()             */
    lv_obj_t *list;         /* LIST view root (scrollable column)            */
    lv_obj_t *detail;       /* DETAIL view root (hidden while listing)       */

    lv_obj_t *d_title;
    lv_obj_t *d_meta;       /* module + id                                   */
    lv_obj_t *d_teaches;
    lv_obj_t *d_apis;
    lv_obj_t *d_run_btn;
    lv_obj_t *d_run_lbl;
    lv_obj_t *d_result;     /* return code + strerror                        */
    lv_obj_t *d_canvas;     /* the `parent` handed to the example            */
    lv_obj_t *d_log;        /* sdk_example_log_text()                        */

    int       selected;     /* index into g_sdk_examples, -1 = none          */
    bool      widgets_used; /* an example touched ui_widget_mgr              */
} page_examples_ctx_t;

static page_examples_ctx_t s_ctx;

/*******************************************************************************
 * Helpers
 *******************************************************************************/
static lv_obj_t *plain_box(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    return o;
}

static lv_obj_t *caption(lv_obj_t *parent, const char *text, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, (text != NULL) ? text : "");
    lv_obj_set_style_text_font(l, UI_FONT_CAPTION, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    return l;
}

/*******************************************************************************
 * DETAIL view
 *******************************************************************************/
static void show_list(void);

static void back_to_list_cb(lv_event_t *e)
{
    (void)e;
    show_list();
}

/* Run the selected example. We are in the GFX task inside an LVGL event
 * callback, which is the only context where an example may touch LVGL — and
 * the reason every example is forbidden to block. */
static void run_cb(lv_event_t *e)
{
    (void)e;
    if ((s_ctx.selected < 0) || ((unsigned)s_ctx.selected >= g_sdk_example_count)) {
        return;
    }
    const sdk_example_t *ex = &g_sdk_examples[s_ctx.selected];
    if (ex->run == NULL) {
        return;                     /* CM33-owned; the button is not shown */
    }

    /* Clear both the report and anything the previous run drew, so what is on
     * screen always belongs to the run whose result is displayed beside it. */
    sdk_example_log_clear();
    if (s_ctx.d_canvas != NULL) {
        lv_obj_clean(s_ctx.d_canvas);
    }

    int rc = ex->run(s_ctx.d_canvas);
    s_ctx.widgets_used = true;      /* conservative: assume it may have */

    if (s_ctx.d_result != NULL) {
        lv_label_set_text_fmt(s_ctx.d_result, "%s  (%d — %s)",
                              (rc == SDK_EX_OK) ? "OK" : "returned",
                              rc, sdk_example_strerror(rc));
        lv_obj_set_style_text_color(
            s_ctx.d_result,
            lv_color_hex((rc == SDK_EX_OK)      ? UI_COLOR_ACCENT_GREEN
                         : (rc == SDK_EX_STARTED) ? UI_COLOR_ACCENT_CYAN
                                                  : UI_COLOR_ACCENT_ORANGE),
            0);
    }
    if (s_ctx.d_log != NULL) {
        const char *txt = sdk_example_log_text();
        lv_label_set_text(s_ctx.d_log, (txt[0] != '\0')
                          ? txt
                          : "(this example reported nothing)");
    }
}

static void show_detail(int index)
{
    if ((index < 0) || ((unsigned)index >= g_sdk_example_count)) {
        return;
    }
    const sdk_example_t *ex = &g_sdk_examples[index];
    s_ctx.selected = index;

    lv_label_set_text(s_ctx.d_title, ex->title);
    lv_obj_set_style_text_color(s_ctx.d_title,
                                lv_color_hex(sdk_group_color(ex->group)), 0);
    lv_label_set_text_fmt(s_ctx.d_meta, "%s   ·   %s",
                          sdk_group_name(ex->group), ex->id);
    lv_label_set_text(s_ctx.d_teaches, ex->teaches);
    lv_label_set_text_fmt(s_ctx.d_apis, "APIs: %s", ex->apis);

    sdk_example_log_clear();
    lv_obj_clean(s_ctx.d_canvas);
    lv_label_set_text(s_ctx.d_log, "");

    if (ex->run != NULL) {
        lv_obj_remove_flag(s_ctx.d_run_btn, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_ctx.d_result, "not run yet");
        lv_obj_set_style_text_color(s_ctx.d_result,
                                    lv_color_hex(UI_COLOR_TEXT_DISABLED), 0);
    } else {
        /* CM33-owned: there is nothing to press. Say where it does run, in the
         * exact shape a developer can paste into a terminal. */
        lv_obj_add_flag(s_ctx.d_run_btn, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(s_ctx.d_result,
                              "Runs on CM33_NS, not on this core.\n%s",
                              (ex->where != NULL) ? ex->where : "");
        lv_obj_set_style_text_color(s_ctx.d_result,
                                    lv_color_hex(UI_COLOR_ACCENT_CYAN), 0);
    }

    lv_obj_add_flag(s_ctx.list, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_ctx.detail, LV_OBJ_FLAG_HIDDEN);
    lv_obj_scroll_to_y(s_ctx.detail, 0, LV_ANIM_OFF);
}

static void show_list(void)
{
    lv_obj_add_flag(s_ctx.detail, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_ctx.list, LV_OBJ_FLAG_HIDDEN);
}

static void row_click_cb(lv_event_t *e)
{
    show_detail((int)(intptr_t)lv_event_get_user_data(e));
}

static void build_detail(lv_obj_t *parent)
{
    lv_obj_t *d = lv_obj_create(parent);
    lv_obj_set_pos(d, 0, 0);
    lv_obj_set_size(d, UI_SCREEN_W, lv_obj_get_height(parent));
    lv_obj_set_style_bg_opa(d, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(d, 0, 0);
    lv_obj_set_style_pad_all(d, UI_SPACE_MD, 0);
    lv_obj_set_style_pad_row(d, UI_SPACE_SM, 0);
    lv_obj_set_flex_flow(d, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(d, LV_DIR_VER);
    lv_obj_add_flag(d, LV_OBJ_FLAG_HIDDEN);
    s_ctx.detail = d;

    /* Top row: back-to-list on the LEFT, then the Run button. Both are primary
     * actions and both stay above the drawing area. */
    lv_obj_t *bar = plain_box(d);
    lv_obj_set_size(bar, UI_CARD_FULL_W, UI_TOUCH_PRIMARY);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(bar, UI_TOUCH_SPACING, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = lv_button_create(bar);
    lv_obj_set_size(back, 150, UI_TOUCH_MIN);
    lv_obj_set_style_bg_color(back, lv_color_hex(UI_COLOR_BG_SURFACE), 0);
    lv_obj_set_style_radius(back, UI_RADIUS_MD, 0);
    lv_obj_add_event_cb(back, back_to_list_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_LEFT "  All examples");
    lv_obj_set_style_text_font(bl, UI_FONT_CAPTION, 0);
    lv_obj_center(bl);

    s_ctx.d_run_btn = lv_button_create(bar);
    lv_obj_set_size(s_ctx.d_run_btn, 180, UI_TOUCH_MIN);
    lv_obj_set_style_bg_color(s_ctx.d_run_btn, lv_color_hex(UI_COLOR_ACCENT), 0);
    lv_obj_set_style_radius(s_ctx.d_run_btn, UI_RADIUS_MD, 0);
    lv_obj_add_event_cb(s_ctx.d_run_btn, run_cb, LV_EVENT_CLICKED, NULL);
    s_ctx.d_run_lbl = lv_label_create(s_ctx.d_run_btn);
    lv_label_set_text(s_ctx.d_run_lbl, LV_SYMBOL_PLAY "  Run this example");
    lv_obj_set_style_text_font(s_ctx.d_run_lbl, UI_FONT_CAPTION, 0);
    lv_obj_center(s_ctx.d_run_lbl);

    s_ctx.d_title = lv_label_create(d);
    lv_obj_set_style_text_font(s_ctx.d_title, UI_FONT_H3, 0);
    lv_label_set_long_mode(s_ctx.d_title, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_ctx.d_title, UI_CARD_FULL_W);

    s_ctx.d_meta    = caption(d, "", UI_COLOR_TEXT_DISABLED);
    lv_obj_set_width(s_ctx.d_meta, UI_CARD_FULL_W);
    s_ctx.d_teaches = caption(d, "", UI_COLOR_TEXT_PRIMARY);
    lv_obj_set_width(s_ctx.d_teaches, UI_CARD_FULL_W);
    s_ctx.d_apis    = caption(d, "", UI_COLOR_TEXT_SECONDARY);
    lv_obj_set_width(s_ctx.d_apis, UI_CARD_FULL_W);
    s_ctx.d_result  = caption(d, "not run yet", UI_COLOR_TEXT_DISABLED);
    lv_obj_set_width(s_ctx.d_result, UI_CARD_FULL_W);

    /* The drawing area handed to the example as `parent`. It is a real,
     * laid-out, scrollable container — ui_widget_* examples draw here. */
    s_ctx.d_canvas = lv_obj_create(d);
    lv_obj_set_size(s_ctx.d_canvas, UI_CARD_FULL_W, 200);
    lv_obj_set_style_bg_color(s_ctx.d_canvas, lv_color_hex(UI_COLOR_BG_CARD), 0);
    lv_obj_set_style_bg_opa(s_ctx.d_canvas, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_ctx.d_canvas, UI_CARD_RADIUS, 0);
    lv_obj_set_style_border_width(s_ctx.d_canvas, 0, 0);
    lv_obj_set_style_pad_all(s_ctx.d_canvas, UI_SPACE_SM, 0);
    lv_obj_set_scroll_dir(s_ctx.d_canvas, LV_DIR_VER);

    s_ctx.d_log = caption(d, "", UI_COLOR_TEXT_SECONDARY);
    lv_obj_set_width(s_ctx.d_log, UI_CARD_FULL_W);
}

/*******************************************************************************
 * LIST view
 *******************************************************************************/
static void build_list(lv_obj_t *parent)
{
    lv_obj_t *l = lv_obj_create(parent);
    lv_obj_set_pos(l, 0, 0);
    lv_obj_set_size(l, UI_SCREEN_W, lv_obj_get_height(parent));
    lv_obj_set_style_bg_opa(l, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(l, 0, 0);
    lv_obj_set_style_pad_all(l, UI_SPACE_MD, 0);
    lv_obj_set_style_pad_row(l, UI_SPACE_SM, 0);
    lv_obj_set_flex_flow(l, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(l, LV_DIR_VER);
    s_ctx.list = l;

    lv_obj_t *hint = caption(l,
        "Every public API in this SDK has an example. Tap one to see what it "
        "teaches, which functions it calls, and — where the library runs on "
        "this core — to run it.", UI_COLOR_TEXT_DISABLED);
    lv_obj_set_width(hint, UI_CARD_FULL_W);

    sdk_group_t current = SDK_GRP_COUNT;

    for (unsigned i = 0u; i < g_sdk_example_count; i++) {
        const sdk_example_t *ex = &g_sdk_examples[i];

        /* Section header when the module changes. The table is generated in
         * module order, so this needs no sorting here. */
        if (ex->group != current) {
            current = ex->group;
            lv_obj_t *hdr = lv_label_create(l);
            lv_label_set_text_fmt(hdr, "%s", sdk_group_name(current));
            lv_obj_set_style_text_font(hdr, UI_FONT_BODY, 0);
            lv_obj_set_style_text_color(hdr,
                lv_color_hex(sdk_group_color(current)), 0);
            lv_obj_set_style_pad_top(hdr, UI_SPACE_SM, 0);
        }

        lv_obj_t *card = lv_obj_create(l);
        lv_obj_set_size(card, UI_CARD_FULL_W, 62);
        lv_obj_set_style_bg_color(card, lv_color_hex(UI_COLOR_BG_CARD), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(card, UI_CARD_RADIUS, 0);
        lv_obj_set_style_border_color(card,
            lv_color_hex(sdk_group_color(ex->group)), 0);
        lv_obj_set_style_border_width(card, 2, 0);
        lv_obj_set_style_border_side(card, LV_BORDER_SIDE_LEFT, 0);
        lv_obj_set_style_pad_all(card, UI_SPACE_SM, 0);
        lv_obj_set_style_pad_row(card, 2, 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, row_click_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);

        lv_obj_t *t = lv_label_create(card);
        /* A row that cannot run here says so on the row, not two taps later. */
        lv_label_set_text_fmt(t, "%s%s",
                              (ex->run != NULL) ? "" : LV_SYMBOL_WARNING "  ",
                              ex->title);
        lv_obj_set_style_text_font(t, UI_FONT_BODY, 0);
        lv_obj_set_style_text_color(t, lv_color_hex(UI_COLOR_TEXT_PRIMARY), 0);
        lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
        lv_obj_set_width(t, UI_CARD_FULL_W - 4 * UI_SPACE_SM);

        lv_obj_t *s = caption(card,
                              (ex->run != NULL) ? ex->teaches : ex->id,
                              UI_COLOR_TEXT_DISABLED);
        lv_label_set_long_mode(s, LV_LABEL_LONG_DOT);
        lv_obj_set_width(s, UI_CARD_FULL_W - 4 * UI_SPACE_SM);
    }
}

/*******************************************************************************
 * Page lifecycle
 *******************************************************************************/
lv_obj_t *page_examples_create(void)
{
    s_ctx.selected     = -1;
    s_ctx.widgets_used = false;

    lv_obj_t *scr = lv_obj_create(NULL);
    page_manager_t *pm = pm_get_instance();
    lv_obj_t *content = pm_create_page_with_header(scr, pm,
                            "SDK Examples", UI_COLOR_ACCENT_CYAN);

    s_ctx.screen  = scr;
    s_ctx.content = content;

    build_list(content);
    build_detail(content);
    show_list();

    return scr;
}

void page_examples_render(sensorhub_snapshot_t *snap)
{
    /* Nothing here polls. An example that wants live data reads it when it
     * runs, or arms its own lv_timer — putting a per-frame hook on a page whose
     * content is developer-supplied would run somebody's example 30 times a
     * second without them asking. */
    LV_UNUSED(snap);
}

void page_examples_destroy(void)
{
    /* An example may have created widgets through the ipc_core widget manager,
     * whose handle table points at LVGL objects inside our content container.
     * The page manager is about to delete that container, so the handles must
     * go first — otherwise the next widget op writes through a freed pointer,
     * and the Playground page inherits a table full of ghosts.
     *
     * needs_container() returns true after this, which is the honest state:
     * whoever draws next sets its own parent. */
    if (s_ctx.widgets_used) {
        ui_widget_mgr_clear_all();
        ui_widget_mgr_set_parent(NULL);
    }

    s_ctx.screen       = NULL;
    s_ctx.content      = NULL;
    s_ctx.list         = NULL;
    s_ctx.detail       = NULL;
    s_ctx.d_title      = NULL;
    s_ctx.d_meta       = NULL;
    s_ctx.d_teaches    = NULL;
    s_ctx.d_apis       = NULL;
    s_ctx.d_run_btn    = NULL;
    s_ctx.d_run_lbl    = NULL;
    s_ctx.d_result     = NULL;
    s_ctx.d_canvas     = NULL;
    s_ctx.d_log        = NULL;
    s_ctx.selected     = -1;
    s_ctx.widgets_used = false;
}
