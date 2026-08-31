/*******************************************************************************
 * File:    mod_shield_motor_v2.c
 *
 * Purpose: MicroPython bindings for the Adafruit Motor Shield v2 on the QWA309
 *          Arduino header.
 *
 * Name:    Exposed as the `shield` package with a `motor_v2` member, so:
 *
 *              from shield import motor_v2
 *              motor_v2.run(1, motor_v2.FORWARD)
 *
 *          The version belongs to the shield, not to the package. A future
 *          MikroBus driver lands beside it as `shield.mikrobus`, and if the v1
 *          motor shield is ever supported it becomes `shield.motor_v1` without
 *          renaming anything.
 *
 * Path:    MicroPython runs on CM33_NS; the shield's I2C bus (SCB5) is owned by
 *          CM55. Every call here packs an ipc_ui_motor_t and fires it across
 *          IPC_CMD_UI_MOTOR, where motor_ipc.c executes it in the GFX task.
 *          Fire-and-forget: the pipe reports whether the message was accepted,
 *          not whether the motor moved.
 *
 * Safety:  These commands are deliberately NOT in ipc_ui_is_droppable(). A
 *          dropped widget update is invisible; a dropped stop is a machine
 *          that keeps running.
 *
 *          Note that a soft reset, Ctrl-C or an uncaught exception does NOT
 *          stop a motor -- the port has no machine_deinit hook. Scripts that
 *          command motion should stop it in a finally: block.
 *
 * Gated by BSP_HAS_QWA309_BASEBOARD.
 ******************************************************************************/

#include "bsp_feature_flags.h"

#if BSP_HAS_QWA309_BASEBOARD

#include "py/runtime.h"
#include "py/obj.h"

#include "cy_pdl.h"
#include "ipc_communication.h"
#include "ipc_ui_protocol.h"

#include <string.h>

/* Anti-copy-paste guard: this file hard-codes canonical opcode-map v2 behavior. */
_Static_assert(IPC_UI_OPMAP_VERSION == 2, "wrong tree's ipc_ui_protocol.h (opcode map mismatch)");

#define MOTOR_IPC_SEND_RETRIES   (100)
#define MOTOR_IPC_RETRY_DELAY_US (100)

/* Dedicated shared-mem message buffer; do not reuse modui's or rgbmatrix's. */
CY_SECTION_SHAREDMEM static ipc_msg_t s_motor_ipc_msg;

static bool s_motor_ipc_ready = false;

static inline void motor_ipc_setup_once(void)
{
    if (!s_motor_ipc_ready) {
        cm33_ipc_communication_setup();
        s_motor_ipc_ready = true;
    }
}

static bool motor_ipc_send(uint8_t op, uint8_t index, uint8_t arg,
                           uint8_t dir, uint32_t steps)
{
    ipc_ui_motor_t payload;
    int retries = MOTOR_IPC_SEND_RETRIES;

    motor_ipc_setup_once();

    payload.op    = op;
    payload.index = index;
    payload.arg   = arg;
    payload.dir   = dir;
    payload.steps = steps;

    memset(&s_motor_ipc_msg, 0, sizeof(s_motor_ipc_msg));
    s_motor_ipc_msg.client_id = CM55_IPC_UI_CLIENT_ID;
    s_motor_ipc_msg.intr_mask = CY_IPC_CYPIPE_INTR_MASK_EP1;
    s_motor_ipc_msg.cmd       = IPC_CMD_UI_MOTOR;
    s_motor_ipc_msg.value     = 0;
    memcpy(s_motor_ipc_msg.data, &payload, sizeof(payload));

    do {
        cy_en_ipc_pipe_status_t st = Cy_IPC_Pipe_SendMessage(
            CM55_IPC_PIPE_EP_ADDR, CM33_IPC_PIPE_EP_ADDR,
            (void *)&s_motor_ipc_msg, NULL);
        if (CY_IPC_PIPE_SUCCESS == st) {
            return true;
        }
        Cy_SysLib_DelayUs(MOTOR_IPC_RETRY_DELAY_US);
    } while (--retries > 0);

    return false;
}

static void motor_raise_if_failed(bool ok)
{
    if (!ok) {
        mp_raise_msg(&mp_type_OSError,
                     MP_ERROR_TEXT("motor shield: IPC busy"));
    }
}

static uint8_t check_dc(mp_int_t n)
{
    if ((n < 1) || (n > 4)) {
        mp_raise_ValueError(MP_ERROR_TEXT("motor must be 1..4"));
    }
    return (uint8_t)n;
}

static uint8_t check_port(mp_int_t n)
{
    if ((n < 1) || (n > 2)) {
        mp_raise_ValueError(MP_ERROR_TEXT("stepper port must be 1 or 2"));
    }
    return (uint8_t)n;
}

/*******************************************************************************
 * DC motors
 ******************************************************************************/

static mp_obj_t mod_motor_speed(mp_obj_t n_obj, mp_obj_t speed_obj)
{
    uint8_t  n     = check_dc(mp_obj_get_int(n_obj));
    mp_int_t speed = mp_obj_get_int(speed_obj);

    if ((speed < 0) || (speed > 255)) {
        mp_raise_ValueError(MP_ERROR_TEXT("speed must be 0..255"));
    }

    motor_raise_if_failed(
        motor_ipc_send(IPC_UI_MOTOR_OP_DC_SPEED, n, (uint8_t)speed, 0U, 0U));

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(mod_motor_speed_obj, mod_motor_speed);

static mp_obj_t mod_motor_run(mp_obj_t n_obj, mp_obj_t cmd_obj)
{
    uint8_t  n   = check_dc(mp_obj_get_int(n_obj));
    mp_int_t cmd = mp_obj_get_int(cmd_obj);

    if ((cmd < 1) || (cmd > 4)) {
        mp_raise_ValueError(
            MP_ERROR_TEXT("use FORWARD, BACKWARD, BRAKE or RELEASE"));
    }

    motor_raise_if_failed(
        motor_ipc_send(IPC_UI_MOTOR_OP_DC_RUN, n, (uint8_t)cmd, 0U, 0U));

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(mod_motor_run_obj, mod_motor_run);

static mp_obj_t mod_motor_stop_all(void)
{
    motor_raise_if_failed(motor_ipc_send(IPC_UI_MOTOR_OP_STOP_ALL, 0U, 0U, 0U, 0U));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_motor_stop_all_obj, mod_motor_stop_all);

/*******************************************************************************
 * Steppers
 ******************************************************************************/

static mp_obj_t mod_motor_stepper_attach(mp_obj_t port_obj, mp_obj_t revsteps_obj)
{
    uint8_t  port     = check_port(mp_obj_get_int(port_obj));
    mp_int_t revsteps = mp_obj_get_int(revsteps_obj);

    if ((revsteps < 1) || (revsteps > 65535)) {
        mp_raise_ValueError(MP_ERROR_TEXT("steps per revolution out of range"));
    }

    /* Port 1 takes terminals M1+M2, port 2 takes M3+M4; those DC channels are
     * refused by the driver while the port is attached. */
    motor_raise_if_failed(motor_ipc_send(IPC_UI_MOTOR_OP_ST_ATTACH, port, 0U, 0U,
                                         (uint32_t)revsteps));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(mod_motor_stepper_attach_obj, mod_motor_stepper_attach);

static mp_obj_t mod_motor_stepper_detach(mp_obj_t port_obj)
{
    motor_raise_if_failed(
        motor_ipc_send(IPC_UI_MOTOR_OP_ST_DETACH,
                       check_port(mp_obj_get_int(port_obj)), 0U, 0U, 0U));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_motor_stepper_detach_obj, mod_motor_stepper_detach);

static mp_obj_t mod_motor_stepper_rpm(size_t n_args, const mp_obj_t *args)
{
    uint8_t  port  = check_port(mp_obj_get_int(args[0]));
    mp_int_t rpm   = mp_obj_get_int(args[1]);
    mp_int_t style = (n_args > 2) ? mp_obj_get_int(args[2]) : 2;   /* DOUBLE */

    if (rpm < 1) {
        mp_raise_ValueError(MP_ERROR_TEXT("rpm must be positive"));
    }

    /* The driver refuses an rpm the I2C bus cannot sustain rather than
     * clamping it, so an over-fast request simply does nothing here. That is
     * deliberate: a silently clamped speed would make any readback a lie. */
    motor_raise_if_failed(motor_ipc_send(IPC_UI_MOTOR_OP_ST_RPM, port,
                                         (uint8_t)style, 0U, (uint32_t)rpm));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_motor_stepper_rpm_obj, 2, 3,
                                           mod_motor_stepper_rpm);

static mp_obj_t mod_motor_stepper_step(size_t n_args, const mp_obj_t *args)
{
    uint8_t  port  = check_port(mp_obj_get_int(args[0]));
    mp_int_t steps = mp_obj_get_int(args[1]);
    mp_int_t dir   = (n_args > 2) ? mp_obj_get_int(args[2]) : 1;   /* FORWARD */
    mp_int_t style = (n_args > 3) ? mp_obj_get_int(args[3]) : 2;   /* DOUBLE  */

    if (steps < 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("steps must not be negative"));
    }

    /* Returns immediately. CM55 advances the motion one step at a time from
     * its own timer, so a long move does not block the REPL -- and does not
     * block the display either. */
    motor_raise_if_failed(motor_ipc_send(IPC_UI_MOTOR_OP_ST_START, port,
                                         (uint8_t)style, (uint8_t)dir,
                                         (uint32_t)steps));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_motor_stepper_step_obj, 2, 4,
                                           mod_motor_stepper_step);

static mp_obj_t mod_motor_stepper_stop(mp_obj_t port_obj)
{
    motor_raise_if_failed(
        motor_ipc_send(IPC_UI_MOTOR_OP_ST_STOP,
                       check_port(mp_obj_get_int(port_obj)), 0U, 0U, 0U));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_motor_stepper_stop_obj, mod_motor_stepper_stop);

static mp_obj_t mod_motor_stepper_release(mp_obj_t port_obj)
{
    motor_raise_if_failed(
        motor_ipc_send(IPC_UI_MOTOR_OP_ST_RELEASE,
                       check_port(mp_obj_get_int(port_obj)), 0U, 0U, 0U));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_motor_stepper_release_obj, mod_motor_stepper_release);

/*******************************************************************************
 * Module: shield.motor_v2
 ******************************************************************************/

static const mp_rom_map_elem_t motor_v2_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),         MP_ROM_QSTR(MP_QSTR_motor_v2) },

    { MP_ROM_QSTR(MP_QSTR_speed),            MP_ROM_PTR(&mod_motor_speed_obj) },
    { MP_ROM_QSTR(MP_QSTR_run),              MP_ROM_PTR(&mod_motor_run_obj) },
    { MP_ROM_QSTR(MP_QSTR_stop_all),         MP_ROM_PTR(&mod_motor_stop_all_obj) },

    { MP_ROM_QSTR(MP_QSTR_stepper_attach),   MP_ROM_PTR(&mod_motor_stepper_attach_obj) },
    { MP_ROM_QSTR(MP_QSTR_stepper_detach),   MP_ROM_PTR(&mod_motor_stepper_detach_obj) },
    { MP_ROM_QSTR(MP_QSTR_stepper_rpm),      MP_ROM_PTR(&mod_motor_stepper_rpm_obj) },
    { MP_ROM_QSTR(MP_QSTR_stepper_step),     MP_ROM_PTR(&mod_motor_stepper_step_obj) },
    { MP_ROM_QSTR(MP_QSTR_stepper_stop),     MP_ROM_PTR(&mod_motor_stepper_stop_obj) },
    { MP_ROM_QSTR(MP_QSTR_stepper_release),  MP_ROM_PTR(&mod_motor_stepper_release_obj) },

    /* Direction / action. Values match motor_shield_v2_run_t. */
    { MP_ROM_QSTR(MP_QSTR_FORWARD),          MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_BACKWARD),         MP_ROM_INT(2) },
    { MP_ROM_QSTR(MP_QSTR_BRAKE),            MP_ROM_INT(3) },
    { MP_ROM_QSTR(MP_QSTR_RELEASE),          MP_ROM_INT(4) },

    /* Stepping style. Values match motor_shield_v2_style_t. */
    { MP_ROM_QSTR(MP_QSTR_SINGLE),           MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_DOUBLE),           MP_ROM_INT(2) },
    { MP_ROM_QSTR(MP_QSTR_INTERLEAVE),       MP_ROM_INT(3) },
    { MP_ROM_QSTR(MP_QSTR_MICROSTEP),        MP_ROM_INT(4) },
};
static MP_DEFINE_CONST_DICT(motor_v2_globals, motor_v2_globals_table);

static const mp_obj_module_t mp_module_shield_motor_v2 = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&motor_v2_globals,
};

/*******************************************************************************
 * Package: shield
 *
 * A package rather than a flat module so more shields can join without
 * crowding the global namespace: shield.mikrobus, shield.motor_v1, and so on.
 ******************************************************************************/

static const mp_rom_map_elem_t shield_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_shield) },
    { MP_ROM_QSTR(MP_QSTR_motor_v2), MP_ROM_PTR(&mp_module_shield_motor_v2) },
};
static MP_DEFINE_CONST_DICT(shield_globals, shield_globals_table);

const mp_obj_module_t mp_module_shield = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&shield_globals,
};

MP_REGISTER_MODULE(MP_QSTR_shield, mp_module_shield);

#endif /* BSP_HAS_QWA309_BASEBOARD */
