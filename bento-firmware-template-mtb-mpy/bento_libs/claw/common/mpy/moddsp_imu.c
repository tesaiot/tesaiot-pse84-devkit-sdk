/*******************************************************************************
 * File Name: moddsp_imu.c
 *
 * Description: MicroPython 'dsp' module — IMU algorithm classes.
 *              Madgwick 6-DOF AHRS + Pedometer
 *
 * Madgwick usage:
 *   ahrs = dsp.Madgwick(beta=0.1, fs=100.0)
 *   roll, pitch, yaw = ahrs.update(ax, ay, az, gx, gy, gz)  # gyro in rad/s
 *   q = ahrs.quaternion()   # (w, x, y, z) tuple
 *
 * Pedometer usage:
 *   ped = dsp.Pedometer(threshold=1.5, min_interval=300)
 *   steps, active = ped.update(ax, ay, az)
 *
 *******************************************************************************/

#include "py/runtime.h"
#include "py/obj.h"
#include "py/mphal.h"
#include <math.h>

#define DSP_PI      3.14159265f
#define DSP_RAD2DEG (180.0f / DSP_PI)

/* ========================================================================== */
/* Madgwick — 6-DOF AHRS (accelerometer + gyroscope)                          */
/* ========================================================================== */

typedef struct {
    mp_obj_base_t base;
    float beta;
    float sp;  /* sample period = 1/fs */
    float q0, q1, q2, q3;
} dsp_madgwick_obj_t;

static mp_obj_t madgwick_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    enum { ARG_beta, ARG_fs };
    static const mp_arg_t allowed[] = {
        { MP_QSTR_beta, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_fs,   MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
    };
    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed)];
    mp_arg_parse_all_kw_array(n_args, n_kw, args, MP_ARRAY_SIZE(allowed), allowed, parsed);

    dsp_madgwick_obj_t *self = mp_obj_malloc(dsp_madgwick_obj_t, type);
    self->beta = (parsed[ARG_beta].u_obj != mp_const_none)
                 ? (float)mp_obj_get_float(parsed[ARG_beta].u_obj) : 0.1f;
    float fs = (parsed[ARG_fs].u_obj != mp_const_none)
               ? (float)mp_obj_get_float(parsed[ARG_fs].u_obj) : 100.0f;
    self->sp = 1.0f / fs;
    self->q0 = 1.0f; self->q1 = 0.0f; self->q2 = 0.0f; self->q3 = 0.0f;
    return MP_OBJ_FROM_PTR(self);
}

/* update(ax, ay, az, gx, gy, gz) -> (roll, pitch, yaw) in degrees */
static mp_obj_t madgwick_update(size_t n_args, const mp_obj_t *args) {
    dsp_madgwick_obj_t *s = MP_OBJ_TO_PTR(args[0]);
    float ax = (float)mp_obj_get_float(args[1]);
    float ay = (float)mp_obj_get_float(args[2]);
    float az = (float)mp_obj_get_float(args[3]);
    float gx = (float)mp_obj_get_float(args[4]);
    float gy = (float)mp_obj_get_float(args[5]);
    float gz = (float)mp_obj_get_float(args[6]);

    float q0 = s->q0, q1 = s->q1, q2 = s->q2, q3 = s->q3;

    /* Normalize accelerometer */
    float norm = sqrtf(ax * ax + ay * ay + az * az);
    if (norm > 0.0f) {
        float inv = 1.0f / norm;
        ax *= inv; ay *= inv; az *= inv;

        /* Objective function: f = q* [0,0,1] q - [ax,ay,az] */
        float f1 = 2.0f * (q1 * q3 - q0 * q2) - ax;
        float f2 = 2.0f * (q0 * q1 + q2 * q3) - ay;
        float f3 = 2.0f * (0.5f - q1 * q1 - q2 * q2) - az;

        /* Gradient: J^T * f */
        float s0 = -2.0f * q2 * f1 + 2.0f * q1 * f2;
        float s1 =  2.0f * q3 * f1 + 2.0f * q0 * f2 - 4.0f * q1 * f3;
        float s2 = -2.0f * q0 * f1 + 2.0f * q3 * f2 - 4.0f * q2 * f3;
        float s3 =  2.0f * q1 * f1 + 2.0f * q2 * f2;

        /* Normalize gradient */
        norm = sqrtf(s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3);
        if (norm > 0.0f) {
            float inv2 = s->beta / norm;
            s0 *= inv2; s1 *= inv2; s2 *= inv2; s3 *= inv2;
        } else {
            s0 = 0.0f; s1 = 0.0f; s2 = 0.0f; s3 = 0.0f;
        }

        /* Quaternion rate: gyro integration - gradient correction */
        float qd0 = 0.5f * (-q1 * gx - q2 * gy - q3 * gz) - s0;
        float qd1 = 0.5f * ( q0 * gx + q2 * gz - q3 * gy) - s1;
        float qd2 = 0.5f * ( q0 * gy - q1 * gz + q3 * gx) - s2;
        float qd3 = 0.5f * ( q0 * gz + q1 * gy - q2 * gx) - s3;

        /* Integrate */
        q0 += qd0 * s->sp;
        q1 += qd1 * s->sp;
        q2 += qd2 * s->sp;
        q3 += qd3 * s->sp;
    } else {
        /* Accel zero — integrate gyro only */
        q0 += 0.5f * (-q1 * gx - q2 * gy - q3 * gz) * s->sp;
        q1 += 0.5f * ( q0 * gx + q2 * gz - q3 * gy) * s->sp;
        q2 += 0.5f * ( q0 * gy - q1 * gz + q3 * gx) * s->sp;
        q3 += 0.5f * ( q0 * gz + q1 * gy - q2 * gx) * s->sp;
    }

    /* Normalize quaternion */
    norm = sqrtf(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
    if (norm > 0.0f) {
        float inv = 1.0f / norm;
        q0 *= inv; q1 *= inv; q2 *= inv; q3 *= inv;
    }
    s->q0 = q0; s->q1 = q1; s->q2 = q2; s->q3 = q3;

    /* Convert to Euler angles (degrees) */
    float roll  = atan2f(2.0f * (q0 * q1 + q2 * q3),
                         1.0f - 2.0f * (q1 * q1 + q2 * q2)) * DSP_RAD2DEG;
    /* Clamp asinf input to [-1, 1] to prevent NaN */
    float sinp = 2.0f * (q0 * q2 - q3 * q1);
    if (sinp > 1.0f) sinp = 1.0f;
    if (sinp < -1.0f) sinp = -1.0f;
    float pitch = asinf(sinp) * DSP_RAD2DEG;
    float yaw   = atan2f(2.0f * (q0 * q3 + q1 * q2),
                         1.0f - 2.0f * (q2 * q2 + q3 * q3)) * DSP_RAD2DEG;

    mp_obj_t items[3] = {
        mp_obj_new_float((mp_float_t)roll),
        mp_obj_new_float((mp_float_t)pitch),
        mp_obj_new_float((mp_float_t)yaw),
    };
    return mp_obj_new_tuple(3, items);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(madgwick_update_obj, 7, 7, madgwick_update);

/* quaternion() -> (w, x, y, z) */
static mp_obj_t madgwick_quaternion(mp_obj_t self_in) {
    dsp_madgwick_obj_t *s = MP_OBJ_TO_PTR(self_in);
    mp_obj_t items[4] = {
        mp_obj_new_float((mp_float_t)s->q0), mp_obj_new_float((mp_float_t)s->q1),
        mp_obj_new_float((mp_float_t)s->q2), mp_obj_new_float((mp_float_t)s->q3),
    };
    return mp_obj_new_tuple(4, items);
}
static MP_DEFINE_CONST_FUN_OBJ_1(madgwick_quaternion_obj, madgwick_quaternion);

static mp_obj_t madgwick_reset(mp_obj_t self_in) {
    dsp_madgwick_obj_t *s = MP_OBJ_TO_PTR(self_in);
    s->q0 = 1.0f; s->q1 = 0.0f; s->q2 = 0.0f; s->q3 = 0.0f;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(madgwick_reset_obj, madgwick_reset);

static void madgwick_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    dsp_madgwick_obj_t *s = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "Madgwick(beta=%.3f, q=[%.3f,%.3f,%.3f,%.3f])",
              (double)s->beta, (double)s->q0, (double)s->q1, (double)s->q2, (double)s->q3);
}

static const mp_rom_map_elem_t madgwick_locals[] = {
    { MP_ROM_QSTR(MP_QSTR_update),     MP_ROM_PTR(&madgwick_update_obj) },
    { MP_ROM_QSTR(MP_QSTR_quaternion), MP_ROM_PTR(&madgwick_quaternion_obj) },
    { MP_ROM_QSTR(MP_QSTR_reset),      MP_ROM_PTR(&madgwick_reset_obj) },
};
static MP_DEFINE_CONST_DICT(madgwick_locals_dict, madgwick_locals);

MP_DEFINE_CONST_OBJ_TYPE(
    dsp_madgwick_type, MP_QSTR_Madgwick, MP_TYPE_FLAG_NONE,
    make_new, madgwick_make_new, print, madgwick_print,
    locals_dict, &madgwick_locals_dict
);

/* ========================================================================== */
/* Pedometer — Step counter from accelerometer magnitude                      */
/* Uses threshold crossing with hysteresis + minimum interval debounce        */
/* ========================================================================== */

typedef struct {
    mp_obj_base_t base;
    float threshold;
    float hysteresis;   /* 80% of threshold */
    uint32_t min_interval_ms;
    uint32_t steps;
    uint32_t last_step_ms;
    float prev_mag;
    bool above;
} dsp_pedometer_obj_t;

static mp_obj_t ped_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    enum { ARG_threshold, ARG_min_interval };
    static const mp_arg_t allowed[] = {
        { MP_QSTR_threshold,    MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_min_interval, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 300} },
    };
    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed)];
    mp_arg_parse_all_kw_array(n_args, n_kw, args, MP_ARRAY_SIZE(allowed), allowed, parsed);

    dsp_pedometer_obj_t *self = mp_obj_malloc(dsp_pedometer_obj_t, type);
    self->threshold = (parsed[ARG_threshold].u_obj != mp_const_none)
                      ? (float)mp_obj_get_float(parsed[ARG_threshold].u_obj) : 1.5f;
    self->hysteresis = self->threshold * 0.8f;
    self->min_interval_ms = (uint32_t)parsed[ARG_min_interval].u_int;
    self->steps = 0;
    self->last_step_ms = 0;
    self->prev_mag = 0.0f;
    self->above = false;
    return MP_OBJ_FROM_PTR(self);
}

/* update(ax, ay, az) -> (steps, active) */
static mp_obj_t ped_update(size_t n_args, const mp_obj_t *args) {
    dsp_pedometer_obj_t *s = MP_OBJ_TO_PTR(args[0]);
    float ax = (float)mp_obj_get_float(args[1]);
    float ay = (float)mp_obj_get_float(args[2]);
    float az = (float)mp_obj_get_float(args[3]);

    float mag = sqrtf(ax * ax + ay * ay + az * az);
    bool active = false;

    if (!s->above && mag > s->threshold) {
        /* Rising edge — potential step */
        uint32_t now = mp_hal_ticks_ms();
        uint32_t elapsed = now - s->last_step_ms;
        if (elapsed >= s->min_interval_ms) {
            s->steps++;
            s->last_step_ms = now;
            active = true;
        }
        s->above = true;
    } else if (s->above && mag < s->hysteresis) {
        /* Falling edge with hysteresis */
        s->above = false;
    }
    s->prev_mag = mag;

    mp_obj_t items[2] = {
        mp_obj_new_int((mp_int_t)s->steps),
        mp_obj_new_bool(active),
    };
    return mp_obj_new_tuple(2, items);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(ped_update_obj, 4, 4, ped_update);

static mp_obj_t ped_reset(mp_obj_t self_in) {
    dsp_pedometer_obj_t *s = MP_OBJ_TO_PTR(self_in);
    s->steps = 0; s->last_step_ms = 0;
    s->prev_mag = 0.0f; s->above = false;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(ped_reset_obj, ped_reset);

static void ped_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    dsp_pedometer_obj_t *s = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "Pedometer(steps=%lu, threshold=%.2f)",
              (unsigned long)s->steps, (double)s->threshold);
}

static const mp_rom_map_elem_t ped_locals[] = {
    { MP_ROM_QSTR(MP_QSTR_update), MP_ROM_PTR(&ped_update_obj) },
    { MP_ROM_QSTR(MP_QSTR_reset),  MP_ROM_PTR(&ped_reset_obj) },
};
static MP_DEFINE_CONST_DICT(ped_locals_dict, ped_locals);

MP_DEFINE_CONST_OBJ_TYPE(
    dsp_pedometer_type, MP_QSTR_Pedometer, MP_TYPE_FLAG_NONE,
    make_new, ped_make_new, print, ped_print,
    locals_dict, &ped_locals_dict
);
