/*******************************************************************************
* File: ui_busy_modal.c   — see ui_busy_modal.h for the rationale.
*******************************************************************************/
#include "ui_busy_modal.h"

#include "lvgl.h"
#include "FreeRTOS.h"
#include "task.h"

#include <string.h>

/* Requested state — written by any task, read by the GFX task. */
static volatile uint32_t s_requests = 0U;
static char              s_reason[UI_BUSY_MODAL_REASON_MAX] = {0};

/* On-screen state — GFX task only. */
static lv_obj_t *s_overlay = NULL;
static char      s_shown_reason[UI_BUSY_MODAL_REASON_MAX] = {0};

static const char *const DEFAULT_REASON = "Please wait";

void ui_busy_modal_request(const char *reason)
{
    taskENTER_CRITICAL();
    bool first = (0U == s_requests);
    s_requests++;
    taskEXIT_CRITICAL();

    /* First holder names the reason; a nested one inherits it rather than
     * overwriting a more specific message with a vaguer one. */
    if (first) {
        if ((NULL != reason) && ('\0' != reason[0])) {
            strncpy(s_reason, reason, sizeof(s_reason) - 1U);
            s_reason[sizeof(s_reason) - 1U] = '\0';
        } else {
            strncpy(s_reason, DEFAULT_REASON, sizeof(s_reason) - 1U);
            s_reason[sizeof(s_reason) - 1U] = '\0';
        }
    }
}

void ui_busy_modal_clear(void)
{
    taskENTER_CRITICAL();
    if (s_requests > 0U) {
        s_requests--;
    }
    taskEXIT_CRITICAL();
}

void ui_busy_modal_reset(void)
{
    taskENTER_CRITICAL();
    s_requests = 0U;
    taskEXIT_CRITICAL();
}

bool ui_busy_modal_active(void)
{
    return (0U != s_requests);
}

/*******************************************************************************
* GFX task only
*******************************************************************************/
static void overlay_create(const char *reason)
{
    /* On the top layer so it covers whatever page is showing without any page
     * needing to know about it. */
    lv_obj_t *scr = lv_layer_top();

    s_overlay = lv_obj_create(scr);
    lv_obj_remove_style_all(s_overlay);
    lv_obj_set_size(s_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_60, 0);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);

    /* Swallow presses rather than letting them fall through to the page
     * underneath: touch is off, but a stylus or a stray event should not reach
     * a control the user cannot see. */
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *card = lv_obj_create(s_overlay);
    /* Height follows the content.
     *
     * The three labels were placed at fixed offsets, which works only while the
     * first one is a single line. "Working with the platform, a few seconds"
     * wraps to two, and the wrapped line landed on top of the one below it. A
     * column layout cannot overlap whatever the caller's reason string turns
     * out to be. */
    lv_obj_set_size(card, 340, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(card, 220, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_set_style_pad_row(card, 10, 0);
    lv_obj_center(card);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x202020), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x606060), 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* Say what the device is doing, then why the screen is not answering.
     *
     * The title used to be "Touch paused", which names what the firmware
     * switched off rather than what it is busy with — accurate, and useless to
     * the person holding the board. A bare "Please wait" is no better on its
     * own: it still does not say why. So the work goes first and largest, the
     * wait is stated plainly under it, and the last line answers the only
     * question left, which is when the screen will come back. */
    lv_obj_t *title = lv_label_create(card);
    lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(title, 300);
    lv_label_set_text(title, reason);
    lv_obj_set_style_text_color(title, lv_color_hex(0xF0F0F0), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *what = lv_label_create(card);
    lv_label_set_text(what, "Please wait");
    lv_obj_set_style_text_color(what, lv_color_hex(0xF2B84B), 0);

    lv_obj_t *hint = lv_label_create(card);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, 300);
    lv_label_set_text(hint, "The screen answers again when this finishes.");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x9A9A9A), 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);

    strncpy(s_shown_reason, reason, sizeof(s_shown_reason) - 1U);
    s_shown_reason[sizeof(s_shown_reason) - 1U] = '\0';
}

static void overlay_destroy(void)
{
    if (NULL != s_overlay) {
        lv_obj_del(s_overlay);
        s_overlay = NULL;
    }
    s_shown_reason[0] = '\0';
}

void ui_busy_modal_service(void)
{
    bool want = (0U != s_requests);
    bool have = (NULL != s_overlay);

    if (want == have) {
        /* Already agreeing. Update the text if the reason changed under a
         * holder that outlived the first one. */
        if (have && (0 != strcmp(s_shown_reason, s_reason))) {
            overlay_destroy();
            overlay_create(s_reason);
        }
        return;
    }

    if (want) {
        overlay_create(s_reason);
    } else {
        overlay_destroy();
    }
}
