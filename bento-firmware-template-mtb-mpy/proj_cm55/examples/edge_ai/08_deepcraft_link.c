/* sdk-example: core=cm55 variant=both group=edge_ai
 * id:      cm55/edge_ai/08_deepcraft_link
 * title:   Drive a model through the DEEPCRAFT link
 * teaches: go through the model link instead of poking the engine, so the READY/STOPPED events fire and CM33 raises the sensor rate -- and keep the watchdog ticking
 * apis:    deepcraft_task_init, deepcraft_task_select, deepcraft_task_request, deepcraft_task_watchdog
 * entry:   example_cm55_deepcraft_link
 */
/*
 * There are two ways to activate a model from CM55, and they are not
 * equivalent.
 *
 *   ai_engine_start(n)        switches the engine. Nothing else learns of it.
 *   deepcraft_task_select(n)  posts SELECT on the model link's command queue.
 *                             The link emits its usual READY / STOPPED events,
 *                             a MicroPython client subscribed to them sees the
 *                             change, and -- the part that actually matters --
 *                             CM33 raises the accelerometer push to the
 *                             model's training rate.
 *
 * Skip the link and an IMU model runs at the dashboard's 100 ms cadence
 * instead of the 20 ms it was trained at: every verdict is computed over five
 * times the intended span of time and is confidently wrong. This is why the
 * on-screen Edge AI page calls deepcraft_task_select() for a tap rather than
 * touching the engine, and why your code should too.
 *
 * ORDER OF OPERATIONS
 *
 *   1. cm55_ipc_communication_setup()  -- done during display bring-up.
 *   2. deepcraft_task_init()           -- creates the task and registers the
 *                                         pipe client. Idempotent; returns
 *                                         true if it already exists. Calling
 *                                         it before step 1 registers a client
 *                                         on a pipe that is not there yet.
 *   3. deepcraft_task_select(n)        -- n is a registry index, n < 16.
 *   4. deepcraft_task_request(true)    -- start; (false) to stop.
 *
 * THE WATCHDOG IS NOT OPTIONAL. deepcraft_task_watchdog() must be called at
 * about 1 Hz from a context that never blocks. The model-link task has been
 * seen asleep inside its own queue receive with a command outstanding and a
 * 100 ms timeout that never fired -- randomly, after 138, 239 and 335
 * switches. Left alone the Edge AI menu stops answering until the board is
 * unplugged. The watchdog does not fix the underlying fault; it detects the
 * backlog and unsticks the task. This example ticks it for the length of the
 * session; a real application hangs it off a permanent 1 Hz timer that lives
 * as long as the firmware does.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "../sdk_examples.h"
#include "ai_engine.h"
#include "deepcraft_task.h"

#define TICK_MS      (1000u)   /* the watchdog's required cadence */
#define SESSION_MS   (6000u)
#define SELECT_MAX   (16u)     /* the link rejects n >= 0x10 */

static struct {
    lv_timer_t *timer;
    uint32_t    elapsed_ms;
    bool        running;
} s;

static const char *safe(const char *t) { return (t != NULL) ? t : "(null)"; }

static void session_end(void)
{
    deepcraft_task_request(false);   /* STOP through the link, not the engine */
    if (s.timer != NULL) { lv_timer_delete(s.timer); s.timer = NULL; }
    s.running = false;
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    s.elapsed_ms += TICK_MS;

    /* 1 Hz, from the GFX task, which cannot block. Cheap on a healthy board:
     * it compares two counters and returns. */
    deepcraft_task_watchdog();

    ai_result_t r;
    if (ai_engine_snapshot(&r)) {
        uint8_t   which = 1u;
        const int pct   = ai_result_top_positive(&r, &which);
        const ai_model_desc_t *d = ai_engine_model(r.model_index);
        sdk_example_logf("t=%lus  %s %d%%  (%lu inferences)",
                         (unsigned long)(s.elapsed_ms / 1000u),
                         (d != NULL && which < d->class_count)
                             ? safe(d->class_labels[which]) : "?",
                         pct, (unsigned long)r.inferences);
    } else {
        sdk_example_logf("t=%lus  no verdict yet (engine active=%d)",
                         (unsigned long)(s.elapsed_ms / 1000u),
                         ai_engine_active());
    }

    if (s.elapsed_ms >= SESSION_MS) {
        sdk_example_logf("stopping through the link so STOPPED reaches every"
                         " subscriber and CM33 drops back to 100 ms");
        session_end();
    }
}

int example_cm55_deepcraft_link(lv_obj_t *parent)
{
    (void)parent;   /* results go to the log; nothing is drawn */

    if (s.running) {
        sdk_example_logf("a link session is already running");
        return SDK_EX_BUSY;
    }

    /* Safe to call again -- it returns true when the task already exists. The
     * IPC pipe must be up first, which it is by the time any UI runs. */
    if (!deepcraft_task_init()) {
        sdk_example_logf("deepcraft_task_init() failed: no queue or no task."
                         " Was cm55_ipc_communication_setup() called first?");
        return SDK_EX_UNAVAILABLE;
    }

    const uint32_t n = ai_engine_model_count();
    if (n == 0u) {
        sdk_example_logf("no models in the registry -- nothing to select");
        return SDK_EX_UNAVAILABLE;
    }

    /* Prefer an IMU model: the sensor-rate handover is the whole point of
     * going through the link, and only an IMU model exercises it. */
    uint32_t idx = 0u;
    for (uint32_t i = 0u; i < n && i < SELECT_MAX; i++) {
        const ai_model_desc_t *d = ai_engine_model(i);
        if (d != NULL && d->sensor == AI_SENSOR_IMU) { idx = i; break; }
    }
    if (idx >= SELECT_MAX) {
        sdk_example_logf("registry index %lu is out of the link's SELECT range"
                         " (n < %u)", (unsigned long)idx, (unsigned)SELECT_MAX);
        return SDK_EX_REFUSED;
    }

    memset(&s, 0, sizeof(s));

    {
        const ai_model_desc_t *d = ai_engine_model(idx);
        sdk_example_logf("select(%lu) -> '%s'", (unsigned long)idx,
                         (d != NULL) ? safe(d->name) : "?");
    }

    /* Both are fire-and-forget: they enqueue a command and return. Neither
     * reports whether the model-link task acted on it -- the engine state a
     * second later does, which is what the timer below reads. */
    deepcraft_task_select(idx);
    deepcraft_task_request(true);

    s.timer = lv_timer_create(tick_cb, TICK_MS, NULL);
    if (s.timer == NULL) {
        deepcraft_task_request(false);
        return SDK_EX_UNAVAILABLE;
    }
    s.running = true;

    sdk_example_logf("started; ticking the watchdog at 1 Hz for %lu ms",
                     (unsigned long)SESSION_MS);
    return SDK_EX_STARTED;
}
