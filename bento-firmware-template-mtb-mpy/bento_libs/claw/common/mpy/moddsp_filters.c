/*******************************************************************************
 * File Name: moddsp_filters.c
 *
 * Description: MicroPython 'dsp' module — Digital filter classes.
 *              EMA, SMA, LPF, HPF, Median, Kalman1D
 *
 * All filters follow the same API:
 *   f = dsp.FilterType(params...)
 *   val = f.update(sample)    # feed new sample, return filtered value
 *   f.value()                 # last output
 *   f.reset()                 # reset state
 *
 *******************************************************************************/

#include "py/runtime.h"
#include "py/obj.h"
#include <math.h>
#include <string.h>

#define DSP_PI 3.14159265f

/* ========================================================================== */
/* EMA — Exponential Moving Average                                           */
/* y = alpha * x + (1 - alpha) * y_prev                                       */
/* ========================================================================== */

typedef struct {
    mp_obj_base_t base;
    float alpha, output;
    bool initialized;
} dsp_ema_obj_t;

static mp_obj_t ema_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    enum { ARG_alpha };
    static const mp_arg_t allowed[] = {
        { MP_QSTR_alpha, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
    };
    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed)];
    mp_arg_parse_all_kw_array(n_args, n_kw, args, MP_ARRAY_SIZE(allowed), allowed, parsed);

    dsp_ema_obj_t *self = mp_obj_malloc(dsp_ema_obj_t, type);
    self->alpha = (parsed[ARG_alpha].u_obj != mp_const_none)
                  ? (float)mp_obj_get_float(parsed[ARG_alpha].u_obj) : 0.1f;
    self->output = 0.0f;
    self->initialized = false;
    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t ema_update(mp_obj_t self_in, mp_obj_t sample) {
    dsp_ema_obj_t *s = MP_OBJ_TO_PTR(self_in);
    float x = (float)mp_obj_get_float(sample);
    if (!s->initialized) { s->output = x; s->initialized = true; }
    else { s->output = s->alpha * x + (1.0f - s->alpha) * s->output; }
    return mp_obj_new_float((mp_float_t)s->output);
}
static MP_DEFINE_CONST_FUN_OBJ_2(ema_update_obj, ema_update);

static mp_obj_t ema_reset(mp_obj_t self_in) {
    dsp_ema_obj_t *s = MP_OBJ_TO_PTR(self_in);
    s->output = 0.0f; s->initialized = false;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(ema_reset_obj, ema_reset);

static mp_obj_t ema_value(mp_obj_t self_in) {
    return mp_obj_new_float((mp_float_t)((dsp_ema_obj_t *)MP_OBJ_TO_PTR(self_in))->output);
}
static MP_DEFINE_CONST_FUN_OBJ_1(ema_value_obj, ema_value);

static void ema_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    dsp_ema_obj_t *s = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "EMA(alpha=%.3f, value=%.4f)", (double)s->alpha, (double)s->output);
}

static const mp_rom_map_elem_t ema_locals[] = {
    { MP_ROM_QSTR(MP_QSTR_update), MP_ROM_PTR(&ema_update_obj) },
    { MP_ROM_QSTR(MP_QSTR_reset),  MP_ROM_PTR(&ema_reset_obj) },
    { MP_ROM_QSTR(MP_QSTR_value),  MP_ROM_PTR(&ema_value_obj) },
};
static MP_DEFINE_CONST_DICT(ema_locals_dict, ema_locals);

MP_DEFINE_CONST_OBJ_TYPE(
    dsp_ema_type, MP_QSTR_EMA, MP_TYPE_FLAG_NONE,
    make_new, ema_make_new, print, ema_print,
    locals_dict, &ema_locals_dict
);

/* ========================================================================== */
/* SMA — Simple Moving Average (circular buffer, max window 64)               */
/* ========================================================================== */

typedef struct {
    mp_obj_base_t base;
    uint16_t window, count, index;
    float sum;
    float *buf;
} dsp_sma_obj_t;

static mp_obj_t sma_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    enum { ARG_window };
    static const mp_arg_t allowed[] = {
        { MP_QSTR_window, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 10} },
    };
    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed)];
    mp_arg_parse_all_kw_array(n_args, n_kw, args, MP_ARRAY_SIZE(allowed), allowed, parsed);

    int w = parsed[ARG_window].u_int;
    if (w < 2) w = 2;
    if (w > 64) w = 64;

    dsp_sma_obj_t *self = mp_obj_malloc(dsp_sma_obj_t, type);
    self->window = (uint16_t)w;
    self->count = 0;
    self->index = 0;
    self->sum = 0.0f;
    self->buf = m_new0(float, (size_t)w);
    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t sma_update(mp_obj_t self_in, mp_obj_t sample) {
    dsp_sma_obj_t *s = MP_OBJ_TO_PTR(self_in);
    float x = (float)mp_obj_get_float(sample);
    if (s->count >= s->window) {
        s->sum -= s->buf[s->index];
    } else {
        s->count++;
    }
    s->buf[s->index] = x;
    s->sum += x;
    s->index = (uint16_t)((s->index + 1) % s->window);
    return mp_obj_new_float((mp_float_t)(s->sum / (float)s->count));
}
static MP_DEFINE_CONST_FUN_OBJ_2(sma_update_obj, sma_update);

static mp_obj_t sma_reset(mp_obj_t self_in) {
    dsp_sma_obj_t *s = MP_OBJ_TO_PTR(self_in);
    s->count = 0; s->index = 0; s->sum = 0.0f;
    memset(s->buf, 0, (size_t)s->window * sizeof(float));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(sma_reset_obj, sma_reset);

static mp_obj_t sma_value(mp_obj_t self_in) {
    dsp_sma_obj_t *s = MP_OBJ_TO_PTR(self_in);
    if (s->count == 0) return mp_obj_new_float(0.0f);
    return mp_obj_new_float((mp_float_t)(s->sum / (float)s->count));
}
static MP_DEFINE_CONST_FUN_OBJ_1(sma_value_obj, sma_value);

static void sma_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    dsp_sma_obj_t *s = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "SMA(window=%d, count=%d)", s->window, s->count);
}

static const mp_rom_map_elem_t sma_locals[] = {
    { MP_ROM_QSTR(MP_QSTR_update), MP_ROM_PTR(&sma_update_obj) },
    { MP_ROM_QSTR(MP_QSTR_reset),  MP_ROM_PTR(&sma_reset_obj) },
    { MP_ROM_QSTR(MP_QSTR_value),  MP_ROM_PTR(&sma_value_obj) },
};
static MP_DEFINE_CONST_DICT(sma_locals_dict, sma_locals);

MP_DEFINE_CONST_OBJ_TYPE(
    dsp_sma_type, MP_QSTR_SMA, MP_TYPE_FLAG_NONE,
    make_new, sma_make_new, print, sma_print,
    locals_dict, &sma_locals_dict
);

/* ========================================================================== */
/* LPF — First-order Low-Pass IIR Filter                                      */
/* alpha = dt / (RC + dt),  RC = 1/(2*pi*cutoff),  dt = 1/fs                  */
/* ========================================================================== */

typedef struct {
    mp_obj_base_t base;
    float alpha, output;
    bool initialized;
} dsp_lpf_obj_t;

static mp_obj_t lpf_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    enum { ARG_cutoff, ARG_fs };
    static const mp_arg_t allowed[] = {
        { MP_QSTR_cutoff, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_fs,     MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
    };
    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed)];
    mp_arg_parse_all_kw_array(n_args, n_kw, args, MP_ARRAY_SIZE(allowed), allowed, parsed);

    float cutoff = (parsed[ARG_cutoff].u_obj != mp_const_none)
                   ? (float)mp_obj_get_float(parsed[ARG_cutoff].u_obj) : 5.0f;
    float fs = (parsed[ARG_fs].u_obj != mp_const_none)
               ? (float)mp_obj_get_float(parsed[ARG_fs].u_obj) : 100.0f;

    float dt = 1.0f / fs;
    float rc = 1.0f / (2.0f * DSP_PI * cutoff);

    dsp_lpf_obj_t *self = mp_obj_malloc(dsp_lpf_obj_t, type);
    self->alpha = dt / (rc + dt);
    self->output = 0.0f;
    self->initialized = false;
    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t lpf_update(mp_obj_t self_in, mp_obj_t sample) {
    dsp_lpf_obj_t *s = MP_OBJ_TO_PTR(self_in);
    float x = (float)mp_obj_get_float(sample);
    if (!s->initialized) { s->output = x; s->initialized = true; }
    else { s->output = s->alpha * x + (1.0f - s->alpha) * s->output; }
    return mp_obj_new_float((mp_float_t)s->output);
}
static MP_DEFINE_CONST_FUN_OBJ_2(lpf_update_obj, lpf_update);

static mp_obj_t lpf_reset(mp_obj_t self_in) {
    dsp_lpf_obj_t *s = MP_OBJ_TO_PTR(self_in);
    s->output = 0.0f; s->initialized = false;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(lpf_reset_obj, lpf_reset);

static mp_obj_t lpf_value(mp_obj_t self_in) {
    return mp_obj_new_float((mp_float_t)((dsp_lpf_obj_t *)MP_OBJ_TO_PTR(self_in))->output);
}
static MP_DEFINE_CONST_FUN_OBJ_1(lpf_value_obj, lpf_value);

static void lpf_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    dsp_lpf_obj_t *s = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "LPF(alpha=%.4f, value=%.4f)", (double)s->alpha, (double)s->output);
}

static const mp_rom_map_elem_t lpf_locals[] = {
    { MP_ROM_QSTR(MP_QSTR_update), MP_ROM_PTR(&lpf_update_obj) },
    { MP_ROM_QSTR(MP_QSTR_reset),  MP_ROM_PTR(&lpf_reset_obj) },
    { MP_ROM_QSTR(MP_QSTR_value),  MP_ROM_PTR(&lpf_value_obj) },
};
static MP_DEFINE_CONST_DICT(lpf_locals_dict, lpf_locals);

MP_DEFINE_CONST_OBJ_TYPE(
    dsp_lpf_type, MP_QSTR_LPF, MP_TYPE_FLAG_NONE,
    make_new, lpf_make_new, print, lpf_print,
    locals_dict, &lpf_locals_dict
);

/* ========================================================================== */
/* HPF — First-order High-Pass IIR Filter                                     */
/* alpha = RC / (RC + dt),  y = alpha * (y_prev + x - x_prev)                 */
/* ========================================================================== */

typedef struct {
    mp_obj_base_t base;
    float alpha, prev_in, output;
    bool initialized;
} dsp_hpf_obj_t;

static mp_obj_t hpf_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    enum { ARG_cutoff, ARG_fs };
    static const mp_arg_t allowed[] = {
        { MP_QSTR_cutoff, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_fs,     MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
    };
    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed)];
    mp_arg_parse_all_kw_array(n_args, n_kw, args, MP_ARRAY_SIZE(allowed), allowed, parsed);

    float cutoff = (parsed[ARG_cutoff].u_obj != mp_const_none)
                   ? (float)mp_obj_get_float(parsed[ARG_cutoff].u_obj) : 0.5f;
    float fs = (parsed[ARG_fs].u_obj != mp_const_none)
               ? (float)mp_obj_get_float(parsed[ARG_fs].u_obj) : 100.0f;

    float dt = 1.0f / fs;
    float rc = 1.0f / (2.0f * DSP_PI * cutoff);

    dsp_hpf_obj_t *self = mp_obj_malloc(dsp_hpf_obj_t, type);
    self->alpha = rc / (rc + dt);
    self->prev_in = 0.0f;
    self->output = 0.0f;
    self->initialized = false;
    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t hpf_update(mp_obj_t self_in, mp_obj_t sample) {
    dsp_hpf_obj_t *s = MP_OBJ_TO_PTR(self_in);
    float x = (float)mp_obj_get_float(sample);
    if (!s->initialized) {
        s->prev_in = x;
        s->output = 0.0f;
        s->initialized = true;
    } else {
        s->output = s->alpha * (s->output + x - s->prev_in);
        s->prev_in = x;
    }
    return mp_obj_new_float((mp_float_t)s->output);
}
static MP_DEFINE_CONST_FUN_OBJ_2(hpf_update_obj, hpf_update);

static mp_obj_t hpf_reset(mp_obj_t self_in) {
    dsp_hpf_obj_t *s = MP_OBJ_TO_PTR(self_in);
    s->prev_in = 0.0f; s->output = 0.0f; s->initialized = false;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(hpf_reset_obj, hpf_reset);

static mp_obj_t hpf_value(mp_obj_t self_in) {
    return mp_obj_new_float((mp_float_t)((dsp_hpf_obj_t *)MP_OBJ_TO_PTR(self_in))->output);
}
static MP_DEFINE_CONST_FUN_OBJ_1(hpf_value_obj, hpf_value);

static void hpf_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    dsp_hpf_obj_t *s = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "HPF(alpha=%.4f, value=%.4f)", (double)s->alpha, (double)s->output);
}

static const mp_rom_map_elem_t hpf_locals[] = {
    { MP_ROM_QSTR(MP_QSTR_update), MP_ROM_PTR(&hpf_update_obj) },
    { MP_ROM_QSTR(MP_QSTR_reset),  MP_ROM_PTR(&hpf_reset_obj) },
    { MP_ROM_QSTR(MP_QSTR_value),  MP_ROM_PTR(&hpf_value_obj) },
};
static MP_DEFINE_CONST_DICT(hpf_locals_dict, hpf_locals);

MP_DEFINE_CONST_OBJ_TYPE(
    dsp_hpf_type, MP_QSTR_HPF, MP_TYPE_FLAG_NONE,
    make_new, hpf_make_new, print, hpf_print,
    locals_dict, &hpf_locals_dict
);

/* ========================================================================== */
/* Median — Median Filter (insertion sort, max window 15, forced odd)         */
/* ========================================================================== */

typedef struct {
    mp_obj_base_t base;
    uint8_t window, count, index;
    float last_median;
    float *buf;
} dsp_median_obj_t;

static mp_obj_t median_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    enum { ARG_window };
    static const mp_arg_t allowed[] = {
        { MP_QSTR_window, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 5} },
    };
    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed)];
    mp_arg_parse_all_kw_array(n_args, n_kw, args, MP_ARRAY_SIZE(allowed), allowed, parsed);

    int w = parsed[ARG_window].u_int;
    if (w < 3) w = 3;
    if (w > 15) w = 15;
    if ((w & 1) == 0) w++;  /* force odd */

    dsp_median_obj_t *self = mp_obj_malloc(dsp_median_obj_t, type);
    self->window = (uint8_t)w;
    self->count = 0;
    self->index = 0;
    self->last_median = 0.0f;
    self->buf = m_new0(float, (size_t)w);
    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t median_update(mp_obj_t self_in, mp_obj_t sample) {
    dsp_median_obj_t *s = MP_OBJ_TO_PTR(self_in);
    float x = (float)mp_obj_get_float(sample);

    s->buf[s->index] = x;
    s->index = (uint8_t)((s->index + 1) % s->window);
    if (s->count < s->window) s->count++;

    /* Copy to stack and insertion sort */
    float tmp[15];
    uint8_t n = s->count;
    memcpy(tmp, s->buf, (size_t)n * sizeof(float));
    for (uint8_t i = 1; i < n; i++) {
        float key = tmp[i];
        int j = (int)i - 1;
        while (j >= 0 && tmp[j] > key) {
            tmp[j + 1] = tmp[j];
            j--;
        }
        tmp[j + 1] = key;
    }
    s->last_median = tmp[n / 2];
    return mp_obj_new_float((mp_float_t)s->last_median);
}
static MP_DEFINE_CONST_FUN_OBJ_2(median_update_obj, median_update);

static mp_obj_t median_reset(mp_obj_t self_in) {
    dsp_median_obj_t *s = MP_OBJ_TO_PTR(self_in);
    s->count = 0; s->index = 0; s->last_median = 0.0f;
    memset(s->buf, 0, (size_t)s->window * sizeof(float));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(median_reset_obj, median_reset);

static mp_obj_t median_value(mp_obj_t self_in) {
    return mp_obj_new_float((mp_float_t)((dsp_median_obj_t *)MP_OBJ_TO_PTR(self_in))->last_median);
}
static MP_DEFINE_CONST_FUN_OBJ_1(median_value_obj, median_value);

static void median_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    dsp_median_obj_t *s = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "Median(window=%d, count=%d)", s->window, s->count);
}

static const mp_rom_map_elem_t median_locals[] = {
    { MP_ROM_QSTR(MP_QSTR_update), MP_ROM_PTR(&median_update_obj) },
    { MP_ROM_QSTR(MP_QSTR_reset),  MP_ROM_PTR(&median_reset_obj) },
    { MP_ROM_QSTR(MP_QSTR_value),  MP_ROM_PTR(&median_value_obj) },
};
static MP_DEFINE_CONST_DICT(median_locals_dict, median_locals);

MP_DEFINE_CONST_OBJ_TYPE(
    dsp_median_type, MP_QSTR_Median, MP_TYPE_FLAG_NONE,
    make_new, median_make_new, print, median_print,
    locals_dict, &median_locals_dict
);

/* ========================================================================== */
/* Kalman1D — Scalar Kalman Filter                                            */
/* predict: p += q                                                            */
/* update:  k = p/(p+r),  x += k*(z-x),  p *= (1-k)                          */
/* ========================================================================== */

typedef struct {
    mp_obj_base_t base;
    float q, r, x, p;
    bool initialized;
} dsp_kalman1d_obj_t;

static mp_obj_t kalman1d_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    enum { ARG_q, ARG_r };
    static const mp_arg_t allowed[] = {
        { MP_QSTR_q, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_r, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
    };
    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed)];
    mp_arg_parse_all_kw_array(n_args, n_kw, args, MP_ARRAY_SIZE(allowed), allowed, parsed);

    dsp_kalman1d_obj_t *self = mp_obj_malloc(dsp_kalman1d_obj_t, type);
    self->q = (parsed[ARG_q].u_obj != mp_const_none)
              ? (float)mp_obj_get_float(parsed[ARG_q].u_obj) : 0.01f;
    self->r = (parsed[ARG_r].u_obj != mp_const_none)
              ? (float)mp_obj_get_float(parsed[ARG_r].u_obj) : 0.1f;
    self->x = 0.0f;
    self->p = 1.0f;
    self->initialized = false;
    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t kalman1d_update(mp_obj_t self_in, mp_obj_t measurement) {
    dsp_kalman1d_obj_t *s = MP_OBJ_TO_PTR(self_in);
    float z = (float)mp_obj_get_float(measurement);

    if (!s->initialized) {
        s->x = z;
        s->p = 1.0f;
        s->initialized = true;
    } else {
        /* Predict */
        s->p += s->q;
        /* Update */
        float k = s->p / (s->p + s->r);
        s->x += k * (z - s->x);
        s->p *= (1.0f - k);
    }
    return mp_obj_new_float((mp_float_t)s->x);
}
static MP_DEFINE_CONST_FUN_OBJ_2(kalman1d_update_obj, kalman1d_update);

static mp_obj_t kalman1d_reset(mp_obj_t self_in) {
    dsp_kalman1d_obj_t *s = MP_OBJ_TO_PTR(self_in);
    s->x = 0.0f; s->p = 1.0f; s->initialized = false;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(kalman1d_reset_obj, kalman1d_reset);

static mp_obj_t kalman1d_value(mp_obj_t self_in) {
    return mp_obj_new_float((mp_float_t)((dsp_kalman1d_obj_t *)MP_OBJ_TO_PTR(self_in))->x);
}
static MP_DEFINE_CONST_FUN_OBJ_1(kalman1d_value_obj, kalman1d_value);

static void kalman1d_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    dsp_kalman1d_obj_t *s = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "Kalman1D(q=%.4f, r=%.4f, x=%.4f)", (double)s->q, (double)s->r, (double)s->x);
}

static const mp_rom_map_elem_t kalman1d_locals[] = {
    { MP_ROM_QSTR(MP_QSTR_update), MP_ROM_PTR(&kalman1d_update_obj) },
    { MP_ROM_QSTR(MP_QSTR_reset),  MP_ROM_PTR(&kalman1d_reset_obj) },
    { MP_ROM_QSTR(MP_QSTR_value),  MP_ROM_PTR(&kalman1d_value_obj) },
};
static MP_DEFINE_CONST_DICT(kalman1d_locals_dict, kalman1d_locals);

MP_DEFINE_CONST_OBJ_TYPE(
    dsp_kalman1d_type, MP_QSTR_Kalman1D, MP_TYPE_FLAG_NONE,
    make_new, kalman1d_make_new, print, kalman1d_print,
    locals_dict, &kalman1d_locals_dict
);
