/*******************************************************************************
 * File Name: modbentobuddy.c
 *
 * Description: MicroPython module `bento_buddy` — thin wrapper around
 *              bento_buddy_request_start / stop / state for REPL-driven
 *              testing of the Bento Desktop Buddy BLE variant.
 *
 *              Compiled only when BENTO_HAS_BLE_NUS=1 (i.e. when
 *              ENABLE_PAGE_BENTO_BUDDY=1 propagates into libmicropython.a
 *              via proj_cm33_ns/Makefile.micropython).
 *
 *              Python API:
 *                bento_buddy.start()    -> 0 started / 1 already-on / OSError
 *                bento_buddy.stop()     -> None
 *                bento_buddy.state()    -> int (0 off / 1 adv / 2 connected / 3 error)
 *
 * Bento forked this protocol under its own branding — see Bento_Buddy/SPEC.md
 * §3.1.1 Not Anthropic-compatible.
 ******************************************************************************/

#if defined(BENTO_HAS_BLE_NUS) && (BENTO_HAS_BLE_NUS == 1)

#include "py/runtime.h"
#include "py/mperrno.h"

#include "ble_nus.h"
#include "ble_nus_lazy.h"

//! [ble_bento_buddy_start_stop_mpy]
static mp_obj_t mp_bento_buddy_start(void)
{
    int rc = bento_buddy_request_start();
    if (rc < 0) {
        mp_raise_OSError(MP_EIO);
    }
    return MP_OBJ_NEW_SMALL_INT(rc);
}
static MP_DEFINE_CONST_FUN_OBJ_0(mp_bento_buddy_start_obj, mp_bento_buddy_start);

static mp_obj_t mp_bento_buddy_stop(void)
{
    bento_buddy_request_stop();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mp_bento_buddy_stop_obj, mp_bento_buddy_stop);
//! [ble_bento_buddy_start_stop_mpy]

static mp_obj_t mp_bento_buddy_state(void)
{
    return MP_OBJ_NEW_SMALL_INT((int)ble_nus_get_state());
}
static MP_DEFINE_CONST_FUN_OBJ_0(mp_bento_buddy_state_obj, mp_bento_buddy_state);

static const mp_rom_map_elem_t bento_buddy_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),  MP_ROM_QSTR(MP_QSTR_bento_buddy) },
    { MP_ROM_QSTR(MP_QSTR_start),     MP_ROM_PTR(&mp_bento_buddy_start_obj) },
    { MP_ROM_QSTR(MP_QSTR_stop),      MP_ROM_PTR(&mp_bento_buddy_stop_obj) },
    { MP_ROM_QSTR(MP_QSTR_state),     MP_ROM_PTR(&mp_bento_buddy_state_obj) },
};
static MP_DEFINE_CONST_DICT(bento_buddy_module_globals,
                            bento_buddy_module_globals_table);

const mp_obj_module_t mp_module_bento_buddy = {
    .base    = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&bento_buddy_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_bento_buddy, mp_module_bento_buddy);

#endif /* BENTO_HAS_BLE_NUS */
