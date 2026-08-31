/*******************************************************************************
 * File Name: moddsp.c
 *
 * Description: MicroPython 'dsp' module — Signal processing for sensor data.
 *
 *              Module shell + stateless functions:
 *              - tilt(ax, ay, az) -> (roll, pitch) degrees
 *              - compass(mx, my, mz) -> heading 0-360 degrees
 *              - altitude(pressure_hpa, sea_level=1013.25) -> meters
 *              - dew_point(temp_c, rh_percent) -> degrees C
 *              - heat_index(temp_c, rh_percent) -> degrees C
 *              - comfort_zone(temp_c, rh_percent) -> string category
 *              - fft_mag(samples, n=None, window=True) -> list of n/2 magnitudes
 *
 *              Classes (defined in moddsp_filters.c and moddsp_imu.c):
 *              - EMA, SMA, LPF, HPF, Median, Kalman1D
 *              - Madgwick, Pedometer
 *
 *******************************************************************************/

#include "py/runtime.h"
#include "py/obj.h"
#include <math.h>
#include <string.h>

#define DSP_PI      3.14159265f
#define DSP_RAD2DEG (180.0f / DSP_PI)

/* ========================================================================== */
/* Extern type declarations from companion files                              */
/* ========================================================================== */

extern const mp_obj_type_t dsp_ema_type;
extern const mp_obj_type_t dsp_sma_type;
extern const mp_obj_type_t dsp_lpf_type;
extern const mp_obj_type_t dsp_hpf_type;
extern const mp_obj_type_t dsp_median_type;
extern const mp_obj_type_t dsp_kalman1d_type;
extern const mp_obj_type_t dsp_madgwick_type;
extern const mp_obj_type_t dsp_pedometer_type;

/* ========================================================================== */
/* IMU stateless functions                                                    */
/* ========================================================================== */

/* tilt(ax, ay, az) -> (roll, pitch) in degrees */
static mp_obj_t dsp_tilt(mp_obj_t ax_obj, mp_obj_t ay_obj, mp_obj_t az_obj) {
    float ax = (float)mp_obj_get_float(ax_obj);
    float ay = (float)mp_obj_get_float(ay_obj);
    float az = (float)mp_obj_get_float(az_obj);

    float roll  = atan2f(ay, az) * DSP_RAD2DEG;
    float pitch = atan2f(-ax, sqrtf(ay * ay + az * az)) * DSP_RAD2DEG;

    mp_obj_t items[2] = {
        mp_obj_new_float((mp_float_t)roll),
        mp_obj_new_float((mp_float_t)pitch),
    };
    return mp_obj_new_tuple(2, items);
}
static MP_DEFINE_CONST_FUN_OBJ_3(dsp_tilt_obj, dsp_tilt);

/* compass(mx, my, mz) -> heading in degrees (0-360) */
static mp_obj_t dsp_compass(mp_obj_t mx_obj, mp_obj_t my_obj, mp_obj_t mz_obj) {
    float mx = (float)mp_obj_get_float(mx_obj);
    float my = (float)mp_obj_get_float(my_obj);
    (void)mz_obj;  /* mz reserved for tilt compensation */

    float heading = atan2f(my, mx) * DSP_RAD2DEG;
    if (heading < 0.0f) heading += 360.0f;
    return mp_obj_new_float((mp_float_t)heading);
}
static MP_DEFINE_CONST_FUN_OBJ_3(dsp_compass_obj, dsp_compass);

/* ========================================================================== */
/* Environment stateless functions                                            */
/* ========================================================================== */

/* altitude(pressure_hpa, sea_level=1013.25) -> meters (hypsometric) */
static mp_obj_t dsp_altitude(size_t n_args, const mp_obj_t *args) {
    float p = (float)mp_obj_get_float(args[0]);
    float p0 = (n_args > 1) ? (float)mp_obj_get_float(args[1]) : 1013.25f;
    if (p <= 0.0f || p0 <= 0.0f) return mp_obj_new_float(0.0f);
    float alt = 44330.0f * (1.0f - powf(p / p0, 0.190295f));
    return mp_obj_new_float((mp_float_t)alt);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(dsp_altitude_obj, 1, 2, dsp_altitude);

/* dew_point(temp_c, rh_percent) -> degrees C (Magnus-Tetens) */
static mp_obj_t dsp_dew_point(mp_obj_t t_obj, mp_obj_t rh_obj) {
    float t = (float)mp_obj_get_float(t_obj);
    float rh = (float)mp_obj_get_float(rh_obj);
    if (rh <= 0.0f) rh = 0.1f;

    float a = 17.27f;
    float b = 237.7f;
    float gamma = (a * t) / (b + t) + logf(rh / 100.0f);
    float dp = (b * gamma) / (a - gamma);
    return mp_obj_new_float((mp_float_t)dp);
}
static MP_DEFINE_CONST_FUN_OBJ_2(dsp_dew_point_obj, dsp_dew_point);

/* heat_index(temp_c, rh_percent) -> degrees C (Rothfusz regression) */
static mp_obj_t dsp_heat_index(mp_obj_t t_obj, mp_obj_t rh_obj) {
    float tc = (float)mp_obj_get_float(t_obj);
    float rh = (float)mp_obj_get_float(rh_obj);

    /* Convert to Fahrenheit for Rothfusz formula */
    float tf = tc * 1.8f + 32.0f;

    /* Simple formula first */
    float hi = 0.5f * (tf + 61.0f + (tf - 68.0f) * 1.2f + rh * 0.094f);

    if (hi >= 80.0f) {
        /* Full Rothfusz regression */
        hi = -42.379f
             + 2.04901523f * tf
             + 10.14333127f * rh
             - 0.22475541f * tf * rh
             - 0.00683783f * tf * tf
             - 0.05481717f * rh * rh
             + 0.00122874f * tf * tf * rh
             + 0.00085282f * tf * rh * rh
             - 0.00000199f * tf * tf * rh * rh;
    }

    /* Convert back to Celsius */
    float result = (hi - 32.0f) / 1.8f;
    return mp_obj_new_float((mp_float_t)result);
}
static MP_DEFINE_CONST_FUN_OBJ_2(dsp_heat_index_obj, dsp_heat_index);

/* comfort_zone(temp_c, rh_percent) -> string category */
static mp_obj_t dsp_comfort_zone(mp_obj_t t_obj, mp_obj_t rh_obj) {
    float t = (float)mp_obj_get_float(t_obj);
    float rh = (float)mp_obj_get_float(rh_obj);

    const char *zone;
    if (t < 18.0f)       zone = "cold";
    else if (t > 27.0f)  zone = "hot";
    else if (rh < 30.0f) zone = "dry";
    else if (rh > 70.0f) zone = "humid";
    else if (t >= 20.0f && t <= 25.0f && rh >= 30.0f && rh <= 60.0f)
                          zone = "comfortable";
    else                  zone = "acceptable";

    return mp_obj_new_str(zone, strlen(zone));
}
static MP_DEFINE_CONST_FUN_OBJ_2(dsp_comfort_zone_obj, dsp_comfort_zone);

/* ========================================================================== */
/* Spectrum                                                                   */
/* ========================================================================== */

/* s16(buf, step=1) -> list of ints
 *
 * แกะ buffer ของ int16 LE (mic.raw() / array('h')) เป็น list — งานเดียว
 * กับลูปใน mic.read() ที่วัดแล้วกิน ~280 ms ต่อชุดบนบอร์ด แต่จบใน C
 * step > 1 = หยิบทุกตัวที่ step (decimate) สำหรับจอ waveform ที่จุด
 * น้อยกว่าจำนวนตัวอย่าง
 */
static mp_obj_t dsp_s16(size_t n_args, const mp_obj_t *args) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[0], &bufinfo, MP_BUFFER_READ);
    mp_int_t step = (n_args > 1) ? mp_obj_get_int(args[1]) : 1;
    if (step < 1) {
        mp_raise_ValueError(MP_ERROR_TEXT("step must be >= 1"));
    }
    const int16_t *s = (const int16_t *)bufinfo.buf;
    size_t total = bufinfo.len / 2;
    size_t count = (total + (size_t)step - 1) / (size_t)step;
    mp_obj_list_t *out = MP_OBJ_TO_PTR(mp_obj_new_list(count, NULL));
    for (size_t i = 0; i < count; i++) {
        out->items[i] = MP_OBJ_NEW_SMALL_INT(s[i * (size_t)step]);
    }
    return MP_OBJ_FROM_PTR(out);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(dsp_s16_obj, 1, 2, dsp_s16);

/* fft_mag(samples, n=None, window=True, demean=True) -> list of n/2 magnitudes
 *
 * samples : list/tuple ของตัวเลข หรือ buffer ของ int16 LE (mic.raw())
 * n       : ขนาด FFT, power of two 8..512; ไม่ใส่ = power of two
 *           ใหญ่สุดที่ไม่เกิน len(samples)
 * window  : True = คูณ Hann ก่อนแปลง (ลด spectral leakage — ค่า default
 *           ตรงกับที่คอร์ส C ใช้ใน aic_scope)
 * demean  : True = หักค่าเฉลี่ยของชุดออกก่อน window เหมือน spectrum
 *           analyzer จริง — จำเป็นเพราะ DC ใหญ่ (เช่นไมค์ PDM ที่ DC ค้าง
 *           เกือบเต็มสเกล) คูณ Hann แล้วไม่ได้อยู่แค่ bin 0: สเปกตรัมของ
 *           Hann มีองค์ประกอบที่ ±1 bin จึงโผล่เป็นยอดขนาด ~DC ที่ bin 1
 *           มาบดบังสัญญาณจริง; ใส่ demean=False เมื่อจะสอนเรื่อง "DC อยู่
 *           bin 0" ด้วยสัญญาณสังเคราะห์ (คู่กับ window=False)
 *
 * คืน magnitude ต่อ bin สเกล 2/N (bin 0 = DC สเกล 1/N) — ป้อน sine เต็ม
 * สเกล A จะได้ยอด ~A ที่ bin ของความถี่นั้นตรง ๆ ใช้แทน fft64() ฝั่ง
 * Python ของ s07 ที่กินเวลาหลักร้อย ms; ฝั่ง C จบใน ~1 ms ที่ N=256
 * buffers อยู่บน GC heap (m_new/m_del) — N=512 ใช้ชั่วคราว 4 KB
 */
static mp_obj_t dsp_fft_mag(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_samples, ARG_n, ARG_window, ARG_demean };
    static const mp_arg_t allowed[] = {
        { MP_QSTR_samples, MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = MP_OBJ_NULL } },
        { MP_QSTR_n,       MP_ARG_OBJ,                   { .u_obj = mp_const_none } },
        { MP_QSTR_window,  MP_ARG_BOOL,                  { .u_bool = true } },
        { MP_QSTR_demean,  MP_ARG_BOOL,                  { .u_bool = true } },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed), allowed, args);

    /* รับสองแบบ: list/tuple ของตัวเลข หรือ buffer ของ int16 LE
     * (bytearray จาก mic.raw() / array('h')) — ทาง buffer ข้ามลูปแกะ byte
     * ฝั่ง Python ที่กิน ~280 ms ต่อชุดบน mic.read() */
    size_t len;
    mp_obj_t *items = NULL;
    const int16_t *s16 = NULL;
    if (mp_obj_is_type(args[ARG_samples].u_obj, &mp_type_list) ||
        mp_obj_is_type(args[ARG_samples].u_obj, &mp_type_tuple)) {
        mp_obj_get_array(args[ARG_samples].u_obj, &len, &items);
    } else {
        mp_buffer_info_t bufinfo;
        mp_get_buffer_raise(args[ARG_samples].u_obj, &bufinfo, MP_BUFFER_READ);
        s16 = (const int16_t *)bufinfo.buf;
        len = bufinfo.len / 2;
    }

    size_t n;
    if (args[ARG_n].u_obj == mp_const_none) {
        n = 8;
        while ((n << 1) <= len && (n << 1) <= 512) {
            n <<= 1;
        }
    } else {
        n = (size_t)mp_obj_get_int(args[ARG_n].u_obj);
    }
    if (n < 8 || n > 512 || (n & (n - 1)) != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("n must be a power of two, 8..512"));
    }
    if (len < n) {
        mp_raise_ValueError(MP_ERROR_TEXT("need at least n samples"));
    }

    float *re = m_new(float, n);
    float *im = m_new(float, n);

    for (size_t i = 0; i < n; i++) {
        re[i] = (items != NULL) ? (float)mp_obj_get_float(items[i])
                                : (float)s16[i];
        im[i] = 0.0f;
    }

    if (args[ARG_demean].u_bool) {
        float mean = 0.0f;
        for (size_t i = 0; i < n; i++) {
            mean += re[i];
        }
        mean /= (float)n;
        for (size_t i = 0; i < n; i++) {
            re[i] -= mean;
        }
    }

    if (args[ARG_window].u_bool) {
        for (size_t i = 0; i < n; i++) {
            re[i] *= 0.5f * (1.0f - cosf(2.0f * DSP_PI * (float)i / (float)n));
        }
    }

    /* bit-reversal permutation */
    for (size_t i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            float t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }

    /* iterative Cooley-Tukey */
    for (size_t seg = 2; seg <= n; seg <<= 1) {
        float ang = -2.0f * DSP_PI / (float)seg;
        float wr = cosf(ang), wi = sinf(ang);
        for (size_t base = 0; base < n; base += seg) {
            float cr = 1.0f, ci = 0.0f;
            for (size_t k = 0; k < seg / 2; k++) {
                size_t a = base + k, b = base + k + seg / 2;
                float tr = re[b] * cr - im[b] * ci;
                float ti = re[b] * ci + im[b] * cr;
                re[b] = re[a] - tr; im[b] = im[a] - ti;
                re[a] += tr;        im[a] += ti;
                float ncr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = ncr;
            }
        }
    }

    /* Hann ลดพลังงานลงครึ่งหนึ่ง (sum of window = N/2) — ชดเชย ×2 เพื่อให้
     * สเกลยอดยังอ่านเป็นแอมพลิจูดจริงของ sine ได้เหมือนตอนไม่ใส่ window */
    float scale = (args[ARG_window].u_bool ? 4.0f : 2.0f) / (float)n;
    size_t half = n / 2;
    mp_obj_list_t *out = MP_OBJ_TO_PTR(mp_obj_new_list(half, NULL));
    for (size_t k = 0; k < half; k++) {
        float mag = sqrtf(re[k] * re[k] + im[k] * im[k]) * scale;
        if (k == 0) {
            mag *= 0.5f;   /* DC ไม่มีคู่ negative-frequency */
        }
        out->items[k] = mp_obj_new_float((mp_float_t)mag);
    }

    m_del(float, im, n);
    m_del(float, re, n);
    return MP_OBJ_FROM_PTR(out);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(dsp_fft_mag_obj, 1, dsp_fft_mag);

/* ========================================================================== */
/* Module Definition                                                          */
/* ========================================================================== */

static const mp_rom_map_elem_t dsp_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),      MP_ROM_QSTR(MP_QSTR_dsp) },
    /* Filter classes */
    { MP_ROM_QSTR(MP_QSTR_EMA),           MP_ROM_PTR(&dsp_ema_type) },
    { MP_ROM_QSTR(MP_QSTR_SMA),           MP_ROM_PTR(&dsp_sma_type) },
    { MP_ROM_QSTR(MP_QSTR_LPF),           MP_ROM_PTR(&dsp_lpf_type) },
    { MP_ROM_QSTR(MP_QSTR_HPF),           MP_ROM_PTR(&dsp_hpf_type) },
    { MP_ROM_QSTR(MP_QSTR_Median),        MP_ROM_PTR(&dsp_median_type) },
    { MP_ROM_QSTR(MP_QSTR_Kalman1D),      MP_ROM_PTR(&dsp_kalman1d_type) },
    /* IMU classes */
    { MP_ROM_QSTR(MP_QSTR_Madgwick),      MP_ROM_PTR(&dsp_madgwick_type) },
    { MP_ROM_QSTR(MP_QSTR_Pedometer),     MP_ROM_PTR(&dsp_pedometer_type) },
    /* IMU functions */
    { MP_ROM_QSTR(MP_QSTR_tilt),          MP_ROM_PTR(&dsp_tilt_obj) },
    { MP_ROM_QSTR(MP_QSTR_compass),       MP_ROM_PTR(&dsp_compass_obj) },
    /* Environment functions */
    { MP_ROM_QSTR(MP_QSTR_altitude),      MP_ROM_PTR(&dsp_altitude_obj) },
    { MP_ROM_QSTR(MP_QSTR_dew_point),     MP_ROM_PTR(&dsp_dew_point_obj) },
    { MP_ROM_QSTR(MP_QSTR_heat_index),    MP_ROM_PTR(&dsp_heat_index_obj) },
    { MP_ROM_QSTR(MP_QSTR_comfort_zone),  MP_ROM_PTR(&dsp_comfort_zone_obj) },
    /* Spectrum */
    { MP_ROM_QSTR(MP_QSTR_fft_mag),       MP_ROM_PTR(&dsp_fft_mag_obj) },
    { MP_ROM_QSTR(MP_QSTR_s16),           MP_ROM_PTR(&dsp_s16_obj) },
};
static MP_DEFINE_CONST_DICT(dsp_module_globals, dsp_module_globals_table);

const mp_obj_module_t mp_module_dsp = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&dsp_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_dsp, mp_module_dsp);
