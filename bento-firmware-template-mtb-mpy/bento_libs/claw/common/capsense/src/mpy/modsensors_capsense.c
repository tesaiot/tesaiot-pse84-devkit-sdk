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

/* sensors.capsense.read() -> dict {btn0: bool, btn1: bool, slider: int} */
static mp_obj_t capsense_read_func(void) {
    capsense_data_t data;
    if (!capsense_read(&data)) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("CapSense read failed"));
    }
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
    if (!capsense_read_buttons(&btn0, &btn1)) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("CapSense read failed"));
    }
    mp_obj_t items[2] = {
        mp_obj_new_bool(btn0), mp_obj_new_bool(btn1)
    };
    return mp_obj_new_tuple(2, items);
}
static MP_DEFINE_CONST_FUN_OBJ_0(capsense_buttons_obj, capsense_buttons_func);

/* sensors.capsense.slider() -> int (0-100) */
static mp_obj_t capsense_slider_func(void) {
    uint8_t pos;
    if (!capsense_read_slider(&pos)) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("CapSense read failed"));
    }
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
