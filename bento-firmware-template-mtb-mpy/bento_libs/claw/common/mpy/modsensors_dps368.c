/*******************************************************************************
 * File Name: modsensors_dps368.c
 *
 * Description: MicroPython DPS368 sub-module for the 'sensors' module.
 *              Provides: sensors.dps368.pressure(), .temperature(), .altitude()
 *
 *              Only compiled when BSP_HAS_DPS368=1 (AI Dev Kit).
 *
 *******************************************************************************/

#include "bsp_feature_flags.h"

#if BSP_HAS_DPS368

#include "py/runtime.h"
#include "py/obj.h"
#include "sensor_dps368.h"
#include "sensor_i2c.h"

/* SCB0 is shared with sensor_auto_task (which takes sensor_i2c_lock around its
 * own multi-transaction cycles). The MPY-facing reads here must do the same or
 * a user REPL read can interleave mid-sequence with an auto push and corrupt
 * both (torn trigger/poll/read). Lock is taken HERE (not inside sensor_dps368.c)
 * because the auto task already holds it when calling the same functions. */
#define DPS368_MPY_LOCK_TIMEOUT_MS  (100u)

static void dps368_mpy_lock(void) {
    if (!sensor_i2c_lock(DPS368_MPY_LOCK_TIMEOUT_MS)) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("sensor I2C busy"));
    }
}

/* sensors.dps368.pressure() -> float (hPa) */
static mp_obj_t dps368_pressure(void) {
    float pressure;
    dps368_mpy_lock();
    bool ok = dps368_read_pressure(&pressure);
    sensor_i2c_unlock();
    if (!ok) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("DPS368 pressure read failed"));
    }
    return mp_obj_new_float(pressure);
}
static MP_DEFINE_CONST_FUN_OBJ_0(dps368_pressure_obj, dps368_pressure);

/* sensors.dps368.temperature() -> float (Celsius) */
static mp_obj_t dps368_temperature(void) {
    float temp;
    dps368_mpy_lock();
    bool ok = dps368_read_temperature(&temp);
    sensor_i2c_unlock();
    if (!ok) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("DPS368 temp read failed"));
    }
    return mp_obj_new_float(temp);
}
static MP_DEFINE_CONST_FUN_OBJ_0(dps368_temperature_obj, dps368_temperature);

/* sensors.dps368.altitude() -> float (meters, barometric formula) */
static mp_obj_t dps368_altitude(void) {
    float pressure;
    dps368_mpy_lock();
    bool ok = dps368_read_pressure(&pressure);
    sensor_i2c_unlock();
    if (!ok) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("DPS368 pressure read failed"));
    }
    /* Simplified barometric formula (linear approximation near sea level):
     * altitude ~ (P0 - P) * 8.43 m/hPa
     * Accurate within ~1% for altitudes 0-1000m */
    float alt = (1013.25f - pressure) * 8.43f;
    return mp_obj_new_float(alt);
}
static MP_DEFINE_CONST_FUN_OBJ_0(dps368_altitude_obj, dps368_altitude);

/* sensors.dps368.pressure_temperature() -> (pressure, temperature) */
static mp_obj_t dps368_pressure_temperature(void) {
    float pressure, temperature;
    dps368_mpy_lock();
    bool ok = dps368_read_both(&pressure, &temperature);
    sensor_i2c_unlock();
    if (!ok) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("DPS368 read failed"));
    }
    mp_obj_t items[2] = {
        mp_obj_new_float(pressure), mp_obj_new_float(temperature)
    };
    return mp_obj_new_tuple(2, items);
}
static MP_DEFINE_CONST_FUN_OBJ_0(dps368_pressure_temperature_obj, dps368_pressure_temperature);

static const mp_rom_map_elem_t dps368_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),              MP_ROM_QSTR(MP_QSTR_dps368) },
    { MP_ROM_QSTR(MP_QSTR_pressure),               MP_ROM_PTR(&dps368_pressure_obj) },
    { MP_ROM_QSTR(MP_QSTR_temperature),            MP_ROM_PTR(&dps368_temperature_obj) },
    { MP_ROM_QSTR(MP_QSTR_altitude),               MP_ROM_PTR(&dps368_altitude_obj) },
    { MP_ROM_QSTR(MP_QSTR_pressure_temperature),   MP_ROM_PTR(&dps368_pressure_temperature_obj) },
};
static MP_DEFINE_CONST_DICT(dps368_module_globals, dps368_module_globals_table);

const mp_obj_module_t mp_module_dps368 = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&dps368_module_globals,
};

#endif /* BSP_HAS_DPS368 */
