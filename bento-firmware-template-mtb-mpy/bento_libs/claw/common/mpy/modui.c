/*******************************************************************************
 * File Name: modui.c
 *
 * Description: MicroPython 'ui' module for CM33_NS.
 *              Creates/modifies/deletes LVGL widgets on CM55 display
 *              via IPC Pipe commands.
 *
 *              API:
 *                import ui
 *                btn = ui.Button("Click Me", x=50, y=100)
 *                lbl = ui.Label("Hello", color=0x00FF00)
 *                slider = ui.Slider(min=0, max=100, value=50)
 *                btn.text("New")
 *                btn.delete()
 *
 *                tv  = ui.Tabview(x=20, y=20, w=400, h=300)
 *                live = tv.add_tab("Live")       # -> a Widget to build inside
 *                ui.Label("23.7 C", x=10, y=10, parent=live)
 *
 *                t = ui.Table(cols=2, rows=3, x=20, y=20)
 *                t.add_row("Sensor", "Reading")
 *                t.add_row("Temp", "23.7 C")
 *
 *                m    = ui.Menu(x=20, y=20, w=380, h=260)
 *                root = m.add_page("Settings")
 *                wifi = m.add_page("Wi-Fi")
 *                root.section().row("Wi-Fi").opens(wifi)
 *                events = ui.poll()
 *                ui.clear()
 *
 *              BARE-METAL: No FreeRTOS. Direct Cy_IPC_Pipe_SendMessage().
 *              Bidirectional: Uses CY_SECTION_SHAREDMEM response buffer.
 *
 *******************************************************************************/

#include "py/runtime.h"
#include "py/obj.h"
#include "py/objlist.h"
#include "py/builtin.h"
#include "py/stream.h"
#include "ipc_communication.h"
#include "ipc_ui_protocol.h"
#include "ui_widget_defaults.h"
#include "tacp.h"
#include "sensor_auto_task.h"
#include "py/mphal.h"
#include <string.h>

/* This file emits opcode map v2 (0x64 SFX, 0x65/0x66 sprites). Compiling
 * against another tree's protocol header must fail here, not misroute
 * opcodes at runtime. */
_Static_assert(IPC_UI_OPMAP_VERSION == 2, "wrong tree's ipc_ui_protocol.h (opcode map mismatch)");

/* ui.sfx()/ui.tone() live behind the codec flag, but a board without a codec
 * may still need the API surface so existing course scripts keep running:
 * UI_SFX_API_STUB=1 keeps the functions and the SFX_ / WAVE_ constants defined.
 * The commands are still sent; a codec-less CM55 has no audio handler on its
 * ext chain, so they are ignored there. A board that defines neither flag
 * gets no ui.sfx/ui.tone symbols at all. */
#ifndef UI_SFX_API_STUB
#define UI_SFX_API_STUB 0
#endif
#if BSP_HAS_AUDIO_CODEC || UI_SFX_API_STUB
#define UI_SFX_API_PRESENT 1
#else
#define UI_SFX_API_PRESENT 0
#endif

/*******************************************************************************
 * IPC Buffers (shared memory)
 *******************************************************************************/
CY_SECTION_SHAREDMEM static ipc_msg_t ui_ipc_msg;
CY_SECTION_SHAREDMEM static ipc_response_t ui_ipc_resp;

static bool ui_ipc_initialized = false;
static bool ui_cm55_ready = false;
static bool ui_sensor_auto_paused = false; /* true while the sensor auto-task is held paused by this module */

#define UI_IPC_SEND_RETRIES    (100)
#define UI_IPC_RETRY_DELAY_US  (100)
#define UI_IPC_RESP_TIMEOUT_MS (2000)
/* 30 probes x (300 ms probe + 100 ms gap) = a 12 s window per round, two
 * rounds. The count went 12 -> 30 alongside the probe timeout going 2000 ->
 * 300 ms so that the total patience is unchanged while the granularity gets
 * finer: the call now returns as soon as CM55 answers instead of at the end of
 * whichever long wait it happened to be sitting in. */
#define UI_IPC_INIT_PROBES     (30)
#define UI_IPC_REPROBE_ROUNDS  (10)

/* Readiness probe timeout. Kept short and deliberately separate from the
 * command timeout: the first probe after a hard reset is guaranteed to fail
 * because CM55 needs over a second to bring the panel up, and waiting the full
 * command timeout for a foregone conclusion is dead time the user watches. */
#define UI_IPC_PROBE_TIMEOUT_MS (300)

/* Forward declarations */
static bool ui_ipc_send_bidir_to(uint32_t cmd, const void *data,
                                  size_t data_len, uint32_t timeout_ms);
static bool ui_ipc_send_bidir(uint32_t cmd, const void *data, size_t data_len);
static void ui_pause_sensor_auto(void);

static inline void ui_ipc_setup_once(void) {
    if (!ui_ipc_initialized) {
        cm33_ipc_communication_setup();
        ui_ipc_initialized = true;
    }
}

/*******************************************************************************
 * IPC Init (lazy) — probes CM55 until ready (fast-fail window)
 *
 * Tracks two states separately:
 *   ui_ipc_initialized — IPC channel set up (cm33_ipc_communication_setup)
 *   ui_cm55_ready      — CM55 actually responded to a bidir probe
 *
 * If CM55 is not ready yet, this function retries only for a short window
 * to keep REPL/Run responsive. Callers can retry later; do not block for tens
 * of seconds on the CM33 side.
 *******************************************************************************/
static void ui_ipc_init(void) {
    ui_ipc_setup_once();
    /* Pause background auto-push before readiness probe.
     * Otherwise sensor IPC traffic can starve UI bidirectional responses
     * and keep ui.list()/Run in "CM55 not ready" loops after soft-reset. */
    ui_pause_sensor_auto();

    if (ui_cm55_ready) return;  /* already confirmed CM55 alive */

    /* Re-arm CM33 IPC routing before first probe in this session.
     * Soft-reset keeps C statics; proactively re-init pipe state to avoid
     * CY_IPC_PIPE_BUSY loops on the first Run after reset. */
    cm33_ipc_communication_recover();

    /* Probe CM55 with a BIDIRECTIONAL request (UI_LIST) and wait for a real
     * response. Fire-and-forget can succeed before CM55 callback is ready.
     * Keep probe window bounded and retry-driven; caller can retry later. */
    for (int probe = 0; probe < UI_IPC_INIT_PROBES; probe++) {
        if (ui_ipc_send_bidir_to(IPC_CMD_UI_LIST, NULL, 0,
                                  UI_IPC_PROBE_TIMEOUT_MS)) {
            ui_cm55_ready = true;
            return;   /* CM55 responded — ready! */
        }
        tacp_poll_uart();
        MICROPY_EVENT_POLL_HOOK;
        Cy_SysLib_Delay(100);
    }

    /* First probe window failed: force CM33 IPC re-init once, then retry.
     * This handles stale pipe state after rapid soft-reset / Run-REPL cycles
     * without needing external debugger reset. */
    cm33_ipc_communication_recover();
    for (int probe = 0; probe < UI_IPC_INIT_PROBES; probe++) {
        if (ui_ipc_send_bidir_to(IPC_CMD_UI_LIST, NULL, 0,
                                  UI_IPC_PROBE_TIMEOUT_MS)) {
            ui_cm55_ready = true;
            return;
        }
        tacp_poll_uart();
        MICROPY_EVENT_POLL_HOOK;
        Cy_SysLib_Delay(100);
    }

    /* CM55 did not respond in probe window — fail fast so caller can retry. */
    mp_raise_msg(&mp_type_RuntimeError,
                 MP_ERROR_TEXT("ui: CM55 not ready (retry shortly)"));
}

/*******************************************************************************
 * ui_ipc_send_fire_forget() — Send command, no response expected
 *******************************************************************************/
static bool ui_ipc_send_fire_forget(uint32_t cmd, const void *data,
                                     size_t data_len)
{
    cy_en_ipc_pipe_status_t status;
    int retries = UI_IPC_SEND_RETRIES;

    memset(&ui_ipc_msg, 0, sizeof(ui_ipc_msg));
    ui_ipc_msg.client_id = CM55_IPC_UI_CLIENT_ID;
    ui_ipc_msg.intr_mask = CY_IPC_CYPIPE_INTR_MASK_EP1;
    ui_ipc_msg.cmd = cmd;
    ui_ipc_msg.value = 0;

    if (data && data_len > 0) {
        size_t copy = (data_len > IPC_DATA_MAX_LEN) ? IPC_DATA_MAX_LEN : data_len;
        memcpy(ui_ipc_msg.data, data, copy);
    }

    do {
        status = Cy_IPC_Pipe_SendMessage(
            CM55_IPC_PIPE_EP_ADDR, CM33_IPC_PIPE_EP_ADDR,
            (void *)&ui_ipc_msg, NULL);
        if (CY_IPC_PIPE_SUCCESS == status) return true;
        Cy_SysLib_DelayUs(UI_IPC_RETRY_DELAY_US);
    } while (--retries > 0);

    return false;
}

/*******************************************************************************
 * ui_ipc_send_bidir() — Send command with response expected
 *******************************************************************************/
static bool ui_ipc_send_bidir_to(uint32_t cmd, const void *data,
                                  size_t data_len, uint32_t timeout_ms)
{
    cy_en_ipc_pipe_status_t status;
    int retries = UI_IPC_SEND_RETRIES;

    memset(&ui_ipc_msg, 0, sizeof(ui_ipc_msg));
    memset(&ui_ipc_resp, 0, sizeof(ui_ipc_resp));

    ui_ipc_msg.client_id = CM55_IPC_UI_CLIENT_ID;
    ui_ipc_msg.intr_mask = CY_IPC_CYPIPE_INTR_MASK_EP1;
    ui_ipc_msg.cmd = cmd;
    ui_ipc_msg.value = (uint32_t)&ui_ipc_resp;  /* Response pointer */

    ui_ipc_resp.ready = 0;

    if (data && data_len > 0) {
        size_t copy = (data_len > IPC_DATA_MAX_LEN) ? IPC_DATA_MAX_LEN : data_len;
        memcpy(ui_ipc_msg.data, data, copy);
    }

    /* Send with retry */
    do {
        status = Cy_IPC_Pipe_SendMessage(
            CM55_IPC_PIPE_EP_ADDR, CM33_IPC_PIPE_EP_ADDR,
            (void *)&ui_ipc_msg, NULL);
        if (CY_IPC_PIPE_SUCCESS == status) break;
        Cy_SysLib_DelayUs(UI_IPC_RETRY_DELAY_US);
    } while (--retries > 0);

    if (CY_IPC_PIPE_SUCCESS != status) return false;

    /* Wait for the response against the clock, not against an iteration count.
     *
     * This loop used to count down UI_IPC_RESP_TIMEOUT_MS * 10 iterations on
     * the assumption that Cy_SysLib_DelayUs(100) set the period. It does not:
     * MICROPY_EVENT_POLL_HOOK expands to mp_hal_rtos_yield(), which ends in
     * vTaskDelay(1), and at configTICK_RATE_HZ = 1000 that blocks to the next
     * tick. One iteration is therefore ~1 ms, so the real bound was 20 s — ten
     * times the documented 2 s. Measured on an Eva Kit 2026-08-13: a script
     * that called ui.screen() at boot sat there for 20.3 s before the screen
     * showed anything, because CM55 drops UI messages until it has registered
     * its client and the first probe simply ate the whole wrong timeout.
     * A deadline in milliseconds cannot drift again when the loop body changes. */
    //! [mpy_tacp_poll_uart_pump]
    /* ...context: inside the UI IPC response wait loop ... */
    mp_uint_t deadline = mp_hal_ticks_ms() + timeout_ms;
    while (!ui_ipc_resp.ready) {
        if ((mp_int_t)(mp_hal_ticks_ms() - deadline) >= 0) break;
        tacp_poll_uart();
        MICROPY_EVENT_POLL_HOOK;
    }
    //! [mpy_tacp_poll_uart_pump]

    return (ui_ipc_resp.ready == 1);
}

static bool ui_ipc_send_bidir(uint32_t cmd, const void *data, size_t data_len)
{
    return ui_ipc_send_bidir_to(cmd, data, data_len, UI_IPC_RESP_TIMEOUT_MS);
}

/*******************************************************************************
 * ui_ipc_send_bidir_robust() — Bidir send with automatic re-probe on failure.
 *
 * After a MicroPython soft-reset (Ctrl-C), C statics persist, so
 * ui_cm55_ready stays true and ui_ipc_init() skips the probe.  The next
 * actual IPC command then fails immediately if CM55 is briefly settling.
 *
 * This wrapper detects the failure, resets ui_cm55_ready, re-probes CM55
 * (bounded rounds), and retries the original command once.
 * Use this for every real widget command; keep the probe loop as-is.
 *******************************************************************************/
static bool ui_ipc_send_bidir_robust(uint32_t cmd, const void *data,
                                      size_t data_len)
{
    if (ui_ipc_send_bidir(cmd, data, data_len)) return true;

    /* Probe, retry, and if the retry fails too, go round again.
     *
     * This used to probe once and retry the command once. When the retry also
     * failed the caller got RuntimeError: ui: IPC send failed -- and a student
     * pressing Run saw it at random, because CM55 can be answering probes while
     * still too busy to service the command behind them. Two of the first
     * forty-two examples in a hardware sweep died this way, on files with
     * nothing wrong with them.
     *
     * Three full rounds. If CM55 truly is not there, the caller still finds
     * out, just not on the first stumble. */
    for (int round = 0; round < 3; round++) {
        ui_cm55_ready = false;
        for (int i = 0; i < UI_IPC_REPROBE_ROUNDS; i++) {
            if (ui_ipc_send_bidir_to(IPC_CMD_UI_LIST, NULL, 0,
                                      UI_IPC_PROBE_TIMEOUT_MS)) {
                ui_cm55_ready = true;
                break;
            }
            tacp_poll_uart();
            MICROPY_EVENT_POLL_HOOK;
            Cy_SysLib_Delay(100);
        }
        if (!ui_cm55_ready) return false;

        if (ui_ipc_send_bidir(cmd, data, data_len)) return true;

        tacp_poll_uart();
        MICROPY_EVENT_POLL_HOOK;
        Cy_SysLib_Delay(100);
    }
    return false;
}

/*******************************************************************************
 * Widget Object Type
 *******************************************************************************/
typedef struct {
    mp_obj_base_t base;
    uint8_t handle;
    uint8_t widget_type;
    /* Where .add_row() writes next. Kept on the Python object rather than on
     * CM55: it is a convenience for filling a Table in order, not state the
     * display owns, and two objects naming the same handle should not share
     * a cursor they never agreed on. */
    uint8_t next_row;
} ui_widget_obj_t;

/* Forward declaration — MP_DEFINE_CONST_OBJ_TYPE defines it non-static */
extern const mp_obj_type_t ui_widget_type;

/*******************************************************************************
 * Widget Methods
 *******************************************************************************/

/*******************************************************************************
 * ui_utf8_fit() — ความยาวมากที่สุดที่ไม่เกิน cap และไม่ผ่ากลางตัวอักษร
 *
 * ภาษาไทยหนึ่งตัวอักษรกิน 3 ไบต์ การตัดด้วย memcpy ตรง ๆ จึงผ่ากลางตัวอักษรได้
 * แล้วไบต์ที่เหลือกลายเป็นเศษที่วาดไม่ออก วัดบนบอร์ด 14 ส.ค. 2026: ป้ายของ
 * examples/s02/02_find_by_name.py ขึ้นจอเป็น "...ไม่ใ" ค้างอยู่อย่างนั้น
 * ถอยกลับไปหาไบต์นำหน้าตัวล่าสุด แล้วตัดตรงนั้นแทน
 *******************************************************************************/
static size_t ui_utf8_fit(const char *s, size_t len, size_t cap) {
    if (len <= cap) {
        return len;
    }
    size_t n = cap;
    while (n > 0 && ((unsigned char)s[n] & 0xC0u) == 0x80u) {
        n--;                      /* ยังอยู่กลางตัวอักษร ถอยอีกหนึ่งไบต์ */
    }
    return n;
}

/* .text([str]) — set or get text */
static mp_obj_t widget_text(size_t n_args, const mp_obj_t *args) {
    ui_widget_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    ui_ipc_init();

    if (n_args == 1) {
        /* GET_TEXT — bidirectional. Works on Textarea, Label, Dropdown and
         * Roller; anything else answers UI_STATUS_INVALID_TYPE and gets "" so
         * that a screen-scraping loop cannot be brought down by one widget
         * that has no words in it.
         *
         * This used to return None with a TODO beside it, and that one line
         * was the whole reason the course had to teach that a password typed
         * on the board "can be read by a person but not by the program". */
        uint8_t buf[1] = { self->handle };
        if (ui_ipc_send_bidir_robust(IPC_CMD_UI_GET_TEXT, buf, 1)) {
            if (ui_ipc_resp.status == UI_STATUS_OK && ui_ipc_resp.data_len > 0) {
                const char *s = (const char *)ui_ipc_resp.data;
                /* CM55 sends strlen + 1. Trust the NUL, not the length, and
                 * stop at the buffer edge whichever arrives first — a length
                 * field is the one part of a response a wedged peer can get
                 * wrong while the memory behind it is still ours to read. */
                size_t max = (ui_ipc_resp.data_len < IPC_RESPONSE_DATA_MAX)
                             ? (size_t)ui_ipc_resp.data_len
                             : (size_t)IPC_RESPONSE_DATA_MAX;
                size_t slen = 0;
                while (slen < max && s[slen] != '\0') {
                    slen++;
                }
                return mp_obj_new_str(s, slen);
            }
        }
        return mp_obj_new_str("", 0);
    }

    size_t len;
    const char *text = mp_obj_str_get_data(args[1], &len);

    uint8_t buf[IPC_DATA_MAX_LEN];
    memset(buf, 0, sizeof(buf));
    buf[0] = self->handle;
    size_t copy = ui_utf8_fit(text, len, IPC_DATA_MAX_LEN - 2);
    memcpy(&buf[1], text, copy);
    buf[1 + copy] = '\0';

    ui_ipc_send_fire_forget(IPC_CMD_UI_SET_TEXT, buf, 2 + copy);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(widget_text_obj, 1, 2, widget_text);

/* .value([int]) — set or get numeric value */
static mp_obj_t widget_value(size_t n_args, const mp_obj_t *args) {
    ui_widget_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    ui_ipc_init();

    if (n_args == 1) {
        /* GET_VALUE — bidirectional */
        uint8_t buf[1] = { self->handle };
        if (ui_ipc_send_bidir_robust(IPC_CMD_UI_GET_VALUE, buf, 1)) {
            if (ui_ipc_resp.status == UI_STATUS_OK && ui_ipc_resp.data_len >= 4) {
                int32_t val;
                memcpy(&val, ui_ipc_resp.data, sizeof(int32_t));
                return mp_obj_new_int(val);
            }
        }
        return mp_obj_new_int(0);
    }

    int32_t value = mp_obj_get_int(args[1]);
    uint8_t buf[5];
    buf[0] = self->handle;
    memcpy(&buf[1], &value, sizeof(int32_t));

    ui_ipc_send_fire_forget(IPC_CMD_UI_SET_VALUE, buf, 5);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(widget_value_obj, 1, 2, widget_value);

/* .pos(x, y) */
static mp_obj_t widget_pos(mp_obj_t self_in, mp_obj_t x_obj, mp_obj_t y_obj) {
    ui_widget_obj_t *self = MP_OBJ_TO_PTR(self_in);
    ui_ipc_init();

    int16_t x = (int16_t)mp_obj_get_int(x_obj);
    int16_t y = (int16_t)mp_obj_get_int(y_obj);

    uint8_t buf[5];
    buf[0] = self->handle;
    memcpy(&buf[1], &x, sizeof(int16_t));
    memcpy(&buf[3], &y, sizeof(int16_t));

    ui_ipc_send_fire_forget(IPC_CMD_UI_SET_POSITION, buf, 5);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(widget_pos_obj, widget_pos);

/* .size(w, h) */
static mp_obj_t widget_size(mp_obj_t self_in, mp_obj_t w_obj, mp_obj_t h_obj) {
    ui_widget_obj_t *self = MP_OBJ_TO_PTR(self_in);
    ui_ipc_init();

    int16_t w = (int16_t)mp_obj_get_int(w_obj);
    int16_t h = (int16_t)mp_obj_get_int(h_obj);

    uint8_t buf[5];
    buf[0] = self->handle;
    memcpy(&buf[1], &w, sizeof(int16_t));
    memcpy(&buf[3], &h, sizeof(int16_t));

    ui_ipc_send_fire_forget(IPC_CMD_UI_SET_SIZE, buf, 5);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(widget_size_obj, widget_size);

/* .color(rgb) */
static mp_obj_t widget_color(mp_obj_t self_in, mp_obj_t color_obj) {
    ui_widget_obj_t *self = MP_OBJ_TO_PTR(self_in);
    ui_ipc_init();

    uint32_t color = (uint32_t)mp_obj_get_int(color_obj);

    uint8_t buf[5];
    buf[0] = self->handle;
    memcpy(&buf[1], &color, sizeof(uint32_t));

    ui_ipc_send_fire_forget(IPC_CMD_UI_SET_COLOR, buf, 5);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(widget_color_obj, widget_color);

/* .show() */
static mp_obj_t widget_show(mp_obj_t self_in) {
    ui_widget_obj_t *self = MP_OBJ_TO_PTR(self_in);
    ui_ipc_init();
    uint8_t buf[2] = { self->handle, 1 };
    ui_ipc_send_fire_forget(IPC_CMD_UI_SET_VISIBLE, buf, 2);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(widget_show_obj, widget_show);

/* .hide() */
static mp_obj_t widget_hide(mp_obj_t self_in) {
    ui_widget_obj_t *self = MP_OBJ_TO_PTR(self_in);
    ui_ipc_init();
    uint8_t buf[2] = { self->handle, 0 };
    ui_ipc_send_fire_forget(IPC_CMD_UI_SET_VISIBLE, buf, 2);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(widget_hide_obj, widget_hide);

/* .delete() */
static mp_obj_t widget_delete(mp_obj_t self_in) {
    ui_widget_obj_t *self = MP_OBJ_TO_PTR(self_in);
    ui_ipc_init();
    uint8_t buf[1] = { self->handle };
    ui_ipc_send_fire_forget(IPC_CMD_UI_DELETE, buf, 1);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(widget_delete_obj, widget_delete);

/* .icon(name) — change built-in icon (Image widget only) */
static mp_obj_t widget_icon(mp_obj_t self_in, mp_obj_t name_obj) {
    ui_widget_obj_t *self = MP_OBJ_TO_PTR(self_in);
    ui_ipc_init();

    size_t len;
    const char *name = mp_obj_str_get_data(name_obj, &len);

    /* Send as SET_TEXT — CM55 side detects IMAGE type and re-renders icon */
    uint8_t buf[IPC_DATA_MAX_LEN];
    memset(buf, 0, sizeof(buf));
    buf[0] = self->handle;
    size_t copy = (len > IPC_DATA_MAX_LEN - 2) ? (IPC_DATA_MAX_LEN - 2) : len;
    memcpy(&buf[1], name, copy);
    buf[1 + copy] = '\0';

    ui_ipc_send_fire_forget(IPC_CMD_UI_SET_TEXT, buf, 2 + copy);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(widget_icon_obj, widget_icon);

/* .set_image(bytearray) — send RGB565 pixel data in chunks (Image widget only) */
static mp_obj_t widget_set_image(mp_obj_t self_in, mp_obj_t data_obj) {
    ui_widget_obj_t *self = MP_OBJ_TO_PTR(self_in);
    ui_ipc_init();

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(data_obj, &bufinfo, MP_BUFFER_READ);

    const uint8_t *src = (const uint8_t *)bufinfo.buf;
    size_t remaining = bufinfo.len;
    uint16_t offset = 0;

    /* Send in chunks: [handle(1)][offset_lo(1)][offset_hi(1)][len(1)][data(max 124)] */
    #define IMAGE_CHUNK_MAX (IPC_DATA_MAX_LEN - 4)

    while (remaining > 0) {
        uint8_t chunk_len = (remaining > IMAGE_CHUNK_MAX)
                            ? IMAGE_CHUNK_MAX : (uint8_t)remaining;

        uint8_t buf[IPC_DATA_MAX_LEN];
        buf[0] = self->handle;
        buf[1] = (uint8_t)(offset & 0xFF);
        buf[2] = (uint8_t)(offset >> 8);
        buf[3] = chunk_len;
        memcpy(&buf[4], src, chunk_len);

        ui_ipc_send_fire_forget(IPC_CMD_UI_SET_IMAGE, buf, 4 + chunk_len);

        src += chunk_len;
        offset += chunk_len;
        remaining -= chunk_len;

        /* Small delay between chunks to avoid flooding IPC queue */
        if (remaining > 0) {
            Cy_SysLib_DelayUs(200);
        }
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(widget_set_image_obj, widget_set_image);

/* .set_pixels(bytearray) — for DotMatrix only */
static mp_obj_t widget_set_pixels(mp_obj_t self_in, mp_obj_t data_obj) {
    ui_widget_obj_t *self = MP_OBJ_TO_PTR(self_in);
    ui_ipc_init();

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(data_obj, &bufinfo, MP_BUFFER_READ);

    uint8_t buf[IPC_DATA_MAX_LEN];
    buf[0] = self->handle;
    uint8_t len = (bufinfo.len > IPC_DATA_MAX_LEN - 2)
                  ? (IPC_DATA_MAX_LEN - 2) : (uint8_t)bufinfo.len;
    buf[1] = len;
    memcpy(&buf[2], bufinfo.buf, len);

    ui_ipc_send_fire_forget(IPC_CMD_UI_SET_DOTMATRIX, buf, 2 + len);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(widget_set_pixels_obj, widget_set_pixels);

/* .add_series(color) — Add a series to a Chart widget (bidirectional) */
static mp_obj_t widget_add_series(mp_obj_t self_in, mp_obj_t color_obj) {
    ui_widget_obj_t *self = MP_OBJ_TO_PTR(self_in);
    ui_ipc_init();

    uint32_t color = (uint32_t)mp_obj_get_int(color_obj);

    uint8_t buf[5];
    buf[0] = self->handle;
    memcpy(&buf[1], &color, sizeof(uint32_t));

    if (!ui_ipc_send_bidir_robust(IPC_CMD_UI_CHART_ADD_SERIES, buf, 5)) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("ui: IPC send failed"));
    }

    if (ui_ipc_resp.status != UI_STATUS_OK) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("ui: add_series failed (max 4)"));
    }

    return mp_obj_new_int(ui_ipc_resp.data[0]);
}
static MP_DEFINE_CONST_FUN_OBJ_2(widget_add_series_obj, widget_add_series);

/* .set_next(series_idx, value) — Set next value for a chart series */
static mp_obj_t widget_set_next(mp_obj_t self_in, mp_obj_t idx_obj,
                                 mp_obj_t val_obj) {
    ui_widget_obj_t *self = MP_OBJ_TO_PTR(self_in);
    ui_ipc_init();

    uint8_t series_idx = (uint8_t)mp_obj_get_int(idx_obj);
    int32_t value = mp_obj_get_int(val_obj);

    uint8_t buf[6];
    buf[0] = self->handle;
    buf[1] = series_idx;
    memcpy(&buf[2], &value, sizeof(int32_t));

    ui_ipc_send_fire_forget(IPC_CMD_UI_CHART_SET_NEXT, buf, 6);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(widget_set_next_obj, widget_set_next);

/* .id() — Return widget handle ID (for matching with ui.poll() events) */
static mp_obj_t widget_id(mp_obj_t self_in) {
    ui_widget_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_int(self->handle);
}
static MP_DEFINE_CONST_FUN_OBJ_1(widget_id_obj, widget_id);

#if ENABLE_GAME_SPRITES
/* widget.frame(sprite_id) — swap a sprite's image (directional head, enemy
 * kind, explosion frame). Fire-and-forget. Game-console boards only:
 * exposure is compile-gated so bentogame's hasattr(ui, "Sprite") probe
 * degrades to colored boxes on boards without a sprite registry. */
static mp_obj_t widget_frame(mp_obj_t self_in, mp_obj_t id_obj) {
    ui_widget_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint8_t buf[2] = { self->handle, (uint8_t)mp_obj_get_int(id_obj) };
    ui_ipc_send_fire_forget(IPC_CMD_UI_SPRITE_FRAME, buf, 2);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(widget_frame_obj, widget_frame);
#endif /* ENABLE_GAME_SPRITES */

/*******************************************************************************
 * Collections and containers — one ITEM_ADD per item.
 *
 * Fire-and-forget for the data widgets and bidirectional for the containers,
 * from the same opcode: CM55 answers only when a response pointer was sent
 * (write_response() no-ops on a zero address), so the container case costs the
 * data widgets nothing.
 *******************************************************************************/
static void widget_item_fill(ipc_ui_item_add_t *it, ui_widget_obj_t *self,
                             uint8_t a, uint8_t b, uint8_t flags,
                             mp_obj_t text_obj)
{
    memset(it, 0, sizeof(*it));
    it->handle = self->handle;
    it->a      = a;
    it->b      = b;
    it->flags  = flags;
    if (text_obj != mp_const_none) {
        size_t len;
        const char *text = mp_obj_str_get_data(text_obj, &len);
        size_t copy = ui_utf8_fit(text, len, UI_ITEM_TEXT_MAX - 1);
        memcpy(it->text, text, copy);
        it->text[copy] = '\0';
    }
}

static void widget_item_send(const ipc_ui_item_add_t *it)
{
    ui_ipc_init();
    ui_ipc_send_fire_forget(IPC_CMD_UI_ITEM_ADD, it, sizeof(*it));
}

/* Container add: the same opcode with a response pointer, so CM55 hands back
 * the child's handle and the caller gets a Widget to pass as parent=. */
static mp_obj_t widget_item_send_child(const ipc_ui_item_add_t *it)
{
    ui_ipc_init();
    if (!ui_ipc_send_bidir_robust(IPC_CMD_UI_ITEM_ADD, it, sizeof(*it))) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("ui: IPC send failed"));
    }
    if (ui_ipc_resp.status == UI_STATUS_TABLE_FULL) {
        mp_raise_msg(&mp_type_RuntimeError,
                     MP_ERROR_TEXT("ui: no free widget handles (max 64)"));
    }
    if (ui_ipc_resp.status != UI_STATUS_OK ||
        ui_ipc_resp.data[0] == UI_ITEM_NO_CHILD) {
        mp_raise_msg(&mp_type_RuntimeError,
                     MP_ERROR_TEXT("ui: this widget has no pages to add"));
    }
    ui_widget_obj_t *w = mp_obj_malloc(ui_widget_obj_t, &ui_widget_type);
    w->handle      = ui_ipc_resp.data[0];
    w->widget_type = UI_WIDGET_CONTAINER;
    w->next_row    = 0;
    return MP_OBJ_FROM_PTR(w);
}

/* .add_item(text, a=0, b=0, flags=0) — the general form of every append below.
 * The named helpers are this with the fields spelled out. */
static mp_obj_t widget_add_item(size_t n_args, const mp_obj_t *args) {
    ui_widget_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    ipc_ui_item_add_t it;
    widget_item_fill(&it, self,
                     (uint8_t)(n_args > 2 ? mp_obj_get_int(args[2]) : 0),
                     (uint8_t)(n_args > 3 ? mp_obj_get_int(args[3]) : 0),
                     (uint8_t)(n_args > 4 ? mp_obj_get_int(args[4]) : 0),
                     (n_args > 1) ? args[1] : mp_const_none);
    widget_item_send(&it);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(widget_add_item_obj, 1, 5, widget_add_item);

/* .cell(row, col, text) — Table */
static mp_obj_t widget_cell(size_t n_args, const mp_obj_t *args) {
    ui_widget_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    ipc_ui_item_add_t it;
    widget_item_fill(&it, self, (uint8_t)mp_obj_get_int(args[1]),
                     (uint8_t)mp_obj_get_int(args[2]), 0, args[3]);
    widget_item_send(&it);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(widget_cell_obj, 4, 4, widget_cell);

/* .add_row("a", "b", ...) — Table. Writes into the row after the last one this
 * object wrote, so a table fills in without anyone counting rows by hand. */
static mp_obj_t widget_add_row(size_t n_args, const mp_obj_t *args) {
    ui_widget_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    for (size_t i = 1; i < n_args; i++) {
        ipc_ui_item_add_t it;
        widget_item_fill(&it, self, self->next_row, (uint8_t)(i - 1), 0, args[i]);
        widget_item_send(&it);
    }
    if (self->next_row < 255) {
        self->next_row++;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR(widget_add_row_obj, 1, widget_add_row);

/* .add_option(text) — Roller and Dropdown. Both LVGL setters copy the option
 * string, so appending needs no storage on either side. */
static mp_obj_t widget_add_option(mp_obj_t self_in, mp_obj_t text_obj) {
    ui_widget_obj_t *self = MP_OBJ_TO_PTR(self_in);
    ipc_ui_item_add_t it;
    widget_item_fill(&it, self, 0, 0, 0, text_obj);
    widget_item_send(&it);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(widget_add_option_obj, widget_add_option);

/* .add_button(text, new_row=False, ctrl=0) — ButtonMatrix, and the footer of a
 * MsgBox (which has one row, so new_row is ignored there). */
static mp_obj_t widget_add_button(size_t n_args, const mp_obj_t *args) {
    ui_widget_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    bool new_row = (n_args > 2) && mp_obj_is_true(args[2]);
    ipc_ui_item_add_t it;
    widget_item_fill(&it, self, 0,
                     (uint8_t)(n_args > 3 ? mp_obj_get_int(args[3]) : 0),
                     new_row ? UI_ITEM_FLAG_NEW_ROW : 0, args[1]);
    widget_item_send(&it);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(widget_add_button_obj, 2, 4, widget_add_button);

/* .add_point(x, y) — Line.
 *
 * The point travels as the decimal text "x y" because a screen coordinate does
 * not fit the byte-wide item fields and this panel is 800 px wide. Formatting
 * it here keeps that entirely inside the implementation: the caller passes two
 * numbers, as they would to any other geometry call. */
static mp_obj_t widget_add_point(mp_obj_t self_in, mp_obj_t x_obj, mp_obj_t y_obj) {
    ui_widget_obj_t *self = MP_OBJ_TO_PTR(self_in);
    ipc_ui_item_add_t it;
    memset(&it, 0, sizeof(it));
    it.handle = self->handle;
    mp_int_t x = mp_obj_get_int(x_obj);
    mp_int_t y = mp_obj_get_int(y_obj);
    (void)snprintf(it.text, sizeof(it.text), "%ld %ld", (long)x, (long)y);
    widget_item_send(&it);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(widget_add_point_obj, widget_add_point);

/* .add_tab(name) -> Widget — Tabview. The returned page is what goes in
 * parent= when creating the widgets that live on that tab. */
static mp_obj_t widget_add_tab(mp_obj_t self_in, mp_obj_t name_obj) {
    ui_widget_obj_t *self = MP_OBJ_TO_PTR(self_in);
    ipc_ui_item_add_t it;
    widget_item_fill(&it, self, 0, 0, 0, name_obj);
    return widget_item_send_child(&it);
}
static MP_DEFINE_CONST_FUN_OBJ_2(widget_add_tab_obj, widget_add_tab);

/* .add_tile(col, row, dir=ui.DIR_ALL) -> Widget — Tileview */
static mp_obj_t widget_add_tile(size_t n_args, const mp_obj_t *args) {
    ui_widget_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    ipc_ui_item_add_t it;
    widget_item_fill(&it, self, (uint8_t)mp_obj_get_int(args[1]),
                     (uint8_t)mp_obj_get_int(args[2]),
                     (uint8_t)(n_args > 3 ? mp_obj_get_int(args[3]) : 0),
                     mp_const_none);
    return widget_item_send_child(&it);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(widget_add_tile_obj, 3, 4, widget_add_tile);

/* .content() -> Widget — Win. The body of the window, i.e. the parent for
 * everything that goes inside it. Asking twice returns the same handle. */
static mp_obj_t widget_content(mp_obj_t self_in) {
    ui_widget_obj_t *self = MP_OBJ_TO_PTR(self_in);
    ipc_ui_item_add_t it;
    widget_item_fill(&it, self, 0, 0, 0, mp_const_none);
    return widget_item_send_child(&it);
}
static MP_DEFINE_CONST_FUN_OBJ_1(widget_content_obj, widget_content);

/* .clear_items() — empty the collection (rows, options, buttons, points). */
static mp_obj_t widget_clear_items(mp_obj_t self_in) {
    ui_widget_obj_t *self = MP_OBJ_TO_PTR(self_in);
    ui_ipc_init();
    uint8_t buf[1] = { self->handle };
    ui_ipc_send_fire_forget(IPC_CMD_UI_ITEM_CLEAR, buf, 1);
    self->next_row = 0;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(widget_clear_items_obj, widget_clear_items);

/*******************************************************************************
 * .prop(id, value) — every per-widget knob.
 *
 * One method rather than one opcode per setting. The three helpers under it
 * exist because their value is a pair packed into one int32, and asking a
 * learner to shift it themselves is how a packing gets miscounted.
 *******************************************************************************/
static void widget_send_prop(ui_widget_obj_t *self, uint8_t prop_id, int32_t value)
{
    ui_ipc_init();
    uint8_t buf[6];
    buf[0] = self->handle;
    buf[1] = prop_id;
    memcpy(&buf[2], &value, sizeof(int32_t));
    ui_ipc_send_fire_forget(IPC_CMD_UI_SET_PROP, buf, sizeof(buf));
}

static mp_obj_t widget_prop(mp_obj_t self_in, mp_obj_t id_obj, mp_obj_t val_obj) {
    widget_send_prop(MP_OBJ_TO_PTR(self_in),
                     (uint8_t)mp_obj_get_int(id_obj),
                     (int32_t)mp_obj_get_int(val_obj));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(widget_prop_obj, widget_prop);

/* .ticks(total, major_every) — Scale */
static mp_obj_t widget_ticks(mp_obj_t self_in, mp_obj_t total_obj, mp_obj_t major_obj) {
    ui_widget_obj_t *self = MP_OBJ_TO_PTR(self_in);
    widget_send_prop(self, UI_PROP_SCALE_TOTAL_TICKS, (int32_t)mp_obj_get_int(total_obj));
    widget_send_prop(self, UI_PROP_SCALE_MAJOR_EVERY, (int32_t)mp_obj_get_int(major_obj));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(widget_ticks_obj, widget_ticks);

/* .digits(count, point_at) — Spinbox. point_at is how many digits stand before
 * the decimal point, so (4, 2) shows the value 2375 as 23.75. */
static mp_obj_t widget_digits(mp_obj_t self_in, mp_obj_t count_obj, mp_obj_t point_obj) {
    int32_t count = (int32_t)mp_obj_get_int(count_obj);
    int32_t point = (int32_t)mp_obj_get_int(point_obj);
    widget_send_prop(MP_OBJ_TO_PTR(self_in), UI_PROP_SPINBOX_DIGITS,
                     (int32_t)(((uint32_t)count << 16) | ((uint32_t)point & 0xFFFFu)));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(widget_digits_obj, widget_digits);

/* .col_width(col, width) — Table */
static mp_obj_t widget_col_width(mp_obj_t self_in, mp_obj_t col_obj, mp_obj_t w_obj) {
    int32_t col = (int32_t)mp_obj_get_int(col_obj);
    int32_t w   = (int32_t)mp_obj_get_int(w_obj);
    widget_send_prop(MP_OBJ_TO_PTR(self_in), UI_PROP_TABLE_COL_WIDTH,
                     (int32_t)(((uint32_t)col << 16) | ((uint32_t)w & 0xFFFFu)));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(widget_col_width_obj, widget_col_width);

/*******************************************************************************
 * .listen("pressed", "released", ...) — subscribe to the input events.
 *
 * Names, not bit numbers, because the name is what comes back in ev['type']
 * and a learner should not have to hold two vocabularies for one idea.
 *
 * A widget reports NOTHING from this list until asked. That is not caution for
 * its own sake — a Button that answered pressed + clicked + released to every
 * tap would step examples/s05/08_six_filters_one_signal.py three places
 * instead of one, because that file reads ev['handle'] and never looks at
 * ev['type']. It was written before these events existed and is not wrong.
 *
 * Calling .listen() again REPLACES the set; .listen() with no arguments turns
 * them all off. The three original types (clicked / value_changed / toggled)
 * are not in the list and cannot be switched off.
 *******************************************************************************/
static const struct { const char *name; uint8_t ev; } ui_event_names[] = {
    { "pressed",             UI_EVENT_PRESSED             },
    { "released",            UI_EVENT_RELEASED            },
    { "press_lost",          UI_EVENT_PRESS_LOST          },
    { "long_pressed",        UI_EVENT_LONG_PRESSED        },
    { "long_pressed_repeat", UI_EVENT_LONG_PRESSED_REPEAT },
    { "ready",               UI_EVENT_READY               },
    { "cancel",              UI_EVENT_CANCEL              },
    { "focused",             UI_EVENT_FOCUSED             },
    { "defocused",           UI_EVENT_DEFOCUSED           },
    { "scroll_begin",        UI_EVENT_SCROLL_BEGIN        },
    { "scroll_end",          UI_EVENT_SCROLL_END          },
    { "gesture",             UI_EVENT_GESTURE             },
};

static mp_obj_t widget_listen(size_t n_args, const mp_obj_t *args) {
    ui_widget_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    ui_ipc_init();

    uint32_t mask = 0;
    for (size_t i = 1; i < n_args; i++) {
        size_t len;
        const char *name = mp_obj_str_get_data(args[i], &len);
        bool found = false;
        for (size_t k = 0; k < MP_ARRAY_SIZE(ui_event_names); k++) {
            if (strlen(ui_event_names[k].name) == len &&
                memcmp(ui_event_names[k].name, name, len) == 0) {
                mask |= UI_EVENT_MASK(ui_event_names[k].ev);
                found = true;
                break;
            }
        }
        if (!found) {
            /* Raise rather than ignore. A typo that silences a subscription
             * looks exactly like a widget that does not support the event,
             * and the learner then goes hunting in the firmware. */
            mp_raise_ValueError(MP_ERROR_TEXT("ui: listen() got an event name that does not exist"));
        }
    }

    widget_send_prop(self, UI_PROP_EVENT_MASK, (int32_t)mask);
    return args[0];   /* chainable: btn = ui.Button(...).listen("pressed") */
}
static MP_DEFINE_CONST_FUN_OBJ_VAR(widget_listen_obj, 1, widget_listen);

/* .bind(textarea) — Keyboard. Takes the Widget, not a handle: CM33 has no CM55
 * addresses, so what crosses the wire is the textarea's handle, and CM55
 * refuses it unless that handle really is a Textarea. */
static mp_obj_t widget_bind(mp_obj_t self_in, mp_obj_t ta_obj) {
    if (!mp_obj_is_type(ta_obj, &ui_widget_type)) {
        mp_raise_TypeError(MP_ERROR_TEXT("ui: bind() needs a Textarea widget"));
    }
    ui_widget_obj_t *ta = MP_OBJ_TO_PTR(ta_obj);
    widget_send_prop(MP_OBJ_TO_PTR(self_in), UI_PROP_KEYBOARD_TEXTAREA,
                     (int32_t)ta->handle);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(widget_bind_obj, widget_bind);


/*******************************************************************************
 * Menu — a page stack. The rows are handles because a row has to be named
 * again later, by .opens(page): that link is the whole point of the widget.
 *******************************************************************************/

/* .add_page(title) -> Widget — Menu. The first page added is also the one the
 * menu shows, so a menu is never blank while it waits for a call nobody made. */
static mp_obj_t widget_add_page(mp_obj_t self_in, mp_obj_t title_obj) {
    ui_widget_obj_t *self = MP_OBJ_TO_PTR(self_in);
    ipc_ui_item_add_t it;
    widget_item_fill(&it, self, 0, 0, 0, title_obj);
    return widget_item_send_child(&it);
}
static MP_DEFINE_CONST_FUN_OBJ_2(widget_add_page_obj, widget_add_page);

/* One helper, three names: a menu page's children differ only in which of the
 * three lv_menu container classes CM55 builds. */
static mp_obj_t widget_menu_child(ui_widget_obj_t *self, uint8_t kind,
                                  mp_obj_t text_obj) {
    ipc_ui_item_add_t it;
    widget_item_fill(&it, self, kind, 0, 0, text_obj);
    return widget_item_send_child(&it);
}

/* .row(text="") -> Widget — a tappable menu row (a container with a label). */
static mp_obj_t widget_row(size_t n_args, const mp_obj_t *args) {
    return widget_menu_child(MP_OBJ_TO_PTR(args[0]), UI_MENU_ITEM_ROW,
                             (n_args > 1) ? args[1] : mp_const_none);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(widget_row_obj, 1, 2, widget_row);

/* .section() -> Widget — a grouped block of rows. */
static mp_obj_t widget_section(mp_obj_t self_in) {
    return widget_menu_child(MP_OBJ_TO_PTR(self_in), UI_MENU_ITEM_SECTION,
                             mp_const_none);
}
static MP_DEFINE_CONST_FUN_OBJ_1(widget_section_obj, widget_section);

/* .separator() -> Widget */
static mp_obj_t widget_separator(mp_obj_t self_in) {
    return widget_menu_child(MP_OBJ_TO_PTR(self_in), UI_MENU_ITEM_SEPARATOR,
                             mp_const_none);
}
static MP_DEFINE_CONST_FUN_OBJ_1(widget_separator_obj, widget_separator);

/* .opens(page) — tapping this row loads that page.
 *
 * Two handles cross the wire and no opcode is spent: CM55 knows which menu owns
 * the page because it recorded that when the page was created. */
static mp_obj_t widget_opens(mp_obj_t self_in, mp_obj_t page_obj) {
    if (!mp_obj_is_type(page_obj, &ui_widget_type)) {
        mp_raise_TypeError(MP_ERROR_TEXT("ui: opens() needs a menu page"));
    }
    ui_widget_obj_t *page = MP_OBJ_TO_PTR(page_obj);
    widget_send_prop(MP_OBJ_TO_PTR(self_in), UI_PROP_MENU_LOAD_PAGE,
                     (int32_t)page->handle);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(widget_opens_obj, widget_opens);

/*******************************************************************************
 * SpanGroup — rich text.
 *
 * A run is not a widget and never gets a handle, so its style travels with it:
 * size and decorations in the item, colour from the pen set beforehand.
 *******************************************************************************/

/* .add_span(text, size=0, flags=0) — size 0 keeps the group's font, which is
 * what Thai text wants (the size ladder is Latin-only). */
static mp_obj_t widget_add_span(size_t n_args, const mp_obj_t *args) {
    ui_widget_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    ipc_ui_item_add_t it;
    widget_item_fill(&it, self,
                     (uint8_t)(n_args > 2 ? mp_obj_get_int(args[2]) : 0),
                     0,
                     (uint8_t)(n_args > 3 ? mp_obj_get_int(args[3]) : 0),
                     args[1]);
    widget_item_send(&it);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(widget_add_span_obj, 2, 4, widget_add_span);

/* .pen(color) — the colour of the runs added next. */
static mp_obj_t widget_pen(mp_obj_t self_in, mp_obj_t color_obj) {
    widget_send_prop(MP_OBJ_TO_PTR(self_in), UI_PROP_SPAN_COLOR,
                     (int32_t)mp_obj_get_int(color_obj));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(widget_pen_obj, widget_pen);

/* .month(year, month) — Calendar. One date, sent the way a date is written. */
static mp_obj_t widget_month(mp_obj_t self_in, mp_obj_t y_obj, mp_obj_t m_obj) {
    int32_t y = (int32_t)mp_obj_get_int(y_obj);
    int32_t m = (int32_t)mp_obj_get_int(m_obj);
    widget_send_prop(MP_OBJ_TO_PTR(self_in), UI_PROP_CALENDAR_SHOWN, y * 100 + m);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(widget_month_obj, widget_month);

/*******************************************************************************
 * Widget Method Table
 *******************************************************************************/
static const mp_rom_map_elem_t widget_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_id),         MP_ROM_PTR(&widget_id_obj) },
    { MP_ROM_QSTR(MP_QSTR_text),       MP_ROM_PTR(&widget_text_obj) },
    { MP_ROM_QSTR(MP_QSTR_value),      MP_ROM_PTR(&widget_value_obj) },
    { MP_ROM_QSTR(MP_QSTR_pos),        MP_ROM_PTR(&widget_pos_obj) },
    { MP_ROM_QSTR(MP_QSTR_size),       MP_ROM_PTR(&widget_size_obj) },
    { MP_ROM_QSTR(MP_QSTR_color),      MP_ROM_PTR(&widget_color_obj) },
    { MP_ROM_QSTR(MP_QSTR_show),       MP_ROM_PTR(&widget_show_obj) },
    { MP_ROM_QSTR(MP_QSTR_hide),       MP_ROM_PTR(&widget_hide_obj) },
    { MP_ROM_QSTR(MP_QSTR_delete),     MP_ROM_PTR(&widget_delete_obj) },
#if ENABLE_GAME_SPRITES
    { MP_ROM_QSTR(MP_QSTR_frame),      MP_ROM_PTR(&widget_frame_obj) },
#endif
    { MP_ROM_QSTR(MP_QSTR_set_pixels), MP_ROM_PTR(&widget_set_pixels_obj) },
    { MP_ROM_QSTR(MP_QSTR_icon),       MP_ROM_PTR(&widget_icon_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_image),  MP_ROM_PTR(&widget_set_image_obj) },
    { MP_ROM_QSTR(MP_QSTR_add_series), MP_ROM_PTR(&widget_add_series_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_next),   MP_ROM_PTR(&widget_set_next_obj) },
    /* Collections: one ITEM_ADD per item (Table, List, Roller, Dropdown,
     * ButtonMatrix, MsgBox, Line). */
    { MP_ROM_QSTR(MP_QSTR_add_item),    MP_ROM_PTR(&widget_add_item_obj) },
    { MP_ROM_QSTR(MP_QSTR_cell),        MP_ROM_PTR(&widget_cell_obj) },
    { MP_ROM_QSTR(MP_QSTR_add_row),     MP_ROM_PTR(&widget_add_row_obj) },
    { MP_ROM_QSTR(MP_QSTR_add_option),  MP_ROM_PTR(&widget_add_option_obj) },
    { MP_ROM_QSTR(MP_QSTR_add_button),  MP_ROM_PTR(&widget_add_button_obj) },
    { MP_ROM_QSTR(MP_QSTR_add_point),   MP_ROM_PTR(&widget_add_point_obj) },
    { MP_ROM_QSTR(MP_QSTR_clear_items), MP_ROM_PTR(&widget_clear_items_obj) },
    /* Containers: the same opcode, answered with the child's handle. */
    { MP_ROM_QSTR(MP_QSTR_add_tab),     MP_ROM_PTR(&widget_add_tab_obj) },
    { MP_ROM_QSTR(MP_QSTR_add_tile),    MP_ROM_PTR(&widget_add_tile_obj) },
    { MP_ROM_QSTR(MP_QSTR_content),     MP_ROM_PTR(&widget_content_obj) },
    /* Per-widget knobs. */
    { MP_ROM_QSTR(MP_QSTR_prop),        MP_ROM_PTR(&widget_prop_obj) },
    { MP_ROM_QSTR(MP_QSTR_ticks),       MP_ROM_PTR(&widget_ticks_obj) },
    { MP_ROM_QSTR(MP_QSTR_digits),      MP_ROM_PTR(&widget_digits_obj) },
    { MP_ROM_QSTR(MP_QSTR_col_width),   MP_ROM_PTR(&widget_col_width_obj) },
    { MP_ROM_QSTR(MP_QSTR_bind),        MP_ROM_PTR(&widget_bind_obj) },
    /* Event subscription — the twelve input events are opt-in per widget. */
    { MP_ROM_QSTR(MP_QSTR_listen),      MP_ROM_PTR(&widget_listen_obj) },
    /* Menu: pages, rows, and the link between them. */
    { MP_ROM_QSTR(MP_QSTR_add_page),    MP_ROM_PTR(&widget_add_page_obj) },
    { MP_ROM_QSTR(MP_QSTR_row),         MP_ROM_PTR(&widget_row_obj) },
    { MP_ROM_QSTR(MP_QSTR_section),     MP_ROM_PTR(&widget_section_obj) },
    { MP_ROM_QSTR(MP_QSTR_separator),   MP_ROM_PTR(&widget_separator_obj) },
    { MP_ROM_QSTR(MP_QSTR_opens),       MP_ROM_PTR(&widget_opens_obj) },
    /* SpanGroup: styled runs, and the pen that colours them. */
    { MP_ROM_QSTR(MP_QSTR_add_span),    MP_ROM_PTR(&widget_add_span_obj) },
    { MP_ROM_QSTR(MP_QSTR_pen),         MP_ROM_PTR(&widget_pen_obj) },
    /* Calendar */
    { MP_ROM_QSTR(MP_QSTR_month),       MP_ROM_PTR(&widget_month_obj) },
};
static MP_DEFINE_CONST_DICT(widget_locals_dict, widget_locals_dict_table);

static void widget_print(const mp_print_t *print, mp_obj_t self_in,
                          mp_print_kind_t kind) {
    (void)kind;
    ui_widget_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "<Widget handle=%d type=%d>", self->handle, self->widget_type);
}

MP_DEFINE_CONST_OBJ_TYPE(
    ui_widget_type,
    MP_QSTR_Widget,
    MP_TYPE_FLAG_NONE,
    print, widget_print,
    locals_dict, &widget_locals_dict
);

/*******************************************************************************
 * create_widget_common() — Helper for all factory functions
 *******************************************************************************/
static mp_obj_t create_widget_common(uint8_t widget_type,
                                      size_t n_args, const mp_obj_t *args,
                                      mp_map_t *kw_args)
{
    ui_ipc_init();
    ui_pause_sensor_auto();

    /* Parse keyword args */
    enum { ARG_text, ARG_x, ARG_y, ARG_w, ARG_h, ARG_color,
           ARG_min, ARG_max, ARG_value, ARG_cols, ARG_rows, ARG_parent };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_text,  MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_x,     MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_y,     MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_w,     MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_h,     MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_color, MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_min,   MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_max,   MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_value, MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_cols,  MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_rows,  MP_ARG_INT, {.u_int = 0} },
        /* parent= appears on every factory: any widget can be created inside a
         * tab page, a tile or a window body, and the caller should not have to
         * remember which factories accept it. */
        { MP_QSTR_parent, MP_ARG_OBJ, {.u_obj = mp_const_none} },
    };
    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, args, kw_args,
                     MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);

    /* Build CREATE payload */
    ipc_ui_create_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.widget_type = widget_type;
    cfg.x = (int16_t)parsed[ARG_x].u_int;
    cfg.y = (int16_t)parsed[ARG_y].u_int;
    cfg.w = (int16_t)parsed[ARG_w].u_int;
    cfg.h = (int16_t)parsed[ARG_h].u_int;
    cfg.color = (uint32_t)parsed[ARG_color].u_int;
    cfg.min_val = parsed[ARG_min].u_int;
    cfg.max_val = parsed[ARG_max].u_int;
    cfg.init_val = parsed[ARG_value].u_int;

    /* DotMatrix uses cols/rows in min_val/max_val */
    if (widget_type == UI_WIDGET_DOTMATRIX) {
        cfg.min_val = parsed[ARG_cols].u_int;
        cfg.max_val = parsed[ARG_rows].u_int;
    }

    /* Table reads the same two words, but only when they were actually given,
     * so min=/max= keep working for anyone who prefers them. */
    if (widget_type == UI_WIDGET_TABLE) {
        if (parsed[ARG_cols].u_int > 0) cfg.min_val = parsed[ARG_cols].u_int;
        if (parsed[ARG_rows].u_int > 0) cfg.max_val = parsed[ARG_rows].u_int;
    }

    /* parent= takes a Widget, never a bare handle: a handle is a number, a
     * number is easy to get wrong, and the widget in hand is the thing the
     * caller is actually thinking about. Encoded as handle+1 so that the
     * memset above -- i.e. no parent= at all -- means the screen container. */
    if (parsed[ARG_parent].u_obj != mp_const_none) {
        if (!mp_obj_is_type(parsed[ARG_parent].u_obj, &ui_widget_type)) {
            mp_raise_TypeError(MP_ERROR_TEXT("ui: parent must be a widget"));
        }
        ui_widget_obj_t *pw = MP_OBJ_TO_PTR(parsed[ARG_parent].u_obj);
        cfg.parent_plus1 = (uint8_t)(pw->handle + 1u);
    }

    /* Text (first positional arg or keyword) */
    const char *full_text = NULL;
    size_t full_len = 0;
    if (parsed[ARG_text].u_obj != mp_const_none) {
        full_text = mp_obj_str_get_data(parsed[ARG_text].u_obj, &full_len);
        size_t copy = ui_utf8_fit(full_text, full_len, UI_CREATE_TEXT_MAX - 1);
        memcpy(cfg.text, full_text, copy);
        cfg.text[copy] = '\0';
    }

    /* Send CREATE (bidirectional) — use robust wrapper to survive soft-reset */
    if (!ui_ipc_send_bidir_robust(IPC_CMD_UI_CREATE, &cfg, sizeof(cfg))) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("ui: IPC send failed"));
    }

    if (ui_ipc_resp.status != UI_STATUS_OK) {
        if (ui_ipc_resp.status == UI_STATUS_TABLE_FULL) {
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("ui: max 64 widgets"));
        }
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("ui: create failed"));
    }

    /* Create Python Widget object */
    ui_widget_obj_t *widget = mp_obj_malloc(ui_widget_obj_t, &ui_widget_type);
    widget->handle = ui_ipc_resp.data[0];
    widget->widget_type = widget_type;
    widget->next_row = 0;

    /* ข้อความที่ยาวเกินช่องของ CREATE ส่งซ้ำทาง SET_TEXT ซึ่งช่องใหญ่กว่า
     *
     * CREATE พาข้อความไปได้ 95 ไบต์ (UI_CREATE_TEXT_MAX ลบตัวปิดสาย) ส่วน
     * SET_TEXT ไปได้ 126 ไบต์ (IPC_DATA_MAX_LEN ลบ handle กับตัวปิดสาย) ป้าย
     * ที่ยาวกว่า 95 ไบต์จึงเคยขึ้นจอไม่ครบและไม่มี error ให้จับ ซึ่งกับภาษาไทย
     * ที่ตัวละ 3 ไบต์ แปลว่าเกิน 31 ตัวอักษรก็หายแล้ว ทีม RED เปิดภาพจากบอร์ด
     * 14 ส.ค. 2026 เจอสี่ประโยคขาดกลางคำ ด่าน check_label_bytes.py จับไม่ได้
     * เพราะมันวัดได้เฉพาะสายที่เขียนตรง ๆ ส่วนที่ประกอบตอนรันมันรายงานเองว่า
     * วัดไม่ได้ 251 จุด และไม่มีใครตามต่อ
     *
     * ยาวเกิน 126 ไบต์ยังขาดอยู่ดี แต่ขาดตรงขอบตัวอักษร ไม่ใช่กลางตัว */
    if (full_text != NULL && full_len > (UI_CREATE_TEXT_MAX - 1)) {
        uint8_t buf[IPC_DATA_MAX_LEN];
        memset(buf, 0, sizeof(buf));
        buf[0] = widget->handle;
        size_t copy = ui_utf8_fit(full_text, full_len, IPC_DATA_MAX_LEN - 2);
        memcpy(&buf[1], full_text, copy);
        buf[1 + copy] = '\0';
        ui_ipc_send_fire_forget(IPC_CMD_UI_SET_TEXT, buf, 2 + copy);
    }

    return MP_OBJ_FROM_PTR(widget);
}

/*******************************************************************************
 * Factory Functions: ui.Button(), ui.Label(), ui.Slider(), etc.
 *******************************************************************************/

#define DEFINE_WIDGET_FACTORY(name, wtype)                                    \
static mp_obj_t ui_##name(size_t n_args, const mp_obj_t *pos_args,           \
                           mp_map_t *kw_args) {                              \
    return create_widget_common(wtype, n_args, pos_args, kw_args);           \
}                                                                            \
static MP_DEFINE_CONST_FUN_OBJ_KW(ui_##name##_obj, 0, ui_##name);

DEFINE_WIDGET_FACTORY(button,    UI_WIDGET_BUTTON)
DEFINE_WIDGET_FACTORY(label,     UI_WIDGET_LABEL)
DEFINE_WIDGET_FACTORY(slider,    UI_WIDGET_SLIDER)
DEFINE_WIDGET_FACTORY(switch_,   UI_WIDGET_SWITCH)
DEFINE_WIDGET_FACTORY(checkbox,  UI_WIDGET_CHECKBOX)
DEFINE_WIDGET_FACTORY(arc,       UI_WIDGET_ARC)
DEFINE_WIDGET_FACTORY(bar,       UI_WIDGET_BAR)
DEFINE_WIDGET_FACTORY(spinner,   UI_WIDGET_SPINNER)
DEFINE_WIDGET_FACTORY(dropdown,  UI_WIDGET_DROPDOWN)
DEFINE_WIDGET_FACTORY(textarea,  UI_WIDGET_TEXTAREA)
DEFINE_WIDGET_FACTORY(seg7,      UI_WIDGET_SEG7)
DEFINE_WIDGET_FACTORY(dotmatrix, UI_WIDGET_DOTMATRIX)
DEFINE_WIDGET_FACTORY(chart,     UI_WIDGET_CHART)
DEFINE_WIDGET_FACTORY(image,     UI_WIDGET_IMAGE)
DEFINE_WIDGET_FACTORY(panel,     UI_WIDGET_PANEL)
DEFINE_WIDGET_FACTORY(compass,   UI_WIDGET_COMPASS)
DEFINE_WIDGET_FACTORY(led,       UI_WIDGET_LED)
DEFINE_WIDGET_FACTORY(scale,     UI_WIDGET_SCALE)
DEFINE_WIDGET_FACTORY(spinbox,   UI_WIDGET_SPINBOX)
DEFINE_WIDGET_FACTORY(table,     UI_WIDGET_TABLE)
/* list_ with the trailing underscore, like switch_: plain `list` would expand
 * to ui_list(), which is already the ui.list() utility. */
DEFINE_WIDGET_FACTORY(list_,     UI_WIDGET_LIST)
DEFINE_WIDGET_FACTORY(roller,    UI_WIDGET_ROLLER)
DEFINE_WIDGET_FACTORY(btnmatrix, UI_WIDGET_BTNMATRIX)
DEFINE_WIDGET_FACTORY(msgbox,    UI_WIDGET_MSGBOX)
DEFINE_WIDGET_FACTORY(keyboard,  UI_WIDGET_KEYBOARD)
DEFINE_WIDGET_FACTORY(tabview,   UI_WIDGET_TABVIEW)
DEFINE_WIDGET_FACTORY(tileview,  UI_WIDGET_TILEVIEW)
DEFINE_WIDGET_FACTORY(win,       UI_WIDGET_WIN)
DEFINE_WIDGET_FACTORY(line,      UI_WIDGET_LINE)
/* ui.Picture is the real lv_image: it rotates and scales. ui.Image is the
 * canvas you write pixels into, and is unchanged. Two names because they are
 * two widgets -- calling this one ui.Img would have read like a typo for the
 * other. */
DEFINE_WIDGET_FACTORY(picture,   UI_WIDGET_IMG)
DEFINE_WIDGET_FACTORY(menu,      UI_WIDGET_MENU)
DEFINE_WIDGET_FACTORY(spangroup, UI_WIDGET_SPANGROUP)
DEFINE_WIDGET_FACTORY(calendar,  UI_WIDGET_CALENDAR)

/*******************************************************************************
 * ui.poll() — Poll pending events from CM55
 *******************************************************************************/
static mp_obj_t ui_poll(void) {
    ui_ipc_init();

    if (!ui_ipc_send_bidir_robust(IPC_CMD_UI_POLL_EVENTS, NULL, 0)) {
        return mp_obj_new_list(0, NULL);
    }

    if (ui_ipc_resp.status != UI_STATUS_OK || ui_ipc_resp.data_len == 0) {
        return mp_obj_new_list(0, NULL);
    }

    int count = ui_ipc_resp.data_len / sizeof(ipc_ui_event_t);
    if (count > UI_MAX_EVENTS_PER_POLL) count = UI_MAX_EVENTS_PER_POLL;

    mp_obj_list_t *list = MP_OBJ_TO_PTR(mp_obj_new_list(0, NULL));
    const ipc_ui_event_t *events = (const ipc_ui_event_t *)ui_ipc_resp.data;

    for (int i = 0; i < count; i++) {
        /* Create dict: {'handle': N, 'type': str, 'value': N} */
        mp_obj_dict_t *dict = MP_OBJ_TO_PTR(mp_obj_new_dict(3));

        mp_obj_dict_store(MP_OBJ_FROM_PTR(dict),
            MP_OBJ_NEW_QSTR(MP_QSTR_handle),
            mp_obj_new_int(events[i].handle_id));

        /* One name per wire code, lowercase_with_underscores like the first
         * three. "unknown" survives as the fallback on purpose: a board
         * running newer firmware than the script's mental model should say so
         * rather than drop the event. */
        const char *type_str = "unknown";
        switch (events[i].event_type) {
            case UI_EVENT_CLICKED:       type_str = "clicked"; break;
            case UI_EVENT_VALUE_CHANGED: type_str = "value_changed"; break;
            case UI_EVENT_TOGGLED:       type_str = "toggled"; break;
            case UI_EVENT_PRESSED:       type_str = "pressed"; break;
            case UI_EVENT_RELEASED:      type_str = "released"; break;
            case UI_EVENT_PRESS_LOST:    type_str = "press_lost"; break;
            case UI_EVENT_LONG_PRESSED:  type_str = "long_pressed"; break;
            case UI_EVENT_LONG_PRESSED_REPEAT:
                                         type_str = "long_pressed_repeat"; break;
            case UI_EVENT_READY:         type_str = "ready"; break;
            case UI_EVENT_CANCEL:        type_str = "cancel"; break;
            case UI_EVENT_FOCUSED:       type_str = "focused"; break;
            case UI_EVENT_DEFOCUSED:     type_str = "defocused"; break;
            case UI_EVENT_SCROLL_BEGIN:  type_str = "scroll_begin"; break;
            case UI_EVENT_SCROLL_END:    type_str = "scroll_end"; break;
            case UI_EVENT_GESTURE:       type_str = "gesture"; break;
        }
        mp_obj_dict_store(MP_OBJ_FROM_PTR(dict),
            MP_OBJ_NEW_QSTR(MP_QSTR_type),
            mp_obj_new_str(type_str, strlen(type_str)));

        mp_obj_dict_store(MP_OBJ_FROM_PTR(dict),
            MP_OBJ_NEW_QSTR(MP_QSTR_value),
            mp_obj_new_int(events[i].value));

        mp_obj_list_append(MP_OBJ_FROM_PTR(list), MP_OBJ_FROM_PTR(dict));
    }

    return MP_OBJ_FROM_PTR(list);
}
static MP_DEFINE_CONST_FUN_OBJ_0(ui_poll_obj, ui_poll);

/*******************************************************************************
 * ui.list() — List all active widgets from CM55
 * Returns list of dicts: [{'id': 0, 'type': 'Button'}, ...]
 *******************************************************************************/
static const char *widget_type_name(uint8_t wtype) {
    switch (wtype) {
        case UI_WIDGET_BUTTON:    return "Button";
        case UI_WIDGET_LABEL:     return "Label";
        case UI_WIDGET_SLIDER:    return "Slider";
        case UI_WIDGET_SWITCH:    return "Switch";
        case UI_WIDGET_CHECKBOX:  return "Checkbox";
        case UI_WIDGET_ARC:       return "Arc";
        case UI_WIDGET_BAR:       return "Bar";
        case UI_WIDGET_SPINNER:   return "Spinner";
        case UI_WIDGET_DROPDOWN:  return "Dropdown";
        case UI_WIDGET_TEXTAREA:  return "Textarea";
        case UI_WIDGET_SEG7:      return "Seg7";
        case UI_WIDGET_DOTMATRIX: return "DotMatrix";
        case UI_WIDGET_CHART:     return "Chart";
        case UI_WIDGET_IMAGE:     return "Image";
        case UI_WIDGET_PANEL:     return "Panel";
        case UI_WIDGET_COMPASS:   return "Compass";
        case UI_WIDGET_LED:       return "Led";
        case UI_WIDGET_SCALE:     return "Scale";
        case UI_WIDGET_SPINBOX:   return "Spinbox";
        case UI_WIDGET_TABLE:     return "Table";
        case UI_WIDGET_LIST:      return "List";
        case UI_WIDGET_ROLLER:    return "Roller";
        case UI_WIDGET_BTNMATRIX: return "ButtonMatrix";
        case UI_WIDGET_MSGBOX:    return "MsgBox";
        case UI_WIDGET_KEYBOARD:  return "Keyboard";
        case UI_WIDGET_TABVIEW:   return "Tabview";
        case UI_WIDGET_TILEVIEW:  return "Tileview";
        case UI_WIDGET_WIN:       return "Win";
        case UI_WIDGET_LINE:      return "Line";
        case UI_WIDGET_IMG:       return "Picture";
        case UI_WIDGET_CONTAINER: return "Container";
        case UI_WIDGET_MENU:      return "Menu";
        case UI_WIDGET_MENU_PAGE: return "MenuPage";
        case UI_WIDGET_SPANGROUP: return "SpanGroup";
        case UI_WIDGET_CALENDAR:  return "Calendar";
        default:                  return "Unknown";
    }
}

static mp_obj_t ui_list(void) {
    ui_ipc_init();

    if (!ui_ipc_send_bidir_robust(IPC_CMD_UI_LIST, NULL, 0)) {
        return mp_obj_new_list(0, NULL);
    }

    if (ui_ipc_resp.status != UI_STATUS_OK || ui_ipc_resp.data_len < 1) {
        return mp_obj_new_list(0, NULL);
    }

    uint8_t count = ui_ipc_resp.data[0];
    const ipc_ui_widget_info_t *info =
        (const ipc_ui_widget_info_t *)&ui_ipc_resp.data[1];

    mp_obj_list_t *list = MP_OBJ_TO_PTR(mp_obj_new_list(0, NULL));

    for (int i = 0; i < count; i++) {
        mp_obj_dict_t *dict = MP_OBJ_TO_PTR(mp_obj_new_dict(2));
        mp_obj_dict_store(MP_OBJ_FROM_PTR(dict),
            MP_OBJ_NEW_QSTR(MP_QSTR_id),
            mp_obj_new_int(info[i].handle_id));

        const char *name = widget_type_name(info[i].widget_type);
        mp_obj_dict_store(MP_OBJ_FROM_PTR(dict),
            MP_OBJ_NEW_QSTR(MP_QSTR_type),
            mp_obj_new_str(name, strlen(name)));

        mp_obj_list_append(MP_OBJ_FROM_PTR(list), MP_OBJ_FROM_PTR(dict));
    }

    return MP_OBJ_FROM_PTR(list);
}
static MP_DEFINE_CONST_FUN_OBJ_0(ui_list_obj, ui_list);

/*******************************************************************************
 * ui._diag() — CM55 UI/display diagnostics for V&V (Bento Engine, 2026-08-20)
 * Returns a dict of counters: ipc_ui queue drops + fast-mode flag, then the
 * display controller block (DC underflow, flush watchdog, GFX idle %) when the
 * platform provides it. Read-only; safe to call from any script or the REPL.
 *******************************************************************************/
static mp_obj_t ui_diag(void) {
    ui_ipc_init();

    static const qstr keys[] = {
        MP_QSTR_drop_shed, MP_QSTR_drop_full_low, MP_QSTR_drop_oldest,
        MP_QSTR_drop_purge_high, MP_QSTR_drop_full_high, MP_QSTR_queue_hwm,
        MP_QSTR_fast_mode,
        MP_QSTR_dc_irq, MP_QSTR_dc_disp0, MP_QSTR_dc_underflow,
        MP_QSTR_dc_bus_err, MP_QSTR_gpu_recovery, MP_QSTR_flush_start,
        MP_QSTR_flush_ready, MP_QSTR_flush_timeout, MP_QSTR_gfx_stack_min,
        MP_QSTR_gfx_idle_pct, MP_QSTR_dc_irq_enable, MP_QSTR_dsi_int_st1,
    };

    if (!ui_ipc_send_bidir_robust(IPC_CMD_UI_GET_DIAG, NULL, 0) ||
        ui_ipc_resp.status != UI_STATUS_OK) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("ui: diag failed"));
    }

    int words = ui_ipc_resp.data_len / 4;
    int nkeys = (int)(sizeof(keys) / sizeof(keys[0]));
    if (words > nkeys) words = nkeys;

    uint32_t v[19];
    memcpy(v, (const void *)ui_ipc_resp.data, (size_t)words * 4u);

    mp_obj_dict_t *dict = MP_OBJ_TO_PTR(mp_obj_new_dict(words));
    for (int i = 0; i < words; i++) {
        mp_obj_dict_store(MP_OBJ_FROM_PTR(dict),
            MP_OBJ_NEW_QSTR(keys[i]), mp_obj_new_int_from_uint(v[i]));
    }
    return MP_OBJ_FROM_PTR(dict);
}
static MP_DEFINE_CONST_FUN_OBJ_0(ui_diag_obj, ui_diag);

/*******************************************************************************
 * ui.get(id) — Get a Widget object by handle ID
 * Queries CM55 to verify the widget exists and get its type.
 * Returns Widget object or raises ValueError if not found.
 *******************************************************************************/
static mp_obj_t ui_get(mp_obj_t id_obj) {
    ui_ipc_init();
    int id = mp_obj_get_int(id_obj);

    /* Was hardcoded to 31 and drifted the moment the table grew. */
    if (id < 0 || id >= UI_MAX_WIDGETS) {
        mp_raise_ValueError(MP_ERROR_TEXT("ui: id out of range"));
    }

    /* Query CM55 for active widget list */
    if (!ui_ipc_send_bidir_robust(IPC_CMD_UI_LIST, NULL, 0)) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("ui: IPC failed"));
    }

    if (ui_ipc_resp.status != UI_STATUS_OK || ui_ipc_resp.data_len < 1) {
        mp_raise_ValueError(MP_ERROR_TEXT("ui: widget not found"));
    }

    uint8_t count = ui_ipc_resp.data[0];
    const ipc_ui_widget_info_t *info =
        (const ipc_ui_widget_info_t *)&ui_ipc_resp.data[1];

    /* Find matching handle */
    for (int i = 0; i < count; i++) {
        if (info[i].handle_id == (uint8_t)id) {
            ui_widget_obj_t *widget = mp_obj_malloc(ui_widget_obj_t,
                                                     &ui_widget_type);
            widget->handle = info[i].handle_id;
            widget->widget_type = info[i].widget_type;
            widget->next_row = 0;
            return MP_OBJ_FROM_PTR(widget);
        }
    }

    mp_raise_ValueError(MP_ERROR_TEXT("ui: widget not found"));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(ui_get_obj, ui_get);

/*******************************************************************************
 * ui.clear() — Delete all widgets
 *******************************************************************************/
static mp_obj_t ui_clear(void) {
    ui_ipc_init();
    ui_ipc_send_fire_forget(IPC_CMD_UI_CLEAR_ALL, NULL, 0);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(ui_clear_obj, ui_clear);

/*******************************************************************************
 * ui.program([code]) — Persist MicroPython code across board resets
 *   ui.program(code_str) → writes code to /main.py, returns True
 *   ui.program()          → reads /main.py, returns string or None
 *   ui.program("")        → deletes /main.py, returns None
 *******************************************************************************/
static mp_obj_t ui_program(size_t n_args, const mp_obj_t *args) {
    mp_obj_t path_obj = mp_obj_new_str("/main.py", 8);

    if (n_args == 0) {
        /* READ: return content of /main.py, or None if not found */
        nlr_buf_t nlr;
        if (nlr_push(&nlr) == 0) {
            mp_obj_t f_args[2] = { path_obj, MP_OBJ_NEW_QSTR(MP_QSTR_r) };
            mp_obj_t f = mp_builtin_open(2, f_args,
                                          (mp_map_t *)&mp_const_empty_map);
            mp_obj_t dest[2];
            mp_load_method(f, MP_QSTR_read, dest);
            mp_obj_t content = mp_call_method_n_kw(0, 0, dest);
            mp_stream_close(f);
            nlr_pop();
            return content;
        } else {
            return mp_const_none;
        }
    }

    const char *code = mp_obj_str_get_str(args[0]);

    if (code[0] == '\0') {
        /* DELETE: remove /main.py (ignore if not found) */
        nlr_buf_t nlr;
        if (nlr_push(&nlr) == 0) {
            mp_obj_t os_mod = mp_import_name(MP_QSTR_os, mp_const_none,
                                              MP_OBJ_NEW_SMALL_INT(0));
            mp_obj_t remove_fn = mp_load_attr(os_mod, MP_QSTR_remove);
            mp_call_function_1(remove_fn, path_obj);
            nlr_pop();
        }
        return mp_const_none;
    }

    /* WRITE: save code to /main.py */
    mp_obj_t f_args[2] = { path_obj, MP_OBJ_NEW_QSTR(MP_QSTR_w) };
    mp_obj_t f = mp_builtin_open(2, f_args,
                                  (mp_map_t *)&mp_const_empty_map);
    mp_obj_t dest[3];
    mp_load_method(f, MP_QSTR_write, dest);
    dest[2] = args[0];
    mp_call_method_n_kw(1, 0, dest);
    mp_stream_close(f);
    return mp_const_true;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(ui_program_obj, 0, 1, ui_program);

/*******************************************************************************
 * ui_pause_sensor_auto() — Pause background sensor auto-push on first UI use.
 *
 * When MicroPython creates widgets, the Playground page is active on CM55.
 * sensor_auto_task is unnecessary (no one reads the data) and wastes I2C bus
 * time + IPC pipe bandwidth, causing queue saturation at high update rates.
 * Pausing it gives MicroPython exclusive I2C + IPC access.
 * Resumed automatically in ui_auto_clear_on_reset() (every soft reset).
 *******************************************************************************/
static void ui_pause_sensor_auto(void) {
    if (sensor_auto_is_running()) {
        sensor_auto_stop();
        ui_sensor_auto_paused = true;
    }
}

/*******************************************************************************
 * ui.screen(width, height) — Set display dimensions and reset auto-layout
 *******************************************************************************/
static mp_obj_t ui_screen(size_t n_args, const mp_obj_t *args) {
    int16_t w = (n_args > 0) ? mp_obj_get_int(args[0]) : UI_DEF_SCREEN_W;
    int16_t h = (n_args > 1) ? mp_obj_get_int(args[1]) : UI_DEF_SCREEN_H;

    ui_ipc_init();
    ui_pause_sensor_auto();

    /* Auto-clear all existing widgets before setting up new screen.
     * Prevents GPU overload when main.py widgets overlap with mpremote scripts. */
    ui_ipc_send_fire_forget(IPC_CMD_UI_CLEAR_ALL, NULL, 0);
    Cy_SysLib_Delay(50);

    uint8_t buf[4];
    memcpy(&buf[0], &w, sizeof(int16_t));
    memcpy(&buf[2], &h, sizeof(int16_t));
    ui_ipc_send_fire_forget(IPC_CMD_UI_SET_SCREEN, buf, 4);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(ui_screen_obj, 0, 2, ui_screen);

/*******************************************************************************
 * ui._deploy() — Show native "Programming Mode" overlay on LCD
 * Called by TESAIoT IDE before file upload. Triggers IPC_CMD_UI_DEPLOY_SCREEN
 * on CM55 which creates a full-screen LVGL overlay (download icon + text).
 * Cleared automatically on next soft reset by ui_auto_clear_on_reset().
 *******************************************************************************/
static mp_obj_t ui_deploy(void) {
    ui_ipc_init();
    ui_ipc_send_fire_forget(IPC_CMD_UI_DEPLOY_SCREEN, NULL, 0);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(ui_deploy_obj, ui_deploy);

/*******************************************************************************
 * ui._ide_status(connected) — Show/hide IDE connection status on LCD
 *******************************************************************************/
static mp_obj_t ui_ide_status(mp_obj_t connected_obj) {
    ui_ipc_init();
    uint8_t buf[1];
    buf[0] = mp_obj_is_true(connected_obj) ? 1 : 0;
    ui_ipc_send_fire_forget(IPC_CMD_UI_IDE_STATUS, buf, 1);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(ui_ide_status_obj, ui_ide_status);

#if UI_SFX_API_PRESENT
/*******************************************************************************
 * ui.sfx(id) — play a sound effect by id (fire-and-forget).
 * Drives bento_sfx on CM55 over IPC_CMD_UI_SFX (fire-and-forget — no
 * completion or error is reported back).
 * Use the ui.SFX_* constants, e.g. ui.sfx(ui.SFX_UI_SELECT).
 * Compiled when BSP_HAS_AUDIO_CODEC=1 (codec present) or UI_SFX_API_STUB=1
 * (codec-less board keeping the API; CM55 ignores the opcode).
 *******************************************************************************/
static mp_obj_t ui_sfx(mp_obj_t id_obj) {
    ui_ipc_init();
    uint8_t buf[2];
    buf[0] = BENTO_SFX_OP_EVENT;
    buf[1] = (uint8_t)mp_obj_get_int(id_obj);
    ui_ipc_send_fire_forget(IPC_CMD_UI_SFX, buf, 2);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(ui_sfx_obj, ui_sfx);

/*******************************************************************************
 * ui.tone(note, wave=WAVE_SQUARE, velocity=100, dur_ms=150) — play a custom
 * tone (fire-and-forget) so students can invent their own sounds.
 *******************************************************************************/
static mp_obj_t ui_tone(size_t n_args, const mp_obj_t *args) {
    ui_ipc_init();
    uint16_t dur = (n_args > 3) ? (uint16_t)mp_obj_get_int(args[3]) : 150;
    uint8_t buf[6];
    buf[0] = BENTO_SFX_OP_TONE;
    buf[1] = (uint8_t)mp_obj_get_int(args[0]);                                   /* MIDI note */
    buf[2] = (n_args > 1) ? (uint8_t)mp_obj_get_int(args[1]) : BENTO_SFX_WAVE_SQUARE;
    buf[3] = (n_args > 2) ? (uint8_t)mp_obj_get_int(args[2]) : 100;              /* velocity */
    buf[4] = (uint8_t)(dur & 0xFF);
    buf[5] = (uint8_t)(dur >> 8);
    ui_ipc_send_fire_forget(IPC_CMD_UI_SFX, buf, 6);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(ui_tone_obj, 1, 4, ui_tone);
#endif /* UI_SFX_API_PRESENT */

#if ENABLE_GAME_SPRITES
/*******************************************************************************
 * ui.Sprite(sprite_id, x, y) -> Widget
 * Creates a real pixel-art sprite from the on-board C games by id. Only the
 * integer id crosses the IPC boundary; CM55 resolves it to the compiled
 * descriptor and draws it with the C engine — pixel-identical, no streaming.
 * Returns a Widget so .pos/.show/.hide/.delete work unchanged.
 *******************************************************************************/
static mp_obj_t ui_sprite(size_t n_args, const mp_obj_t *args) {
    ui_ipc_init();
    ui_pause_sensor_auto();
    ipc_ui_sprite_new_t cfg;
    cfg.sprite_id = (uint8_t)mp_obj_get_int(args[0]);
    cfg.x = (int16_t)(n_args > 1 ? mp_obj_get_int(args[1]) : 0);
    cfg.y = (int16_t)(n_args > 2 ? mp_obj_get_int(args[2]) : 0);
    if (!ui_ipc_send_bidir_robust(IPC_CMD_UI_SPRITE_NEW, &cfg, sizeof(cfg))) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("ui: IPC send failed"));
    }
    if (ui_ipc_resp.status == UI_STATUS_TABLE_FULL) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("ui: too many sprites (max 32) - reuse a pool"));
    }
    if (ui_ipc_resp.status != UI_STATUS_OK) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("ui: sprite create failed"));
    }
    ui_widget_obj_t *w = mp_obj_malloc(ui_widget_obj_t, &ui_widget_type);
    w->handle = ui_ipc_resp.data[0];
    w->widget_type = UI_WIDGET_SPRITE;
    w->next_row = 0;
    return MP_OBJ_FROM_PTR(w);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(ui_sprite_obj, 1, 3, ui_sprite);
#endif /* ENABLE_GAME_SPRITES */

/*******************************************************************************
 * Module Definition
 *******************************************************************************/
static const mp_rom_map_elem_t ui_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),  MP_ROM_QSTR(MP_QSTR_ui) },
    /* Factory functions */
    { MP_ROM_QSTR(MP_QSTR_Button),    MP_ROM_PTR(&ui_button_obj) },
    { MP_ROM_QSTR(MP_QSTR_Label),     MP_ROM_PTR(&ui_label_obj) },
    { MP_ROM_QSTR(MP_QSTR_Slider),    MP_ROM_PTR(&ui_slider_obj) },
    { MP_ROM_QSTR(MP_QSTR_Switch),    MP_ROM_PTR(&ui_switch__obj) },
    { MP_ROM_QSTR(MP_QSTR_Checkbox),  MP_ROM_PTR(&ui_checkbox_obj) },
    { MP_ROM_QSTR(MP_QSTR_Arc),       MP_ROM_PTR(&ui_arc_obj) },
    { MP_ROM_QSTR(MP_QSTR_Bar),       MP_ROM_PTR(&ui_bar_obj) },
    { MP_ROM_QSTR(MP_QSTR_Spinner),   MP_ROM_PTR(&ui_spinner_obj) },
    { MP_ROM_QSTR(MP_QSTR_Dropdown),  MP_ROM_PTR(&ui_dropdown_obj) },
    { MP_ROM_QSTR(MP_QSTR_Textarea),  MP_ROM_PTR(&ui_textarea_obj) },
    { MP_ROM_QSTR(MP_QSTR_Seg7),      MP_ROM_PTR(&ui_seg7_obj) },
    { MP_ROM_QSTR(MP_QSTR_DotMatrix), MP_ROM_PTR(&ui_dotmatrix_obj) },
    { MP_ROM_QSTR(MP_QSTR_Chart),     MP_ROM_PTR(&ui_chart_obj) },
    { MP_ROM_QSTR(MP_QSTR_Image),     MP_ROM_PTR(&ui_image_obj) },
    { MP_ROM_QSTR(MP_QSTR_Panel),    MP_ROM_PTR(&ui_panel_obj) },
    { MP_ROM_QSTR(MP_QSTR_Compass),  MP_ROM_PTR(&ui_compass_obj) },
    { MP_ROM_QSTR(MP_QSTR_Led),      MP_ROM_PTR(&ui_led_obj) },
    { MP_ROM_QSTR(MP_QSTR_Scale),    MP_ROM_PTR(&ui_scale_obj) },
    { MP_ROM_QSTR(MP_QSTR_Spinbox),  MP_ROM_PTR(&ui_spinbox_obj) },
    { MP_ROM_QSTR(MP_QSTR_Table),        MP_ROM_PTR(&ui_table_obj) },
    { MP_ROM_QSTR(MP_QSTR_List),         MP_ROM_PTR(&ui_list__obj) },
    { MP_ROM_QSTR(MP_QSTR_Roller),       MP_ROM_PTR(&ui_roller_obj) },
    { MP_ROM_QSTR(MP_QSTR_ButtonMatrix), MP_ROM_PTR(&ui_btnmatrix_obj) },
    { MP_ROM_QSTR(MP_QSTR_MsgBox),       MP_ROM_PTR(&ui_msgbox_obj) },
    { MP_ROM_QSTR(MP_QSTR_Keyboard),     MP_ROM_PTR(&ui_keyboard_obj) },
    { MP_ROM_QSTR(MP_QSTR_Tabview),      MP_ROM_PTR(&ui_tabview_obj) },
    { MP_ROM_QSTR(MP_QSTR_Tileview),     MP_ROM_PTR(&ui_tileview_obj) },
    { MP_ROM_QSTR(MP_QSTR_Win),          MP_ROM_PTR(&ui_win_obj) },
    { MP_ROM_QSTR(MP_QSTR_Line),         MP_ROM_PTR(&ui_line_obj) },
    { MP_ROM_QSTR(MP_QSTR_Picture),      MP_ROM_PTR(&ui_picture_obj) },
    { MP_ROM_QSTR(MP_QSTR_Menu),         MP_ROM_PTR(&ui_menu_obj) },
    /* Rich text. Named after the LVGL widget, like every other factory here;
     * it is one paragraph made of differently styled runs. */
    { MP_ROM_QSTR(MP_QSTR_SpanGroup),    MP_ROM_PTR(&ui_spangroup_obj) },
    { MP_ROM_QSTR(MP_QSTR_Calendar),     MP_ROM_PTR(&ui_calendar_obj) },
    /* Property ids for .prop(id, value). The named methods above cover the
     * three whose value is a packed pair; everything here takes a plain int. */
    { MP_ROM_QSTR(MP_QSTR_PROP_SCALE_TICKS),    MP_ROM_INT(UI_PROP_SCALE_TOTAL_TICKS) },
    { MP_ROM_QSTR(MP_QSTR_PROP_SCALE_MAJOR),    MP_ROM_INT(UI_PROP_SCALE_MAJOR_EVERY) },
    { MP_ROM_QSTR(MP_QSTR_PROP_SCALE_MODE),     MP_ROM_INT(UI_PROP_SCALE_MODE) },
    { MP_ROM_QSTR(MP_QSTR_PROP_SPINBOX_DIGITS), MP_ROM_INT(UI_PROP_SPINBOX_DIGITS) },
    { MP_ROM_QSTR(MP_QSTR_PROP_LED_BRIGHTNESS), MP_ROM_INT(UI_PROP_LED_BRIGHTNESS) },
    { MP_ROM_QSTR(MP_QSTR_PROP_CHART_POINTS),   MP_ROM_INT(UI_PROP_CHART_POINTS) },
    { MP_ROM_QSTR(MP_QSTR_PROP_SCALE_NEEDLE_COLOR), MP_ROM_INT(UI_PROP_SCALE_NEEDLE_COLOR) },
    { MP_ROM_QSTR(MP_QSTR_PROP_SCALE_NEEDLE),   MP_ROM_INT(UI_PROP_SCALE_NEEDLE) },
    { MP_ROM_QSTR(MP_QSTR_PROP_ONE_LINE),       MP_ROM_INT(UI_PROP_TEXTAREA_ONE_LINE) },
    { MP_ROM_QSTR(MP_QSTR_PROP_PASSWORD),       MP_ROM_INT(UI_PROP_TEXTAREA_PASSWORD) },
    { MP_ROM_QSTR(MP_QSTR_PROP_VISIBLE_ROWS),   MP_ROM_INT(UI_PROP_ROLLER_VISIBLE_ROWS) },
    { MP_ROM_QSTR(MP_QSTR_PROP_COL_WIDTH),      MP_ROM_INT(UI_PROP_TABLE_COL_WIDTH) },
    { MP_ROM_QSTR(MP_QSTR_PROP_KEYBOARD_TA),    MP_ROM_INT(UI_PROP_KEYBOARD_TEXTAREA) },
    { MP_ROM_QSTR(MP_QSTR_PROP_ROTATION),       MP_ROM_INT(UI_PROP_IMAGE_ROTATION) },
    { MP_ROM_QSTR(MP_QSTR_PROP_SCALE),          MP_ROM_INT(UI_PROP_IMAGE_SCALE) },
    { MP_ROM_QSTR(MP_QSTR_PROP_PIVOT),          MP_ROM_INT(UI_PROP_IMAGE_PIVOT) },
    { MP_ROM_QSTR(MP_QSTR_PROP_LINE_WIDTH),     MP_ROM_INT(UI_PROP_LINE_WIDTH) },
    { MP_ROM_QSTR(MP_QSTR_PROP_ACTIVE_TAB),     MP_ROM_INT(UI_PROP_TABVIEW_ACTIVE) },
    { MP_ROM_QSTR(MP_QSTR_PROP_CLOSE_BUTTON),   MP_ROM_INT(UI_PROP_MSGBOX_CLOSE_BTN) },
    { MP_ROM_QSTR(MP_QSTR_PROP_MENU_LOAD_PAGE), MP_ROM_INT(UI_PROP_MENU_LOAD_PAGE) },
    { MP_ROM_QSTR(MP_QSTR_PROP_MENU_MAIN),      MP_ROM_INT(UI_PROP_MENU_MAIN_PAGE) },
    { MP_ROM_QSTR(MP_QSTR_PROP_MENU_SIDEBAR),   MP_ROM_INT(UI_PROP_MENU_SIDEBAR_PAGE) },
    { MP_ROM_QSTR(MP_QSTR_PROP_MENU_ROOT_BACK), MP_ROM_INT(UI_PROP_MENU_ROOT_BACK) },
    { MP_ROM_QSTR(MP_QSTR_PROP_SPAN_COLOR),     MP_ROM_INT(UI_PROP_SPAN_COLOR) },
    { MP_ROM_QSTR(MP_QSTR_PROP_SPAN_MODE),      MP_ROM_INT(UI_PROP_SPAN_MODE) },
    { MP_ROM_QSTR(MP_QSTR_PROP_CALENDAR_SHOWN), MP_ROM_INT(UI_PROP_CALENDAR_SHOWN) },
    { MP_ROM_QSTR(MP_QSTR_PROP_CALENDAR_HEADER), MP_ROM_INT(UI_PROP_CALENDAR_HEADER) },
    /* Subscription mask. Exposed for completeness, but .listen("pressed", ...)
     * is what course code uses — a name is checkable, a bit position is not. */
    { MP_ROM_QSTR(MP_QSTR_PROP_EVENT_MASK),     MP_ROM_INT(UI_PROP_EVENT_MASK) },
    /* Swipe direction, the value carried by a 'gesture' event. */
    { MP_ROM_QSTR(MP_QSTR_DIR_NONE),            MP_ROM_INT(UI_DIR_NONE) },
    { MP_ROM_QSTR(MP_QSTR_DIR_LEFT),            MP_ROM_INT(UI_DIR_LEFT) },
    { MP_ROM_QSTR(MP_QSTR_DIR_RIGHT),           MP_ROM_INT(UI_DIR_RIGHT) },
    { MP_ROM_QSTR(MP_QSTR_DIR_TOP),             MP_ROM_INT(UI_DIR_TOP) },
    { MP_ROM_QSTR(MP_QSTR_DIR_BOTTOM),          MP_ROM_INT(UI_DIR_BOTTOM) },
    /* Span run decorations and layout modes */
    { MP_ROM_QSTR(MP_QSTR_SPAN_UNDERLINE),     MP_ROM_INT(UI_SPAN_UNDERLINE) },
    { MP_ROM_QSTR(MP_QSTR_SPAN_STRIKETHROUGH), MP_ROM_INT(UI_SPAN_STRIKETHROUGH) },
    { MP_ROM_QSTR(MP_QSTR_SPAN_MODE_FIXED),    MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_SPAN_MODE_EXPAND),   MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_SPAN_MODE_BREAK),    MP_ROM_INT(2) },
    /* Scale layouts (LV_SCALE_MODE_*) for .prop(ui.PROP_SCALE_MODE, ...) */
    { MP_ROM_QSTR(MP_QSTR_SCALE_TOP),     MP_ROM_INT(0x00) },
    { MP_ROM_QSTR(MP_QSTR_SCALE_BOTTOM),  MP_ROM_INT(0x01) },
    { MP_ROM_QSTR(MP_QSTR_SCALE_LEFT),    MP_ROM_INT(0x02) },
    { MP_ROM_QSTR(MP_QSTR_SCALE_RIGHT),   MP_ROM_INT(0x04) },
    { MP_ROM_QSTR(MP_QSTR_SCALE_ROUND_IN),  MP_ROM_INT(0x08) },
    { MP_ROM_QSTR(MP_QSTR_SCALE_ROUND_OUT), MP_ROM_INT(0x10) },
    /* Swipe directions for .add_tile() (lv_dir_t) */
    { MP_ROM_QSTR(MP_QSTR_DIR_LEFT),   MP_ROM_INT(0x01) },
    { MP_ROM_QSTR(MP_QSTR_DIR_RIGHT),  MP_ROM_INT(0x02) },
    { MP_ROM_QSTR(MP_QSTR_DIR_TOP),    MP_ROM_INT(0x04) },
    { MP_ROM_QSTR(MP_QSTR_DIR_BOTTOM), MP_ROM_INT(0x08) },
    { MP_ROM_QSTR(MP_QSTR_DIR_ALL),    MP_ROM_INT(0x0F) },
    /* List entry icons (ui.List().add_item(text, ui.ICON_WIFI)) */
    { MP_ROM_QSTR(MP_QSTR_ICON_NONE),      MP_ROM_INT(UI_LIST_ICON_NONE) },
    { MP_ROM_QSTR(MP_QSTR_ICON_FILE),      MP_ROM_INT(UI_LIST_ICON_FILE) },
    { MP_ROM_QSTR(MP_QSTR_ICON_DIR),       MP_ROM_INT(UI_LIST_ICON_DIR) },
    { MP_ROM_QSTR(MP_QSTR_ICON_SETTINGS),  MP_ROM_INT(UI_LIST_ICON_SETTINGS) },
    { MP_ROM_QSTR(MP_QSTR_ICON_WIFI),      MP_ROM_INT(UI_LIST_ICON_WIFI) },
    { MP_ROM_QSTR(MP_QSTR_ICON_BATTERY),   MP_ROM_INT(UI_LIST_ICON_BATTERY) },
    { MP_ROM_QSTR(MP_QSTR_ICON_BELL),      MP_ROM_INT(UI_LIST_ICON_BELL) },
    { MP_ROM_QSTR(MP_QSTR_ICON_HOME),      MP_ROM_INT(UI_LIST_ICON_HOME) },
    { MP_ROM_QSTR(MP_QSTR_ICON_OK),        MP_ROM_INT(UI_LIST_ICON_OK) },
    { MP_ROM_QSTR(MP_QSTR_ICON_CLOSE),     MP_ROM_INT(UI_LIST_ICON_CLOSE) },
    { MP_ROM_QSTR(MP_QSTR_ICON_PLAY),      MP_ROM_INT(UI_LIST_ICON_PLAY) },
    { MP_ROM_QSTR(MP_QSTR_ICON_PAUSE),     MP_ROM_INT(UI_LIST_ICON_PAUSE) },
    { MP_ROM_QSTR(MP_QSTR_ICON_EDIT),      MP_ROM_INT(UI_LIST_ICON_EDIT) },
    { MP_ROM_QSTR(MP_QSTR_ICON_TRASH),     MP_ROM_INT(UI_LIST_ICON_TRASH) },
    { MP_ROM_QSTR(MP_QSTR_ICON_POWER),     MP_ROM_INT(UI_LIST_ICON_POWER) },
    { MP_ROM_QSTR(MP_QSTR_ICON_USB),       MP_ROM_INT(UI_LIST_ICON_USB) },
    { MP_ROM_QSTR(MP_QSTR_ICON_BLUETOOTH), MP_ROM_INT(UI_LIST_ICON_BLUETOOTH) },
    { MP_ROM_QSTR(MP_QSTR_ICON_AUDIO),     MP_ROM_INT(UI_LIST_ICON_AUDIO) },
    { MP_ROM_QSTR(MP_QSTR_ICON_IMAGE),     MP_ROM_INT(UI_LIST_ICON_IMAGE) },
    { MP_ROM_QSTR(MP_QSTR_ICON_REFRESH),   MP_ROM_INT(UI_LIST_ICON_REFRESH) },
    { MP_ROM_QSTR(MP_QSTR_ICON_WARNING),   MP_ROM_INT(UI_LIST_ICON_WARNING) },
    /* Utility functions */
    { MP_ROM_QSTR(MP_QSTR_poll),      MP_ROM_PTR(&ui_poll_obj) },
    { MP_ROM_QSTR(MP_QSTR_clear),     MP_ROM_PTR(&ui_clear_obj) },
    { MP_ROM_QSTR(MP_QSTR_list),      MP_ROM_PTR(&ui_list_obj) },
    { MP_ROM_QSTR(MP_QSTR_get),       MP_ROM_PTR(&ui_get_obj) },
    { MP_ROM_QSTR(MP_QSTR_screen),    MP_ROM_PTR(&ui_screen_obj) },
    { MP_ROM_QSTR(MP_QSTR_program),   MP_ROM_PTR(&ui_program_obj) },
    { MP_ROM_QSTR(MP_QSTR__ide_status), MP_ROM_PTR(&ui_ide_status_obj) },
    { MP_ROM_QSTR(MP_QSTR__deploy),    MP_ROM_PTR(&ui_deploy_obj) },
    { MP_ROM_QSTR(MP_QSTR__diag),      MP_ROM_PTR(&ui_diag_obj) },
#if UI_SFX_API_PRESENT
    /* Sound (MicroPython -> C bento_sfx engine over IPC) */
    { MP_ROM_QSTR(MP_QSTR_sfx),       MP_ROM_PTR(&ui_sfx_obj) },
    { MP_ROM_QSTR(MP_QSTR_tone),      MP_ROM_PTR(&ui_tone_obj) },
    /* Sound effect ids (match sfx_id_t in bento_sfx.h) */
    { MP_ROM_QSTR(MP_QSTR_SFX_UI_MOVE),        MP_ROM_INT(BENTO_SFX_UI_MOVE) },
    { MP_ROM_QSTR(MP_QSTR_SFX_UI_SELECT),      MP_ROM_INT(BENTO_SFX_UI_SELECT) },
    { MP_ROM_QSTR(MP_QSTR_SFX_UI_BACK),        MP_ROM_INT(BENTO_SFX_UI_BACK) },
    { MP_ROM_QSTR(MP_QSTR_SFX_UI_DENY),        MP_ROM_INT(BENTO_SFX_UI_DENY) },
    { MP_ROM_QSTR(MP_QSTR_SFX_UI_START),       MP_ROM_INT(BENTO_SFX_UI_START) },
    { MP_ROM_QSTR(MP_QSTR_SFX_SNAKE_EAT),      MP_ROM_INT(BENTO_SFX_SNAKE_EAT) },
    { MP_ROM_QSTR(MP_QSTR_SFX_SNAKE_TURN),     MP_ROM_INT(BENTO_SFX_SNAKE_TURN) },
    { MP_ROM_QSTR(MP_QSTR_SFX_SNAKE_DIE),      MP_ROM_INT(BENTO_SFX_SNAKE_DIE) },
    { MP_ROM_QSTR(MP_QSTR_SFX_FLAPPY_FLAP),    MP_ROM_INT(BENTO_SFX_FLAPPY_FLAP) },
    { MP_ROM_QSTR(MP_QSTR_SFX_FLAPPY_SCORE),   MP_ROM_INT(BENTO_SFX_FLAPPY_SCORE) },
    { MP_ROM_QSTR(MP_QSTR_SFX_FLAPPY_DIE),     MP_ROM_INT(BENTO_SFX_FLAPPY_DIE) },
    { MP_ROM_QSTR(MP_QSTR_SFX_PONG_WALL),      MP_ROM_INT(BENTO_SFX_PONG_WALL) },
    { MP_ROM_QSTR(MP_QSTR_SFX_PONG_PADDLE),    MP_ROM_INT(BENTO_SFX_PONG_PADDLE) },
    { MP_ROM_QSTR(MP_QSTR_SFX_PONG_SCORE),     MP_ROM_INT(BENTO_SFX_PONG_SCORE) },
    { MP_ROM_QSTR(MP_QSTR_SFX_PONG_WIN),       MP_ROM_INT(BENTO_SFX_PONG_WIN) },
    { MP_ROM_QSTR(MP_QSTR_SFX_PONG_LOSE),      MP_ROM_INT(BENTO_SFX_PONG_LOSE) },
    { MP_ROM_QSTR(MP_QSTR_SFX_SHOOT_FIRE),     MP_ROM_INT(BENTO_SFX_SHOOT_FIRE) },
    { MP_ROM_QSTR(MP_QSTR_SFX_SHOOT_HIT),      MP_ROM_INT(BENTO_SFX_SHOOT_HIT) },
    { MP_ROM_QSTR(MP_QSTR_SFX_SHOOT_EXPLODE),  MP_ROM_INT(BENTO_SFX_SHOOT_EXPLODE) },
    { MP_ROM_QSTR(MP_QSTR_SFX_SHOOT_LOSE_LIFE), MP_ROM_INT(BENTO_SFX_SHOOT_LOSE_LIFE) },
    { MP_ROM_QSTR(MP_QSTR_SFX_GAME_OVER),      MP_ROM_INT(BENTO_SFX_GAME_OVER) },
    /* Tone waveforms (match sfx_wave_t in bento_sfx.h) */
    { MP_ROM_QSTR(MP_QSTR_WAVE_SINE),          MP_ROM_INT(BENTO_SFX_WAVE_SINE) },
    { MP_ROM_QSTR(MP_QSTR_WAVE_SQUARE),        MP_ROM_INT(BENTO_SFX_WAVE_SQUARE) },
    { MP_ROM_QSTR(MP_QSTR_WAVE_TRIANGLE),      MP_ROM_INT(BENTO_SFX_WAVE_TRIANGLE) },
    { MP_ROM_QSTR(MP_QSTR_WAVE_SAW),           MP_ROM_INT(BENTO_SFX_WAVE_SAW) },
#endif /* UI_SFX_API_PRESENT */
#if ENABLE_GAME_SPRITES
    /* Sprite factory (real C pixel-art by id; see game_sprite_registry.c) */
    { MP_ROM_QSTR(MP_QSTR_Sprite),   MP_ROM_PTR(&ui_sprite_obj) },
    /* Sprite ids (match BENTO_SPR_* / the per-project registry order) */
    { MP_ROM_QSTR(MP_QSTR_SPR_SNAKE_HEAD_R),  MP_ROM_INT(BENTO_SPR_SNAKE_HEAD_R) },
    { MP_ROM_QSTR(MP_QSTR_SPR_SNAKE_HEAD_D),  MP_ROM_INT(BENTO_SPR_SNAKE_HEAD_D) },
    { MP_ROM_QSTR(MP_QSTR_SPR_SNAKE_HEAD_L),  MP_ROM_INT(BENTO_SPR_SNAKE_HEAD_L) },
    { MP_ROM_QSTR(MP_QSTR_SPR_SNAKE_HEAD_U),  MP_ROM_INT(BENTO_SPR_SNAKE_HEAD_U) },
    { MP_ROM_QSTR(MP_QSTR_SPR_SNAKE_BODY),    MP_ROM_INT(BENTO_SPR_SNAKE_BODY) },
    { MP_ROM_QSTR(MP_QSTR_SPR_SNAKE_FOOD),    MP_ROM_INT(BENTO_SPR_SNAKE_FOOD) },
    { MP_ROM_QSTR(MP_QSTR_SPR_FLAPPY_BIRD),   MP_ROM_INT(BENTO_SPR_FLAPPY_BIRD) },
    { MP_ROM_QSTR(MP_QSTR_SPR_FLAPPY_PIPE_CAP), MP_ROM_INT(BENTO_SPR_FLAPPY_PIPE_CAP) },
    { MP_ROM_QSTR(MP_QSTR_SPR_PONG_BALL),     MP_ROM_INT(BENTO_SPR_PONG_BALL) },
    { MP_ROM_QSTR(MP_QSTR_SPR_PONG_PADDLE),   MP_ROM_INT(BENTO_SPR_PONG_PADDLE) },
    { MP_ROM_QSTR(MP_QSTR_SPR_SHOOTER_SHIP),  MP_ROM_INT(BENTO_SPR_SHOOTER_SHIP) },
    { MP_ROM_QSTR(MP_QSTR_SPR_SHOOTER_ENEMY), MP_ROM_INT(BENTO_SPR_SHOOTER_ENEMY) },
    { MP_ROM_QSTR(MP_QSTR_SPR_SHOOTER_ENEMY2), MP_ROM_INT(BENTO_SPR_SHOOTER_ENEMY2) },
    { MP_ROM_QSTR(MP_QSTR_SPR_SHOOTER_ENEMY3), MP_ROM_INT(BENTO_SPR_SHOOTER_ENEMY3) },
    { MP_ROM_QSTR(MP_QSTR_SPR_SHOOTER_BOOM_P1), MP_ROM_INT(BENTO_SPR_SHOOTER_BOOM_P1) },
    { MP_ROM_QSTR(MP_QSTR_SPR_SHOOTER_BOOM_P2), MP_ROM_INT(BENTO_SPR_SHOOTER_BOOM_P2) },
    { MP_ROM_QSTR(MP_QSTR_SPR_SHOOTER_BOOM_C1), MP_ROM_INT(BENTO_SPR_SHOOTER_BOOM_C1) },
    { MP_ROM_QSTR(MP_QSTR_SPR_SHOOTER_BOOM_C2), MP_ROM_INT(BENTO_SPR_SHOOTER_BOOM_C2) },
    { MP_ROM_QSTR(MP_QSTR_SPR_SHOOTER_BOOM_F1), MP_ROM_INT(BENTO_SPR_SHOOTER_BOOM_F1) },
    { MP_ROM_QSTR(MP_QSTR_SPR_SHOOTER_BOOM_F2), MP_ROM_INT(BENTO_SPR_SHOOTER_BOOM_F2) },
#endif /* ENABLE_GAME_SPRITES */
    /* Widget type */
    { MP_ROM_QSTR(MP_QSTR_Widget),    MP_ROM_PTR(&ui_widget_type) },
};
static MP_DEFINE_CONST_DICT(ui_module_globals, ui_module_globals_table);

const mp_obj_module_t mp_module_ui = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&ui_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_ui, mp_module_ui);

/*******************************************************************************
 * ui_notify_ide_connected() — Called from tacp.c on TACP_CMD_IDE_STATUS.
 * Sends fire-and-forget IPC to CM55 to show/hide IDE connection icon.
 * Non-blocking: skips silently if CM55 is not ready (no probe wait).
 *******************************************************************************/
void ui_notify_ide_connected(bool connected) {
    ui_ipc_setup_once();
    if (!ui_cm55_ready) return;  /* CM55 not ready — skip silently */

    uint8_t buf[1] = { connected ? 1 : 0 };
    if (!ui_ipc_send_fire_forget(IPC_CMD_UI_IDE_STATUS, buf, 1)) {
        ui_cm55_ready = false;
    }
}

/*******************************************************************************
 * ui_auto_clear_on_reset() — Called from mpy_main.c on every soft reset
 * Clears all LVGL widgets so display is clean before running user code.
 *******************************************************************************/
void ui_auto_clear_on_reset(void) {
    /* Always send CLEAR_ALL regardless of ui_cm55_ready.
     * ui_show_deploy_screen() sends fire-and-forget without checking
     * ui_cm55_ready and it works.  The old code skipped CLEAR_ALL when
     * ui_cm55_ready was false (which is ALWAYS the case at boot since
     * nobody calls ui_ipc_init()).  This caused the deploy screen
     * overlay to stay stuck forever after PROGRAM_MODE safe boot. */
    ui_ipc_setup_once();
    ui_ipc_send_fire_forget(IPC_CMD_UI_CLEAR_ALL, NULL, 0);

    /* Resume background sensor auto-push if we paused it.
     * Script has ended (soft reset) — Dashboard/Motion need live data. */
    if (ui_sensor_auto_paused) {
        sensor_auto_start();
        ui_sensor_auto_paused = false;
    }
}

/*******************************************************************************
 * ui_show_deploy_screen() — Called from mpy_main.c in BOOT_MODE_SAFE_DEPLOY.
 * Sends IPC command to CM55 which creates a native full-screen overlay
 * (matching Delete confirmation style). Replaces Python deploy_screen_script.
 *******************************************************************************/
void ui_show_deploy_screen(void) {
    /* Fire-and-forget without blocking on CM55 probe.
     * On warm soft reset (PROGRAM_MODE), CM55 is already running — send works.
     * On cold boot, CM55 may not be ready — send fails silently, no harm. */
    ui_ipc_setup_once();
    ui_ipc_send_fire_forget(IPC_CMD_UI_DEPLOY_SCREEN, NULL, 0);
}
