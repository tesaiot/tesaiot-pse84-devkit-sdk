/*******************************************************************************
 * File Name: modsensors_capsense.c
 *
 * Description: MicroPython CapSense sub-module for the 'sensors' module.
 *              Provides: sensors.capsense.read(), .buttons(), .slider()
 *
 *              Only compiled when BSP_HAS_CAPSENSE=1.
 *
 *******************************************************************************/

#include "bsp_feature_flags.h"

#if BSP_HAS_CAPSENSE

#include "py/runtime.h"
#include "py/obj.h"
#include "sensor_capsense.h"

#ifdef USE_KIT_PSE84_EVAL_EPC2
/* The CapSense co-processor sits at 0x08 on SCB0, and on Eva Kit SCB0 belongs
 * to the display core. Reading it from here put a second master on a live bus
 * and wedged the board until someone attached a debugger. CM55 already polls
 * this chip every 200 ms for its own Controls page, so ask it instead.
 * Defined in modsensors.c. */
bool eva_snapshot_capsense(uint8_t *b0, uint8_t *b1, uint8_t *slider);

static void eva_capsense_or_raise(uint8_t *b0, uint8_t *b1, uint8_t *slider) {
    if (!eva_snapshot_capsense(b0, b1, slider)) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT(
            "CapSense: CM55 did not answer. On Eva Kit these readings come "
            "from the display core's snapshot, and it is not replying"));
    }
}
#endif

/* sensors.capsense.read() -> dict {btn0: bool, btn1: bool, slider: int} */
static mp_obj_t capsense_read_func(void) {
    capsense_data_t data;
#ifdef USE_KIT_PSE84_EVAL_EPC2
    uint8_t b0 = 0, b1 = 0, sl = 0;
    eva_capsense_or_raise(&b0, &b1, &sl);
    data.btn0_pressed = b0;
    data.btn1_pressed = b1;
    data.slider = sl;
#else
    if (!capsense_read(&data)) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("CapSense read failed"));
    }
#endif
    mp_obj_dict_t *result = MP_OBJ_TO_PTR(mp_obj_new_dict(3));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(result),
                      MP_OBJ_NEW_QSTR(MP_QSTR_btn0),
                      mp_obj_new_bool(data.btn0_pressed));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(result),
                      MP_OBJ_NEW_QSTR(MP_QSTR_btn1),
                      mp_obj_new_bool(data.btn1_pressed));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(result),
                      MP_OBJ_NEW_QSTR(MP_QSTR_slider),
                      mp_obj_new_int(data.slider));
    return MP_OBJ_FROM_PTR(result);
}
static MP_DEFINE_CONST_FUN_OBJ_0(capsense_read_obj, capsense_read_func);

/* sensors.capsense.buttons() -> (btn0: bool, btn1: bool) */
static mp_obj_t capsense_buttons_func(void) {
    bool btn0, btn1;
#ifdef USE_KIT_PSE84_EVAL_EPC2
    uint8_t b0 = 0, b1 = 0;
    eva_capsense_or_raise(&b0, &b1, NULL);
    btn0 = (b0 != 0);
    btn1 = (b1 != 0);
#else
    if (!capsense_read_buttons(&btn0, &btn1)) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("CapSense read failed"));
    }
#endif
    mp_obj_t items[2] = {
        mp_obj_new_bool(btn0), mp_obj_new_bool(btn1)
    };
    return mp_obj_new_tuple(2, items);
}
static MP_DEFINE_CONST_FUN_OBJ_0(capsense_buttons_obj, capsense_buttons_func);

/* sensors.capsense.slider() -> int (0-100) */
static mp_obj_t capsense_slider_func(void) {
    uint8_t pos;
#ifdef USE_KIT_PSE84_EVAL_EPC2
    eva_capsense_or_raise(NULL, NULL, &pos);
#else
    if (!capsense_read_slider(&pos)) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("CapSense read failed"));
    }
#endif
    return mp_obj_new_int(pos);
}
static MP_DEFINE_CONST_FUN_OBJ_0(capsense_slider_obj, capsense_slider_func);

static const mp_rom_map_elem_t capsense_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),   MP_ROM_QSTR(MP_QSTR_capsense) },
    { MP_ROM_QSTR(MP_QSTR_read),        MP_ROM_PTR(&capsense_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_buttons),     MP_ROM_PTR(&capsense_buttons_obj) },
    { MP_ROM_QSTR(MP_QSTR_slider),      MP_ROM_PTR(&capsense_slider_obj) },
};
static MP_DEFINE_CONST_DICT(capsense_module_globals, capsense_module_globals_table);

const mp_obj_module_t mp_module_capsense = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&capsense_module_globals,
};

#endif /* BSP_HAS_CAPSENSE */
