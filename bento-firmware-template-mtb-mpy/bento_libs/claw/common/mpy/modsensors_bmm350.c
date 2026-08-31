/*******************************************************************************
 * File Name: modsensors_bmm350.c
 *
 * Description: MicroPython BMM350 sub-module for the 'sensors' module.
 *              Provides: sensors.bmm350.magnetic(), .heading(), .chip_id()
 *
 *              Compiled when BSP_HAS_BMM350=1.
 *
 *******************************************************************************/

#include "bsp_feature_flags.h"

#if BSP_HAS_BMM350

#include "py/runtime.h"
#include "py/obj.h"
#include "sensor_bmm350.h"

/* sensors.bmm350.magnetic() -> (mx, my, mz) in micro-Tesla */
static mp_obj_t bmm350_magnetic(void) {
    float mx, my, mz;
    if (!bmm350_read_xyz(&mx, &my, &mz)) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("BMM350 read failed"));
    }
    mp_obj_t items[3] = {
        mp_obj_new_float(mx), mp_obj_new_float(my), mp_obj_new_float(mz)
    };
    return mp_obj_new_tuple(3, items);
}
static MP_DEFINE_CONST_FUN_OBJ_0(bmm350_magnetic_obj, bmm350_magnetic);

/* sensors.bmm350.heading() -> float (degrees, 0-360, 0=North) */
static mp_obj_t bmm350_heading_func(void) {
    float heading;
    if (!bmm350_read_heading(&heading)) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("BMM350 heading read failed"));
    }
    return mp_obj_new_float(heading);
}
static MP_DEFINE_CONST_FUN_OBJ_0(bmm350_heading_obj, bmm350_heading_func);

/* sensors.bmm350.chip_id() -> int */
static mp_obj_t bmm350_chip_id_func(void) {
    uint8_t id;
    if (!bmm350_read_chip_id(&id)) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("BMM350 chip ID read failed"));
    }
    return mp_obj_new_int(id);
}
static MP_DEFINE_CONST_FUN_OBJ_0(bmm350_chip_id_obj, bmm350_chip_id_func);

/* sensors.bmm350.cal_reset() — Reset hard iron calibration.
 * Call this, then slowly rotate the board 360 degrees for fresh calibration. */
static mp_obj_t bmm350_cal_reset_func(void) {
    bmm350_cal_reset();
    mp_printf(&mp_plat_print, "BMM350: calibration reset. Rotate board 360 deg.\n");
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(bmm350_cal_reset_obj, bmm350_cal_reset_func);

/* sensors.bmm350.cal_status() -> dict with calibration info */
static mp_obj_t bmm350_cal_status_func(void) {
    float ox, oy;
    bool valid;
    bmm350_cal_get_offsets(&ox, &oy, &valid);

    mp_obj_dict_t *d = MP_OBJ_TO_PTR(mp_obj_new_dict(3));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        MP_OBJ_NEW_QSTR(MP_QSTR_valid),
        mp_obj_new_bool(valid));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        mp_obj_new_str("offset_x", 8),
        mp_obj_new_float(ox));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        mp_obj_new_str("offset_y", 8),
        mp_obj_new_float(oy));
    return MP_OBJ_FROM_PTR(d);
}
static MP_DEFINE_CONST_FUN_OBJ_0(bmm350_cal_status_obj, bmm350_cal_status_func);

static const mp_rom_map_elem_t bmm350_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),   MP_ROM_QSTR(MP_QSTR_bmm350) },
    { MP_ROM_QSTR(MP_QSTR_magnetic),    MP_ROM_PTR(&bmm350_magnetic_obj) },
    { MP_ROM_QSTR(MP_QSTR_heading),     MP_ROM_PTR(&bmm350_heading_obj) },
    { MP_ROM_QSTR(MP_QSTR_chip_id),     MP_ROM_PTR(&bmm350_chip_id_obj) },
    { MP_ROM_QSTR(MP_QSTR_cal_reset),   MP_ROM_PTR(&bmm350_cal_reset_obj) },
    { MP_ROM_QSTR(MP_QSTR_cal_status),  MP_ROM_PTR(&bmm350_cal_status_obj) },
};
static MP_DEFINE_CONST_DICT(bmm350_module_globals, bmm350_module_globals_table);

const mp_obj_module_t mp_module_bmm350 = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&bmm350_module_globals,
};

#endif /* BSP_HAS_BMM350 */
