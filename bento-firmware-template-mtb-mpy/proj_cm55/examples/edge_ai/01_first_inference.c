/* sdk-example: core=cm55 variant=both group=edge_ai
 * id:      cm55/edge_ai/01_first_inference
 * title:   Run your first inference
 * teaches: create the engine, activate one model, feed it at its training rate, read the verdict back
 * apis:    ai_engine_init, ai_engine_model_count, ai_engine_model, ai_engine_set_sensor_rate, ai_engine_start, ai_engine_requested, ai_engine_active, ai_engine_snapshot, ai_engine_stop, ai_engine_resume_sensor
 * entry:   example_edge_ai_first_inference
 */
/*
 * The smallest complete Edge AI session: bring the engine up, choose a model,
 * feed it at the rate it was TRAINED at, read one verdict, put everything back.
 *
 * FOUR THINGS THAT ARE EASY TO GET WRONG
 *
 *  1. THE RATE IS PART OF THE MODEL. An IMU model here is trained at 50 Hz,
 *     which is a 20 ms push from CM33. The dashboard runs the same sensor at
 *     100 ms. Start a model without asking for 20 ms and its window covers
 *     five times the span of time it was trained on: every verdict is computed
 *     over the wrong stretch of history and is confidently wrong. Nothing on
 *     screen says so. ai_engine_set_sensor_rate(20) is what prevents it.
 *
 *  2. ai_engine_requested() IS NOT ai_engine_active(). start() records a wish;
 *     the inference task acts on it a tick or two later, after it has cold-
 *     initialised the model. Poll active() to learn that the switch happened,
 *     and read requested() to see what it is heading towards. A fallback that
 *     tests active() fires while a perfectly good start is still in flight and
 *     clobbers the selection.
 *
 *  3. A SNAPSHOT OUTLIVES ITS SESSION. ai_engine_snapshot() keeps answering
 *     with the last verdict any model published, including one from a previous
 *     run of this example. Check result.model_index before believing it.
 *
 *  4. ai_engine_resume_sensor() IS NOT THE OPPOSITE OF set_sensor_rate(). Both
 *     are built in ONE shared IPC message buffer inside the engine and the send
 *     is asynchronous -- CM33 reads the buffer after the call returns. Issue a
 *     resume in the same breath as a rate request and the resume's memset wipes
 *     a message still in flight; CM33 then reads a zeroed command and pauses
 *     the sensor push instead. Use it standalone, seconds later, as this file
 *     does at teardown.
 *
 * The wait is an lv_timer. run() is called from the GFX task and must return
 * promptly: a busy-wait here would freeze the display AND the busy overlay that
 * would have explained the freeze.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "../sdk_examples.h"
#include "ai_engine.h"

#define POLL_MS         (1000u)   /* coarse on purpose: one log line per tick */
#define SESSION_MS      (8000u)   /* long enough for a 2 s window to fill     */
#define IMU_MODEL_MS    (20u)     /* 50 Hz -- what the IMU models were trained at */

static struct {
    lv_timer_t *timer;
    uint32_t    elapsed_ms;
    uint8_t     model;
    bool        raised_rate;
    bool        running;
} s;

static const char *sensor_name(ai_sensor_t s_id)
{
    switch (s_id) {
        case AI_SENSOR_IMU:   return "IMU";
        case AI_SENSOR_RADAR: return "radar";
        case AI_SENSOR_MIC:   return "mic";
        default:              return "?";
    }
}

static void session_end(void)
{
    ai_engine_stop();

    /* Standalone, and a long way from the op=2 rate request above -- see note
     * 4 in the header. Only sent if we actually raised the rate; asking CM33
     * to resume a push we never altered is a message for nothing. */
    if (s.raised_rate) {
        ai_engine_resume_sensor();
        s.raised_rate = false;
    }
    if (s.timer != NULL) {
        lv_timer_delete(s.timer);
        s.timer = NULL;
    }
    s.running = false;
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    s.elapsed_ms += POLL_MS;

    ai_result_t r;
    /* model_index is the guard: without it the first tick reports a verdict
     * this session did not produce (note 3). */
    if (ai_engine_snapshot(&r) && (r.model_index == s.model) &&
        (r.inferences > 0u)) {

        uint8_t   which = 1u;
        const int pct   = ai_result_top_positive(&r, &which);
        const ai_model_desc_t *d = ai_engine_model(r.model_index);
        const char *label = "?";
        if ((d != NULL) && (which < d->class_count) &&
            (d->class_labels[which] != NULL)) {
            label = d->class_labels[which];
        }

        /* top_positive skips class 0 by registry contract -- class 0 means
         * "nothing is happening", and reporting it as a detection is how a
         * quiet room reads as 99% idle. */
        sdk_example_logf("verdict: %s %d%% (top class %u of %u)",
                         label, pct, (unsigned)which, (unsigned)r.class_count);
        sdk_example_logf("%lu inference(s), last %lu us, worst %lu us",
                         (unsigned long)r.inferences,
                         (unsigned long)r.inference_us,
                         (unsigned long)r.inference_us_max);
        sdk_example_logf("stopping, and handing the sensor back to 100 ms");
        session_end();
        return;
    }

    if (s.elapsed_ms >= SESSION_MS) {
        sdk_example_logf("no verdict in %lu ms: requested=%d active=%d",
                         (unsigned long)s.elapsed_ms,
                         ai_engine_requested(), ai_engine_active());
        sdk_example_logf("requested set with active still -1 means the model's"
                         " own init() failed -- 07_engine_health reads that");
        session_end();
        return;
    }

    sdk_example_logf("t=%lums requested=%d active=%d, window still filling",
                     (unsigned long)s.elapsed_ms,
                     ai_engine_requested(), ai_engine_active());
}

int example_edge_ai_first_inference(lv_obj_t *parent)
{
    (void)parent;   /* the numbers go to the log; nothing is drawn */

    if (s.running) {
        sdk_example_logf("a session is already running -- let it finish");
        return SDK_EX_BUSY;
    }

    /* Idempotent: it returns true when the inference task already exists, so
     * calling it from a page that may be entered twice is safe. It creates the
     * task IDLE -- nothing infers until ai_engine_start(). */
    if (!ai_engine_init()) {
        sdk_example_logf("ai_engine_init() failed: the inference task could not"
                         " be created. CM55 heap is exhausted");
        return SDK_EX_UNAVAILABLE;
    }

    const uint32_t count = ai_engine_model_count();
    if (count == 0u) {
        sdk_example_logf("the registry is empty -- build with"
                         " BENTO_HAS_EDGE_AI=1 and at least one model");
        return SDK_EX_UNAVAILABLE;
    }

    /* Prefer an IMU model, because the sensor-rate step is the lesson and only
     * an IMU model exercises it. Radar and mic models run pipelines that live
     * on this core and need no rate request. */
    uint32_t idx = 0u;
    for (uint32_t i = 0u; i < count; i++) {
        const ai_model_desc_t *d = ai_engine_model(i);
        if ((d != NULL) && (d->sensor == AI_SENSOR_IMU)) {
            idx = i;
            break;
        }
    }

    const ai_model_desc_t *desc = ai_engine_model(idx);
    if (desc == NULL) {
        sdk_example_logf("ai_engine_model(%lu) is NULL although model_count()"
                         " is %lu", (unsigned long)idx, (unsigned long)count);
        return SDK_EX_UNAVAILABLE;
    }

    memset(&s, 0, sizeof(s));
    s.model = (uint8_t)idx;

    sdk_example_logf("%lu model(s); using [%lu] '%s' (%s, %u classes, %ums)",
                     (unsigned long)count, (unsigned long)idx,
                     (desc->name != NULL) ? desc->name : "?",
                     sensor_name(desc->sensor),
                     (unsigned)desc->class_count,
                     (unsigned)desc->period_ms);

    if (desc->sensor == AI_SENSOR_IMU) {
        ai_engine_set_sensor_rate(IMU_MODEL_MS);
        s.raised_rate = true;
        sdk_example_logf("asked CM33 for a %ums IMU push (%u Hz) -- the rate"
                         " this model was trained at",
                         (unsigned)IMU_MODEL_MS,
                         (unsigned)(1000u / IMU_MODEL_MS));
    } else {
        sdk_example_logf("%s model: its pipeline lives on CM55, so there is no"
                         " CM33 rate to raise", sensor_name(desc->sensor));
    }

    if (!ai_engine_start(idx)) {
        sdk_example_logf("ai_engine_start(%lu) refused: index out of range, or"
                         " the inference task does not exist",
                         (unsigned long)idx);
        if (s.raised_rate) {
            ai_engine_resume_sensor();
            s.raised_rate = false;
        }
        return SDK_EX_REFUSED;
    }

    sdk_example_logf("start() accepted: requested=%d, active=%d (active lags"
                     " by a task tick -- that is normal)",
                     ai_engine_requested(), ai_engine_active());

    s.timer = lv_timer_create(tick_cb, POLL_MS, NULL);
    if (s.timer == NULL) {
        sdk_example_logf("no LVGL timer available -- unwinding the session");
        session_end();
        return SDK_EX_UNAVAILABLE;
    }
    s.running = true;

    return SDK_EX_STARTED;
}
