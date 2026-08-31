/*******************************************************************************
 * File Name: page_joystick.c
 *
 * Description: Joystick page — F310 controller image with live button overlay
 *              indicators + button state table.
 *              Page-based conversion from tab_joystick.c.
 *
 *              Layout: Golden Ratio phi split (~480 left / ~290 right).
 *              Joystick data is LOCAL on CM55 (USB Host). No IPC needed.
 *
 *******************************************************************************/

#include "page_joystick.h"
#include "page_manager.h"
#include "tesaiot_ui_theme.h"
#include "tesaiot_ui_helpers.h"
#include "usb_hid_joystick.h"
#include "img_f310.h"
#include "lvgl.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>
#include <malloc.h>

/* Boot-race diagnostics from proj_cm55/main.c */
extern volatile uint32_t app_task_beat;
extern volatile uint16_t usb_init_retry_fails;

/* CM55 runs FreeRTOS heap_3 (= newlib malloc), so xTaskCreate competes for
 * the SAME heap as LVGL/ThorVG. mallinfo(): arena = bytes claimed via sbrk,
 * fordblks = free bytes inside the arena. xTaskCreate fails when both the
 * freelist AND sbrk headroom are exhausted. */
static void joy_heap_stats(unsigned *arena, unsigned *free_bytes)
{
    struct mallinfo mi = mallinfo();
    *arena      = (unsigned)mi.arena;
    *free_bytes = (unsigned)mi.fordblks;
}

/*******************************************************************************
 * Module-Static Context
 *******************************************************************************/
#define JOY_BTN_DOT_COUNT  10
#define JOY_DPAD_DOT_COUNT 4

typedef struct {
    lv_obj_t *img_obj;
    lv_obj_t *left_stick_dot;
    lv_obj_t *right_stick_dot;
    lv_obj_t *btn_dots[JOY_BTN_DOT_COUNT];
    lv_obj_t *dpad_dots[JOY_DPAD_DOT_COUNT];
    lv_obj_t *conn_label;
    lv_obj_t *info_label;
    lv_obj_t *btn_table;
} page_joystick_ctx_t;

static page_joystick_ctx_t s_ctx;

/*******************************************************************************
 * Button Overlay Constants
 *******************************************************************************/
enum {
    DOT_X = 0, DOT_A, DOT_B, DOT_Y,
    DOT_LB, DOT_RB,
    DOT_BACK, DOT_START,
    DOT_L3, DOT_R3
};

#define BTN_DOT_SIZE     14
#define STICK_DOT_SIZE   14
#define STICK_RANGE      26

typedef struct { int16_t x; int16_t y; } btn_pos_t;

static const btn_pos_t s_btn_pos[JOY_BTN_DOT_COUNT] = {
    [DOT_X]     = { 311,  96 },
    [DOT_A]     = { 338, 121 },
    [DOT_B]     = { 374, 88 },
    [DOT_Y]     = { 346, 62 },
    [DOT_LB]    = {  80,  18 },
    [DOT_RB]    = { 340,  18 },
    [DOT_BACK]  = { 172,  77 },
    [DOT_START] = { 250,  77 },
    [DOT_L3]    = { 148, 163 },
    [DOT_R3]    = { 265, 163 },
};

static const uint32_t s_btn_color[JOY_BTN_DOT_COUNT] = {
    [DOT_X]     = UI_COLOR_ACCENT_BLUE,
    [DOT_A]     = UI_COLOR_ACCENT_GREEN,
    [DOT_B]     = UI_COLOR_ACCENT_RED,
    [DOT_Y]     = UI_COLOR_ACCENT_ORANGE,
    [DOT_LB]    = UI_COLOR_TEXT_SECONDARY,
    [DOT_RB]    = UI_COLOR_TEXT_SECONDARY,
    [DOT_BACK]  = UI_COLOR_TEXT_SECONDARY,
    [DOT_START] = UI_COLOR_TEXT_SECONDARY,
    [DOT_L3]    = UI_COLOR_ACCENT_CYAN,
    [DOT_R3]    = UI_COLOR_ACCENT_CYAN,
};

#define LEFT_STICK_CX    148
#define LEFT_STICK_CY    163
#define RIGHT_STICK_CX   265
#define RIGHT_STICK_CY   163

enum { DPAD_UP = 0, DPAD_DOWN, DPAD_LEFT, DPAD_RIGHT };

#define DPAD_DOT_SIZE    12
static const btn_pos_t s_dpad_pos[JOY_DPAD_DOT_COUNT] = {
    [DPAD_UP]    = {  86,  83 },
    [DPAD_DOWN]  = {  86, 117 },
    [DPAD_LEFT]  = {  69, 100 },
    [DPAD_RIGHT] = { 103, 100 },
};
#define DPAD_DOT_COLOR   UI_COLOR_ACCENT_CYAN

/*******************************************************************************
 * Table layout constants
 *******************************************************************************/
#define TBL_ROWS    7
#define TBL_COLS    4
#define TBL_COL_W0  56
#define TBL_COL_W1  67
#define TBL_COL_W2  56
#define TBL_COL_W3  67

static const char * const s_dpad_dirs[] = {
    "Up", "Up-Right", "Right", "Down-Right",
    "Down", "Down-Left", "Left", "Up-Left",
    "---"
};

static const uint8_t s_hat_to_dpad[] = {
    (1 << DPAD_UP),
    (1 << DPAD_UP)   | (1 << DPAD_RIGHT),
    (1 << DPAD_RIGHT),
    (1 << DPAD_DOWN) | (1 << DPAD_RIGHT),
    (1 << DPAD_DOWN),
    (1 << DPAD_DOWN) | (1 << DPAD_LEFT),
    (1 << DPAD_LEFT),
    (1 << DPAD_UP)   | (1 << DPAD_LEFT),
    0,
};

static char s_info_buf[160];

/*******************************************************************************
 * Helper: create overlay dot
 *******************************************************************************/
static lv_obj_t *create_dot(lv_obj_t *parent, int size, int cx, int cy,
                             uint32_t color, lv_opa_t opa)
{
    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_set_size(dot, size, size);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(dot, opa, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_pad_all(dot, 0, 0);
    lv_obj_set_pos(dot, cx - size / 2, cy - size / 2);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return dot;
}

/*******************************************************************************
 * Table draw event
 *******************************************************************************/
static void _tbl_draw_cb(lv_event_t *e)
{
    lv_draw_task_t *draw_task = lv_event_get_draw_task(e);
    lv_draw_dsc_base_t *base = (lv_draw_dsc_base_t *)lv_draw_task_get_draw_dsc(draw_task);
    if (base == NULL) return;
    if (base->part != LV_PART_ITEMS) return;

    uint32_t row = base->id1;
    uint32_t col = base->id2;
    if (col != 1 && col != 3) return;

    lv_obj_t *table = lv_event_get_target(e);
    const char *val = lv_table_get_cell_value(table, row, col);

    lv_draw_label_dsc_t *label_dsc = lv_draw_task_get_label_dsc(draw_task);
    if (label_dsc == NULL) return;

    if (val && strcmp(val, "ON") == 0) {
        label_dsc->color = lv_color_hex(UI_COLOR_ACCENT_GREEN);
        label_dsc->font = &lv_font_montserrat_16;
    }
}

/*******************************************************************************
 * Helper: create button state table
 *******************************************************************************/
static lv_obj_t *create_btn_table(lv_obj_t *parent)
{
    lv_obj_t *table = lv_table_create(parent);
    lv_table_set_column_count(table, TBL_COLS);
    lv_table_set_row_count(table, TBL_ROWS);

    lv_table_set_column_width(table, 0, TBL_COL_W0);
    lv_table_set_column_width(table, 1, TBL_COL_W1);
    lv_table_set_column_width(table, 2, TBL_COL_W2);
    lv_table_set_column_width(table, 3, TBL_COL_W3);

    lv_table_set_cell_value(table, 0, 0, "X");
    lv_table_set_cell_value(table, 0, 2, "Y");
    lv_table_set_cell_value(table, 1, 0, "A");
    lv_table_set_cell_value(table, 1, 2, "B");
    lv_table_set_cell_value(table, 2, 0, "LB");
    lv_table_set_cell_value(table, 2, 2, "RB");
    lv_table_set_cell_value(table, 3, 0, "LT");
    lv_table_set_cell_value(table, 3, 2, "RT");
    lv_table_set_cell_value(table, 4, 0, "Back");
    lv_table_set_cell_value(table, 4, 2, "Start");
    lv_table_set_cell_value(table, 5, 0, "L3");
    lv_table_set_cell_value(table, 5, 2, "R3");
    lv_table_set_cell_value(table, 6, 0, "DPad");
    lv_table_set_cell_value(table, 6, 2, "");

    for (int r = 0; r < TBL_ROWS - 1; r++) {
        lv_table_set_cell_value(table, r, 1, "--");
        lv_table_set_cell_value(table, r, 3, "--");
    }
    lv_table_set_cell_value(table, 6, 1, "---");
    lv_table_set_cell_value(table, 6, 3, "");

    lv_obj_set_style_bg_color(table, lv_color_hex(UI_COLOR_BG_CARD), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(table, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(table, lv_color_hex(UI_COLOR_SENSOR_JOY), LV_PART_MAIN);
    lv_obj_set_style_border_width(table, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(table, UI_CARD_RADIUS, LV_PART_MAIN);
    lv_obj_set_style_pad_all(table, 0, LV_PART_MAIN);

    lv_obj_set_style_text_font(table, UI_FONT_CAPTION, LV_PART_ITEMS);
    lv_obj_set_style_text_color(table, lv_color_hex(UI_COLOR_TEXT_PRIMARY), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(table, lv_color_hex(UI_COLOR_BG_CARD), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(table, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_border_color(table, lv_color_hex(UI_COLOR_BG_HOVER), LV_PART_ITEMS);
    lv_obj_set_style_border_width(table, 1, LV_PART_ITEMS);
    lv_obj_set_style_pad_top(table, 4, LV_PART_ITEMS);
    lv_obj_set_style_pad_bottom(table, 4, LV_PART_ITEMS);
    lv_obj_set_style_pad_left(table, 6, LV_PART_ITEMS);
    lv_obj_set_style_pad_right(table, 4, LV_PART_ITEMS);

    lv_obj_remove_flag(table, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_event_cb(table, _tbl_draw_cb, LV_EVENT_DRAW_TASK_ADDED, NULL);
    lv_obj_add_flag(table, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);

    return table;
}

/*******************************************************************************
 * page_joystick_create
 *******************************************************************************/
lv_obj_t *page_joystick_create(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));
    (void)usb_hid_joystick_request_init();

    lv_obj_t *scr = lv_obj_create(NULL);
    page_manager_t *pm = pm_get_instance();
    lv_obj_t *content = pm_create_page_with_header(scr, pm,
                            "Joystick", UI_COLOR_SENSOR_JOY);

    /* Root: flex row (phi split) */
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(content, UI_SPACE_MD, 0);
    lv_obj_set_style_pad_all(content, UI_SPACE_SM, 0);

    /* LEFT PANEL (~480px) - F310 image with overlays */
    lv_obj_t *left_col = tesaiot_col_create(content, UI_SPACE_SM);
    lv_obj_set_width(left_col, 480);
    lv_obj_set_height(left_col, LV_PCT(100));
    lv_obj_set_style_pad_all(left_col, 0, 0);
    lv_obj_set_flex_align(left_col, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* Image container */
    lv_obj_t *img_cont = lv_obj_create(left_col);
    lv_obj_set_size(img_cont, IMG_F310_W, IMG_F310_H);
    lv_obj_set_style_pad_all(img_cont, 0, 0);
    lv_obj_set_style_bg_opa(img_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(img_cont, 0, 0);
    lv_obj_clear_flag(img_cont, LV_OBJ_FLAG_SCROLLABLE);

    s_ctx.img_obj = lv_image_create(img_cont);
    lv_image_set_src(s_ctx.img_obj, &img_f310);
    lv_obj_set_pos(s_ctx.img_obj, 0, 0);

    /* Button overlay dots */
    for (int i = 0; i < JOY_BTN_DOT_COUNT; i++) {
        s_ctx.btn_dots[i] = create_dot(img_cont, BTN_DOT_SIZE,
                                       s_btn_pos[i].x, s_btn_pos[i].y,
                                       s_btn_color[i], LV_OPA_40);
    }

    /* Analog stick dots */
    s_ctx.left_stick_dot = create_dot(img_cont, STICK_DOT_SIZE,
                                      LEFT_STICK_CX, LEFT_STICK_CY,
                                      UI_COLOR_ACCENT_CYAN, LV_OPA_COVER);
    s_ctx.right_stick_dot = create_dot(img_cont, STICK_DOT_SIZE,
                                       RIGHT_STICK_CX, RIGHT_STICK_CY,
                                       UI_COLOR_ACCENT_CYAN, LV_OPA_COVER);

    /* DPad dots */
    for (int i = 0; i < JOY_DPAD_DOT_COUNT; i++) {
        s_ctx.dpad_dots[i] = create_dot(img_cont, DPAD_DOT_SIZE,
                                         s_dpad_pos[i].x, s_dpad_pos[i].y,
                                         DPAD_DOT_COLOR, LV_OPA_40);
    }

    /* RIGHT PANEL (~290px) - Connection + Button table */
    lv_obj_t *right_col = tesaiot_col_create(content, UI_SPACE_SM);
    lv_obj_set_width(right_col, 290);
    lv_obj_set_height(right_col, LV_PCT(100));
    lv_obj_set_style_pad_all(right_col, 0, 0);

    s_ctx.conn_label = lv_label_create(right_col);
    lv_label_set_text(s_ctx.conn_label, "USB Host:\nNOT init");
    lv_obj_set_style_text_font(s_ctx.conn_label, UI_FONT_H2, 0);
    lv_obj_set_style_text_color(s_ctx.conn_label,
                                lv_color_hex(UI_COLOR_TEXT_DISABLED), 0);

    s_ctx.info_label = lv_label_create(right_col);
    lv_label_set_text(s_ctx.info_label, "VID:---- PID:----");
    lv_obj_set_style_text_font(s_ctx.info_label, UI_FONT_CAPTION, 0);
    lv_obj_set_style_text_color(s_ctx.info_label,
                                lv_color_hex(UI_COLOR_TEXT_SECONDARY), 0);

    s_ctx.btn_table = create_btn_table(right_col);

    return scr;
}

/*******************************************************************************
 * page_joystick_render
 *******************************************************************************/
void page_joystick_render(sensorhub_snapshot_t *snap)
{
    (void)snap;

    const joystick_state_t *joy = usb_hid_joystick_get_state();

    if (!joy->connected) {
        if (s_ctx.conn_label) {
            if (joy->usb_init_done) {
                lv_label_set_text(s_ctx.conn_label,
                                  "USB Host: OK\nWaiting for\ndevice...");
                lv_obj_set_style_text_color(s_ctx.conn_label,
                                            lv_color_hex(UI_COLOR_ACCENT_ORANGE), 0);
            } else {
                /* init_stage pinpoints where the one-shot init wedged:
                 * 0=never ran 1=in USBH_Init 2=USBH done 5=HID init
                 * 6=callbacks 3/4=tasks 7=complete.
                 * hp/min = FreeRTOS heap free/min-ever, b = app_task
                 * heartbeat (0 = app_task never scheduled), rf = failed
                 * USBH-init task creations (heap exhaustion counter). */
                unsigned arena, freeb;
                joy_heap_stats(&arena, &freeb);
                lv_label_set_text_fmt(s_ctx.conn_label,
                                      "USB Host:\nNOT init (st %u)\n"
                                      "ar:%u fr:%u\nb:%u rf:%u",
                                      (unsigned)joy->init_stage,
                                      arena, freeb,
                                      (unsigned)app_task_beat,
                                      (unsigned)usb_init_retry_fails);
                lv_obj_set_style_text_color(s_ctx.conn_label,
                                            lv_color_hex(UI_COLOR_ACCENT_RED), 0);
            }
        }

        for (int i = 0; i < JOY_BTN_DOT_COUNT; i++) {
            if (s_ctx.btn_dots[i])
                lv_obj_set_style_bg_opa(s_ctx.btn_dots[i], LV_OPA_20, 0);
        }

        if (s_ctx.left_stick_dot) {
            lv_obj_set_style_bg_color(s_ctx.left_stick_dot,
                                      lv_color_hex(UI_COLOR_TEXT_DISABLED), 0);
            lv_obj_set_pos(s_ctx.left_stick_dot,
                           LEFT_STICK_CX - STICK_DOT_SIZE / 2,
                           LEFT_STICK_CY - STICK_DOT_SIZE / 2);
        }
        if (s_ctx.right_stick_dot) {
            lv_obj_set_style_bg_color(s_ctx.right_stick_dot,
                                      lv_color_hex(UI_COLOR_TEXT_DISABLED), 0);
            lv_obj_set_pos(s_ctx.right_stick_dot,
                           RIGHT_STICK_CX - STICK_DOT_SIZE / 2,
                           RIGHT_STICK_CY - STICK_DOT_SIZE / 2);
        }

        for (int i = 0; i < JOY_DPAD_DOT_COUNT; i++) {
            if (s_ctx.dpad_dots[i])
                lv_obj_set_style_bg_opa(s_ctx.dpad_dots[i], LV_OPA_20, 0);
        }

        if (s_ctx.btn_table) {
            for (int r = 0; r < TBL_ROWS - 1; r++) {
                lv_table_set_cell_value(s_ctx.btn_table, r, 1, "--");
                lv_table_set_cell_value(s_ctx.btn_table, r, 3, "--");
            }
            lv_table_set_cell_value(s_ctx.btn_table, 6, 1, "---");
        }
        if (s_ctx.info_label)
            lv_label_set_text(s_ctx.info_label, "VID:---- PID:----");

    } else {
        if (s_ctx.conn_label) {
            lv_label_set_text(s_ctx.conn_label, "Connected");
            lv_obj_set_style_text_color(s_ctx.conn_label,
                                        lv_color_hex(UI_COLOR_ACCENT_GREEN), 0);
        }

        uint8_t lx = joy->report.left_x;
        uint8_t ly = joy->report.left_y;
        uint8_t rx = joy->report.right_x;
        uint8_t ry = joy->report.right_y;
        uint8_t b1 = joy->report.buttons1;
        uint8_t b2 = joy->report.buttons2;

        bool pressed[JOY_BTN_DOT_COUNT];
        pressed[DOT_X]     = (b1 & F310_BTN_X)     != 0;
        pressed[DOT_A]     = (b1 & F310_BTN_A)     != 0;
        pressed[DOT_B]     = (b1 & F310_BTN_B)     != 0;
        pressed[DOT_Y]     = (b1 & F310_BTN_Y)     != 0;
        pressed[DOT_LB]    = (b2 & F310_BTN_LB)    != 0;
        pressed[DOT_RB]    = (b2 & F310_BTN_RB)    != 0;
        pressed[DOT_BACK]  = (b2 & F310_BTN_BACK)  != 0;
        pressed[DOT_START] = (b2 & F310_BTN_START) != 0;
        pressed[DOT_L3]    = (b2 & F310_BTN_L3)    != 0;
        pressed[DOT_R3]    = (b2 & F310_BTN_R3)    != 0;

        for (int i = 0; i < JOY_BTN_DOT_COUNT; i++) {
            if (s_ctx.btn_dots[i])
                lv_obj_set_style_bg_opa(s_ctx.btn_dots[i],
                                        pressed[i] ? LV_OPA_COVER : LV_OPA_40, 0);
        }

        if (s_ctx.left_stick_dot) {
            int dx = ((int)lx - 128) * STICK_RANGE / 128;
            int dy = ((int)ly - 128) * STICK_RANGE / 128;
            lv_obj_set_pos(s_ctx.left_stick_dot,
                           LEFT_STICK_CX + dx - STICK_DOT_SIZE / 2,
                           LEFT_STICK_CY + dy - STICK_DOT_SIZE / 2);
            lv_obj_set_style_bg_color(s_ctx.left_stick_dot,
                                      lv_color_hex(UI_COLOR_ACCENT_CYAN), 0);
        }
        if (s_ctx.right_stick_dot) {
            int dx = ((int)rx - 128) * STICK_RANGE / 128;
            int dy = ((int)ry - 128) * STICK_RANGE / 128;
            lv_obj_set_pos(s_ctx.right_stick_dot,
                           RIGHT_STICK_CX + dx - STICK_DOT_SIZE / 2,
                           RIGHT_STICK_CY + dy - STICK_DOT_SIZE / 2);
            lv_obj_set_style_bg_color(s_ctx.right_stick_dot,
                                      lv_color_hex(UI_COLOR_ACCENT_CYAN), 0);
        }

        uint8_t hat = b1 & F310_HAT_MASK;
        if (hat > F310_HAT_NEUTRAL) hat = F310_HAT_NEUTRAL;

        {
            uint8_t dpad_mask = s_hat_to_dpad[hat];
            for (int i = 0; i < JOY_DPAD_DOT_COUNT; i++) {
                if (s_ctx.dpad_dots[i])
                    lv_obj_set_style_bg_opa(s_ctx.dpad_dots[i],
                                            (dpad_mask & (1 << i)) ? LV_OPA_COVER : LV_OPA_40, 0);
            }
        }

        if (s_ctx.btn_table) {
            #define ON  "ON"
            #define OFF "--"
            lv_table_set_cell_value(s_ctx.btn_table, 0, 1, pressed[DOT_X]     ? ON : OFF);
            lv_table_set_cell_value(s_ctx.btn_table, 0, 3, pressed[DOT_Y]     ? ON : OFF);
            lv_table_set_cell_value(s_ctx.btn_table, 1, 1, pressed[DOT_A]     ? ON : OFF);
            lv_table_set_cell_value(s_ctx.btn_table, 1, 3, pressed[DOT_B]     ? ON : OFF);
            lv_table_set_cell_value(s_ctx.btn_table, 2, 1, pressed[DOT_LB]    ? ON : OFF);
            lv_table_set_cell_value(s_ctx.btn_table, 2, 3, pressed[DOT_RB]    ? ON : OFF);
            lv_table_set_cell_value(s_ctx.btn_table, 3, 1, (b2 & F310_BTN_LT) ? ON : OFF);
            lv_table_set_cell_value(s_ctx.btn_table, 3, 3, (b2 & F310_BTN_RT) ? ON : OFF);
            lv_table_set_cell_value(s_ctx.btn_table, 4, 1, pressed[DOT_BACK]  ? ON : OFF);
            lv_table_set_cell_value(s_ctx.btn_table, 4, 3, pressed[DOT_START] ? ON : OFF);
            lv_table_set_cell_value(s_ctx.btn_table, 5, 1, pressed[DOT_L3]    ? ON : OFF);
            lv_table_set_cell_value(s_ctx.btn_table, 5, 3, pressed[DOT_R3]    ? ON : OFF);
            lv_table_set_cell_value(s_ctx.btn_table, 6, 1, s_dpad_dirs[hat]);
            #undef ON
            #undef OFF
        }

        if (s_ctx.info_label) {
            /* rpt frozen + isr climbing = interrupt-IN pipe dead above the
             * hardware ISR; rpt frozen + isr frozen = USBHS interrupt dead;
             * rpt climbing = stream OK, decode/UI problem instead. */
            unsigned arena, freeb;
            joy_heap_stats(&arena, &freeb);
            snprintf(s_info_buf, sizeof(s_info_buf),
                     "VID:%04X PID:%04X  Mode:%u\n"
                     "rpt:%u isr:%lu add:%u st:%u\n"
                     "ar:%u fr:%u b:%u rf:%u",
                     joy->vid, joy->pid, (unsigned)joy->report.mode,
                     (unsigned)joy->report_cnt,
                     (unsigned long)joy->isr_count,
                     (unsigned)joy->add_event_cnt,
                     (unsigned)joy->init_stage,
                     arena, freeb,
                     (unsigned)app_task_beat,
                     (unsigned)usb_init_retry_fails);
            lv_label_set_text(s_ctx.info_label, s_info_buf);
        }
    }
}

/*******************************************************************************
 * page_joystick_destroy
 *******************************************************************************/
void page_joystick_destroy(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));
}
