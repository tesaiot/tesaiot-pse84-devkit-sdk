/* sdk-example: core=cm55 variant=both group=sensors
 * id:      cm55/sensors/02_radar_presence
 * title:   Read range and presence from the BGT60TR13C radar
 * teaches: the range snapshot is a seqlock and bin 0 is dead; the threshold call has a magic zero that re-learns the room; and the two stats calls that tell a stalled sensor from a stalled task
 * apis:    radar_dsp_snapshot, radar_dsp_set_threshold_x10, radar_dsp_process, tesaiot_radar_task, tesaiot_radar_loop_stats, tesaiot_radar_recover_stats
 * entry:   example_cm55_radar_presence
 */
/*******************************************************************************
 * sensors/02 — the 60 GHz radar, read from the display side.
 *
 * WHOSE TASK, AND WHERE THE PINS ARE
 * ----------------------------------
 * The radar is CM55. proj_cm55/Makefile:233-244 (reference:
 * proj_cm55/Makefile:202-213) puts the tesaiot-radar tree on this core under
 * BSP_HAS_RADAR=1, and main.c creates the task:
 *
 *     xTaskCreate(tesaiot_radar_task, RADAR_TASK_NAME,
 *                 RADAR_TASK_STACK_SIZE, NULL, RADAR_TASK_PRIORITY, NULL);
 *     — reference proj_cm55/main.c:75-77
 *
 * RADAR_TASK_PRIORITY is configMAX_PRIORITIES - 4 (radar_task.h:35). The Edge
 * AI task deliberately sits one priority ABOVE it. That ordering is not
 * decoration: when it was the other way round the radar starved.
 *
 * The SPI is SCB3 on port 21 — MISO P21.4, MOSI P21.5, CLK P21.6, CS P21.7,
 * IRQ/DRDY P20.3, reset CYBSP_RXRES_L, 25 Mbps (bsps/TARGET_KIT_PSE84_AI/
 * config/GeneratedSource/cycfg_pins.h:1053,1074,1095,1116,908; design.modus
 * :2957). Use the CYBSP_RSPI_* macros. Note this is NOT the header SPI: the
 * P9.0-versus-P9.2 chip-select trap belongs to the Arduino/mikroBUS header,
 * not to the radar — see cm55/io/05_arduino_shield.
 *
 * THIS EXAMPLE DOES NOT START THE RADAR
 * -------------------------------------
 * tesaiot_radar_task is main()'s to create, once, before the scheduler is
 * busy. Creating a second one would put two owners on SCB3. The symbol is
 * named below in an unreachable branch so its signature and its one correct
 * call site are documented where a reader will look for them.
 *
 * THE SNAPSHOT IS A SEQLOCK
 * -------------------------
 * radar_dsp_snapshot() retries on a torn or odd sequence and is safe from any
 * CM55 task (radar_dsp.h:53-57). It returns FALSE until the first frame has
 * been processed — that is "not yet", not "no target". "No target" is
 * out.target == 0 with initialized == 1.
 *
 * BIN 0 IS DC AND IS DEAD
 * -----------------------
 * The peak search starts at bin 1. Range per bin is computed at run time from
 * the chirp bandwidth, c / (2 * BW), and reported back in the snapshot as
 * resolution_mm — 0.326 m for the shipped P0 profile (459.804 MHz sweep).
 * Read it from the struct; do not hard-code 326.
 *
 * THE MAGIC ZERO
 * --------------
 * radar_dsp_set_threshold_x10() takes dB-above-baseline times ten, 1..600.
 * ZERO IS NOT "no threshold" — it is a command that re-captures the clutter
 * baseline, resets the peak-tracking state and pauses detection for ~160 ms.
 * Send it with the room empty, or you teach the radar that a person is
 * furniture.
 *
 * NOT BLOCKING
 * ------------
 * Snapshot and stats are volatile word reads. Nothing here waits on SPI.
 *******************************************************************************/

#include "../sdk_examples.h"

#include <string.h>

#include "bsp_feature_flags.h"
#include "ipc_communication.h"

#if BSP_HAS_RADAR
#include "radar_dsp.h"
#include "radar_task.h"
#endif

#if BSP_HAS_RADAR

#define REFRESH_MS      (200u)

/* radar_dsp.h:32 — the compiled default, in dB above the clutter baseline. */
#define THRESHOLD_X10   ((uint32_t)(RADAR_DSP_THRESHOLD_DB * 10.0f))

static lv_obj_t   *s_range;
static lv_obj_t   *s_health;
static lv_timer_t *s_timer;

static void parent_deleted_cb(lv_event_t *e)
{
    (void)e;
    if (s_timer != NULL) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    s_range  = NULL;
    s_health = NULL;
}

static void refresh_cb(lv_timer_t *t)
{
    (void)t;

    ipc_radar_range_t r;
    if (!radar_dsp_snapshot(&r)) {
        if (s_range != NULL) {
            lv_label_set_text(s_range, "no frame processed yet");
        }
    } else if (s_range != NULL) {
        if (r.target != 0u) {
            lv_label_set_text_fmt(s_range,
                                  "target %lu mm  bin %u  %d.%01d dB  bin width %u mm",
                                  (unsigned long)r.distance_mm, (unsigned)r.bin,
                                  (int)r.peak_db,
                                  (int)((r.peak_db - (float)(int)r.peak_db) * 10.0f),
                                  (unsigned)r.resolution_mm);
        } else {
            lv_label_set_text_fmt(s_range,
                                  "no target  seq %u  bin width %u mm",
                                  (unsigned)r.seq, (unsigned)r.resolution_mm);
        }
    }

    /* The two stats calls answer different questions, and the header spells
     * out the decision table (radar_task.h:98-128). Reproduced here because
     * this is where somebody reads it at 2 a.m. */
    uint32_t loops = 0u, frames = 0u, phase = 0u;
    uint32_t tries = 0u, fails = 0u;
    int32_t  last_rc = 0;
    tesaiot_radar_loop_stats(&loops, &frames, &phase);
    tesaiot_radar_recover_stats(&tries, &fails, &last_rc);

    if (s_health != NULL) {
        lv_label_set_text_fmt(s_health,
                              "loops %lu  frames %lu  phase %lu  |  recover %lu/%lu rc %ld",
                              (unsigned long)loops, (unsigned long)frames,
                              (unsigned long)phase, (unsigned long)tries,
                              (unsigned long)fails, (long)last_rc);
    }
}

static void on_relearn(lv_event_t *e)
{
    (void)e;
    sdk_example_log_clear();
    /* THE MAGIC ZERO. Say what it does, every time it is offered. */
    radar_dsp_set_threshold_x10(0u);
    sdk_example_logf("baseline re-capture requested (threshold_x10 == 0)");
    sdk_example_logf("  this is NOT 'threshold off'. It re-learns the empty");
    sdk_example_logf("  room, resets peak tracking and pauses detection for");
    sdk_example_logf("  about 160 ms. Stand clear while it runs.");
}

static void on_default(lv_event_t *e)
{
    (void)e;
    sdk_example_log_clear();
    radar_dsp_set_threshold_x10(THRESHOLD_X10);
    sdk_example_logf("threshold back to the compiled default, %u (= %d.%d dB)",
                     (unsigned)THRESHOLD_X10,
                     (int)RADAR_DSP_THRESHOLD_DB,
                     (int)((RADAR_DSP_THRESHOLD_DB -
                            (float)(int)RADAR_DSP_THRESHOLD_DB) * 10.0f));
    sdk_example_logf("  legal range is 1..600, i.e. 0.1 .. 60.0 dB");
}

int example_cm55_radar_presence(lv_obj_t *parent)
{
    if (s_timer != NULL) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    lv_obj_remove_event_cb(parent, parent_deleted_cb);
    lv_obj_add_event_cb(parent, parent_deleted_cb, LV_EVENT_DELETE, NULL);

    lv_obj_t *caption = lv_label_create(parent);
    lv_label_set_text(caption, "BGT60TR13C on SCB3 (P21.4-7), 128-pt FFT, bin 0 is DC");
    lv_obj_set_style_text_color(caption, lv_color_hex(0xD8E0F0), 0);
    lv_obj_set_pos(caption, 10, 8);

    s_range = lv_label_create(parent);
    lv_obj_set_pos(s_range, 10, 36);
    lv_obj_set_style_text_color(s_range, lv_color_hex(0xB8C4D8), 0);
    lv_label_set_text(s_range, "...");

    s_health = lv_label_create(parent);
    lv_obj_set_pos(s_health, 10, 58);
    lv_obj_set_style_text_color(s_health, lv_color_hex(0x8FA0B8), 0);
    lv_label_set_text(s_health, "...");

    lv_obj_t *b1 = lv_button_create(parent);
    lv_obj_set_pos(b1, 10, 88);
    lv_obj_set_size(b1, 200, 44);
    lv_obj_add_event_cb(b1, on_relearn, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l1 = lv_label_create(b1);
    lv_label_set_text(l1, "re-learn the room");
    lv_obj_center(l1);

    lv_obj_t *b2 = lv_button_create(parent);
    lv_obj_set_pos(b2, 220, 88);
    lv_obj_set_size(b2, 200, 44);
    lv_obj_add_event_cb(b2, on_default, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l2 = lv_label_create(b2);
    lv_label_set_text(l2, "default threshold");
    lv_obj_center(l2);

    if (!tesaiot_radar_initialized) {
        sdk_example_logf("radar task reports NOT initialised.");
        sdk_example_logf("  The task parks in a 1 s sleep loop rather than");
        sdk_example_logf("  dying, so the UI still boots — which is why the");
        sdk_example_logf("  screen looks healthy while the radar is not.");
        sdk_example_logf("  tesaiot_radar_loop_stats() phase says where it is.");
    }

    if (false) {
        /* Unreachable. main() owns this, once, before the scheduler is busy:
         *   xTaskCreate(tesaiot_radar_task, RADAR_TASK_NAME,
         *               RADAR_TASK_STACK_SIZE, NULL, RADAR_TASK_PRIORITY, NULL);
         * A second task would put two owners on SCB3.
         *
         * radar_dsp_process() is likewise the radar task's alone — the header
         * says "radar task context only, not ISR-safe, not multi-core-safe"
         * (radar_dsp.h:46-47). Calling it from here races the writer that
         * radar_dsp_snapshot()'s seqlock is protecting you FROM. */
        tesaiot_radar_task(NULL);
        static int16_t chirp[RADAR_DSP_N];
        radar_dsp_process(chirp, RADAR_DSP_N);
    }

    s_timer = lv_timer_create(refresh_cb, REFRESH_MS, NULL);
    if (s_timer == NULL) {
        sdk_example_logf("lv_timer_create failed — nothing will refresh");
        return SDK_EX_REFUSED;
    }

    sdk_example_logf("polling the range snapshot every %u ms", REFRESH_MS);
    sdk_example_logf("  distance 0 with target 0 = nothing above threshold;");
    sdk_example_logf("  snapshot false = no frame yet. They are not the same.");
    return SDK_EX_STARTED;
}

#else  /* !BSP_HAS_RADAR */

int example_cm55_radar_presence(lv_obj_t *parent)
{
    (void)parent;
    sdk_example_logf("No radar on this board (BSP_HAS_RADAR is 0).");
    sdk_example_logf("  The BGT60TR13C is fitted on the AI Kit SoM only.");
    sdk_example_logf("  proj_cm55/Makefile drops the whole tesaiot-radar tree");
    sdk_example_logf("  when the flag is off, so there is nothing to call.");
    return SDK_EX_UNAVAILABLE;
}

#endif /* BSP_HAS_RADAR */
