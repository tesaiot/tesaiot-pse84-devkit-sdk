/*******************************************************************************
 * File Name: modsensors_potentiometer.c
 *
 * Description: MicroPython Potentiometer sub-module for the 'sensors' module.
 *              Provides: sensors.pot.read(), .percent(), .voltage()
 *
 *              Only compiled when BSP_HAS_POTENTIOMETER=1.
 *
 *******************************************************************************/

#include "bsp_feature_flags.h"

#if BSP_HAS_POTENTIOMETER

#include "py/runtime.h"
#include "py/obj.h"
#include "sensor_potentiometer.h"

#ifdef USE_KIT_PSE84_EVAL_EPC2
/* On Eva Kit the potentiometer is read by CM55 through AutAnalog -- that is
 * what CM33's build says with -DCM55_OWNS_POT. Setting up the SAR from this
 * core as well gives one converter two owners. Take CM55's reading instead;
 * it samples every 200 ms for its own Controls page. Defined in modsensors.c. */
bool eva_snapshot_pot(uint16_t *raw, uint16_t *percent_x10);

/* Same value sensor_potentiometer.c scales against, restated here because that
 * file is not compiled into this path on Eva. The course already records that
 * the schematic says VDD_1V8 and this says 3.3 V, and forbids stating either
 * until someone meters P15[1] -- do not "fix" one without the measurement. */
#define EVA_POT_VDDA_V  (3.3f)

static void eva_pot_or_raise(uint16_t *raw, uint16_t *pct_x10) {
    if (!eva_snapshot_pot(raw, pct_x10)) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT(
            "Potentiometer: CM55 did not answer. On Eva Kit this reading comes "
            "from the display core's snapshot, and it is not replying"));
    }
}
#endif

/* sensors.pot.read() -> int (0-65535) */
static mp_obj_t pot_read(void) {
    uint16_t raw;
#ifdef USE_KIT_PSE84_EVAL_EPC2
    eva_pot_or_raise(&raw, NULL);
#else
    if (!potentiometer_read_raw(&raw)) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("Potentiometer read failed"));
    }
#endif
    return mp_obj_new_int(raw);
}
static MP_DEFINE_CONST_FUN_OBJ_0(pot_read_obj, pot_read);

/* sensors.pot.percent() -> float (0.0 - 100.0) */
static mp_obj_t pot_percent(void) {
    float pct;
#ifdef USE_KIT_PSE84_EVAL_EPC2
    uint16_t x10 = 0;
    eva_pot_or_raise(NULL, &x10);
    pct = (float)x10 / 10.0f;
#else
    if (!potentiometer_read_percent(&pct)) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("Potentiometer read failed"));
    }
#endif
    return mp_obj_new_float(pct);
}
static MP_DEFINE_CONST_FUN_OBJ_0(pot_percent_obj, pot_percent);

/* sensors.pot.voltage() -> float (0.0 - 3.3V) */
static mp_obj_t pot_voltage(void) {
    float v;
#ifdef USE_KIT_PSE84_EVAL_EPC2
    /* Derived from the percentage rather than measured here, because the ADC
     * that measures it belongs to the other core. The reference this scales
     * against is the unresolved 1.8 V / 3.3 V question the course already
     * flags -- treat the number as relative until someone meters P15[1]. */
    uint16_t x10 = 0;
    eva_pot_or_raise(NULL, &x10);
    v = ((float)x10 / 1000.0f) * EVA_POT_VDDA_V;
#else
    if (!potentiometer_read_voltage(&v)) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("Potentiometer read failed"));
    }
#endif
    return mp_obj_new_float(v);
}
static MP_DEFINE_CONST_FUN_OBJ_0(pot_voltage_obj, pot_voltage);

static const mp_rom_map_elem_t pot_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),   MP_ROM_QSTR(MP_QSTR_pot) },
    { MP_ROM_QSTR(MP_QSTR_read),        MP_ROM_PTR(&pot_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_percent),     MP_ROM_PTR(&pot_percent_obj) },
    { MP_ROM_QSTR(MP_QSTR_voltage),     MP_ROM_PTR(&pot_voltage_obj) },
};
static MP_DEFINE_CONST_DICT(pot_module_globals, pot_module_globals_table);

const mp_obj_module_t mp_module_pot = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&pot_module_globals,
};

#endif /* BSP_HAS_POTENTIOMETER */
