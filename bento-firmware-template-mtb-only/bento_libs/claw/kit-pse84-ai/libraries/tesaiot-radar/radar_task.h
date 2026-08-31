/******************************************************************************
 * \file radar_task.h
 *
 * \brief
 *     TESAIoT Radar Task — BGT60TR13C 60 GHz radar presence detection.
 *
 *     FreeRTOS task that reads radar data via SPI, calculates signal energy,
 *     and sets a presence flag used by lcd_task for LCD power management.
 *
 *******************************************************************************
 * SPDX-FileCopyrightText: 2025-2026 TESAIoT Foundation Platform
 *
 * \author Assoc. Prof. Wiroon Sriborrirux and TESA Developer Team
 *
 * Link: https://tesaiot.github.io/developer-hub/
 ******************************************************************************/

#ifndef RADAR_TASK_H
#define RADAR_TASK_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Configuration
 ******************************************************************************/

/** Radar task FreeRTOS parameters */
#define RADAR_TASK_NAME                 "Radar Task"
#define RADAR_TASK_STACK_SIZE           (8192U)
#define RADAR_TASK_PRIORITY             (configMAX_PRIORITIES - 4)  /* Keep well below GFX/app tasks */

/** Presence detection parameters */
#define RADAR_PRESENCE_THRESHOLD        20000.0f  /**< Smoothed delta threshold for motion-based presence detection */
#define RADAR_PRESENCE_ON_FRAMES        3         /**< Consecutive frames to confirm presence */
#define RADAR_ABSENCE_OFF_FRAMES        30        /**< Frames before absence (30 * ~5ms = ~150ms) */
#define RADAR_ABSENCE_TIMEOUT_MS        5000      /**< Total timeout before LCD off */

/** SPI and FIFO configuration */
#define RADAR_DEFAULT_FIFO_SETTING      128       /**< One frame per read to minimize SPI/load bursts */
#define RADAR_RING_BUFFER_SIZE          0x00004000 /**< 16K samples (32KB) */
#define RADAR_RING_BUFFER_MASK          0x00003FFF
#define RADAR_RING_BUFFER_MASK32        0x00001FFF
/* Must keep up with the sensor's hardware-timed 200 fps free-run: at ~5-6 ms
 * per task cycle a cap of 1 throttled processing (and the Edge AI publish) to
 * the loop rate (~88-170 fps) and let the backlog grow. 4 per cycle absorbs
 * scheduling jitter while bounding worst-case task time (~4 x 350 us). */
#define RADAR_MAX_FRAMES_PER_CYCLE      4

/*******************************************************************************
 * Types
 ******************************************************************************/

/** Radar presence state */
typedef enum {
    RADAR_PRESENCE_ABSENT   = 0,    /**< No person detected */
    RADAR_PRESENCE_DETECTED = 1,    /**< Person present */
} radar_presence_state_t;

/*******************************************************************************
 * Global Variables (extern)
 ******************************************************************************/

/**
 * Radar presence flag — set by tesaiot_radar_task, read by lcd_task.
 * volatile for cross-task access.
 */
extern volatile bool tesaiot_radar_presence_detected;

/**
 * Current radar signal energy — for debugging/display.
 */
extern volatile float tesaiot_radar_current_energy;

/**
 * Radar initialized flag.
 */
extern volatile bool tesaiot_radar_initialized;

/*******************************************************************************
 * Public Functions
 ******************************************************************************/

/**
 * Radar FreeRTOS task entry point.
 *
 * Initializes SPI, configures BGT60TR13C, and enters a polling loop
 * that reads radar data, calculates energy, and updates presence flag.
 *
 * \param[in] arg  Unused task argument.
 */
void tesaiot_radar_task(void *arg);

/** How the frame-sequencer stall recovery is going.
 *
 *  The BGT60 can halt with FSTAT reading empty and no error bit, and the
 *  watchdog in the radar task restarts it once a second. Every step of that
 *  restart is an SPI transaction that can itself fail, in which case the
 *  restart accomplishes nothing and says nothing -- the sensor simply stops,
 *  which is what "Energy freezes after a minute or two" looks like from the UI.
 *
 *  tries climbing with fails at 0   -> recovery is running and working; the
 *                                      stall is real but self-healing
 *  tries and fails climbing together -> the SPI link is down; restarting the
 *                                      sequencer cannot work and never will
 *  tries flat while frames are frozen -> the radar task itself is not running
 *
 *  Any argument may be NULL. Safe to call from another task: three volatile
 *  word reads, no lock. */
void tesaiot_radar_recover_stats(uint32_t *tries, uint32_t *fails, int32_t *last_rc);

/** Whether the radar task's main loop is still going round, and where it is.
 *
 *  The stall watchdog lives at the bottom of that loop, so it can only fire if
 *  the loop gets there. phase is one of the RADAR_PH_* values; the SPI ones
 *  (FSTAT, FIFO, RECOVER) each end in an unbounded spin inside the vendor
 *  platform layer waiting on a flag only the SCB interrupt clears.
 *
 *  loops frozen with phase on an SPI value -> stuck in that spin; nothing
 *                                            downstream can recover it
 *  loops climbing, frames frozen           -> the loop is fine and the sensor
 *                                            has stopped delivering
 *
 *  Any argument may be NULL. Three volatile word reads, no lock. */
void tesaiot_radar_loop_stats(uint32_t *loops, uint32_t *frames, uint32_t *phase);

#if defined(BENTO_HAS_EDGE_AI) && (BENTO_HAS_EDGE_AI == 1)
/**
 * Fetch the NEXT unread raw int16 128-sample radar chirp for the Edge AI model.
 *
 * Chirps queue through an 8-deep ring so the temporal model receives EVERY
 * chirp in order (a newest-only slot dropped every chirp but the last of each
 * processing burst — 88/s reached the model out of the radar's 200/s, which
 * wrecked the inter-chirp Doppler pattern the net classifies on). Single reader
 * (CM55 ai_engine feed_radar); lock-free and bounded against the radar-task
 * writer. Call repeatedly until it returns false to drain the backlog.
 *
 * \param[out]    out     buffer of at least 128 int16 samples (raw ADC counts).
 * \param[in,out] cursor  reader position; initialise to UINT32_MAX (or 0) —
 *                        the first call resyncs it and returns false.
 * \return true if \p out holds the next chirp, false when caught up/resyncing.
 */
bool radar_ai_frame_next(int16_t *out, uint32_t *cursor);
#endif

#ifdef __cplusplus
}
#endif

#endif /* RADAR_TASK_H */
