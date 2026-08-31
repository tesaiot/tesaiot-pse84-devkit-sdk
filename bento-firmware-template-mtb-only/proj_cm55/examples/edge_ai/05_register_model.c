/* sdk-example: core=cm55 variant=both group=edge_ai
 * id:      cm55/edge_ai/05_register_model
 * title:   Register your own model, run it, release it
 * teaches: fill an ai_model_desc_t with your own entry points, join the registry at run time, then hand the resources back
 * apis:    ai_engine_register, ai_engine_dyn_count, ai_engine_dyn_capacity, ai_engine_model_count, ai_engine_model, ai_engine_init, ai_engine_start, ai_engine_snapshot_model, ai_engine_active, ai_engine_stop, ai_engine_unload, ai_engine_unload_done, ai_engine_unload_refused
 * entry:   example_edge_ai_register_model
 */
/*
 * ai_engine_register() is the engine's open extension point. A model is four
 * function pointers and a descriptor; nothing about it has to come from a
 * DEEPCRAFT export, and nothing about it needs a rebuilt engine or a slot name
 * chosen in advance. The detector below is ordinary C -- a sliding window over
 * the BMI270 that tells shaking from stillness -- and once registered it is an
 * ordinary registry row: it appears in the Edge AI menu, ai_engine_start()
 * takes its index, and a set can hold it.
 *
 * THE FOUR ENTRY POINTS, AND WHAT THE ENGINE EXPECTS BACK
 *
 *   int  init(void)             0 on success. Called on the COLD path, on the
 *                               inference task, the first time the model is
 *                               selected and again after any release. Allocate
 *                               and zero here, not in the descriptor.
 *   int  enqueue(const float*)  0 = frame accepted. Non-zero = not accepted;
 *                               the engine treats that as "skip this sample"
 *                               and drains with dequeue on the same pass, so
 *                               it must never mean "error, retry".
 *   int  dequeue(float *out)    0 = a FRESH verdict is in out[0..class_count).
 *                               Non-zero = nothing new. This is called on
 *                               every pass; returning 0 with a stale buffer is
 *                               how a model appears to fire continuously.
 *   void finalize(void)         release everything. NOT optional if you ever
 *                               intend to unload: the engine refuses an unload
 *                               whose descriptor has no finalize.
 *
 * THE FRAME. An IMU model is handed six floats per call: accel x, y, z in g,
 * then gyro x, y, z in dps, with the X and Y axes already negated to match the
 * training rig. Six is the whole array. Reading a seventh reads past the
 * caller's stack, in bounds of the frame and silently wrong, fifty times a
 * second.
 *
 * THE RULES THAT BITE
 *
 *  1. THE DESCRIPTOR IS COPIED. THE POINTERS IN IT ARE NOT. name, description
 *     and every class_labels[] entry must outlive the boot, and they are read
 *     from an ISR -- the IPC callback answers registry queries. String literals
 *     and static buffers qualify. A stack buffer, or a name built into a local
 *     char[], is a dangling pointer read from interrupt context.
 *  2. CLASS 0 IS THE NEGATIVE CLASS. Registry contract: index 0 means "nothing
 *     is happening". Every summary percentage in this SDK is a maximum over
 *     classes 1 and up, so a model whose classes are all positive renders as if
 *     its loudest class were permanently firing.
 *  3. NAMES ARE UNIQUE. They are the key for set membership and for the watch
 *     thresholds. A duplicate name is refused, which means a second run of this
 *     example must FIND its row rather than register it again -- exactly what
 *     the code below does.
 *  4. TASK CONTEXT ONLY. Registration runs inside a critical section because
 *     the registry is read from an ISR. Never call it from one.
 *  5. ROWS ARE NEVER REMOVED. Unloading frees what the model owns; the row and
 *     its index stay valid, and dyn_count() does not go back down. Capacity is
 *     a per-BOOT budget.
 *
 * RATE. This file activates through the engine, so the CM33 IMU push stays at
 * its dashboard cadence -- the window below is sized in FRAMES and covers more
 * wall-clock time at 10 Hz than at 50 Hz. 08_deepcraft_link is the route that
 * raises the push rate to what a model was trained at.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "../sdk_examples.h"
#include "ai_engine.h"

/*******************************************************************************
 * The model: a windowed shake detector over the BMI270
 *
 * Deliberately arithmetic only -- no libm call, no allocation, no table. An
 * example that drags in a dependency is an example that fails to link in
 * somebody else's project for a reason that has nothing to do with the lesson.
 ******************************************************************************/
#define SHAKE_FRAME_FLOATS  (6u)      /* what the IMU path supplies, exactly  */
#define SHAKE_WINDOW        (24u)     /* frames of history per decision       */
#define SHAKE_HOP           (8u)      /* frames between decisions             */
#define SHAKE_CLASSES       (2u)

/* Scales that turn the two features into a 0..1 confidence. Both are SQUARED
 * magnitudes, so no square root is needed anywhere:
 *   accel spread, g^2  -- a still board holds |a|^2 at 1.0 whatever its
 *                         orientation, so gravity cancels in the deviation
 *   gyro energy, dps^2 -- a still board reads a few, a shaken one tens of
 *                         thousands
 * Measured on a bench BMI270 at rest and hand-shaken; adjust for your mounting. */
#define SHAKE_ACC_REF       (0.30f)
#define SHAKE_GYR_REF       (4000.0f)

static struct {
    bool     ready;
    float    acc2[SHAKE_WINDOW];      /* |accel|^2 per frame, g^2   */
    float    gyr2[SHAKE_WINDOW];      /* |gyro|^2  per frame, dps^2 */
    uint32_t head;
    uint32_t filled;
    uint32_t since;
    bool     have;
    float    out[SHAKE_CLASSES];
} s_shake;

static float absf(float v)
{
    return (v < 0.0f) ? -v : v;
}

/* One decision over the whole window. Runs on the inference task, inside
 * enqueue, which is where the engine expects a model's work to happen. */
static void shake_decide(void)
{
    float amean = 0.0f;
    float gmean = 0.0f;
    for (uint32_t i = 0u; i < SHAKE_WINDOW; i++) {
        amean += s_shake.acc2[i];
        gmean += s_shake.gyr2[i];
    }
    amean /= (float)SHAKE_WINDOW;
    gmean /= (float)SHAKE_WINDOW;

    /* Mean absolute deviation, not variance: one pass, no large intermediate,
     * and it does not let a single spike dominate the window. */
    float aspread = 0.0f;
    for (uint32_t i = 0u; i < SHAKE_WINDOW; i++) {
        aspread += absf(s_shake.acc2[i] - amean);
    }
    aspread /= (float)SHAKE_WINDOW;

    float energy = (aspread / SHAKE_ACC_REF) + (gmean / SHAKE_GYR_REF);
    if (energy < 0.0f) {
        energy = 0.0f;
    }
    /* energy/(1+energy) saturates at 1 without an exponential, so a violent
     * shake cannot push a score above 1.0 and break the percentage. */
    const float p = energy / (1.0f + energy);

    s_shake.out[0] = 1.0f - p;        /* class 0: still -- the NEGATIVE class */
    s_shake.out[1] = p;               /* class 1: shake                       */
    s_shake.have   = true;
}

static int shake_init(void)
{
    /* Cold start, every time. The engine calls this again after a release, and
     * a window left holding pre-release history would answer from data the
     * caller believes was thrown away. */
    memset(&s_shake, 0, sizeof(s_shake));
    s_shake.ready = true;
    return 0;
}

static int shake_enqueue(const float *in)
{
    if (!s_shake.ready || (in == NULL)) {
        return -1;
    }

    /* Exactly SHAKE_FRAME_FLOATS are read. accel g, then gyro dps. */
    s_shake.acc2[s_shake.head] = (in[0] * in[0]) + (in[1] * in[1]) + (in[2] * in[2]);
    s_shake.gyr2[s_shake.head] = (in[3] * in[3]) + (in[4] * in[4]) + (in[5] * in[5]);

    s_shake.head = (s_shake.head + 1u) % SHAKE_WINDOW;
    if (s_shake.filled < SHAKE_WINDOW) {
        s_shake.filled++;
    }
    s_shake.since++;

    /* Nothing is said until the window is genuinely full: a verdict computed
     * over a half-empty buffer is not a quiet reading, it is a wrong one. */
    if ((s_shake.filled >= SHAKE_WINDOW) && (s_shake.since >= SHAKE_HOP)) {
        s_shake.since = 0u;
        shake_decide();
    }
    return 0;
}

static int shake_dequeue(float *out)
{
    if (!s_shake.ready || (out == NULL)) {
        return -1;
    }
    if (!s_shake.have) {
        return -1;                    /* nothing new -- the engine moves on */
    }
    out[0] = s_shake.out[0];
    out[1] = s_shake.out[1];
    s_shake.have = false;             /* consumed: never publish it twice */
    return 0;
}

static void shake_finalize(void)
{
    /* Free whatever you own here. This model owns only static memory, so the
     * work is to stop accepting frames: the engine clears its own ready flag
     * right after this returns, but enqueue must not keep succeeding in the
     * window between. */
    s_shake.ready = false;
    s_shake.have  = false;
}

/* Every pointer here is a string literal, so it lives in flash for the boot --
 * see rule 1. static, so the descriptor itself is stable too; the engine copies
 * it, but keeping it static costs nothing and removes the question. */
static const ai_model_desc_t k_shake_desc = {
    .name         = "Shake Detector",
    .description  = "sliding window over the BMI270: agitation against still",
    .sensor       = AI_SENSOR_IMU,
    .class_count  = (uint8_t)SHAKE_CLASSES,
    .class_labels = { "still", "shake" },   /* [0] is the negative class */
    .flash_bytes  = 0u,                     /* no weights: it is arithmetic */
    .period_ms    = 800u,                   /* SHAKE_HOP frames at the 100 ms
                                             * dashboard push; four times
                                             * faster through the model link */
    .init         = shake_init,
    .enqueue      = shake_enqueue,
    .dequeue      = shake_dequeue,
    .finalize     = shake_finalize,         /* required for unload to succeed */
};

/*******************************************************************************
 * The session: register -> run -> release
 ******************************************************************************/
#define POLL_MS     (1000u)
#define RUN_TICKS   (4u)
#define WAIT_TICKS  (4u)

typedef enum {
    PH_RUN = 0,     /* the model is inferring                       */
    PH_DRAIN,       /* stop requested; waiting for the engine to go idle */
    PH_UNLOAD       /* unload requested; waiting for done or refused */
} phase_t;

static struct {
    lv_timer_t *timer;
    uint32_t    ticks;
    uint32_t    idx;
    uint32_t    done0;
    uint32_t    refused0;
    phase_t     phase;
    bool        running;
} s;

static void session_end(void)
{
    ai_engine_stop();
    if (s.timer != NULL) {
        lv_timer_delete(s.timer);
        s.timer = NULL;
    }
    s.running = false;
}

/* Rows are never removed, so a second tap must reuse the row the first one
 * made. strcmp, because names are the registry's key -- the same test the
 * engine itself applies when it refuses a duplicate. */
static int find_row(const char *name)
{
    const uint32_t n = ai_engine_model_count();
    for (uint32_t i = 0u; i < n; i++) {
        const ai_model_desc_t *d = ai_engine_model(i);
        if ((d != NULL) && (d->name != NULL) && (strcmp(d->name, name) == 0)) {
            return (int)i;
        }
    }
    return -1;
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    s.ticks++;

    if (s.phase == PH_RUN) {
        ai_result_t r;
        if (ai_engine_snapshot_model(s.idx, &r)) {
            uint8_t   which = 1u;
            const int pct   = ai_result_top_positive(&r, &which);
            sdk_example_logf("  %s %d%% (%lu inference(s), %lu us)",
                             (which == 1u) ? "shake" : "?", pct,
                             (unsigned long)r.inferences,
                             (unsigned long)r.inference_us);
        } else {
            sdk_example_logf("  no verdict yet (active=%d) -- the window is"
                             " still filling", ai_engine_active());
        }

        if (s.ticks >= RUN_TICKS) {
            sdk_example_logf("stopping, then releasing the model");
            ai_engine_stop();
            s.ticks = 0u;
            s.phase = PH_DRAIN;
        }
        return;
    }

    if (s.phase == PH_DRAIN) {
        /* An unload is REFUSED unless the engine is idle: a parallel set
         * dequeues every member on every pass, and releasing a live one would
         * dequeue through freed memory on the next. stop() is a request --
         * active() going to -1 is the confirmation. */
        if (ai_engine_active() >= 0) {
            if (s.ticks >= WAIT_TICKS) {
                sdk_example_logf("engine still active after %lums; not"
                                 " attempting an unload it would refuse",
                                 (unsigned long)(s.ticks * POLL_MS));
                session_end();
            }
            return;
        }

        /* Cumulative counters, so the deltas are the answer. Take the baseline
         * BEFORE the request or another caller's unload reads as ours. */
        s.done0    = ai_engine_unload_done();
        s.refused0 = ai_engine_unload_refused();
        ai_engine_unload(s.idx);
        sdk_example_logf("unload(%lu) queued; it is serviced on the inference"
                         " task, not here", (unsigned long)s.idx);
        s.ticks = 0u;
        s.phase = PH_UNLOAD;
        return;
    }

    /* PH_UNLOAD */
    if (ai_engine_unload_done() > s.done0) {
        sdk_example_logf("released: finalize() ran, the model's resources are"
                         " back");
        sdk_example_logf("row [%lu] still exists and dyn_count is still %lu --"
                         " rows are never removed", (unsigned long)s.idx,
                         (unsigned long)ai_engine_dyn_count());
        session_end();
        return;
    }
    if (ai_engine_unload_refused() > s.refused0) {
        sdk_example_logf("REFUSED. The engine was not idle, the model had never"
                         " initialised, or its descriptor has no finalize()");
        session_end();
        return;
    }
    if (s.ticks >= WAIT_TICKS) {
        sdk_example_logf("no verdict on the unload after %lums -- the inference"
                         " task is not running (see 07_engine_health)",
                         (unsigned long)(s.ticks * POLL_MS));
        session_end();
    }
}

int example_edge_ai_register_model(lv_obj_t *parent)
{
    (void)parent;   /* the session reports through the log */

    if (s.running) {
        sdk_example_logf("a session is already running");
        return SDK_EX_BUSY;
    }
    memset(&s, 0, sizeof(s));

    const uint32_t dyn_before = ai_engine_dyn_count();
    const uint32_t capacity   = ai_engine_dyn_capacity();
    sdk_example_logf("run-time rows: %lu of %lu used before registering",
                     (unsigned long)dyn_before, (unsigned long)capacity);

    int idx = find_row(k_shake_desc.name);
    if (idx >= 0) {
        sdk_example_logf("'%s' is already row [%d] from an earlier run --"
                         " reusing it; a duplicate name would be refused",
                         k_shake_desc.name, idx);
    } else {
        if (dyn_before >= capacity) {
            sdk_example_logf("no run-time rows left (%lu of %lu used). They are"
                             " a per-boot budget", (unsigned long)dyn_before,
                             (unsigned long)capacity);
            return SDK_EX_REFUSED;
        }
        /* Ideally this runs BEFORE ai_engine_init(), while nothing can observe
         * a registry mid-registration. Here the engine is long since up, which
         * is safe -- the write is in a critical section -- but an enumeration
         * issued in the same instant sees the shorter list. */
        idx = ai_engine_register(&k_shake_desc);
        if (idx < 0) {
            sdk_example_logf("register refused: an incomplete descriptor,"
                             " class_count outside 1..%u, a duplicate name, or"
                             " capacity exhausted", (unsigned)AI_MAX_CLASSES);
            return SDK_EX_REFUSED;
        }
        sdk_example_logf("registered '%s' as row [%d]; %lu of %lu run-time rows"
                         " now used", k_shake_desc.name, idx,
                         (unsigned long)ai_engine_dyn_count(),
                         (unsigned long)capacity);
    }
    s.idx = (uint32_t)idx;

    {   /* It is an ordinary registry row now -- read it back like any other. */
        const ai_model_desc_t *d = ai_engine_model(s.idx);
        sdk_example_logf("registry now holds %lu row(s); [%lu] = '%s', %u class",
                         (unsigned long)ai_engine_model_count(),
                         (unsigned long)s.idx,
                         ((d != NULL) && (d->name != NULL)) ? d->name : "?",
                         (d != NULL) ? (unsigned)d->class_count : 0u);
    }

    if (!ai_engine_init()) {
        sdk_example_logf("ai_engine_init() failed: no inference task, so"
                         " nothing would ever call init()");
        return SDK_EX_UNAVAILABLE;
    }

    if (!ai_engine_start(s.idx)) {
        sdk_example_logf("start(%lu) refused -- index out of range, or the"
                         " inference task does not exist",
                         (unsigned long)s.idx);
        return SDK_EX_REFUSED;
    }

    s.timer = lv_timer_create(tick_cb, POLL_MS, NULL);
    if (s.timer == NULL) {
        sdk_example_logf("no LVGL timer available -- unwinding");
        session_end();
        return SDK_EX_UNAVAILABLE;
    }
    s.phase   = PH_RUN;
    s.running = true;

    sdk_example_logf("running for %lums -- shake the board",
                     (unsigned long)(RUN_TICKS * POLL_MS));
    return SDK_EX_STARTED;
}
