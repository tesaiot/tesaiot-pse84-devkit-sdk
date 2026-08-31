/*******************************************************************************
 * File Name        : radar_dsp.c
 *
 * Description      : BGT60TR13C range-profile DSP — see radar_dsp.h.
 *
 * Chain (per frame of 128 raw ADC samples, one chirp):
 *   1. Chebyshev 2nd-order high-pass biquad over the time samples
 *      (removes DC/ramp leakage before the FFT; Infineon reference coeffs).
 *   2. 128-pt radix-2 complex FFT (real input, precomputed twiddles).
 *   3. Magnitude -> dB: 20*log10(max(0.001, |X[k]|)).
 *   4. Clutter-map baseline: first CAL_FRAMES profiles of the static scene
 *      are averaged as the baseline (re-capture via threshold cmd 0).
 *   5. Hann window before the FFT; FIRST bin above baseline by >
 *      threshold (default 6 dB) = nearest changed edge => target;
 *      distance = bin * resolution. Baseline slowly adapts (tau ~10 s).
 *
 * NOTE: this deliberately replaces the Infineon reference's declining-k
 * anti-coupling + first-peak search. HW-measured on the AI Kit P0 chirp:
 * that curve leaves a static ~9 dB floor at bin 3 which pins the first-peak
 * at 1.0 m forever, and its 3 zeroed bins blind the first metre. Baseline
 * subtraction detects what CHANGED in the scene — the correct primitive for
 * presence/tracking demos. Only the lower half-spectrum is valid range data.
 *******************************************************************************/

#include <math.h>
#include <string.h>
#include "cy_pdl.h"      /* __DMB barrier */
#include "radar_dsp.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- FFT tables (N = 128, log2N = 7) ------------------------------------ */
#define N        RADAR_DSP_N
#define LOG2N    (7u)

static float    tw_cos[N / 2];
static float    tw_sin[N / 2];
static float    hann[N];
static uint16_t bitrev[N];

/* Work buffers — radar task context only, so plain statics are fine. */
static float re[N];
static float im[N];
static float profile_db[N / 2];

/* Clutter map (baseline spectrum of the static scene). HW finding on the AI
 * Kit chirp profile: the Infineon declining-k anti-coupling curve does not fit
 * our gain/chirp — a static ~9 dB floor at bin 3 pinned the first-peak search
 * at 1.0 m forever, and zeroing bins 0-2 blinded the first metre entirely.
 * Classic fix: average the first CAL_FRAMES range profiles at boot as the
 * static-clutter baseline, then detect the strongest bin ABOVE baseline —
 * moving targets pop out, furniture/walls cancel to ~0. */
#define CAL_FRAMES  (32u)
static float    baseline_db[N / 2];
static uint32_t cal_count = 0;

/* Temporal smoothing + report hysteresis (HW: single-frame first-crossing
 * swung wildly indoors — multipath transients cross threshold on random
 * bins mid-step). EMA over ~6 frames adds SNR at negligible latency
 * (200 fps); a reported-bin jump must persist JUMP_CONFIRM consecutive
 * frames, and a target clears only after CLEAR_CONFIRM misses. */
/* NOTE: these run at the RADAR FRAME RATE (~200 fps), so confirm counts are
 * frames-not-polls. 1.0m-calibration session findings: scale 0.326 m/bin is
 * physically CORRECT (still person at 1.0 m = bin 3, +3.3 dB breathing echo);
 * the on-screen swing came from these constants being ~100x too short in
 * time (3 frames = 15 ms filtered nothing). */
#define EMA_ALPHA      (0.03f)   /* tau ~33 frames = ~165 ms                */
#define JUMP_CONFIRM   (40u)     /* far jump must persist ~200 ms           */
#define CLEAR_CONFIRM  (400u)    /* hold last position ~2 s w/o any crossing */
static float    smooth_db[N / 2];
static bool     smooth_primed = false;
static uint16_t rep_bin   = 0;      /* currently reported bin (0 = none) */
static float    rep_db    = 0.0f;
static uint16_t cand_bin  = 0;      /* pending jump candidate            */
static uint32_t cand_cnt  = 0;
static uint32_t miss_cnt  = 0;

/* ---- Result snapshot (single writer: radar task; reader: IPC service) ---- */
static volatile ipc_radar_range_t s_snap;
static volatile uint32_t          s_snap_seq  = 0;   /* even = stable        */
static bool                       s_ready     = false;
static bool                       s_inited    = false;

static float s_resolution_m  = 0.0f;
static float s_threshold_db  = RADAR_DSP_THRESHOLD_DB;

/* ---- Init ---------------------------------------------------------------- */
void radar_dsp_init(double bandwidth_hz)
{
    for (uint32_t i = 0; i < (N / 2u); i++) {
        double ang = (-2.0 * M_PI * (double)i) / (double)N;
        tw_cos[i] = (float)cos(ang);
        tw_sin[i] = (float)sin(ang);
    }
    for (uint32_t i = 0; i < N; i++) {
        uint32_t r = 0;
        for (uint32_t b = 0; b < LOG2N; b++) {
            r = (r << 1) | ((i >> b) & 1u);
        }
        bitrev[i] = (uint16_t)r;
    }

    for (uint32_t i = 0; i < N; i++) {
        hann[i] = 0.5f - 0.5f * (float)cos((2.0 * M_PI * (double)i) / (double)(N - 1));
    }

    /* Range resolution: c / (2 * bandwidth). P0 profile (459.8 MHz) -> ~0.326 m. */
    if (bandwidth_hz > 0.0) {
        s_resolution_m = (float)(299792458.0 / (2.0 * bandwidth_hz));
    }

    memset((void *)&s_snap, 0, sizeof(s_snap));
    cal_count = 0;                           /* fresh clutter calibration    */
    smooth_primed = false;
    rep_bin = 0; cand_bin = 0; cand_cnt = 0; miss_cnt = 0;
    s_inited = true;
}

void radar_dsp_set_threshold_x10(uint32_t threshold_x10)
{
    if (threshold_x10 == 0u) {
        /* Special value 0 = re-capture the clutter baseline (scene changed /
         * board moved). Detection pauses for CAL_FRAMES frames (~160 ms). */
        cal_count = 0;
        smooth_primed = false;
        rep_bin = 0; cand_bin = 0; cand_cnt = 0; miss_cnt = 0;
        return;
    }
    /* Bound to something sane: 0.1 .. 60.0 dB above baseline */
    if (threshold_x10 >= 1u && threshold_x10 <= 600u) {
        s_threshold_db = (float)threshold_x10 / 10.0f;
    }
}

/* ---- In-place iterative radix-2 DIT FFT ---------------------------------- */
static void fft_run(void)
{
    /* Bit-reversal permutation */
    for (uint32_t i = 0; i < N; i++) {
        uint32_t j = bitrev[i];
        if (j > i) {
            float tr = re[i]; re[i] = re[j]; re[j] = tr;
            float ti = im[i]; im[i] = im[j]; im[j] = ti;
        }
    }

    for (uint32_t len = 2; len <= N; len <<= 1) {
        uint32_t half = len >> 1;
        uint32_t step = N / len;            /* twiddle stride */
        for (uint32_t base = 0; base < N; base += len) {
            for (uint32_t k = 0; k < half; k++) {
                uint32_t ti = k * step;
                float wr = tw_cos[ti];
                float wi = tw_sin[ti];
                uint32_t a = base + k;
                uint32_t b = a + half;
                float xr = re[b] * wr - im[b] * wi;
                float xi = re[b] * wi + im[b] * wr;
                re[b] = re[a] - xr;
                im[b] = im[a] - xi;
                re[a] = re[a] + xr;
                im[a] = im[a] + xi;
            }
        }
    }
}

/* ---- Per-frame processing ------------------------------------------------ */
void radar_dsp_process(const int16_t *samples, uint32_t n)
{
    if (!s_inited || samples == NULL || n < N) {
        return;
    }

    /* 1. Chebyshev 2nd-order HPF (coefficients from the Infineon reference:
     *    y0 = 0.943*x0 - 1.885*x1 + 0.943*x2 + 1.881*y1 - 0.890*y2) */
    float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
    for (uint32_t i = 0; i < N; i++) {
        float x0 = (float)samples[i];
        float y0 = x0 * 0.943f - x1 * 1.885f + x2 * 0.943f
                 + y1 * 1.881f - y2 * 0.890f;
        x2 = x1; x1 = x0;
        y2 = y1; y1 = y0;
        re[i] = y0 * hann[i];   /* Hann window: kill leakage smear (HW: the
                                 * unwindowed blob spanned bins 2-8) */
        im[i] = 0.0f;
    }

    /* 2. FFT */
    fft_run();

    /* 3. Magnitude -> dB over the valid half-spectrum (raw, uncalibrated). */
    for (uint32_t i = 0; i < (N / 2u); i++) {
        float mag = sqrtf(re[i] * re[i] + im[i] * im[i]);
        if (mag < 0.001f) { mag = 0.001f; }
        profile_db[i] = 20.0f * log10f(mag);
    }

    /* 4. Clutter-map calibration: average the first CAL_FRAMES profiles of
     * the (assumed static) scene as the baseline. No detection until done
     * (~160 ms at 200 Hz). Bins 0-1 = direct Tx->Rx coupling, always dead. */
    if (cal_count < CAL_FRAMES) {
        for (uint32_t i = 0; i < (N / 2u); i++) {
            baseline_db[i] = (cal_count == 0)
                ? profile_db[i]
                : (baseline_db[i] + profile_db[i]) * 0.5f;
        }
        cal_count++;
        return;                              /* still calibrating            */
    }

    /* 4b. Slow baseline adaptation (tau ~10 s at 200 Hz): persistent NEW
     * reflectors — a fan switched on, a moved chair — fade into the scene
     * within tens of seconds, while people/hands moving at human timescales
     * stay well above it. (HW finding: a modulating reflector at ~5.2 m sat
     * 10 dB over the boot baseline forever and hogged the argmax.) */
    for (uint32_t i = 0; i < (N / 2u); i++) {
        baseline_db[i] += 0.0005f * (profile_db[i] - baseline_db[i]);
    }

    /* 4c. EMA spectrum integration (~6-frame time constant at 200 fps):
     * multipath flicker averages out before detection ever sees it. */
    if (!smooth_primed) {
        for (uint32_t i = 0; i < (N / 2u); i++) {
            smooth_db[i] = profile_db[i];
        }
        smooth_primed = true;
    } else {
        for (uint32_t i = 0; i < (N / 2u); i++) {
            smooth_db[i] += EMA_ALPHA * (profile_db[i] - smooth_db[i]);
        }
    }

    /* 5. FIRST smoothed bin above baseline by > threshold = nearest edge of
     * whatever changed. Search from bin 1 (0.33 m); DC bin 0 is dead. */
    uint16_t hit_bin = 0;
    float    hit_db  = 0.0f;
    for (uint32_t i = 1; i < (N / 2u); i++) {
        float diff = smooth_db[i] - baseline_db[i];
        if (diff > s_threshold_db) {
            hit_bin = (uint16_t)i;
            hit_db  = diff;
            break;
        }
    }

    /* 5b. Report hysteresis: near moves (+/-2 bins) track instantly; a far
     * jump must persist JUMP_CONFIRM consecutive frames; losing the target
     * needs CLEAR_CONFIRM consecutive misses. Kills the on-screen zigzag. */
    if (hit_bin != 0u) {
        miss_cnt = 0;
        int d = (int)hit_bin - (int)rep_bin;
        if (rep_bin != 0u && d >= -2 && d <= 2) {
            rep_bin = hit_bin;               /* smooth local tracking        */
            rep_db  = hit_db;
            cand_cnt = 0;
        } else if (hit_bin == cand_bin) {
            if (++cand_cnt >= JUMP_CONFIRM) {
                rep_bin = hit_bin;
                rep_db  = hit_db;
                cand_cnt = 0;
            }
        } else {
            cand_bin = hit_bin;
            cand_cnt = 1;
            if (rep_bin == 0u) {             /* first acquisition: accept    */
                rep_bin = hit_bin;
                rep_db  = hit_db;
            }
        }
    } else {
        cand_cnt = 0;
        if (rep_bin != 0u && ++miss_cnt >= CLEAR_CONFIRM) {
            rep_bin = 0u;
            rep_db  = 0.0f;
            miss_cnt = 0;
        }
    }
    uint16_t peak_bin = rep_bin;
    float    peak_db  = rep_db;

    /* Publish snapshot (seq odd while writing; reader re-checks). */
    uint32_t seq = s_snap_seq + 1u;          /* -> odd                       */
    s_snap_seq = seq;
    __DMB();
    s_snap.initialized   = 1;
    s_snap.target        = (peak_bin != 0) ? 1 : 0;
    s_snap.seq           = (uint16_t)(seq >> 1);
    s_snap.distance_mm   = (peak_bin != 0)
        ? (uint32_t)((float)peak_bin * s_resolution_m * 1000.0f) : 0u;
    s_snap.peak_db       = peak_db;
    s_snap.resolution_mm = (uint16_t)(s_resolution_m * 1000.0f);
    s_snap.bin           = peak_bin;
    __DMB();
    s_snap_seq = seq + 1u;                   /* -> even = stable             */

    s_ready = true;
}

/* ---- Snapshot read ------------------------------------------------------- */
bool radar_dsp_snapshot(ipc_radar_range_t *out)
{
    if (out == NULL) {
        return false;
    }
    if (!s_ready) {
        memset(out, 0, sizeof(*out));
        out->initialized = s_inited ? 1 : 0;
        return false;
    }
    uint32_t s1, s2;
    do {
        s1 = s_snap_seq;
        memcpy(out, (const void *)&s_snap, sizeof(*out));
        s2 = s_snap_seq;
    } while ((s1 != s2) || (s1 & 1u));       /* retry on torn/odd            */
    return true;
}
