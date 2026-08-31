/*******************************************************************************
 * File Name: modsensors_sht40.c
 *
 * Description: MicroPython SHT40 sub-module for the 'sensors' module.
 *              Provides: sensors.sht40.temperature(), .humidity(),
 *                        .temperature_humidity()
 *
 *              Only compiled when BSP_HAS_SHT40=1 (AI Dev Kit).
 *
 *******************************************************************************/

#include "bsp_feature_flags.h"

#if BSP_HAS_SHT40

#include "py/runtime.h"
#include "py/obj.h"
#include "sensor_sht40.h"

/* sensors.sht40.temperature() -> float (Celsius) */
static mp_obj_t sht40_temperature(void) {
    float temp;
    if (!sht40_read_temperature(&temp)) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("SHT40 temp read failed"));
    }
    return mp_obj_new_float(temp);
}
static MP_DEFINE_CONST_FUN_OBJ_0(sht40_temperature_obj, sht40_temperature);

/* sensors.sht40.humidity() -> float (%RH) */
static mp_obj_t sht40_humidity(void) {
    float hum;
    if (!sht40_read_humidity(&hum)) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("SHT40 humidity read failed"));
    }
    return mp_obj_new_float(hum);
}
static MP_DEFINE_CONST_FUN_OBJ_0(sht40_humidity_obj, sht40_humidity);

/* sensors.sht40.temperature_humidity() -> (temperature, humidity) */
static mp_obj_t sht40_temperature_humidity(void) {
    float temperature, humidity;
    if (!sht40_read_both(&temperature, &humidity)) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("SHT40 read failed"));
    }
    mp_obj_t items[2] = {
        mp_obj_new_float(temperature), mp_obj_new_float(humidity)
    };
    return mp_obj_new_tuple(2, items);
}
static MP_DEFINE_CONST_FUN_OBJ_0(sht40_temperature_humidity_obj, sht40_temperature_humidity);

static const mp_rom_map_elem_t sht40_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),              MP_ROM_QSTR(MP_QSTR_sht40) },
    { MP_ROM_QSTR(MP_QSTR_temperature),            MP_ROM_PTR(&sht40_temperature_obj) },
    { MP_ROM_QSTR(MP_QSTR_humidity),               MP_ROM_PTR(&sht40_humidity_obj) },
    { MP_ROM_QSTR(MP_QSTR_temperature_humidity),   MP_ROM_PTR(&sht40_temperature_humidity_obj) },
};
static MP_DEFINE_CONST_DICT(sht40_module_globals, sht40_module_globals_table);

const mp_obj_module_t mp_module_sht40 = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&sht40_module_globals,
};

#endif /* BSP_HAS_SHT40 */
