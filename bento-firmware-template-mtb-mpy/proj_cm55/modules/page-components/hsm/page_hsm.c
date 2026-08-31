/*******************************************************************************
 * File Name: page_hsm.c
 *
 * Description: HSM - OPTIGA Trust M security dashboard page.
 *              Card-based layout showing chip identity, certificates,
 *              key store, health status, security counters, and action buttons.
 *
 *              When ENABLE_OPTIGA=1, reads real data from OPTIGA Trust M
 *              via optiga_util_read_data() — same C functions as modoptiga.c.
 *              Touch controller paused during I2C access (shared SCB5 bus).
 *
 *              Design follows Astro UX status system:
 *              Green=normal, Amber=warning, Red=critical.
 *
 *******************************************************************************/

#include "page_hsm.h"
#include "page_manager.h"
#include "tesaiot_ui_theme.h"
#include "tesaiot_ui_helpers.h"
#include "font_hsm_icons.h"
#include "hsm_provision_ui.h"
#include "ipc_communication.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

/*******************************************************************************
 * HSM-specific color definitions (Astro UX status system)
 *******************************************************************************/
/* HSM Benchmark response layout (must match ipc_hsm_handler.h):
 * 3 sets x 5 ops, each timing uint32_t LE in MICROSECONDS,
 * then 1 byte optiga_present. 0xFFFFFFFF = op unavailable/failed. */
#define HSM_BENCH_COUNT          5
#define HSM_BENCH_SET_OPTIGA     0
#define HSM_BENCH_SET_MCU_HW     1
#define HSM_BENCH_SET_MCU_SW     2
#define HSM_BENCH_SET_COUNT      3
#define HSM_BENCH_SET_LEN        (HSM_BENCH_COUNT * 4)
#define HSM_BENCH_SET_OFF(s)     ((s) * HSM_BENCH_SET_LEN)
#define HSM_BENCH_PRESENT_OFF    (HSM_BENCH_SET_COUNT * HSM_BENCH_SET_LEN)
#define HSM_BENCH_TOTAL_LEN      (HSM_BENCH_PRESENT_OFF + 1)
#define HSM_BENCH_FAIL_SENTINEL  0xFFFFFFFFu

/* HSM Cert response layout */
#define HSM_CERT_SIZE_OFF      0
#define HSM_CERT_STRINGS_OFF   2

/* HSM Health response layout */
#define HSM_HEALTH_HW_OFF      0
#define HSM_HEALTH_UID_OFF     1
#define HSM_HEALTH_CERT_OFF    2
#define HSM_HEALTH_RNG_OFF     3
#define HSM_HEALTH_SHA_OFF     4
#define HSM_HEALTH_ECC_OFF     5
#define HSM_HEALTH_RW_OFF      6
#define HSM_HEALTH_META_OFF    7
#define HSM_HEALTH_TOTAL_LEN   8

/*******************************************************************************
 * HSM IPC send helper - sends command to CM33_NS and waits for response
 *
 * Returns true on success, false on timeout/error.
 * Uses dedicated shared-memory buffers per command type.
 *******************************************************************************/
CY_SECTION_SHAREDMEM static ipc_msg_t      s_hsm_cmd_msg;
CY_SECTION_SHAREDMEM static ipc_response_t s_hsm_cmd_resp;

static bool hsm_ipc_send(uint32_t cmd, const uint8_t *data, uint16_t data_len,
                          uint8_t cmd_field, uint32_t timeout_ms)
{
    memset(&s_hsm_cmd_msg, 0, sizeof(s_hsm_cmd_msg));
    memset(&s_hsm_cmd_resp, 0, sizeof(s_hsm_cmd_resp));

    s_hsm_cmd_msg.client_id = CM33_IPC_HSM_CLIENT_ID;
    s_hsm_cmd_msg.intr_mask = CY_IPC_CYPIPE_INTR_MASK_EP1;
    s_hsm_cmd_msg.cmd       = cmd;
    s_hsm_cmd_msg.value     = (uint32_t)(uintptr_t)&s_hsm_cmd_resp;

    /* Copy input data if provided */
    if (data && data_len > 0) {
        uint16_t copy = (data_len < IPC_DATA_MAX_LEN) ? data_len : IPC_DATA_MAX_LEN;
        memcpy(s_hsm_cmd_msg.data, data, copy);
    }

    /* Set cmd field for cert slot index etc. */
    s_hsm_cmd_resp.cmd   = cmd_field;
    s_hsm_cmd_resp.ready = 0;

    /* Send with retry */
    cy_en_ipc_pipe_status_t st = CY_IPC_PIPE_ERROR_NO_IPC;
    for (int i = 0; i < 20; i++) {
        st = Cy_IPC_Pipe_SendMessage(
            CM33_IPC_PIPE_EP_ADDR, CM55_IPC_PIPE_EP_ADDR,
            (void *)&s_hsm_cmd_msg, NULL);
        if (st == CY_IPC_PIPE_SUCCESS) break;
        Cy_SysLib_DelayUs(200);
    }
    if (st != CY_IPC_PIPE_SUCCESS) return false;

    /* Poll for response */
    uint32_t elapsed = 0;
    while (s_hsm_cmd_resp.ready != 1 && elapsed < timeout_ms) {
        Cy_SysLib_DelayUs(1000);
        elapsed++;
    }
    return (s_hsm_cmd_resp.ready == 1 && s_hsm_cmd_resp.status == 0);
}

#define HSM_COLOR_TITLE      0xBB86FC
#define HSM_COLOR_OK         0x50D890
#define HSM_COLOR_WARN       0xF2B84B
#define HSM_COLOR_CRIT       0xE85B5B
#define HSM_COLOR_CERT       0x448AFF
#define HSM_COLOR_KEY        0xFFD740
#define HSM_COLOR_CHIP       0x71C7EC
#define HSM_COLOR_COUNTER    0xE040FB
#define HSM_COLOR_BTN_PIN    0x448AFF
#define HSM_COLOR_BTN_CERT   0xFF9800
#define HSM_COLOR_BTN_BENCH  0x50D890
#define HSM_COLOR_BTN_ENROL   0xBB86FC
#define HSM_COLOR_BTN_PROTECT 0xFF5722

#define HSM_COL_W            384
#define HSM_CARD_H_CHIP      190
#define HSM_CARD_H_COUNTER   76
#define HSM_CARD_H_HEALTH    LV_SIZE_CONTENT  /* Auto-grow to fit text */
#define HSM_CARD_H_CERT      110
#define HSM_CARD_H_KEY       110

/*******************************************************************************
 * HSM IPC response layout (must match ipc_hsm_handler.h on CM33_NS)
 *******************************************************************************/
#define HSM_RESP_UID_OFF     0
#define HSM_RESP_LCS_OFF     27
#define HSM_RESP_CTR0_OFF    28
#define HSM_RESP_CTR1_OFF    32
#define HSM_RESP_CERT_OFF    36
#define HSM_RESP_HEALTH_OFF  40
#define HSM_RESP_TOTAL_LEN   41

/*******************************************************************************
 * Static data buffers (populated once at page create)
 *******************************************************************************/
static char s_uid_str[96];      /* Full UID hex string (27 bytes = ~80 chars) */
static char s_fw_str[32];       /* Firmware version */
static char s_lcs_str[32];      /* Lifecycle state */
static char s_ctr0_str[24];    /* Counter 0 value (integer) */
static char s_ctr1_str[24];    /* Counter 1 value (integer) */
static char s_cert_size[4][24]; /* Certificate sizes per slot */

/*******************************************************************************
 * Module-Static Context
 *******************************************************************************/
typedef struct {
    lv_obj_t *chip_name_lbl;
    lv_obj_t *chip_uid_lbl;
    lv_obj_t *chip_fw_lbl;
    lv_obj_t *cert_slot_lbls[4];
    lv_obj_t *key_slot_lbls[4];
    lv_obj_t *health_icon_lbl;
    lv_obj_t *health_status_lbl;
    lv_obj_t *health_detail_lbl;
    lv_obj_t *counter_lbl;
    lv_obj_t *btn_pin;
    lv_obj_t *btn_cert;
    lv_obj_t *btn_bench;
    lv_obj_t *btn_enrol;
    lv_obj_t *btn_protect;
    lv_obj_t *status_lbl;
    bool      optiga_ok;
} page_hsm_ctx_t;

static page_hsm_ctx_t s_ctx;

/*******************************************************************************
 * PIN Code Entry UI — iOS-style passcode screen
 *
 * Flow:
 *   No PIN set  → "Create a PIN Code" → enter 4 → "Confirm PIN" → enter 4
 *                 → match? saved : "Mismatch" → retry
 *   PIN exists  → "Enter your PIN Code" → enter 4
 *                 → correct? success : "Incorrect" → retry (max 5 attempts)
 *******************************************************************************/
#define PIN_LENGTH   4
#define PIN_MAX_TRIES 5
#define PIN_DOT_SZ   22
#define PIN_DOT_GAP  18
#define PIN_BTN_SZ   72
#define PIN_BTN_GAP  16

#define PIN_COLOR_ACCENT  0x71C7EC   /* Cyan accent (dots, cancel) */
#define PIN_COLOR_NUM_BG  0x2A2020   /* Number button background */
#define PIN_COLOR_NUM_BD  0x5A4A4A   /* Number button border */
#define PIN_COLOR_SUCCESS 0x50D890
#define PIN_COLOR_ERROR   0xE85B5B

typedef enum {
    PIN_MODE_ENTER,         /* Verify existing PIN */
    PIN_MODE_SET_NEW,       /* First entry of new PIN */
    PIN_MODE_CONFIRM,       /* Confirm new PIN */
    PIN_MODE_CHANGE_VERIFY  /* Verify old PIN before changing */
} pin_mode_t;

static struct {
    lv_obj_t   *overlay;            /* Full-screen overlay on content */
    lv_obj_t   *icon_lbl;           /* Lock icon */
    lv_obj_t   *title_lbl;          /* Title text */
    lv_obj_t   *dots[PIN_LENGTH];   /* Dot indicators */
    lv_obj_t   *msg_lbl;            /* Error/success message below dots */
    uint8_t     entered[PIN_LENGTH];
    uint8_t     new_pin[PIN_LENGTH];
    uint8_t     stored_pin[PIN_LENGTH];
    int         pos;                /* Current digit index (0..5) */
    pin_mode_t  mode;
    bool        pin_set;            /* Is a PIN stored? */
    int         attempts;           /* Failed attempt count */
    lv_timer_t *timer;              /* Pending close/retry one-shot timer */
} s_pin;

/* ── Forward declarations ──────────────────────────────────────────────── */
static void pin_open(void);
static void pin_close(void);
static void pin_close_timer_cb(lv_timer_t *t);
static void pin_retry_timer_cb(lv_timer_t *t);
static void pin_update_dots(void);
static void pin_on_complete(void);
static void pin_show_msg(const char *msg, uint32_t color);
static void pin_reset_entry(const char *title);
static void pin_change_cb(lv_event_t *e);
static void pin_reset_pin_cb(lv_event_t *e);

/* Arm the one-shot PIN timer, replacing any pending one. The stored handle
 * lets pin_close()/page_hsm_destroy() cancel it — a timer firing after the
 * overlay is deleted would dereference freed LVGL objects. */
static void pin_arm_timer(lv_timer_cb_t cb, uint32_t ms)
{
    if (s_pin.timer) {
        lv_timer_delete(s_pin.timer);
        s_pin.timer = NULL;
    }
    s_pin.timer = lv_timer_create(cb, ms, NULL);
}

/* ── PIN button callbacks ──────────────────────────────────────────────── */
static void pin_num_cb(lv_event_t *e)
{
    if (s_pin.pos >= PIN_LENGTH) return;
    int digit = (int)(intptr_t)lv_event_get_user_data(e);
    s_pin.entered[s_pin.pos++] = (uint8_t)digit;
    pin_update_dots();
    if (s_pin.pos == PIN_LENGTH) {
        pin_on_complete();
    }
}

static void pin_delete_cb(lv_event_t *e)
{
    (void)e;
    if (s_pin.pos > 0) {
        s_pin.pos--;
        pin_update_dots();
    }
}

static void pin_cancel_cb(lv_event_t *e)
{
    (void)e;
    pin_close();
}

/* ── Update dot indicators ─────────────────────────────────────────────── */
static void pin_update_dots(void)
{
    if (!s_pin.overlay || !s_pin.dots[0]) return;
    for (int i = 0; i < PIN_LENGTH; i++) {
        if (i < s_pin.pos) {
            /* Filled dot */
            lv_obj_set_style_bg_color(s_pin.dots[i],
                                       lv_color_hex(PIN_COLOR_ACCENT), 0);
            lv_obj_set_style_bg_opa(s_pin.dots[i], LV_OPA_COVER, 0);
        } else {
            /* Hollow dot */
            lv_obj_set_style_bg_opa(s_pin.dots[i], LV_OPA_TRANSP, 0);
        }
    }
}

/* ── Show message below dots ───────────────────────────────────────────── */
static void pin_show_msg(const char *msg, uint32_t color)
{
    if (s_pin.msg_lbl) {
        lv_label_set_text(s_pin.msg_lbl, msg);
        lv_obj_set_style_text_color(s_pin.msg_lbl, lv_color_hex(color), 0);
    }
}

/* ── Reset entry for next phase ────────────────────────────────────────── */
static void pin_reset_entry(const char *title)
{
    s_pin.pos = 0;
    memset(s_pin.entered, 0, PIN_LENGTH);
    pin_update_dots();
    if (s_pin.title_lbl) lv_label_set_text(s_pin.title_lbl, title);
    pin_show_msg("", 0);
}

/* ── Handle completed 4-digit entry (uses OPTIGA IPC for real storage) ── */
static void pin_on_complete(void)
{
    switch (s_pin.mode) {
    case PIN_MODE_SET_NEW:
        /* Save first entry, ask for confirmation */
        memcpy(s_pin.new_pin, s_pin.entered, PIN_LENGTH);
        s_pin.mode = PIN_MODE_CONFIRM;
        pin_reset_entry("Confirm your PIN Code");
        pin_show_msg("Re-enter the same PIN to confirm", PIN_COLOR_ACCENT);
        break;

    case PIN_MODE_CONFIRM:
        if (memcmp(s_pin.entered, s_pin.new_pin, PIN_LENGTH) == 0) {
            /* Match - store PIN via IPC to OPTIGA DATA_3 */
            pin_show_msg("Saving to OPTIGA...", PIN_COLOR_ACCENT);
            lv_refr_now(NULL);

            bool ok = hsm_ipc_send(IPC_CMD_HSM_PIN_SET, s_pin.new_pin,
                                    PIN_LENGTH, 0, 5000);
            if (ok && s_hsm_cmd_resp.data[0] == 1) {
                s_pin.pin_set = true;
                pin_show_msg("PIN Code Saved to OPTIGA!", PIN_COLOR_SUCCESS);
                pin_arm_timer(pin_close_timer_cb, 800);
            } else {
                pin_show_msg("Failed to save PIN. Try again.", PIN_COLOR_ERROR);
                s_pin.mode = PIN_MODE_SET_NEW;
                pin_arm_timer(pin_retry_timer_cb, 1500);
            }
        } else {
            /* Mismatch - start over */
            pin_show_msg("PINs don't match. Try again.", PIN_COLOR_ERROR);
            s_pin.mode = PIN_MODE_SET_NEW;
            s_pin.pos = 0;
            memset(s_pin.entered, 0, PIN_LENGTH);
            pin_arm_timer(pin_retry_timer_cb, 1000);
        }
        break;

    case PIN_MODE_ENTER:
        /* Verify PIN via IPC against OPTIGA stored hash */
        pin_show_msg("Verifying...", PIN_COLOR_ACCENT);
        lv_refr_now(NULL);

        {
            bool ok = hsm_ipc_send(IPC_CMD_HSM_PIN_VERIFY, s_pin.entered,
                                    PIN_LENGTH, 0, 5000);
            if (ok && s_hsm_cmd_resp.data[0] == 1) {
                /* Correct PIN */
                pin_show_msg("Unlocked!", PIN_COLOR_SUCCESS);
                if (s_pin.icon_lbl) {
                    lv_label_set_text(s_pin.icon_lbl, HSM_ICON_UNLOCK);
                    lv_obj_set_style_text_color(s_pin.icon_lbl,
                                                lv_color_hex(PIN_COLOR_SUCCESS), 0);
                }
                pin_arm_timer(pin_close_timer_cb, 800);
            } else {
                /* Wrong PIN — or the stored hash is unreachable because
                 * the secure element is missing (tamper/counterfeit demo).
                 * status==1 is exactly the optiga_open() failure code;
                 * IPC timeouts / transient hash errors must NOT claim the
                 * chip is gone. */
                bool chip_missing = !s_ctx.optiga_ok ||
                                    (s_hsm_cmd_resp.ready == 1 &&
                                     s_hsm_cmd_resp.status == 1);
                s_pin.attempts++;
                char buf[64];
                snprintf(buf, sizeof(buf), "Incorrect PIN (%d/%d)%s",
                         s_pin.attempts, PIN_MAX_TRIES,
                         chip_missing ? "\nSecure element not found" : "");
                pin_show_msg(buf, PIN_COLOR_ERROR);
                if (s_pin.attempts >= PIN_MAX_TRIES) {
                    pin_show_msg("Too many attempts. Locked.", PIN_COLOR_ERROR);
                    pin_arm_timer(pin_close_timer_cb, 1500);
                } else {
                    s_pin.pos = 0;
                    memset(s_pin.entered, 0, PIN_LENGTH);
                    pin_update_dots();
                }
            }
        }
        break;

    case PIN_MODE_CHANGE_VERIFY:
        /* Verify old PIN before allowing change */
        pin_show_msg("Verifying...", PIN_COLOR_ACCENT);
        lv_refr_now(NULL);

        {
            bool ok = hsm_ipc_send(IPC_CMD_HSM_PIN_VERIFY, s_pin.entered,
                                    PIN_LENGTH, 0, 5000);
            if (ok && s_hsm_cmd_resp.data[0] == 1) {
                /* Old PIN correct - proceed to set new */
                s_pin.mode = PIN_MODE_SET_NEW;
                pin_reset_entry("Set New PIN Code");
                pin_show_msg("Enter a new 4-digit PIN", PIN_COLOR_ACCENT);
            } else {
                /* Wrong PIN */
                s_pin.attempts++;
                char buf[48];
                snprintf(buf, sizeof(buf), "Incorrect PIN (%d/%d)",
                         s_pin.attempts, PIN_MAX_TRIES);
                pin_show_msg(buf, PIN_COLOR_ERROR);
                if (s_pin.attempts >= PIN_MAX_TRIES) {
                    pin_show_msg("Too many attempts. Locked.", PIN_COLOR_ERROR);
                    pin_arm_timer(pin_close_timer_cb, 1500);
                } else {
                    s_pin.pos = 0;
                    memset(s_pin.entered, 0, PIN_LENGTH);
                    pin_update_dots();
                }
            }
        }
        break;
    }
}

/* ── Create a single circular number button ────────────────────────────── */
static lv_obj_t *pin_create_num_btn(lv_obj_t *parent, int digit,
                                     int x, int y)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_set_size(btn, PIN_BTN_SZ, PIN_BTN_SZ);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(PIN_COLOR_NUM_BG), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_80, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(PIN_COLOR_NUM_BD), 0);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_COVER, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    /* Pressed state */
    lv_obj_set_style_bg_color(btn, lv_color_hex(PIN_COLOR_ACCENT), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_PRESSED);

    char txt[2] = { '0' + digit, 0 };
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, txt);
    lv_obj_set_style_text_font(lbl, UI_FONT_H1, 0);
    lv_obj_set_style_text_color(lbl,
                                lv_color_hex(UI_COLOR_TEXT_PRIMARY), 0);
    lv_obj_center(lbl);

    lv_obj_add_event_cb(btn, pin_num_cb, LV_EVENT_CLICKED,
                         (void *)(intptr_t)digit);
    return btn;
}

/* ── Open PIN entry overlay ────────────────────────────────────────────── */
static void pin_open(void)
{
    if (s_pin.overlay) return;  /* Already open */

    s_pin.pos = 0;
    s_pin.attempts = 0;
    memset(s_pin.entered, 0, PIN_LENGTH);

    if (s_ctx.optiga_ok) {
        /* Check if PIN exists in OPTIGA via IPC */
        bool ok = hsm_ipc_send(IPC_CMD_HSM_PIN_CHECK, NULL, 0, 0, 5000);
        s_pin.pin_set = (ok && s_hsm_cmd_resp.data[0] == 1);
        s_pin.mode = s_pin.pin_set ? PIN_MODE_ENTER : PIN_MODE_SET_NEW;
    } else {
        /* OPTIGA absent — tamper demo: always ask for the PIN.
         * The stored hash lives only inside the chip, so every attempt
         * fails verification until the genuine chip is re-attached. */
        s_pin.pin_set = true;
        s_pin.mode = PIN_MODE_ENTER;
    }

    /* Full-screen overlay on top of everything */
    lv_obj_t *scr = lv_screen_active();
    lv_obj_t *ov = lv_obj_create(scr);
    lv_obj_set_size(ov, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_pos(ov, 0, 0);
    lv_obj_set_style_bg_color(ov, lv_color_hex(0x0D0A08), 0);
    lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ov, 0, 0);
    lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
    s_pin.overlay = ov;

    /* ── Back button (top-left) ────────────────────────────────────── */
    {
        lv_obj_t *btn = lv_button_create(ov);
        lv_obj_set_size(btn, 80, 36);
        lv_obj_set_pos(btn, UI_SPACE_MD, UI_SPACE_MD);
        lv_obj_set_style_bg_color(btn, lv_color_hex(UI_COLOR_BG_CARD), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(btn, UI_CARD_RADIUS, 0);
        lv_obj_set_style_border_width(btn, 0, 0);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, LV_SYMBOL_LEFT " Back");
        lv_obj_set_style_text_color(lbl,
                                    lv_color_hex(UI_COLOR_TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(lbl, UI_FONT_CAPTION, 0);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, pin_cancel_cb, LV_EVENT_CLICKED, NULL);
    }

    /* Layout: left ~40% = icon+title+dots+message, right ~60% = numpad */
    int left_cx = UI_SCREEN_W * 3 / 10;    /* 240 */
    int right_cx = UI_SCREEN_W * 13 / 20;  /* 520 */

    /* ── Left side: Lock icon (use explicit size container) ────────── */
    {
        lv_obj_t *icon_box = lv_obj_create(ov);
        lv_obj_set_size(icon_box, 48, 48);
        lv_obj_set_pos(icon_box, left_cx - 24, 70);
        lv_obj_set_style_bg_opa(icon_box, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(icon_box, 0, 0);
        lv_obj_clear_flag(icon_box, LV_OBJ_FLAG_SCROLLABLE);

        s_pin.icon_lbl = lv_label_create(icon_box);
        lv_label_set_text(s_pin.icon_lbl, HSM_ICON_LOCK);
        lv_obj_set_style_text_font(s_pin.icon_lbl, &font_hsm_icons_28, 0);
        lv_obj_set_style_text_color(s_pin.icon_lbl,
                                    lv_color_hex(PIN_COLOR_ACCENT), 0);
        lv_obj_center(s_pin.icon_lbl);
    }

    /* Title */
    s_pin.title_lbl = lv_label_create(ov);
    lv_label_set_text(s_pin.title_lbl,
                      s_pin.pin_set ? "Enter your PIN Code"
                                    : "Create a PIN Code");
    lv_obj_set_style_text_font(s_pin.title_lbl, UI_FONT_H1, 0);
    lv_obj_set_style_text_color(s_pin.title_lbl,
                                lv_color_hex(UI_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_width(s_pin.title_lbl, left_cx * 2 - 40);
    lv_obj_set_style_text_align(s_pin.title_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_pin.title_lbl, 20, 140);

    /* ── Dots row (centered in left area) ──────────────────────────── */
    int dots_total_w = PIN_LENGTH * PIN_DOT_SZ + (PIN_LENGTH - 1) * PIN_DOT_GAP;
    int dots_x0 = left_cx - dots_total_w / 2;
    int dots_y = 220;

    for (int i = 0; i < PIN_LENGTH; i++) {
        lv_obj_t *dot = lv_obj_create(ov);
        lv_obj_set_size(dot, PIN_DOT_SZ, PIN_DOT_SZ);
        lv_obj_set_pos(dot, dots_x0 + i * (PIN_DOT_SZ + PIN_DOT_GAP), dots_y);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_color(dot, lv_color_hex(PIN_COLOR_ACCENT), 0);
        lv_obj_set_style_border_width(dot, 2, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_TRANSP, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        s_pin.dots[i] = dot;
    }

    /* Message label below dots */
    s_pin.msg_lbl = lv_label_create(ov);
    lv_label_set_text(s_pin.msg_lbl,
                      s_pin.pin_set ? "" : "Choose a 4-digit PIN");
    lv_obj_set_style_text_font(s_pin.msg_lbl, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(s_pin.msg_lbl,
                                lv_color_hex(PIN_COLOR_ACCENT), 0);
    lv_obj_set_width(s_pin.msg_lbl, left_cx * 2 - 40);
    lv_obj_set_style_text_align(s_pin.msg_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_pin.msg_lbl, 20, dots_y + 44);

    /* ── Change PIN / Reset PIN buttons (need PIN + chip present) ──── */
    if (s_pin.pin_set && s_ctx.optiga_ok) {
        int btn_y = dots_y + 80;

        lv_obj_t *chg_btn = lv_btn_create(ov);
        lv_obj_set_size(chg_btn, 140, 32);
        lv_obj_set_pos(chg_btn, left_cx - 148, btn_y);
        lv_obj_set_style_bg_color(chg_btn, lv_color_hex(UI_COLOR_BG_CARD), 0);
        lv_obj_set_style_bg_opa(chg_btn, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(chg_btn, UI_RADIUS_SM, 0);
        lv_obj_set_style_border_color(chg_btn, lv_color_hex(PIN_COLOR_ACCENT), 0);
        lv_obj_set_style_border_width(chg_btn, 1, 0);
        lv_obj_t *chg_lbl = lv_label_create(chg_btn);
        lv_label_set_text(chg_lbl, "Change PIN");
        lv_obj_set_style_text_font(chg_lbl, UI_FONT_CAPTION, 0);
        lv_obj_set_style_text_color(chg_lbl,
                                    lv_color_hex(PIN_COLOR_ACCENT), 0);
        lv_obj_center(chg_lbl);
        lv_obj_add_event_cb(chg_btn, pin_change_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *rst_btn = lv_btn_create(ov);
        lv_obj_set_size(rst_btn, 140, 32);
        lv_obj_set_pos(rst_btn, left_cx + 8, btn_y);
        lv_obj_set_style_bg_color(rst_btn, lv_color_hex(UI_COLOR_BG_CARD), 0);
        lv_obj_set_style_bg_opa(rst_btn, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(rst_btn, UI_RADIUS_SM, 0);
        lv_obj_set_style_border_color(rst_btn, lv_color_hex(HSM_COLOR_CRIT), 0);
        lv_obj_set_style_border_width(rst_btn, 1, 0);
        lv_obj_t *rst_lbl = lv_label_create(rst_btn);
        lv_label_set_text(rst_lbl, "Reset PIN");
        lv_obj_set_style_text_font(rst_lbl, UI_FONT_CAPTION, 0);
        lv_obj_set_style_text_color(rst_lbl,
                                    lv_color_hex(HSM_COLOR_CRIT), 0);
        lv_obj_center(rst_lbl);
        lv_obj_add_event_cb(rst_btn, pin_reset_pin_cb, LV_EVENT_CLICKED, NULL);
    }

    /* ── Right side: Number pad (3x3 + bottom row) ─────────────────── */
    int grid_w = 3 * PIN_BTN_SZ + 2 * PIN_BTN_GAP;
    int gx0 = right_cx - grid_w / 2;
    int grid_h = 4 * PIN_BTN_SZ + 3 * PIN_BTN_GAP;
    int gy0 = (UI_SCREEN_H - grid_h) / 2;  /* Vertically center numpad */

    /* Rows 1-3: digits 1-9 */
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            int digit = r * 3 + c + 1;
            int x = gx0 + c * (PIN_BTN_SZ + PIN_BTN_GAP);
            int y = gy0 + r * (PIN_BTN_SZ + PIN_BTN_GAP);
            pin_create_num_btn(ov, digit, x, y);
        }
    }

    /* Row 4: [DEL] [0] [CANCEL] */
    int row4_y = gy0 + 3 * (PIN_BTN_SZ + PIN_BTN_GAP);

    /* Delete button */
    {
        lv_obj_t *btn = lv_obj_create(ov);
        lv_obj_set_size(btn, PIN_BTN_SZ, PIN_BTN_SZ);
        lv_obj_set_pos(btn, gx0, row4_y);
        lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(PIN_COLOR_NUM_BD), 0);
        lv_obj_set_style_border_width(btn, 2, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(btn, lv_color_hex(PIN_COLOR_ACCENT),
                                   LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_PRESSED);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, LV_SYMBOL_BACKSPACE);
        lv_obj_set_style_text_font(lbl, UI_FONT_H2, 0);
        lv_obj_set_style_text_color(lbl,
                                    lv_color_hex(UI_COLOR_TEXT_PRIMARY), 0);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, pin_delete_cb, LV_EVENT_CLICKED, NULL);
    }

    /* 0 button */
    pin_create_num_btn(ov, 0,
                        gx0 + 1 * (PIN_BTN_SZ + PIN_BTN_GAP), row4_y);

    /* Bottom-right slot intentionally empty (Back button is in top-left) */
}

/* ── Close PIN overlay ─────────────────────────────────────────────────── */
static void pin_close(void)
{
    if (s_pin.timer) {
        lv_timer_delete(s_pin.timer);
        s_pin.timer = NULL;
    }
    if (s_pin.overlay) {
        lv_obj_delete(s_pin.overlay);
        s_pin.overlay = NULL;
        s_pin.icon_lbl = NULL;
        s_pin.title_lbl = NULL;
        s_pin.msg_lbl = NULL;
        for (int i = 0; i < PIN_LENGTH; i++) s_pin.dots[i] = NULL;
    }
}

/* ── Timer wrapper for pin_close (lv_timer_cb_t signature) ─────────────── */
static void pin_close_timer_cb(lv_timer_t *t)
{
    lv_timer_delete(t);
    s_pin.timer = NULL;
    pin_close();
}

static void pin_retry_timer_cb(lv_timer_t *t)
{
    lv_timer_delete(t);
    s_pin.timer = NULL;
    if (!s_pin.overlay) return;  /* Overlay closed while timer was pending */
    pin_reset_entry("Set New PIN Code");
    pin_show_msg("Enter a 4-digit PIN", 0);
}

/* ── Change PIN callback (requires old PIN verification first) ─────────── */
static void pin_change_cb(lv_event_t *e)
{
    (void)e;
    s_pin.mode = PIN_MODE_CHANGE_VERIFY;
    s_pin.attempts = 0;
    pin_reset_entry("Enter Current PIN");
    pin_show_msg("Verify your current PIN to change", PIN_COLOR_ACCENT);
}

/* ── Reset PIN callback (erase PIN from OPTIGA) ───────────────────────── */
static void pin_reset_pin_cb(lv_event_t *e)
{
    (void)e;
    pin_show_msg("Resetting PIN on OPTIGA...", PIN_COLOR_ACCENT);
    lv_refr_now(NULL);

    bool ok = hsm_ipc_send(IPC_CMD_HSM_PIN_RESET, NULL, 0, 0, 5000);
    if (ok) {
        s_pin.pin_set = false;
        pin_show_msg("PIN Removed. Set a new PIN.", PIN_COLOR_SUCCESS);
        s_pin.mode = PIN_MODE_SET_NEW;
        pin_reset_entry("Create a PIN Code");
        pin_show_msg("Choose a 4-digit PIN", PIN_COLOR_ACCENT);
    } else {
        pin_show_msg("Failed to reset PIN.", PIN_COLOR_ERROR);
    }
}

/* ── PIN button click handler (from HSM page) ──────────────────────────── */
static void pin_btn_clicked_cb(lv_event_t *e)
{
    (void)e;
    pin_open();
}

/*******************************************************************************
 * Device Certificate Viewer — simple overlay showing cert slot details
 *******************************************************************************/
static lv_obj_t *s_cert_overlay;

static void cert_close_cb(lv_event_t *e)
{
    (void)e;
    if (s_cert_overlay) {
        lv_obj_delete(s_cert_overlay);
        s_cert_overlay = NULL;
    }
}

/* Cert detail expand/collapse callback */
static void cert_card_click_cb(lv_event_t *e)
{
    int slot = (int)(intptr_t)lv_event_get_user_data(e);
    lv_obj_t *card = lv_event_get_target(e);

    /* Find detail label (last child is the detail container) */
    uint32_t cnt = lv_obj_get_child_count(card);
    if (cnt < 2) return;
    lv_obj_t *detail = lv_obj_get_child(card, cnt - 1);
    if (!detail) return;

    bool hidden = lv_obj_has_flag(detail, LV_OBJ_FLAG_HIDDEN);
    if (hidden) {
        /* Expand: fetch cert details via IPC if not cached */
        lv_obj_clear_flag(detail, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_height(card, LV_SIZE_CONTENT);

        /* Check if already populated */
        lv_obj_t *first_child = lv_obj_get_child(detail, 0);
        if (first_child) return;  /* Already has content */

        /* Read cert via IPC */
        lv_label_set_text(lv_obj_get_child(card, 1), "Loading...");
        lv_refr_now(NULL);

        uint8_t slot_byte = (uint8_t)slot;
        bool ok = hsm_ipc_send(IPC_CMD_HSM_READ_CERT, &slot_byte, 1,
                                0, 15000);

        /* Parse response */
        uint16_t der_sz = 0;
        if (ok && s_hsm_cmd_resp.data_len >= 2) {
            der_sz = (uint16_t)(s_hsm_cmd_resp.data[0]) |
                     ((uint16_t)(s_hsm_cmd_resp.data[1]) << 8);
        }

        if (!ok) {
            /* IPC communication error */
            lv_obj_t *err = lv_label_create(detail);
            lv_label_set_text(err, "Communication error with OPTIGA");
            lv_obj_set_style_text_font(err, UI_FONT_CAPTION, 0);
            lv_obj_set_style_text_color(err,
                lv_color_hex(HSM_COLOR_CRIT), 0);
            lv_label_set_text(lv_obj_get_child(card, 1), "Error");
        } else if (der_sz == 0) {
            /* Empty slot - show single "Not Provisioned" on status line only */
            lv_label_set_text(lv_obj_get_child(card, 1), "Not Provisioned");
            /* Keep detail hidden - nothing to expand */
            lv_obj_add_flag(detail, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_height(card, 64);
        } else if (s_hsm_cmd_resp.data_len > HSM_CERT_STRINGS_OFF) {
            /* Valid cert with parsed fields */
            const char *strs[6] = {NULL};
            const char *p = (const char *)&s_hsm_cmd_resp.data[HSM_CERT_STRINGS_OFF];
            const char *pend = (const char *)&s_hsm_cmd_resp.data[s_hsm_cmd_resp.data_len];
            for (int si = 0; si < 6 && p < pend; si++) {
                strs[si] = p;
                p += strlen(p) + 1;
            }
            /* Display empty strings as "-" instead of blank */
            const char *subj  = (strs[0] && strs[0][0]) ? strs[0] : "-";
            const char *issuer= (strs[1] && strs[1][0]) ? strs[1] : "-";
            const char *org   = (strs[2] && strs[2][0]) ? strs[2] : "-";
            const char *ctry  = (strs[3] && strs[3][0]) ? strs[3] : "-";
            const char *nbef  = (strs[4] && strs[4][0]) ? strs[4] : "-";
            const char *naft  = (strs[5] && strs[5][0]) ? strs[5] : "-";

            static const char * const labels[] = {
                "Subject CN:", "Issuer CN:", "Organization:",
                "Country:", "Valid From:", "Valid To:", "DER Size:"
            };

            for (int r = 0; r < 7; r++) {
                lv_obj_t *row = lv_obj_create(detail);
                lv_obj_set_size(row, LV_PCT(100), 20);
                lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
                lv_obj_set_style_border_width(row, 0, 0);
                lv_obj_set_style_pad_all(row, 0, 0);
                lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

                lv_obj_t *ll = lv_label_create(row);
                lv_label_set_text(ll, labels[r]);
                lv_obj_set_style_text_font(ll, UI_FONT_CAPTION, 0);
                lv_obj_set_style_text_color(ll,
                    lv_color_hex(UI_COLOR_TEXT_SECONDARY), 0);
                lv_obj_set_pos(ll, 0, 0);

                lv_obj_t *vl = lv_label_create(row);
                if (r < 6) {
                    const char *vals[] = { subj, issuer, org, ctry, nbef, naft };
                    lv_label_set_text(vl, vals[r]);
                } else {
                    char sz_buf[16];
                    snprintf(sz_buf, sizeof(sz_buf), "%u bytes", der_sz);
                    lv_label_set_text(vl, sz_buf);
                }
                lv_obj_set_style_text_font(vl, UI_FONT_CAPTION, 0);
                lv_obj_set_style_text_color(vl,
                    lv_color_hex(UI_COLOR_TEXT_PRIMARY), 0);
                lv_obj_set_pos(vl, 110, 0);
            }
            char status_buf[48];
            snprintf(status_buf, sizeof(status_buf), "DER: %u bytes", der_sz);
            lv_label_set_text(lv_obj_get_child(card, 1), status_buf);
        } else {
            /* DER exists but parse failed */
            lv_obj_t *msg = lv_label_create(detail);
            char pbuf[48];
            snprintf(pbuf, sizeof(pbuf), "DER: %u bytes (parse error)", der_sz);
            lv_label_set_text(msg, pbuf);
            lv_obj_set_style_text_font(msg, UI_FONT_CAPTION, 0);
            lv_obj_set_style_text_color(msg,
                lv_color_hex(HSM_COLOR_WARN), 0);
            lv_label_set_text(lv_obj_get_child(card, 1), pbuf);
        }
    } else {
        /* Collapse */
        lv_obj_add_flag(detail, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_height(card, 64);
    }
}

static void cert_btn_clicked_cb(lv_event_t *e)
{
    (void)e;
    if (s_cert_overlay) return;

    lv_obj_t *scr = lv_screen_active();
    lv_obj_t *ov = lv_obj_create(scr);
    lv_obj_set_size(ov, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_pos(ov, 0, 0);
    lv_obj_set_style_bg_color(ov, lv_color_hex(0x0D0A08), 0);
    lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ov, 0, 0);
    lv_obj_set_style_pad_all(ov, 0, 0);
    lv_obj_set_flex_flow(ov, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ov, 0, 0);
    lv_obj_set_scroll_dir(ov, LV_DIR_VER);
    s_cert_overlay = ov;

    /* Back button row */
    lv_obj_t *hdr = lv_obj_create(ov);
    lv_obj_set_size(hdr, UI_SCREEN_W, 52);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn = lv_button_create(hdr);
    lv_obj_set_size(btn, 80, 36);
    lv_obj_set_pos(btn, UI_SPACE_MD, UI_SPACE_MD);
    lv_obj_set_style_bg_color(btn, lv_color_hex(UI_COLOR_BG_CARD), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, UI_CARD_RADIUS, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_t *bl = lv_label_create(btn);
    lv_label_set_text(bl, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_color(bl, lv_color_hex(UI_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(bl, UI_FONT_CAPTION, 0);
    lv_obj_center(bl);
    lv_obj_add_event_cb(btn, cert_close_cb, LV_EVENT_CLICKED, NULL);

    /* Title */
    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, "Device Certificates");
    lv_obj_set_style_text_font(title, UI_FONT_H2, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(HSM_COLOR_BTN_CERT), 0);
    lv_obj_set_pos(title, 110, UI_SPACE_MD + 6);

    /* Scrollable content area */
    lv_obj_t *content = lv_obj_create(ov);
    lv_obj_set_size(content, UI_SCREEN_W, UI_SCREEN_H - 52);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, UI_SPACE_MD, 0);
    lv_obj_set_style_pad_row(content, UI_SPACE_SM, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);

    /* Hint */
    lv_obj_t *hint = lv_label_create(content);
    lv_label_set_text(hint, "Tap a certificate to view details");
    lv_obj_set_style_text_font(hint, UI_FONT_CAPTION, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(UI_COLOR_TEXT_DISABLED), 0);

    /* Cert cards (expand/collapse) */
    static const char * const oids[] = {"0xE0E0","0xE0E1","0xE0E2","0xE0E3"};
    static const char * const names[] = {
        "IFX Device Certificate (Factory)",
        "Project Certificate Slot 1",
        "Project Certificate Slot 2",
        "Trust Anchor (CA Root)"
    };
    for (int i = 0; i < 4; i++) {
        const char *st = s_cert_size[i][0] ? s_cert_size[i] : "Empty";
        uint32_t color = (st[0] == 'P') ? HSM_COLOR_OK : HSM_COLOR_WARN;

        /* Card container */
        lv_obj_t *card = lv_obj_create(content);
        lv_obj_set_size(card, UI_SCREEN_W - 4 * UI_SPACE_MD, 64);
        lv_obj_set_style_bg_color(card, lv_color_hex(UI_COLOR_BG_CARD), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(card, UI_CARD_RADIUS, 0);
        lv_obj_set_style_border_color(card, lv_color_hex(color), 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_border_side(card, LV_BORDER_SIDE_LEFT, 0);
        lv_obj_set_style_pad_all(card, UI_CARD_PAD, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(card, 4, 0);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);

        /* Title line: OID - Name + expand arrow */
        lv_obj_t *title_row = lv_label_create(card);
        lv_label_set_text_fmt(title_row, "%s %s  -  %s",
                              (st[0] == 'P') ? LV_SYMBOL_RIGHT : LV_SYMBOL_CLOSE,
                              oids[i], names[i]);
        lv_obj_set_style_text_font(title_row, UI_FONT_BODY, 0);
        lv_obj_set_style_text_color(title_row,
            lv_color_hex(UI_COLOR_TEXT_PRIMARY), 0);

        /* Status line */
        lv_obj_t *status = lv_label_create(card);
        lv_label_set_text_fmt(status, "Status: %s", st);
        lv_obj_set_style_text_font(status, UI_FONT_CAPTION, 0);
        lv_obj_set_style_text_color(status, lv_color_hex(color), 0);

        /* Detail container (hidden by default) */
        lv_obj_t *detail = lv_obj_create(card);
        lv_obj_set_size(detail, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(detail, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(detail, 0, 0);
        lv_obj_set_style_pad_all(detail, 0, 0);
        lv_obj_set_style_pad_row(detail, 2, 0);
        lv_obj_clear_flag(detail, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(detail, LV_FLEX_FLOW_COLUMN);
        lv_obj_add_flag(detail, LV_OBJ_FLAG_HIDDEN);

        /* Only clickable if cert is present */
        if (st[0] == 'P') {
            lv_obj_add_event_cb(card, cert_card_click_cb, LV_EVENT_CLICKED,
                                (void *)(intptr_t)i);
        }
    }
}

/*******************************************************************************
 * Benchmark Overlay — shows crypto capability summary
 *******************************************************************************/
static lv_obj_t *s_bench_overlay;

static void bench_close_cb(lv_event_t *e)
{
    (void)e;
    if (s_bench_overlay) {
        lv_obj_delete(s_bench_overlay);
        s_bench_overlay = NULL;
    }
}

static void bench_btn_clicked_cb(lv_event_t *e)
{
    (void)e;
    if (s_bench_overlay) return;

    lv_obj_t *scr = lv_screen_active();
    lv_obj_t *ov = lv_obj_create(scr);
    lv_obj_set_size(ov, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_pos(ov, 0, 0);
    lv_obj_set_style_bg_color(ov, lv_color_hex(0x0D0A08), 0);
    lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ov, 0, 0);
    lv_obj_set_style_pad_all(ov, 0, 0);
    lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
    s_bench_overlay = ov;

    /* Back button */
    lv_obj_t *btn = lv_button_create(ov);
    lv_obj_set_size(btn, 80, 36);
    lv_obj_set_pos(btn, UI_SPACE_MD, UI_SPACE_MD);
    lv_obj_set_style_bg_color(btn, lv_color_hex(UI_COLOR_BG_CARD), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, UI_CARD_RADIUS, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_t *bl = lv_label_create(btn);
    lv_label_set_text(bl, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_color(bl, lv_color_hex(UI_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(bl, UI_FONT_CAPTION, 0);
    lv_obj_center(bl);
    lv_obj_add_event_cb(btn, bench_close_cb, LV_EVENT_CLICKED, NULL);

    /* Title */
    lv_obj_t *title = lv_label_create(ov);
    lv_label_set_text(title, "Crypto Benchmark - OPTIGA vs MCU");
    lv_obj_set_style_text_font(title, UI_FONT_H2, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(HSM_COLOR_BTN_BENCH), 0);
    lv_obj_set_pos(title, UI_SPACE_MD, 60);

    /* Show "Running benchmark..." while IPC completes */
    lv_obj_t *status_lbl = lv_label_create(ov);
    lv_label_set_text(status_lbl, "Running benchmark (OPTIGA + MCU crypto)...");
    lv_obj_set_style_text_font(status_lbl, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(UI_COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_pos(status_lbl, UI_SPACE_MD, 100);
    lv_refr_now(NULL);  /* Force display update before blocking IPC */

    /* Run real benchmark via IPC */
    bool ok = hsm_ipc_send(IPC_CMD_HSM_BENCHMARK, NULL, 0, 0, 30000);

    /* Parse: 3 sets x 5 ops, uint32_t LE microseconds + present flag */
    uint32_t actual[HSM_BENCH_SET_COUNT][HSM_BENCH_COUNT];
    for (int s = 0; s < HSM_BENCH_SET_COUNT; s++)
        for (int i = 0; i < HSM_BENCH_COUNT; i++)
            actual[s][i] = HSM_BENCH_FAIL_SENTINEL;
    bool optiga_present = false;

    if (ok && s_hsm_cmd_resp.data_len >= HSM_BENCH_TOTAL_LEN) {
        for (int s = 0; s < HSM_BENCH_SET_COUNT; s++) {
            for (int i = 0; i < HSM_BENCH_COUNT; i++) {
                int off = HSM_BENCH_SET_OFF(s) + i * 4;
                actual[s][i] =
                    (uint32_t)(uint8_t)s_hsm_cmd_resp.data[off] |
                    ((uint32_t)(uint8_t)s_hsm_cmd_resp.data[off + 1] << 8) |
                    ((uint32_t)(uint8_t)s_hsm_cmd_resp.data[off + 2] << 16) |
                    ((uint32_t)(uint8_t)s_hsm_cmd_resp.data[off + 3] << 24);
            }
        }
        optiga_present = (s_hsm_cmd_resp.data[HSM_BENCH_PRESENT_OFF] == 1);
    }

    /* Remove status message */
    lv_obj_delete(status_lbl);

    /* Benchmark table: Operation | Typical | OPTIGA | MCU (HW) | MCU (SW) */
    static const struct { const char *op; uint16_t typical_ms; } rows[] = {
        { "Random (32 B)",      15  },
        { "SHA-256 (256 B)",    30  },
        { "ECC P-256 Key Gen",  80  },
        { "ECDSA Sign",         70  },
        { "ECDSA Verify",       80  },
    };
    int n = sizeof(rows) / sizeof(rows[0]);

    /* Column positions (fixed X for alignment) */
    #define BENCH_COL_OP     UI_SPACE_MD
    #define BENCH_COL_TYP    230
    #define BENCH_COL_OPTIGA 360
    #define BENCH_COL_HW     510
    #define BENCH_COL_SW     650

    /* Column headers */
    {
        static const struct { const char *txt; int x; } hdrs[] = {
            { "Operation", BENCH_COL_OP     },
            { "Typical",   BENCH_COL_TYP    },
            { "OPTIGA",    BENCH_COL_OPTIGA },
            { "MCU (HW)",  BENCH_COL_HW     },
            { "MCU (SW)",  BENCH_COL_SW     },
        };
        for (int i = 0; i < 5; i++) {
            lv_obj_t *h = lv_label_create(ov);
            lv_label_set_text(h, hdrs[i].txt);
            lv_obj_set_style_text_font(h, UI_FONT_CAPTION, 0);
            lv_obj_set_style_text_color(h,
                lv_color_hex(UI_COLOR_TEXT_SECONDARY), 0);
            lv_obj_set_pos(h, hdrs[i].x, 100);
        }
    }

    for (int i = 0; i < n; i++) {
        int y = 130 + i * 36;
        char buf[32];

        /* Operation name */
        lv_obj_t *op_lbl = lv_label_create(ov);
        lv_label_set_text(op_lbl, rows[i].op);
        lv_obj_set_style_text_font(op_lbl, UI_FONT_CAPTION, 0);
        lv_obj_set_style_text_color(op_lbl,
            lv_color_hex(UI_COLOR_TEXT_PRIMARY), 0);
        lv_obj_set_pos(op_lbl, BENCH_COL_OP, y);

        /* Typical time (Infineon datasheet, OPTIGA reference) */
        snprintf(buf, sizeof(buf), "%u ms", rows[i].typical_ms);
        lv_obj_t *typ_lbl = lv_label_create(ov);
        lv_label_set_text(typ_lbl, buf);
        lv_obj_set_style_text_font(typ_lbl, UI_FONT_CAPTION, 0);
        lv_obj_set_style_text_color(typ_lbl,
            lv_color_hex(UI_COLOR_TEXT_SECONDARY), 0);
        lv_obj_set_pos(typ_lbl, BENCH_COL_TYP, y);

        /* Measured columns: OPTIGA / MCU HW / MCU SW */
        static const int col_x[HSM_BENCH_SET_COUNT] = {
            BENCH_COL_OPTIGA, BENCH_COL_HW, BENCH_COL_SW
        };
        for (int s = 0; s < HSM_BENCH_SET_COUNT; s++) {
            uint32_t us = actual[s][i];
            lv_obj_t *lbl = lv_label_create(ov);
            uint32_t color;
            if (us == HSM_BENCH_FAIL_SENTINEL) {
                lv_label_set_text(lbl, "---");
                color = UI_COLOR_TEXT_DISABLED;
            } else {
                if (us < 1000)
                    snprintf(buf, sizeof(buf), "%lu us", (unsigned long)us);
                else if (us < 100000)
                    snprintf(buf, sizeof(buf), "%lu.%lu ms",
                             (unsigned long)(us / 1000),
                             (unsigned long)((us % 1000) / 100));
                else
                    snprintf(buf, sizeof(buf), "%lu ms",
                             (unsigned long)(us / 1000));
                lv_label_set_text(lbl, buf);
                if (s == HSM_BENCH_SET_OPTIGA)
                    color = (us <= (uint32_t)rows[i].typical_ms * 2000u)
                            ? HSM_COLOR_OK : HSM_COLOR_WARN;
                else
                    color = UI_COLOR_TEXT_PRIMARY;
            }
            lv_obj_set_style_text_font(lbl, UI_FONT_CAPTION, 0);
            lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
            lv_obj_set_pos(lbl, col_x[s], y);
        }
    }

    /* Summary note */
    lv_obj_t *note = lv_label_create(ov);
    uint32_t note_color = UI_COLOR_TEXT_DISABLED;
    if (!ok) {
        lv_label_set_text(note, "Benchmark failed - no response from CM33.");
        note_color = HSM_COLOR_CRIT;
    } else if (!optiga_present) {
        lv_label_set_text(note,
            "OPTIGA not detected - secure element results unavailable. "
            "MCU (HW) = PSoC Edge MXCRYPTO, MCU (SW) = software crypto. "
            "Typical = Infineon datasheet (SLS 32AIA).");
        note_color = HSM_COLOR_WARN;
    } else {
        lv_label_set_text(note,
            "Typical = Infineon datasheet (SLS 32AIA). "
            "OPTIGA = secure element (keys never leave the chip). "
            "MCU (HW) = MXCRYPTO accelerator, MCU (SW) = software crypto "
            "(keys exposed in RAM).");
    }
    lv_obj_set_style_text_font(note, UI_FONT_CAPTION, 0);
    lv_obj_set_style_text_color(note, lv_color_hex(note_color), 0);
    lv_obj_set_pos(note, UI_SPACE_MD, 130 + n * 36 + 10);
    lv_obj_set_width(note, UI_SCREEN_W - 2 * UI_SPACE_MD);
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
}

/*******************************************************************************
 * Read all OPTIGA data via IPC (CM55 → CM33_NS → OPTIGA Trust M)
 *
 * Sends IPC_CMD_HSM_REQUEST to CM33_NS, which reads the OPTIGA chip
 * and returns all data in a single ipc_response_t (41 bytes).
 * Falls back to placeholder values on IPC timeout.
 *******************************************************************************/
static void read_optiga_data(void)
{
    /* Send IPC_CMD_HSM_REQUEST to CM33_NS.
     * Timeout reduced from 10s to 5s — optiga_open() now fails within ~2s
     * when chip is absent, so 5s is generous. */
    bool ok = hsm_ipc_send(IPC_CMD_HSM_REQUEST, NULL, 0, 0, 5000);

    if (!ok) goto fallback;

    /* ── Parse response data ─────────────────────────────────────────── */
    s_ctx.optiga_ok = true;

    /* UID -> hex string */
    {
        const uint8_t *uid = s_hsm_cmd_resp.data + HSM_RESP_UID_OFF;
        char *p = s_uid_str;
        for (int i = 0; i < 27; i++) {
            p += snprintf(p, sizeof(s_uid_str) - (size_t)(p - s_uid_str),
                          "%02X", uid[i]);
        }
        /* FW version from UID bytes 25-26 */
        snprintf(s_fw_str, sizeof(s_fw_str), "v%d.%02d.%04X",
                 uid[25] >> 4, uid[25] & 0x0F,
                 (uid[23] << 8) | uid[24]);
    }

    /* LCS */
    {
        uint8_t lcs = s_hsm_cmd_resp.data[HSM_RESP_LCS_OFF];
        switch (lcs) {
            case 0x01: snprintf(s_lcs_str, sizeof(s_lcs_str), "Creation"); break;
            case 0x03: snprintf(s_lcs_str, sizeof(s_lcs_str), "Initialization"); break;
            case 0x07: snprintf(s_lcs_str, sizeof(s_lcs_str), "Operational"); break;
            case 0x0F: snprintf(s_lcs_str, sizeof(s_lcs_str), "Terminated"); break;
            default:   snprintf(s_lcs_str, sizeof(s_lcs_str), "0x%02X", lcs); break;
        }
    }

    /* Counters (big-endian -> uint32) */
    {
        const uint8_t *c0 = s_hsm_cmd_resp.data + HSM_RESP_CTR0_OFF;
        const uint8_t *c1 = s_hsm_cmd_resp.data + HSM_RESP_CTR1_OFF;
        uint32_t ctr0 = ((uint32_t)c0[0] << 24) | ((uint32_t)c0[1] << 16) |
                         ((uint32_t)c0[2] << 8) | c0[3];
        uint32_t ctr1 = ((uint32_t)c1[0] << 24) | ((uint32_t)c1[1] << 16) |
                         ((uint32_t)c1[2] << 8) | c1[3];
        snprintf(s_ctr0_str, sizeof(s_ctr0_str), "%lu", (unsigned long)ctr0);
        snprintf(s_ctr1_str, sizeof(s_ctr1_str), "%lu", (unsigned long)ctr1);
    }

    /* Certificate presence */
    for (int i = 0; i < 4; i++) {
        snprintf(s_cert_size[i], sizeof(s_cert_size[i]),
                 s_hsm_cmd_resp.data[HSM_RESP_CERT_OFF + i] ? "Present" : "Empty");
    }
    return;

fallback:
    /* Placeholder when IPC fails */
    snprintf(s_uid_str, sizeof(s_uid_str),
             /* obviously-fake: a real-format UID here once shipped in every
              * package and read as a live chip identity */
             "CD16xxxxxxxxxxxxxxxxxxxxxxxx"
             "xxxxxxxxxxxxxxxxxxxxxxxxxx");
    snprintf(s_fw_str, sizeof(s_fw_str), "v2.15.2801");
    snprintf(s_lcs_str, sizeof(s_lcs_str), "Operational");
    snprintf(s_ctr0_str, sizeof(s_ctr0_str), "18");
    snprintf(s_ctr1_str, sizeof(s_ctr1_str), "0");
    for (int i = 0; i < 4; i++) {
        snprintf(s_cert_size[i], sizeof(s_cert_size[i]),
                 i < 3 ? "Present" : "Empty");
    }
    s_ctx.optiga_ok = false;
}

/*******************************************************************************
 * UI Helpers
 *******************************************************************************/
static lv_obj_t *create_status_dot(lv_obj_t *parent, uint32_t color)
{
    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_set_size(dot, 10, 10);
    lv_obj_set_style_bg_color(dot, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    return dot;
}


static lv_obj_t *create_section_title(lv_obj_t *parent, const char *icon_utf8,
                                       const char *text, uint32_t color)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), 26);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, UI_SPACE_SM, 0);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *icon_lbl = lv_label_create(row);
    lv_label_set_text(icon_lbl, icon_utf8);
    lv_obj_set_style_text_font(icon_lbl, &font_hsm_icons_28, 0);
    lv_obj_set_style_text_color(icon_lbl, lv_color_hex(color), 0);

    lv_obj_t *title = lv_label_create(row);
    lv_label_set_text(title, text);
    lv_obj_set_style_text_font(title, UI_FONT_H3, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(color), 0);

    return row;
}

static void create_info_row(lv_obj_t *parent, const char *label,
                             const char *value, lv_obj_t **out_val_lbl,
                             int label_w, int row_h)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), row_h);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, UI_FONT_CAPTION, 0);
    lv_obj_set_style_text_color(lbl,
                                lv_color_hex(UI_COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_pos(lbl, 0, 0);

    lv_obj_t *val = lv_label_create(row);
    lv_label_set_text(val, value);
    lv_obj_set_style_text_font(val, UI_FONT_CAPTION, 0);
    lv_obj_set_style_text_color(val,
                                lv_color_hex(UI_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_pos(val, label_w, 0);
    lv_obj_set_width(val, HSM_COL_W - label_w - UI_CARD_PAD * 2 - 10);
    lv_label_set_long_mode(val, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(val, 4, 0);

    if (out_val_lbl) *out_val_lbl = val;
}

static lv_obj_t *create_action_btn(lv_obj_t *parent, const char *text,
                                    uint32_t bg_color)
{
    /* 110x54, dark ink on the accent.
     *
     * Two measured problems, both of which these buttons had since they were
     * written and both of which got worse when two more were added.
     *
     * Contrast: white text on these fills is 1.82:1 on the green, 2.16:1 on
     * the orange and 2.65:1 on the purple, against WCAG 2.2's 4.5:1 for normal
     * text. Nothing here passed. The same fills carry dark ink at 5.68:1 to
     * 10.38:1, so the palette was right and the ink was wrong.
     *
     * Size: 36 px on this panel is 4.2 mm, below WCAG 2.2's 24 CSS px floor
     * (6.35 mm = 54 px) and well below the 9 mm that Material, the W3C mobile
     * note and Parhi et al. all land on. 54 px is the floor, not the target -
     * the header strip cannot give more without eating the content area, and
     * WCAG's spacing exception then requires 54 px centre to centre, which the
     * 12 px gap below provides. */
    lv_obj_t *btn = lv_btn_create(parent);
    /*
     * Width follows the label; only the height is fixed.
     *
     * These were 110 px wide with UI_FONT_CAPTION. Moving to UI_FONT_H3 for
     * legibility kept the width, so the two longest labels — "Device Cert" and
     * "Benchmark" — ran outside their own buttons. A fixed width only works
     * while nobody changes the font or adds a longer label, and both happened.
     * LV_SIZE_CONTENT plus symmetric padding cannot overflow whatever the
     * label says; the row is LV_SIZE_CONTENT too, so it absorbs the change.
     * 110 px stays the minimum so short labels keep a touch target.
     */
    lv_obj_set_size(btn, LV_SIZE_CONTENT, 54);
    lv_obj_set_style_min_width(btn, 110, 0);
    lv_obj_set_style_pad_hor(btn, 14, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg_color), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, UI_RADIUS_SM, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, UI_FONT_H3, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x141014), 0);
    lv_obj_center(lbl);
    return btn;
}

/*******************************************************************************
 * Card Builders
 *******************************************************************************/

static lv_obj_t *create_chip_identity_card(lv_obj_t *parent)
{
    lv_obj_t *card = tesaiot_card_create(parent, HSM_COL_W, HSM_CARD_H_CHIP,
                                          NULL, HSM_COLOR_CHIP, NULL);
    lv_obj_t *col = tesaiot_col_create(card, UI_SPACE_XS);

    create_section_title(col, HSM_ICON_LOCK, "Chip Identity", HSM_COLOR_CHIP);
    create_info_row(col, "Type:", "OPTIGA Trust M V3",
                    &s_ctx.chip_name_lbl, 44, 20);
    create_info_row(col, "FW:", s_fw_str,
                    &s_ctx.chip_fw_lbl, 44, 20);

    /* UID label (full width, prominent, wrapped) */
    lv_obj_t *uid_hdr = lv_label_create(col);
    lv_label_set_text(uid_hdr, "Trust M UID:");
    lv_obj_set_style_text_font(uid_hdr, UI_FONT_CAPTION, 0);
    lv_obj_set_style_text_color(uid_hdr,
                                lv_color_hex(UI_COLOR_TEXT_SECONDARY), 0);

    s_ctx.chip_uid_lbl = lv_label_create(col);
    lv_label_set_text(s_ctx.chip_uid_lbl, s_uid_str);
    lv_obj_set_width(s_ctx.chip_uid_lbl, HSM_COL_W - UI_CARD_PAD * 2);
    lv_label_set_long_mode(s_ctx.chip_uid_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_ctx.chip_uid_lbl, UI_FONT_CAPTION, 0);
    lv_obj_set_style_text_color(s_ctx.chip_uid_lbl,
                                lv_color_hex(HSM_COLOR_CHIP), 0);
    lv_obj_set_style_text_line_space(s_ctx.chip_uid_lbl, 4, 0);

    lv_obj_t *i2c_lbl = lv_label_create(col);
    lv_label_set_text(i2c_lbl, "I2C: SCB5 (0x30)  |  3.3V");
    lv_obj_set_style_text_font(i2c_lbl, UI_FONT_CAPTION, 0);
    lv_obj_set_style_text_color(i2c_lbl,
                                lv_color_hex(UI_COLOR_TEXT_DISABLED), 0);
    return card;
}

static lv_obj_t *create_certificates_card(lv_obj_t *parent)
{
    lv_obj_t *card = tesaiot_card_create(parent, HSM_COL_W, HSM_CARD_H_CERT,
                                          NULL, HSM_COLOR_CERT, NULL);
    lv_obj_t *col = tesaiot_col_create(card, UI_SPACE_XS);

    create_section_title(col, HSM_ICON_KEY, "X.509 Certificates",
                         HSM_COLOR_CERT);

    static const char * const slot_names[] = {
        "0xE0E0", "0xE0E1", "0xE0E2", "0xE0E3"
    };
    static const char * const slot_desc_fallback[] = {
        "Present", "Present", "Present", "Empty"
    };

    /* 2x2 grid: use flex-row-wrap with 50% width items */
    lv_obj_t *grid = lv_obj_create(col);
    lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_row(grid, 2, 0);
    lv_obj_set_style_pad_column(grid, UI_SPACE_SM, 0);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);

    int half_w = (HSM_COL_W - UI_CARD_PAD * 2 - UI_SPACE_SM) / 2;
    for (int i = 0; i < 4; i++) {
        const char *desc = s_cert_size[i][0] ? s_cert_size[i] : slot_desc_fallback[i];
        uint32_t color = (desc[0] == 'P') ? HSM_COLOR_OK : HSM_COLOR_WARN;
        lv_obj_t *cell = lv_obj_create(grid);
        lv_obj_set_size(cell, half_w, 22);
        lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(cell, 0, 0);
        lv_obj_set_style_pad_all(cell, 0, 0);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(cell, 4, 0);

        create_status_dot(cell, color);

        lv_obj_t *nm = lv_label_create(cell);
        lv_label_set_text(nm, slot_names[i]);
        lv_obj_set_style_text_font(nm, UI_FONT_CAPTION, 0);
        lv_obj_set_style_text_color(nm, lv_color_hex(UI_COLOR_TEXT_SECONDARY), 0);

        s_ctx.cert_slot_lbls[i] = lv_label_create(cell);
        lv_label_set_text(s_ctx.cert_slot_lbls[i], desc);
        lv_obj_set_style_text_font(s_ctx.cert_slot_lbls[i], UI_FONT_CAPTION, 0);
        lv_obj_set_style_text_color(s_ctx.cert_slot_lbls[i],
                                    lv_color_hex(UI_COLOR_TEXT_PRIMARY), 0);
    }
    return card;
}

static lv_obj_t *create_key_store_card(lv_obj_t *parent)
{
    lv_obj_t *card = tesaiot_card_create(parent, HSM_COL_W, HSM_CARD_H_KEY,
                                          NULL, HSM_COLOR_KEY, NULL);
    lv_obj_t *col = tesaiot_col_create(card, UI_SPACE_XS);

    create_section_title(col, HSM_ICON_USER_SECRET, "Key Store",
                         HSM_COLOR_KEY);

    static const char * const slot_names[] = {
        "0xE0F0", "0xE0F1", "0xE0F2", "0xE0F3"
    };
    static const char * const slot_desc[] = {
        "IFX RSA",
        "Dev ECC-256",
        "Proj ECC-256",
        "User Slot"
    };
    static const uint32_t slot_colors[] = {
        HSM_COLOR_CRIT, HSM_COLOR_OK, HSM_COLOR_OK, HSM_COLOR_WARN
    };

    /* 2x2 grid */
    lv_obj_t *grid = lv_obj_create(col);
    lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_row(grid, 2, 0);
    lv_obj_set_style_pad_column(grid, UI_SPACE_SM, 0);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);

    int half_w = (HSM_COL_W - UI_CARD_PAD * 2 - UI_SPACE_SM) / 2;
    for (int i = 0; i < 4; i++) {
        lv_obj_t *cell = lv_obj_create(grid);
        lv_obj_set_size(cell, half_w, 22);
        lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(cell, 0, 0);
        lv_obj_set_style_pad_all(cell, 0, 0);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(cell, 4, 0);

        create_status_dot(cell, slot_colors[i]);

        lv_obj_t *nm = lv_label_create(cell);
        lv_label_set_text(nm, slot_names[i]);
        lv_obj_set_style_text_font(nm, UI_FONT_CAPTION, 0);
        lv_obj_set_style_text_color(nm, lv_color_hex(UI_COLOR_TEXT_SECONDARY), 0);

        s_ctx.key_slot_lbls[i] = lv_label_create(cell);
        lv_label_set_text(s_ctx.key_slot_lbls[i], slot_desc[i]);
        lv_obj_set_style_text_font(s_ctx.key_slot_lbls[i], UI_FONT_CAPTION, 0);
        lv_obj_set_style_text_color(s_ctx.key_slot_lbls[i],
                                    lv_color_hex(UI_COLOR_TEXT_PRIMARY), 0);
    }
    return card;
}

/* Health test results (populated by IPC at page create) */
static uint8_t s_health_results[HSM_HEALTH_TOTAL_LEN];
static int     s_health_pass_count;
static bool    s_health_tested;

static void run_health_check(void)
{
    memset(s_health_results, 0, sizeof(s_health_results));
    s_health_pass_count = 0;
    s_health_tested = false;

    bool ok = hsm_ipc_send(IPC_CMD_HSM_HEALTH, NULL, 0, 0, 60000);
    if (ok && s_hsm_cmd_resp.data_len >= HSM_HEALTH_TOTAL_LEN) {
        memcpy(s_health_results, (const void *)s_hsm_cmd_resp.data,
               HSM_HEALTH_TOTAL_LEN);
        for (int i = 0; i < HSM_HEALTH_TOTAL_LEN; i++)
            s_health_pass_count += s_health_results[i];
        s_health_tested = true;
    }
}

static lv_obj_t *create_health_card(lv_obj_t *parent)
{
    /* Determine status color based on health results */
    uint32_t status_color;
    const char *status_text;
    if (!s_ctx.optiga_ok) {
        lv_obj_add_state(s_ctx.btn_enrol, LV_STATE_DISABLED);
        lv_obj_set_style_opa(s_ctx.btn_enrol, LV_OPA_40, 0);
        lv_obj_add_state(s_ctx.btn_protect, LV_STATE_DISABLED);
        lv_obj_set_style_opa(s_ctx.btn_protect, LV_OPA_40, 0);
        status_color = HSM_COLOR_CRIT;
        status_text = "NOT CONNECTED";
    } else if (!s_health_tested) {
        status_color = HSM_COLOR_WARN;
        status_text = "CONNECTED";
    } else if (s_health_pass_count == 8) {
        status_color = HSM_COLOR_OK;
        status_text = "OPERATIONAL";
    } else if (s_health_pass_count >= 5) {
        status_color = HSM_COLOR_WARN;
        status_text = "DEGRADED";
    } else {
        status_color = HSM_COLOR_CRIT;
        status_text = "CRITICAL";
    }

    lv_obj_t *card = tesaiot_card_create(parent, HSM_COL_W, HSM_CARD_H_HEALTH,
                                          NULL, status_color, NULL);
    lv_obj_t *col = tesaiot_col_create(card, UI_SPACE_XS);

    create_section_title(col, HSM_ICON_LOCK, "Health Status", status_color);

    /* Status icon + text */
    lv_obj_t *sr = lv_obj_create(col);
    lv_obj_set_size(sr, LV_PCT(100), 28);
    lv_obj_set_style_bg_opa(sr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sr, 0, 0);
    lv_obj_set_style_pad_all(sr, 0, 0);
    lv_obj_clear_flag(sr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(sr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sr, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(sr, UI_SPACE_MD, 0);

    s_ctx.health_icon_lbl = lv_label_create(sr);
    lv_label_set_text(s_ctx.health_icon_lbl, HSM_ICON_LOCK);
    lv_obj_set_style_text_font(s_ctx.health_icon_lbl,
                                &font_hsm_icons_28, 0);
    lv_obj_set_style_text_color(s_ctx.health_icon_lbl,
                                lv_color_hex(status_color), 0);

    s_ctx.health_status_lbl = lv_label_create(sr);
    lv_label_set_text(s_ctx.health_status_lbl, status_text);
    lv_obj_set_style_text_font(s_ctx.health_status_lbl, UI_FONT_H2, 0);
    lv_obj_set_style_text_color(s_ctx.health_status_lbl,
                                lv_color_hex(status_color), 0);

    /* Health test results grid */
    if (s_health_tested) {
        static const char * const test_names[] = {
            "HW", "UID", "Cert", "RNG", "SHA", "ECC", "R/W", "Meta"
        };
        lv_obj_t *grid = lv_obj_create(col);
        lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(grid, 0, 0);
        lv_obj_set_style_pad_all(grid, 0, 0);
        lv_obj_set_style_pad_column(grid, 4, 0);
        lv_obj_set_style_pad_row(grid, 2, 0);
        lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);

        for (int i = 0; i < 8; i++) {
            bool pass = s_health_results[i];
            lv_obj_t *cell = lv_obj_create(grid);
            lv_obj_set_size(cell, 80, 18);
            lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(cell, 0, 0);
            lv_obj_set_style_pad_all(cell, 0, 0);
            lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_START,
                                  LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(cell, 4, 0);

            create_status_dot(cell, pass ? HSM_COLOR_OK : HSM_COLOR_CRIT);

            lv_obj_t *nm = lv_label_create(cell);
            lv_label_set_text(nm, test_names[i]);
            lv_obj_set_style_text_font(nm, UI_FONT_CAPTION, 0);
            lv_obj_set_style_text_color(nm,
                lv_color_hex(pass ? UI_COLOR_TEXT_PRIMARY : HSM_COLOR_CRIT), 0);
        }

        /* Pass count summary */
        lv_obj_t *summary = lv_label_create(col);
        char sbuf[32];
        snprintf(sbuf, sizeof(sbuf), "%d/8 tests passed", s_health_pass_count);
        lv_label_set_text(summary, sbuf);
        lv_obj_set_style_text_font(summary, UI_FONT_CAPTION, 0);
        lv_obj_set_style_text_color(summary,
            lv_color_hex(UI_COLOR_TEXT_DISABLED), 0);
    } else if (!s_ctx.optiga_ok) {
        /* OPTIGA not connected — show clear warning */
        s_ctx.health_detail_lbl = lv_label_create(col);
        lv_label_set_text(s_ctx.health_detail_lbl,
                          "OPTIGA Trust M not detected.\n"
                          "PIN verify will fail until the genuine\n"
                          "chip is re-attached (tamper demo).\n"
                          "Benchmark runs MCU crypto only.");
        lv_obj_set_width(s_ctx.health_detail_lbl, HSM_COL_W - UI_CARD_PAD * 2);
        lv_label_set_long_mode(s_ctx.health_detail_lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(s_ctx.health_detail_lbl, UI_FONT_CAPTION, 0);
        lv_obj_set_style_text_line_space(s_ctx.health_detail_lbl, 2, 0);
        lv_obj_set_style_text_color(s_ctx.health_detail_lbl,
                                    lv_color_hex(HSM_COLOR_CRIT), 0);
    } else {
        /* OPTIGA connected but health not tested — show basic info */
        s_ctx.health_detail_lbl = lv_label_create(col);
        lv_label_set_text_fmt(s_ctx.health_detail_lbl,
                              "LCS: %s\nCrypto: ECC/RSA/AES/SHA\nRNG: DRNG+TRNG",
                              s_lcs_str);
        lv_obj_set_width(s_ctx.health_detail_lbl, HSM_COL_W - UI_CARD_PAD * 2);
        lv_label_set_long_mode(s_ctx.health_detail_lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(s_ctx.health_detail_lbl, UI_FONT_CAPTION, 0);
        lv_obj_set_style_text_line_space(s_ctx.health_detail_lbl, 2, 0);
        lv_obj_set_style_text_color(s_ctx.health_detail_lbl,
                                    lv_color_hex(UI_COLOR_TEXT_DISABLED), 0);
    }
    return card;
}

static lv_obj_t *create_counters_card(lv_obj_t *parent)
{
    lv_obj_t *card = tesaiot_card_create(parent, HSM_COL_W, HSM_CARD_H_COUNTER,
                                          NULL, HSM_COLOR_COUNTER, NULL);
    lv_obj_t *col = tesaiot_col_create(card, UI_SPACE_XS);

    create_section_title(col, HSM_ICON_KEY, "Security Counters",
                         HSM_COLOR_COUNTER);

    /* Single row with 2 columns: Counter 0 | Counter 1 */
    lv_obj_t *row = lv_obj_create(col);
    lv_obj_set_size(row, LV_PCT(100), 24);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, UI_SPACE_LG, 0);

    /* Counter 0 */
    lv_obj_t *lbl0 = lv_label_create(row);
    lv_label_set_text_fmt(lbl0, "Counter 0:  %s", s_ctr0_str);
    lv_obj_set_style_text_font(lbl0, UI_FONT_CAPTION, 0);
    lv_obj_set_style_text_color(lbl0, lv_color_hex(UI_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_flex_grow(lbl0, 1);

    /* Counter 1 */
    lv_obj_t *lbl1 = lv_label_create(row);
    lv_label_set_text_fmt(lbl1, "Counter 1:  %s", s_ctr1_str);
    lv_obj_set_style_text_font(lbl1, UI_FONT_CAPTION, 0);
    lv_obj_set_style_text_color(lbl1, lv_color_hex(UI_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_flex_grow(lbl1, 1);

    s_ctx.counter_lbl = lbl0;  /* Keep reference for future IPC updates */
    return card;
}

//! [cm55_hsm_enrol_protect_open]
/* ...context: button event callbacks - GFX task context ... */
static void enrol_btn_clicked_cb(lv_event_t *e)   { (void)e; hsm_enrol_open(); }
static void protect_btn_clicked_cb(lv_event_t *e) { (void)e; hsm_protect_open(); }
//! [cm55_hsm_enrol_protect_open]

static void create_header_buttons(lv_obj_t *scr)
{
    /* Place action buttons on the screen at the same Y as "< Home" button.
     * Back button is at y = UI_STATUS_BAR_H + UI_SPACE_XS (=36), h=36.
     * We right-align our 3 buttons in that same row. */
    lv_obj_t *row = lv_obj_create(scr);
    lv_obj_set_size(row, LV_SIZE_CONTENT, 54);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    /* 12 px, the centre-to-centre distance WCAG 2.2's target-size spacing
     * exception needs beside a 54 px target. This landed on
     * create_section_title()'s icon row by mistake, which silently pushed the
     * title text of all five cards 6 px right and left the button row on the
     * 6 px it always had. */
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_align(row, LV_ALIGN_TOP_RIGHT, -UI_SPACE_MD,
                 UI_STATUS_BAR_H + UI_SPACE_XS);

    s_ctx.btn_pin   = create_action_btn(row, "PIN Code",    HSM_COLOR_BTN_PIN);
    s_ctx.btn_cert  = create_action_btn(row, "Device Cert", HSM_COLOR_BTN_CERT);
    s_ctx.btn_bench = create_action_btn(row, "Benchmark",   HSM_COLOR_BTN_BENCH);

    lv_obj_add_event_cb(s_ctx.btn_pin, pin_btn_clicked_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_ctx.btn_cert, cert_btn_clicked_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_ctx.btn_bench, bench_btn_clicked_cb, LV_EVENT_CLICKED, NULL);

    /* Enrolment and Protected Update drive the secure element and the network,
     * so they are meaningless without a chip to drive. */
    s_ctx.btn_enrol   = create_action_btn(row, "Enrol",   HSM_COLOR_BTN_ENROL);
    s_ctx.btn_protect = create_action_btn(row, "Protect", HSM_COLOR_BTN_PROTECT);
    lv_obj_add_event_cb(s_ctx.btn_enrol,   enrol_btn_clicked_cb,   LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_ctx.btn_protect, protect_btn_clicked_cb, LV_EVENT_CLICKED, NULL);

    /* When OPTIGA is not connected only Device Cert is unusable.
     * PIN stays active for the tamper demo (verify fails without the chip)
     * and Benchmark still runs its MCU crypto sets. */
    if (!s_ctx.optiga_ok) {
        lv_obj_add_state(s_ctx.btn_cert, LV_STATE_DISABLED);
        lv_obj_set_style_opa(s_ctx.btn_cert, LV_OPA_40, 0);
    }
}

/*******************************************************************************
 * page_hsm_create
 *******************************************************************************/
lv_obj_t *page_hsm_create(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));

    /* Read OPTIGA data before building UI */
    read_optiga_data();

    /* Run 8 health self-tests via IPC (only if OPTIGA responded) */
    if (s_ctx.optiga_ok)
        run_health_check();

    lv_obj_t *scr = lv_obj_create(NULL);
    page_manager_t *pm = pm_get_instance();

    /* No page title — the cards speak for themselves */
    lv_obj_t *content = pm_create_page_with_header(scr, pm, "", HSM_COLOR_TITLE);

    /* Action buttons in header row (same Y as "< Home") */
    create_header_buttons(scr);

    /* Single row container holding two columns, positioned at top of content */
    lv_obj_t *main_row = lv_obj_create(content);
    lv_obj_set_pos(main_row, 0, 0);
    lv_obj_set_size(main_row, UI_SCREEN_W, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(main_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(main_row, 0, 0);
    /* The action row above is 54 px tall from y 36, so it reaches y 90 while
     * the content area starts at y 72. It is created after `content` and is a
     * sibling, so it draws on top and covered the top 12 px — border and
     * rounded corners — of the Chip Identity and Certificates cards. The
     * buttons cannot shrink back to 36 px without going under the touch
     * target size these screens were rebuilt for, so the cards start lower. */
    lv_obj_set_style_pad_all(main_row, UI_SPACE_SM, 0);
    lv_obj_set_style_pad_top(main_row, UI_SPACE_SM + 18, 0);
    lv_obj_clear_flag(main_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(main_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(main_row, UI_SPACE_MD, 0);

    /* Left column: Chip Identity + Security Counters */
    lv_obj_t *left_col = tesaiot_col_create(main_row, UI_SPACE_SM);
    lv_obj_set_width(left_col, HSM_COL_W);

    create_chip_identity_card(left_col);
    create_counters_card(left_col);

    /* Right column: Certificates + Key Store + Health */
    lv_obj_t *right_col = tesaiot_col_create(main_row, UI_SPACE_SM);
    lv_obj_set_width(right_col, HSM_COL_W);

    create_certificates_card(right_col);
    create_key_store_card(right_col);
    create_health_card(right_col);

    return scr;
}

/*******************************************************************************
 * page_hsm_render
 *******************************************************************************/
void page_hsm_render(sensorhub_snapshot_t *snap)
{
    (void)snap;
    /* Static page — no periodic updates needed.
     * All data read once at create time. */
}

/*******************************************************************************
 * page_hsm_destroy
 *******************************************************************************/
//! [cm55_hsm_provision_ui_teardown]
void page_hsm_destroy(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_cert_overlay = NULL;
    s_bench_overlay = NULL;

    /* The screen (and every PIN widget under it) is being deleted by the
     * page manager — cancel the pending one-shot timer and drop all widget
     * pointers so a late callback cannot touch freed LVGL objects. */
    if (s_pin.timer) {
        lv_timer_delete(s_pin.timer);
        s_pin.timer = NULL;
    }
    s_pin.overlay = NULL;
    s_pin.icon_lbl = NULL;
    s_pin.title_lbl = NULL;
    s_pin.msg_lbl = NULL;
    for (int i = 0; i < PIN_LENGTH; i++) s_pin.dots[i] = NULL;
    /* Any provisioning overlay and its poll timer go with the page. A timer
     * that fires after these widgets are freed dereferences them. */
    hsm_provision_ui_teardown();

//! [cm55_hsm_provision_ui_teardown]
}
