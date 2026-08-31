/*******************************************************************************
 * File Name        : radar_dsp.h
 *
 * Description      : BGT60TR13C range-profile DSP for the TESAIoT radar task.
 *                    Per-frame chain: Chebyshev 2nd-order HPF -> 128-pt FFT ->
 *                    magnitude dB -> clutter-baseline subtraction ->
 *                    strongest-bin-above-baseline search.
 *
 *                    Algorithm reference: Infineon micropython-radar-bgt60
 *                    (BGT60TRXX.py, MIT) — reimplemented in C for the CM55
 *                    radar task (the pure-Python driver is not viable on this
 *                    firmware: no machine.SPI, 10x too slow at frame rate).
 *
 * Target           : PSoC Edge E84, CM55 (radar task context only)
 *******************************************************************************/

#ifndef RADAR_DSP_H
#define RADAR_DSP_H

#include <stdint.h>
#include <stdbool.h>
#include "ipc_communication.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Samples per chirp the DSP is built for (matches radar_settings.h P0). */
#define RADAR_DSP_N              (128u)

/** Default detection threshold: dB ABOVE the clutter baseline. */
#define RADAR_DSP_THRESHOLD_DB   (6.0f)

/**
 * One-time init: builds FFT twiddle/bit-reverse tables and computes the
 * range resolution from the chirp bandwidth. Call from radar task init,
 * BEFORE the first radar_dsp_process().
 *
 * @param bandwidth_hz  Chirp sweep bandwidth (END_FREQ - START_FREQ), Hz.
 */
void radar_dsp_init(double bandwidth_hz);

/**
 * Process one de-interleaved frame of raw ADC samples (one chirp,
 * RADAR_DSP_N samples) and refresh the range snapshot. Radar task context
 * only — not ISR-safe, not multi-core-safe.
 */
void radar_dsp_process(const int16_t *samples, uint32_t n);

/** Runtime threshold override (value = dB-above-baseline * 10, e.g. 60 ->
 *  6.0 dB). Special value 0 = re-capture the static-scene clutter baseline. */
void radar_dsp_set_threshold_x10(uint32_t threshold_x10);

/**
 * Copy the latest range result. Safe to call from the CM55 IPC service task
 * (same core as the writer; a seq re-check guards against torn reads).
 * Returns false until the first frame has been processed.
 */
bool radar_dsp_snapshot(ipc_radar_range_t *out);

#ifdef __cplusplus
}
#endif

#endif /* RADAR_DSP_H */
