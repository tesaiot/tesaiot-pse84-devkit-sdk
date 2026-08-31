/*******************************************************************************
 * File Name: mod_qwa309_buttons.c
 *
 * Description: MicroPython 'buttons' module for CM33_NS (QWA309 base board).
 *
 *              Two active-low tactile buttons with internal pull-ups,
 *              named by the PRODUCTION silkscreen (per the professor,
 *              2026-08-20 — older docs call the same nets SW9/SW10 and the
 *              native GPIO & RGB page calls them SW5/SW6):
 *                  SW4 = P17.5   (index 0)
 *                  SW5 = P17.7   (index 1)
 *
 *              Mirrors qwa309-training-base/fw/examples/button_monitor:
 *              internal pull-up + active-low read + 50 ms debounce. That
 *              example's README carries the two caveats that apply here too:
 *              (1) released inputs float without a pull-up (R43/R44 may be
 *              unassembled) — hence the MCU pull-ups; (2) these nets are
 *              shared with DVP camera signals AND P17.5 doubles as the USB
 *              host VBUS enable (usbh_config.c) — using the buttons and the
 *              USB joystick at the same time is NOT supported on this base:
 *              first buttons_hw_init() takes P17.5 for input. Accepted
 *              tradeoff by the professor's direction, 2026-08-20.
 *
 *              Pure-code GPIO bring-up (Cy_GPIO_Pin_FastInit, pull-up), no
 *              Device Configurator changes. Adapted from the tested
 *              button_monitor example (reference/qwa309-training-base).
 *
 *              A raw read is debounced in software (50 ms stable window) so
 *              buttons.pressed(n) reports a clean, bounce-free state.
 *
 * Usage:
 *   import buttons
 *   sw4, sw5 = buttons.read()    # -> (bool, bool), True = pressed
 *   if buttons.pressed(0): ...   # SW4
 *   if buttons.pressed(1): ...   # SW5
 *
 * Gated by BSP_HAS_QWA309_BASEBOARD.
 *******************************************************************************/

#include "bsp_feature_flags.h"

#if BSP_HAS_QWA309_BASEBOARD

#include "py/runtime.h"
#include "py/obj.h"
#include "py/mphal.h"

#include "cy_pdl.h"
#include "gpio_pse84_bga_220.h"

#include <string.h>

#define BTN_COUNT            (2U)
#define BTN_DEBOUNCE_MS      (50U)

typedef struct {
    GPIO_PRT_Type *port;
    uint32_t       pin;
    const char    *name;
    /* Debounce state */
    bool     stable_pressed;
    bool     last_raw_pressed;
    uint32_t last_change_ms;
} qwa309_btn_t;

static qwa309_btn_t s_btns[BTN_COUNT] = {
    { P17_5_PORT, P17_5_PIN, "SW4", false, false, 0U },
    { P17_7_PORT, P17_7_PIN, "SW5", false, false, 0U },
};

static bool s_initialized = false;

static void buttons_hw_init(void)
{
    if (s_initialized) {
        return;
    }
    for (uint32_t i = 0U; i < BTN_COUNT; i++) {
        Cy_GPIO_Pin_FastInit(s_btns[i].port, s_btns[i].pin,
                             CY_GPIO_DM_PULLUP, 1UL, HSIOM_SEL_GPIO);
        s_btns[i].stable_pressed   = false;
        s_btns[i].last_raw_pressed = false;
        s_btns[i].last_change_ms   = mp_hal_ticks_ms();
    }
    s_initialized = true;
}

/* Sample one channel, apply 50 ms debounce, return the stable pressed state. */
static bool buttons_sample(uint32_t idx)
{
    /* Active low: Cy_GPIO_Read() == 0 means pressed. */
    bool raw = (Cy_GPIO_Read(s_btns[idx].port, s_btns[idx].pin) == 0U);
    uint32_t now = mp_hal_ticks_ms();

    if (raw != s_btns[idx].last_raw_pressed) {
        s_btns[idx].last_raw_pressed = raw;
        s_btns[idx].last_change_ms   = now;
    } else if ((now - s_btns[idx].last_change_ms) >= BTN_DEBOUNCE_MS) {
        s_btns[idx].stable_pressed = raw;
    }
    return s_btns[idx].stable_pressed;
}

/* buttons.read() -> (bool sw9, bool sw10) */
static mp_obj_t mod_buttons_read(void)
{
    buttons_hw_init();
    mp_obj_t tuple[BTN_COUNT];
    for (uint32_t i = 0U; i < BTN_COUNT; i++) {
        tuple[i] = mp_obj_new_bool(buttons_sample(i));
    }
    return mp_obj_new_tuple(BTN_COUNT, tuple);
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_buttons_read_obj, mod_buttons_read);

/* buttons.pressed(n) -> bool  (n = 0 for SW9, 1 for SW10) */
static mp_obj_t mod_buttons_pressed(mp_obj_t idx_obj)
{
    buttons_hw_init();
    int idx = mp_obj_get_int(idx_obj);
    if (idx < 0 || idx >= (int)BTN_COUNT) {
        mp_raise_msg_varg(&mp_type_ValueError,
            MP_ERROR_TEXT("button index %d out of range (0-%d)"),
            idx, BTN_COUNT - 1);
    }
    return mp_obj_new_bool(buttons_sample((uint32_t)idx));
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_buttons_pressed_obj, mod_buttons_pressed);

/* buttons.name(n) -> str */
static mp_obj_t mod_buttons_name(mp_obj_t idx_obj)
{
    int idx = mp_obj_get_int(idx_obj);
    if (idx < 0 || idx >= (int)BTN_COUNT) {
        mp_raise_msg_varg(&mp_type_ValueError,
            MP_ERROR_TEXT("button index %d out of range (0-%d)"),
            idx, BTN_COUNT - 1);
    }
    return mp_obj_new_str(s_btns[idx].name, strlen(s_btns[idx].name));
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_buttons_name_obj, mod_buttons_name);

/* buttons.count() -> int */
static mp_obj_t mod_buttons_count(void)
{
    return mp_obj_new_int(BTN_COUNT);
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_buttons_count_obj, mod_buttons_count);

static const mp_rom_map_elem_t buttons_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_buttons) },
    { MP_ROM_QSTR(MP_QSTR_read),     MP_ROM_PTR(&mod_buttons_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_pressed),  MP_ROM_PTR(&mod_buttons_pressed_obj) },
    { MP_ROM_QSTR(MP_QSTR_name),     MP_ROM_PTR(&mod_buttons_name_obj) },
    { MP_ROM_QSTR(MP_QSTR_count),    MP_ROM_PTR(&mod_buttons_count_obj) },
};
static MP_DEFINE_CONST_DICT(buttons_module_globals, buttons_module_globals_table);

const mp_obj_module_t mp_module_buttons = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&buttons_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_buttons, mp_module_buttons);

#endif /* BSP_HAS_QWA309_BASEBOARD */
