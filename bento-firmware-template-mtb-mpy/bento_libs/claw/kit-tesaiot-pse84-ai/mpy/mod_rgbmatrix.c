/*******************************************************************************
 * File Name: mod_rgbmatrix.c
 *
 * Description: MicroPython 'rgbmatrix' module for CM33_NS (QWA309 base board).
 *
 *              Drives the DFRobot DFR0522 16x8 RGB LED matrix at I2C 0x10.
 *              That panel lives on the HEADER I2C bus (P17.0/P17.1), which on
 *              the AI Kit is the DISPLAY / touch SCB owned by CM55. So the
 *              MicroPython module (CM33_NS) does NOT touch I2C directly: it
 *              packs a small RGB sub-op into an IPC UI-band message
 *              (IPC_CMD_UI_RGB_MATRIX) and fires it to CM55, where the GFX
 *              task (owner of the display I2C) performs the actual transaction
 *              via the dfr0522_rgb driver. Fire-and-forget — no response.
 *
 *              This mirrors the modui.c IPC send pattern but is self-contained
 *              (its own shared-mem message buffer + client id) so it does not
 *              depend on modui internals.
 *
 * Usage:
 *   import rgbmatrix
 *   rgbmatrix.clear()               # all pixels off
 *   rgbmatrix.fill(rgbmatrix.RED)   # solid color 1..7
 *   rgbmatrix.pixel(3, 2, rgbmatrix.CYAN)  # x 0..15, y 0..7, color 0..7
 *
 * Color constants: OFF RED GREEN YELLOW BLUE PURPLE CYAN WHITE (0..7)
 *
 * Gated by BSP_HAS_QWA309_BASEBOARD.
 *******************************************************************************/

#include "bsp_feature_flags.h"

#if BSP_HAS_QWA309_BASEBOARD

#include "py/runtime.h"
#include "py/obj.h"

#include "cy_pdl.h"
#include "ipc_communication.h"
#include "ipc_ui_protocol.h"

/* Anti-copy-paste guard: this file hard-codes canonical opcode-map v2 behavior. */
_Static_assert(IPC_UI_OPMAP_VERSION == 2, "wrong tree's ipc_ui_protocol.h (opcode map mismatch)");

#include <string.h>

#define RGB_WIDTH        (16)
#define RGB_HEIGHT       (8)
#define RGB_COLOR_MAX    (7)

#define RGB_IPC_SEND_RETRIES   (100)
#define RGB_IPC_RETRY_DELAY_US (100)

/* Dedicated shared-mem message buffer (do not reuse modui's). */
CY_SECTION_SHAREDMEM static ipc_msg_t s_rgb_ipc_msg;

static bool s_rgb_ipc_ready = false;

static inline void rgb_ipc_setup_once(void)
{
    if (!s_rgb_ipc_ready) {
        cm33_ipc_communication_setup();
        s_rgb_ipc_ready = true;
    }
}

/* Fire-and-forget an arbitrary RGB sub-op payload to CM55.
 * Returns true if the pipe accepted it. */
static bool rgb_ipc_send_payload(const void *payload, size_t payload_len)
{
    rgb_ipc_setup_once();

    if (payload_len > IPC_DATA_MAX_LEN) {
        return false;
    }

    memset(&s_rgb_ipc_msg, 0, sizeof(s_rgb_ipc_msg));
    s_rgb_ipc_msg.client_id = CM55_IPC_UI_CLIENT_ID;
    s_rgb_ipc_msg.intr_mask = CY_IPC_CYPIPE_INTR_MASK_EP1;
    s_rgb_ipc_msg.cmd       = IPC_CMD_UI_RGB_MATRIX;
    s_rgb_ipc_msg.value     = 0;
    memcpy(s_rgb_ipc_msg.data, payload, payload_len);

    int retries = RGB_IPC_SEND_RETRIES;
    do {
        cy_en_ipc_pipe_status_t st = Cy_IPC_Pipe_SendMessage(
            CM55_IPC_PIPE_EP_ADDR, CM33_IPC_PIPE_EP_ADDR,
            (void *)&s_rgb_ipc_msg, NULL);
        if (CY_IPC_PIPE_SUCCESS == st) {
            return true;
        }
        Cy_SysLib_DelayUs(RGB_IPC_RETRY_DELAY_US);
    } while (--retries > 0);

    return false;
}

static bool rgb_ipc_send(uint8_t op, uint8_t color, uint8_t x, uint8_t y)
{
    ipc_ui_rgb_matrix_t payload;
    payload.op    = op;
    payload.color = color;
    payload.x     = x;
    payload.y     = y;
    return rgb_ipc_send_payload(&payload, sizeof(payload));
}

static void rgb_raise_if_failed(bool ok)
{
    if (!ok) {
        mp_raise_msg(&mp_type_OSError,
            MP_ERROR_TEXT("rgbmatrix: IPC to CM55 display core failed"));
    }
}

/* rgbmatrix.clear() -> None */
static mp_obj_t mod_rgb_clear(void)
{
    rgb_raise_if_failed(rgb_ipc_send(IPC_RGB_OP_CLEAR, 0U, 0U, 0U));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_rgb_clear_obj, mod_rgb_clear);

/* rgbmatrix.fill(color) -> None  (color 1..7) */
static mp_obj_t mod_rgb_fill(mp_obj_t color_obj)
{
    int color = mp_obj_get_int(color_obj);
    if (color < 1 || color > RGB_COLOR_MAX) {
        mp_raise_msg_varg(&mp_type_ValueError,
            MP_ERROR_TEXT("color must be 1..%d"), RGB_COLOR_MAX);
    }
    rgb_raise_if_failed(rgb_ipc_send(IPC_RGB_OP_FILL, (uint8_t)color, 0U, 0U));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_rgb_fill_obj, mod_rgb_fill);

/* rgbmatrix.pixel(x, y, color) -> None */
static mp_obj_t mod_rgb_pixel(mp_obj_t x_obj, mp_obj_t y_obj, mp_obj_t color_obj)
{
    int x = mp_obj_get_int(x_obj);
    int y = mp_obj_get_int(y_obj);
    int color = mp_obj_get_int(color_obj);

    if (x < 0 || x >= RGB_WIDTH) {
        mp_raise_msg_varg(&mp_type_ValueError,
            MP_ERROR_TEXT("x must be 0..%d"), RGB_WIDTH - 1);
    }
    if (y < 0 || y >= RGB_HEIGHT) {
        mp_raise_msg_varg(&mp_type_ValueError,
            MP_ERROR_TEXT("y must be 0..%d"), RGB_HEIGHT - 1);
    }
    if (color < 0 || color > RGB_COLOR_MAX) {
        mp_raise_msg_varg(&mp_type_ValueError,
            MP_ERROR_TEXT("color must be 0..%d"), RGB_COLOR_MAX);
    }

    rgb_raise_if_failed(
        rgb_ipc_send(IPC_RGB_OP_PIXEL, (uint8_t)color, (uint8_t)x, (uint8_t)y));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(mod_rgb_pixel_obj, mod_rgb_pixel);

/* rgbmatrix.blit(buf) -> None
 * buf = 64-byte 4bpp packed frame (bytes/bytearray): byte = buf[y*8 + x//2],
 * even x = low nibble. ONE IPC message per frame; CM55 shadow-diffs so only
 * changed pixels hit the wire — the game-animation path. */
static mp_obj_t mod_rgb_blit(mp_obj_t buf_obj)
{
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_obj, &bufinfo, MP_BUFFER_READ);
    if (bufinfo.len != IPC_RGB_BLIT_FRAME_LEN) {
        mp_raise_msg_varg(&mp_type_ValueError,
            MP_ERROR_TEXT("blit needs exactly %d bytes (16x8 px, 4bpp)"),
            (int)IPC_RGB_BLIT_FRAME_LEN);
    }
    ipc_ui_rgb_blit_t payload;
    memset(&payload, 0, sizeof(payload));
    payload.op = IPC_RGB_OP_BLIT;
    memcpy(payload.frame, bufinfo.buf, IPC_RGB_BLIT_FRAME_LEN);
    rgb_raise_if_failed(rgb_ipc_send_payload(&payload, sizeof(payload)));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_rgb_blit_obj, mod_rgb_blit);

/* rgbmatrix.score(value, color=RED) -> None  (0..9999, 3x5 digits C-side) */
static mp_obj_t mod_rgb_score(size_t n_args, const mp_obj_t *args)
{
    int value = mp_obj_get_int(args[0]);
    int color = (n_args > 1) ? mp_obj_get_int(args[1]) : 1 /* RED */;
    if (value < 0) value = 0;
    if (value > 9999) value = 9999;
    if (color < 1 || color > RGB_COLOR_MAX) {
        mp_raise_msg_varg(&mp_type_ValueError,
            MP_ERROR_TEXT("color must be 1..%d"), RGB_COLOR_MAX);
    }
    ipc_ui_rgb_score_t payload;
    payload.op    = IPC_RGB_OP_SCORE;
    payload.color = (uint8_t)color;
    payload.value = (uint16_t)value;
    rgb_raise_if_failed(rgb_ipc_send_payload(&payload, sizeof(payload)));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_rgb_score_obj, 1, 2, mod_rgb_score);

/* rgbmatrix.bar(value, max=5, color=GREEN) -> None  (bottom-row life/energy bar) */
static mp_obj_t mod_rgb_bar(size_t n_args, const mp_obj_t *args)
{
    int value = mp_obj_get_int(args[0]);
    int maxv  = (n_args > 1) ? mp_obj_get_int(args[1]) : 5;
    int color = (n_args > 2) ? mp_obj_get_int(args[2]) : 2 /* GREEN */;
    if (maxv < 1 || maxv > 255) {
        mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("max must be 1..255"));
    }
    if (value < 0) value = 0;
    if (value > maxv) value = maxv;
    if (color < 1 || color > RGB_COLOR_MAX) {
        mp_raise_msg_varg(&mp_type_ValueError,
            MP_ERROR_TEXT("color must be 1..%d"), RGB_COLOR_MAX);
    }
    ipc_ui_rgb_bar_t payload;
    payload.op    = IPC_RGB_OP_BAR;
    payload.color = (uint8_t)color;
    payload.value = (uint8_t)value;
    payload.max   = (uint8_t)maxv;
    rgb_raise_if_failed(rgb_ipc_send_payload(&payload, sizeof(payload)));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_rgb_bar_obj, 1, 3, mod_rgb_bar);

/* rgbmatrix.scroll(text, color=WHITE, ms=80) -> None
 * C-side marquee on a CM55 timer — Python sends the string ONCE, the scroll
 * keeps running even while the game loop is busy. scroll("") stops it. */
static mp_obj_t mod_rgb_scroll(size_t n_args, const mp_obj_t *args)
{
    size_t text_len = 0;
    const char *text = mp_obj_str_get_data(args[0], &text_len);
    int color = (n_args > 1) ? mp_obj_get_int(args[1]) : 7 /* WHITE */;
    int ms    = (n_args > 2) ? mp_obj_get_int(args[2]) : 80;
    if (color < 1 || color > RGB_COLOR_MAX) {
        mp_raise_msg_varg(&mp_type_ValueError,
            MP_ERROR_TEXT("color must be 1..%d"), RGB_COLOR_MAX);
    }
    /* Floor 40 ms: each marquee step diff-flushes up to ~30 glyph pixels
     * (~36 ms of display-bus I2C in the GFX task) — a faster period would
     * starve LVGL/touch permanently from one Python call. */
    if (ms < 40) ms = 40;
    if (ms > 2550) ms = 2550;
    if (text_len > IPC_RGB_SCROLL_TEXT_MAX) {
        text_len = IPC_RGB_SCROLL_TEXT_MAX;
    }
    ipc_ui_rgb_scroll_t payload;
    memset(&payload, 0, sizeof(payload));
    payload.op        = IPC_RGB_OP_SCROLL;
    payload.color     = (uint8_t)color;
    payload.period_cs = (uint8_t)(ms / 10);
    payload.len       = (uint8_t)text_len;
    memcpy(payload.text, text, text_len);
    rgb_raise_if_failed(rgb_ipc_send_payload(&payload, sizeof(payload)));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_rgb_scroll_obj, 1, 3, mod_rgb_scroll);

/*******************************************************************************
 * Module definition
 *******************************************************************************/
static const mp_rom_map_elem_t rgbmatrix_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_rgbmatrix) },
    { MP_ROM_QSTR(MP_QSTR_clear),    MP_ROM_PTR(&mod_rgb_clear_obj) },
    { MP_ROM_QSTR(MP_QSTR_fill),     MP_ROM_PTR(&mod_rgb_fill_obj) },
    { MP_ROM_QSTR(MP_QSTR_pixel),    MP_ROM_PTR(&mod_rgb_pixel_obj) },
    { MP_ROM_QSTR(MP_QSTR_blit),     MP_ROM_PTR(&mod_rgb_blit_obj) },
    { MP_ROM_QSTR(MP_QSTR_score),    MP_ROM_PTR(&mod_rgb_score_obj) },
    { MP_ROM_QSTR(MP_QSTR_bar),      MP_ROM_PTR(&mod_rgb_bar_obj) },
    { MP_ROM_QSTR(MP_QSTR_scroll),   MP_ROM_PTR(&mod_rgb_scroll_obj) },
    /* Color constants */
    { MP_ROM_QSTR(MP_QSTR_OFF),      MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_RED),      MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_GREEN),    MP_ROM_INT(2) },
    { MP_ROM_QSTR(MP_QSTR_YELLOW),   MP_ROM_INT(3) },
    { MP_ROM_QSTR(MP_QSTR_BLUE),     MP_ROM_INT(4) },
    { MP_ROM_QSTR(MP_QSTR_PURPLE),   MP_ROM_INT(5) },
    { MP_ROM_QSTR(MP_QSTR_CYAN),     MP_ROM_INT(6) },
    { MP_ROM_QSTR(MP_QSTR_WHITE),    MP_ROM_INT(7) },
};
static MP_DEFINE_CONST_DICT(rgbmatrix_module_globals, rgbmatrix_module_globals_table);

const mp_obj_module_t mp_module_rgbmatrix = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&rgbmatrix_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_rgbmatrix, mp_module_rgbmatrix);

#endif /* BSP_HAS_QWA309_BASEBOARD */
