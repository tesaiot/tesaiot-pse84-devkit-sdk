/*******************************************************************************
 * File Name: page_bentoclaw.c
 *
 * Description: BentoClaw Agent Status page — displays connection state,
 *              last command, last result, agent statistics, and streaming
 *              AI response text (Phase 4: token-by-token display).
 *              Data arrives via IPC push from CM33_NS modbentoclaw.c.
 *              Deferred flag pattern: IPC ISR sets flags, render_cb updates UI.
 *
 *******************************************************************************/

#include "page_bentoclaw.h"
#include "page_manager.h"
#include "tesaiot_ui_theme.h"
#include "tesaiot_ui_helpers.h"
#include "lv_fonts_thai.h"
#include "ipc_communication.h"
#include "ipc_bentoclaw_defs.h"
#include "bentoclaw_version.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

/*******************************************************************************
 * Streaming Response Buffer (Phase 4)
 ******************************************************************************/
#define STREAMING_BUF_SIZE  (1024)

static char     s_stream_buf[STREAMING_BUF_SIZE];
static size_t   s_stream_len = 0;
static bool     s_is_streaming = false;

/*******************************************************************************
 * Deferred IPC data — bitmask + per-type buffers to avoid race conditions.
 * Multiple IPC updates can arrive between render_cb ticks (33ms).
 * Each type has its own buffer so none are overwritten.
 ******************************************************************************/
#define CLAW_IPC_TYPE_COUNT  7   /* types 0..6 */
static volatile uint32_t s_ipc_pending_mask = 0;
static char              s_ipc_bufs[CLAW_IPC_TYPE_COUNT][IPC_DATA_MAX_LEN];

/* Streaming token: separate dirty flag to avoid overwriting main updates */
static volatile bool     s_stream_dirty = false;
static char              s_stream_token_buf[IPC_DATA_MAX_LEN];

/* Cached display strings (updated in render_cb from IPC data) */
static char s_conn_text[32]    = "Offline";
static char s_cmd_text[64]     = "(none)";
static char s_result_text[128] = "(none)";
static char s_error_text[64]   = "";
static char s_status_text[32]  = BENTOCLAW_VERSION_STR;

/*******************************************************************************
 * Module-Static Context
 ******************************************************************************/
typedef struct {
    lv_obj_t *conn_label;
    lv_obj_t *cmd_label;
    lv_obj_t *result_label;
    lv_obj_t *stream_label;     /* Phase 4: scrollable streaming text */
    lv_obj_t *stream_card;      /* Phase 4: streaming container (hideable) */
    lv_obj_t *error_label;
    lv_obj_t *status_label;
    lv_obj_t *version_label;
    uint32_t  update_count;
} page_bentoclaw_ctx_t;

static page_bentoclaw_ctx_t s_ctx;

/*******************************************************************************
 * Static text buffers
 ******************************************************************************/
static char s_stat_buf[128];

/*******************************************************************************
 * IPC Callback — dispatched by IPC pipe for CM55_IPC_CLAW_CLIENT_ID
 ******************************************************************************/
static void ipc_bentoclaw_callback(uint32_t *msg_ptr)
{
    ipc_msg_t *msg = (ipc_msg_t *)msg_ptr;

    if (msg->cmd == IPC_CMD_CLAW_UI_UPDATE) {
        uint8_t update_type = (uint8_t)msg->data[0];
        const char *text = &msg->data[1];
        size_t len = strnlen(text, IPC_DATA_MAX_LEN - 2);
        page_bentoclaw_ipc_update(update_type, text, len);
    }
    else if (msg->cmd == IPC_CMD_CLAW_STREAM_TOKEN) {
        /* Phase 4: streaming token — separate buffer to avoid race */
        const char *text = &msg->data[0];
        size_t len = strnlen(text, IPC_DATA_MAX_LEN - 1);
        if (len > 0 && len < sizeof(s_stream_token_buf)) {
            memcpy(s_stream_token_buf, text, len);
            s_stream_token_buf[len] = '\0';
            s_stream_dirty = true;
        }
    }
}

bool ipc_bentoclaw_init(void)
{
    cy_en_ipc_pipe_status_t status;
    status = Cy_IPC_Pipe_RegisterCallback(
        CM55_IPC_PIPE_EP_ADDR,
        ipc_bentoclaw_callback,
        (uint32_t)CM55_IPC_CLAW_CLIENT_ID);
    return (CY_IPC_PIPE_SUCCESS == status);
}

/*******************************************************************************
 * page_bentoclaw_ipc_update() — called from IPC ISR context
 * Must NOT call LVGL. Only sets deferred flags.
 ******************************************************************************/
void page_bentoclaw_ipc_update(uint8_t update_type, const char *text, size_t len)
{
    if (update_type >= CLAW_IPC_TYPE_COUNT) return;

    if (text && len > 0) {
        size_t copy = (len >= IPC_DATA_MAX_LEN - 1) ?
                      (IPC_DATA_MAX_LEN - 1) : len;
        memcpy(s_ipc_bufs[update_type], text, copy);
        s_ipc_bufs[update_type][copy] = '\0';
    } else {
        s_ipc_bufs[update_type][0] = '\0';
    }
    s_ipc_pending_mask |= (1u << update_type);
}

/*******************************************************************************
 * Create
 ******************************************************************************/
lv_obj_t *page_bentoclaw_create(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_stream_len = 0;
    s_stream_buf[0] = '\0';
    s_is_streaming = false;

    lv_obj_t *scr = lv_obj_create(NULL);
    page_manager_t *pm = pm_get_instance();
    lv_obj_t *content = pm_create_page_with_header(
        scr, pm, "BentoClaw Agent", UI_COLOR_ACCENT_CYAN);

    /*--------------------------------------------------------------------------
     * Layout: 800×408 content area (after 72px header).
     * Pixel widths to avoid gap overflow.
     * Card min height for title+value: pad(16)+title(20)+gap(8)+value(16)+pad(16)=76px
     *
     * Left 460px + gap 6px + Right 334px = 800px.
     * Left:  76 + 4 + 76 + 4 + 230 = 390px  (< 408)
     * Right: 306 + 4 + 76 = 386px            (< 408)
     *------------------------------------------------------------------------*/
#define BC_COL_GAP      UI_SPACE_SM                /* 6 */
#define BC_RIGHT_W      334
#define BC_LEFT_W       (800 - BC_RIGHT_W - BC_COL_GAP)  /* 460 */
#define BC_CARD_GAP     UI_SPACE_XS                /* 4 */
#define BC_CARD_MIN_H   76   /* Min to show title (H3=20) + value (body=16) with pad=16 */

    lv_obj_t *row = tesaiot_row_create(content, BC_COL_GAP);

    /* Left column */
    lv_obj_t *left = tesaiot_col_create(row, BC_CARD_GAP);
    lv_obj_set_width(left, BC_LEFT_W);

    /* Connection card */
    {
        lv_obj_t *val = NULL;
        tesaiot_card_create(left, BC_LEFT_W, BC_CARD_MIN_H,
                            "Connection", UI_COLOR_ACCENT_GREEN, &val);
        if (val) {
            lv_label_set_text(val, s_conn_text);
            lv_obj_set_style_text_color(val, lv_color_hex(UI_COLOR_ACCENT_GREEN),
                                         LV_PART_MAIN);
            s_ctx.conn_label = val;
        }
    }

    /* Last Command card (Thai+EN font for BentoClaw agent text) */
    {
        lv_obj_t *val = NULL;
        tesaiot_card_create(left, BC_LEFT_W, BC_CARD_MIN_H,
                            "Last Command", UI_COLOR_ACCENT_CYAN, &val);
        if (val) {
            lv_label_set_text(val, s_cmd_text);
            lv_label_set_long_mode(val, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(val, BC_LEFT_W - 2 * UI_CARD_PAD - 8);
            lv_obj_set_style_text_font(val, &lv_font_noto_thai_16, LV_PART_MAIN);
            s_ctx.cmd_label = val;
        }
    }

    /* AI Response card — large, scrollable (Thai+EN font) */
    {
        lv_obj_t *val = NULL;
        lv_obj_t *card = tesaiot_card_create(left, BC_LEFT_W, 230,
                            "AI Response", UI_COLOR_ACCENT_BLUE, &val);
        if (val) {
            lv_label_set_text(val, s_result_text);
            lv_label_set_long_mode(val, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(val, BC_LEFT_W - 2 * UI_CARD_PAD - 8);
            lv_obj_set_style_text_font(val, &lv_font_noto_thai_16, LV_PART_MAIN);
            s_ctx.stream_label = val;
            s_ctx.result_label = val;
        }
        if (card) {
            lv_obj_add_flag(card, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_scroll_dir(card, LV_DIR_VER);
            s_ctx.stream_card = card;
        }
    }

    /* Error label (hidden, excluded from flex layout) */
    {
        lv_obj_t *err = lv_label_create(left);
        lv_label_set_text(err, "");
        lv_obj_set_style_text_color(err, lv_color_hex(UI_COLOR_ACCENT_RED), LV_PART_MAIN);
        lv_obj_set_style_text_font(err, UI_FONT_CAPTION, LV_PART_MAIN);
        lv_label_set_long_mode(err, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(err, BC_LEFT_W - 2 * UI_CARD_PAD);
        lv_obj_add_flag(err, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(err, LV_OBJ_FLAG_IGNORE_LAYOUT);
        s_ctx.error_label = err;
    }

    /*--------------------------------------------------------------------------
     * Right column: Agent Stats + Version
     *------------------------------------------------------------------------*/
    lv_obj_t *right = tesaiot_col_create(row, BC_CARD_GAP);
    lv_obj_set_width(right, BC_RIGHT_W);

    /* Agent Statistics card */
    {
        lv_obj_t *val = NULL;
        tesaiot_card_create(right, BC_RIGHT_W, 306,
                            "Agent Statistics", UI_COLOR_ACCENT_PURPLE, &val);
        if (val) {
            snprintf(s_stat_buf, sizeof(s_stat_buf),
                     "State       idle\n"
                     "Tools       ...\n"
                     "Executed    0\n"
                     "Errors      0\n"
                     "Transport   USB");
            lv_label_set_text(val, s_stat_buf);
            lv_obj_set_style_text_font(val, UI_FONT_BODY, LV_PART_MAIN);
            s_ctx.status_label = val;
        }
    }

    /* Version card */
    {
        lv_obj_t *val = NULL;
        tesaiot_card_create(right, BC_RIGHT_W, BC_CARD_MIN_H,
                            "Version", UI_COLOR_ACCENT, &val);
        if (val) {
            lv_label_set_text(val, s_status_text);
            lv_obj_set_style_text_font(val, UI_FONT_CAPTION, LV_PART_MAIN);
            s_ctx.version_label = val;
        }
    }

    return scr;
}

/*******************************************************************************
 * Render — called every 33ms from LVGL timer
 ******************************************************************************/
void page_bentoclaw_render(sensorhub_snapshot_t *snap)
{
    (void)snap;  /* BentoClaw uses IPC push, not sensor snapshot */

    /* Process streaming tokens (Phase 4) */
    if (s_stream_dirty) {
        s_stream_dirty = false;

        /* Accumulate token into streaming buffer */
        size_t tlen = strlen(s_stream_token_buf);
        if (s_stream_len + tlen < STREAMING_BUF_SIZE - 1) {
            memcpy(s_stream_buf + s_stream_len, s_stream_token_buf, tlen);
            s_stream_len += tlen;
            s_stream_buf[s_stream_len] = '\0';
        }

        /* Update streaming label */
        if (s_ctx.stream_label) {
            lv_label_set_text(s_ctx.stream_label, s_stream_buf);
            /* Auto-scroll to bottom */
            if (s_ctx.stream_card) {
                lv_obj_scroll_to_y(s_ctx.stream_card, LV_COORD_MAX, LV_ANIM_ON);
            }
        }

        if (!s_is_streaming) {
            s_is_streaming = true;
        }
    }

    /* Process ALL pending IPC updates (bitmask — no more race conditions) */
    uint32_t mask = s_ipc_pending_mask;
    if (mask) {
        s_ipc_pending_mask = 0;
        s_ctx.update_count++;

        /* Process each pending type in order */
        for (uint8_t t = 0; t < CLAW_IPC_TYPE_COUNT && mask; t++) {
            if (!(mask & (1u << t))) continue;
            mask &= ~(1u << t);
            const char *buf = s_ipc_bufs[t];

            switch (t) {
            case CLAW_UI_STATUS_TEXT:
                strncpy(s_conn_text, buf, sizeof(s_conn_text) - 1);
                s_conn_text[sizeof(s_conn_text) - 1] = '\0';
                if (s_ctx.conn_label) {
                    lv_label_set_text(s_ctx.conn_label, s_conn_text);
                }
                if (strstr(s_conn_text, "Streaming") != NULL) {
                    s_stream_len = 0;
                    s_stream_buf[0] = '\0';
                    s_is_streaming = true;
                } else if (s_is_streaming &&
                           (strstr(s_conn_text, "Online") != NULL ||
                            strstr(s_conn_text, "Offline") != NULL ||
                            strstr(s_conn_text, "Local") != NULL)) {
                    s_is_streaming = false;
                }
                break;

            case CLAW_UI_LAST_CMD:
                strncpy(s_cmd_text, buf, sizeof(s_cmd_text) - 1);
                s_cmd_text[sizeof(s_cmd_text) - 1] = '\0';
                if (s_ctx.cmd_label) {
                    lv_label_set_text(s_ctx.cmd_label, s_cmd_text);
                }
                break;

            case CLAW_UI_LAST_RESULT:
                strncpy(s_result_text, buf, sizeof(s_result_text) - 1);
                s_result_text[sizeof(s_result_text) - 1] = '\0';
                if (s_is_streaming && s_stream_len > 0) {
                    if (s_ctx.result_label) {
                        lv_label_set_text(s_ctx.result_label, s_stream_buf);
                    }
                    s_is_streaming = false;
                } else {
                    if (s_ctx.result_label) {
                        lv_label_set_text(s_ctx.result_label, s_result_text);
                    }
                }
                break;

            case CLAW_UI_PROGRESS:
                if (s_ctx.conn_label) {
                    static char prog_text[48];
                    snprintf(prog_text, sizeof(prog_text), "Processing... %s", buf);
                    lv_label_set_text(s_ctx.conn_label, prog_text);
                }
                break;

            case CLAW_UI_ERROR:
                strncpy(s_error_text, buf, sizeof(s_error_text) - 1);
                s_error_text[sizeof(s_error_text) - 1] = '\0';
                if (s_ctx.error_label) {
                    lv_label_set_text(s_ctx.error_label, s_error_text);
                    lv_obj_clear_flag(s_ctx.error_label, LV_OBJ_FLAG_HIDDEN);
                }
                if (s_ctx.conn_label) {
                    lv_label_set_text(s_ctx.conn_label, "Error");
                    lv_obj_set_style_text_color(s_ctx.conn_label,
                                                 lv_color_hex(UI_COLOR_ACCENT_RED), LV_PART_MAIN);
                }
                break;

            case CLAW_UI_CLEAR:
                strncpy(s_conn_text, "Offline", sizeof(s_conn_text));
                strncpy(s_cmd_text, "(none)", sizeof(s_cmd_text));
                strncpy(s_result_text, "(none)", sizeof(s_result_text));
                s_error_text[0] = '\0';
                s_stream_len = 0;
                s_stream_buf[0] = '\0';
                s_is_streaming = false;
                if (s_ctx.conn_label) {
                    lv_label_set_text(s_ctx.conn_label, s_conn_text);
                    lv_obj_set_style_text_color(s_ctx.conn_label,
                                                 lv_color_hex(UI_COLOR_ACCENT_GREEN),
                                                 LV_PART_MAIN);
                }
                if (s_ctx.cmd_label) lv_label_set_text(s_ctx.cmd_label, s_cmd_text);
                if (s_ctx.result_label) lv_label_set_text(s_ctx.result_label, s_result_text);
                if (s_ctx.error_label) {
                    lv_label_set_text(s_ctx.error_label, "");
                    lv_obj_add_flag(s_ctx.error_label, LV_OBJ_FLAG_HIDDEN);
                }
                break;

            case CLAW_UI_STATS:
                if (s_ctx.status_label) {
                    strncpy(s_stat_buf, buf, sizeof(s_stat_buf) - 1);
                    s_stat_buf[sizeof(s_stat_buf) - 1] = '\0';
                    lv_label_set_text(s_ctx.status_label, s_stat_buf);
                }
                break;

            default:
                break;
            }
        }
    }
}

/*******************************************************************************
 * Destroy
 ******************************************************************************/
void page_bentoclaw_destroy(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_stream_len = 0;
    s_stream_buf[0] = '\0';
    s_is_streaming = false;
}
