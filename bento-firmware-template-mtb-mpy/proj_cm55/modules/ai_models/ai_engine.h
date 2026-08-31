/*******************************************************************************
 * File Name        : ai_engine.h
 *
 * Description      : BENTO Edge AI engine — runs ONE DEEPCRAFT model at a time
 *                    on CM55 (TFLite-Micro + Ethos-U55) and publishes a result
 *                    snapshot for the UI and the MicroPython model link.
 *
 *                    WHY ONE AT A TIME (design ruling, three independent walls):
 *                      1. one NPU  — Ethos-U55 inferences serialize anyway;
 *                      2. one sensor pipeline per model family (IMU / radar /
 *                         mic) with different sample rates and windows;
 *                      3. CM55 CPU budget beside LVGL (GFX task, priority 6).
 *                    Models are all COMPILED IN; activating one is a registry
 *                    switch, not a reload — no flash or weight copy.
 *
 *                    Every inference is timed with the DWT cycle counter and
 *                    published in the snapshot, so a model that outgrows its
 *                    cadence is visible on-screen rather than silent.
 *
 * Target           : PSoC Edge E84, CM55
 * //! [doc-drift-fix] — docs/template_local_deltas.list
 *******************************************************************************/

#ifndef AI_ENGINE_H
#define AI_ENGINE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>   /* NULL — ai_result_top_positive() below is inline here */

#ifdef __cplusplus
extern "C" {
#endif

#define AI_MAX_CLASSES      (8u)
#define AI_MAX_LABEL_LEN    (16u)

/** Sensor pipeline a model consumes. */
typedef enum {
    AI_SENSOR_IMU = 0,      /**< BMI270 accel+gyro via ipc_sensorhub (CM33-owned) */
    AI_SENSOR_RADAR,        /**< BGT60TR13C frames from the CM55 radar task       */
    AI_SENSOR_MIC,          /**< PDM microphone front-end (audio_pdm.c), shared by all mic models */
} ai_sensor_t;

/** One entry in the compiled-in model registry. */
typedef struct {
    const char *name;                   /**< UI name, e.g. "Motion"              */
    const char *description;            /**< one-line explanation for the page   */
    ai_sensor_t sensor;
    uint8_t     class_count;
    /* Registry contract: index 0 is the negative class -- the one that means
     * "nothing is happening". It is "unlabelled" in the audio and radar models,
     * "idle" in motion, "normal" in fall. Everything from 1 up is a positive
     * detection.
     *
     * The watch rows depend on this: a row's confidence is the maximum over
     * classes 1..class_count-1 (see row_score_pct in page_edge_ai.c), so a
     * model whose classes were all positive would render as if the loudest one
     * were always firing. A new model that cannot honour this needs that code
     * changed, not a quiet exception here. */
    const char *class_labels[AI_MAX_CLASSES];
    uint32_t    flash_bytes;            /**< weights size, for the UI            */
    uint16_t    period_ms;              /**< natural output cadence              */

    /* DEEPCRAFT-generated entry points (symbol-prefixed per model so several
     * models can be linked into one image — the generated sources all declare
     * IMAI_* otherwise). */
    int (*init)(void);
    int (*enqueue)(const float *in);
    int (*dequeue)(float *out);
    void (*finalize)(void);
} ai_model_desc_t;

/** Live result, published by the engine, read by the UI and the model link. */
typedef struct {
    uint8_t  model_index;               /**< active registry index               */
    uint8_t  class_count;
    uint8_t  top_class;                 /**< argmax over scores                  */
    uint8_t  running;                   /**< 1 while inference is active         */
    float    scores[AI_MAX_CLASSES];    /**< raw model outputs                   */
    uint32_t inference_us;              /**< LAST inference time (DWT-measured)  */
    uint32_t inference_us_max;          /**< worst case seen since start         */
    uint32_t inferences;                /**< total completed                     */
    uint32_t seq;                       /**< increments per published result     */
} ai_result_t;

/** How many models are compiled in. */
/** Set index: several models listening to the same sensor data.
 *
 *  Pass one of these to ai_engine_start() in place of a model index.
 *  ai_engine_active() reports it back. ai_engine_set_models() lists the
 *  members and ai_engine_set_name() gives the display name.
 *
 *  These are the top of the uint8 range, not the numbers immediately above the
 *  registry. While they sat just above it they were its ceiling, and the
 *  registry has to be free to grow. The older 13/14/15 spellings still work:
 *  the firmware translates them to the values here, and ai_engine_active()
 *  answers with the value here, so a caller that sends 13 is told 253. */
#define AI_PARALLEL_MIC  (255)

/** MIC       every microphone model at once.
 *  ROOM      radar plus the microphone models that suit a room.
 *  INTRUDER  motion, radar and the alarm models. Contains an IMU model, so
 *            starting it raises the CM33 sensor push rate for the session. */
#define AI_PARALLEL_ROOM      (254)
#define AI_PARALLEL_INTRUDER  (253)

/** Every registered model at once — the widest watch. */
#define AI_PARALLEL_ALL       (252)

/** Lowest pseudo-index in use; anything at or above this is a set, not a model.
 *  (AI_PARALLEL_ALL was absent here for one day — the archive then shipping
 *  predated the set and rejected 252 on hardware. The archive was re-cut from
 *  the current engine 2026-08-28 and honours it; verified over the REPL.) */
#define AI_PARALLEL_FIRST     AI_PARALLEL_ALL

/* These numbers are also spelled in the wire header, because MicroPython has to
 * resolve a legacy 13/14/15 to the same value before it can confirm a select.
 * When the two drifted, every Sound Watch select reported "not confirmed" on a
 * board that had in fact switched. Checked here rather than trusted, and only
 * when the wire header is in the translation unit, so this header still
 * compiles on its own. */
#if defined(MODEL_LINK_SET_MIC)
_Static_assert(AI_PARALLEL_ALL      == MODEL_LINK_SET_ALL,      "set index drift");
_Static_assert(AI_PARALLEL_INTRUDER == MODEL_LINK_SET_INTRUDER, "set index drift");
_Static_assert(AI_PARALLEL_ROOM     == MODEL_LINK_SET_ROOM,     "set index drift");
_Static_assert(AI_PARALLEL_MIC      == MODEL_LINK_SET_MIC,      "set index drift");
#endif

/** Members of a set, in registry order. Returns how many were written. */
uint32_t ai_engine_set_models(uint32_t set_index, uint8_t *idx, uint32_t max);

/** Human name for a set, or NULL if that index is not one. */
const char *ai_engine_set_name(uint32_t set_index);

/** Give a set an explicit membership at run time, replacing the compiled one.
 *  Task context. Undefined sets keep their compiled membership exactly. */
int ai_engine_set_define(uint32_t set_index, const uint8_t *members, uint32_t n);

/** Explicit membership if one was defined, else 0. */
uint32_t ai_engine_set_members_defined(uint32_t set_index, uint8_t *out, uint32_t max);

/** Release a run-time model and free what it owns.
 *  Asynchronous: the request is serviced in the inference task and REFUSED
 *  unless the engine is idle, because a parallel set dequeues every member on
 *  every pass. The two counters below are how a caller learns which happened. */
void     ai_engine_unload(uint32_t idx);
uint32_t ai_engine_unload_done(void);
uint32_t ai_engine_unload_refused(void);

uint32_t ai_engine_model_count(void);

/** Registry entry (NULL if out of range). */
const ai_model_desc_t *ai_engine_model(uint32_t index);

/** Register a model descriptor at run time. Returns its registry index, or -1.
 *
 *  BEFORE YOU USE THIS: confirm the archive beside you exports it —
 *      grep ai_engine_register lib/edge_ai/api.txt
 *  An archive cut before this symbol was exported carries it under an internal
 *  name, and the link fails with "undefined reference to ai_engine_register".
 *  The compile-time route (modules/ai_models/README.md, "Filling a model slot")
 *  works against every build.
 *
 *  This is the engine's open extension point: a model added here needs no
 *  rebuilt engine, no fixed slot name, and no source from TESAIoT. Fill in an
 *  ai_model_desc_t with your own init/enqueue/dequeue/finalize and the model
 *  joins the registry, the Edge AI menu and the sets like any built-in one.
 *  The compile-time alternative — filling one of the named slots the engine
 *  already imports — is in modules/ai_models/README.md, "Filling a model slot".
 *
 *  Task context only. The registry is read from the IPC pipe callback in ISR
 *  context, so registration runs inside a critical section rather than behind a
 *  mutex the ISR could not take. Do not call it from an ISR.
 *
 *  The descriptor is COPIED; the pointers inside it are not. `name`,
 *  `description` and every `class_labels[]` entry must outlive the boot, and
 *  they are read from an ISR. String literals and static buffers qualify. A
 *  stack buffer does not.
 *
 *  Rejected if: the descriptor is incomplete, class_count is 0 or above
 *  AI_MAX_CLASSES, capacity is exhausted, or the name duplicates a row that is
 *  already registered. Names are the key for set membership and for the watch
 *  threshold overrides, so a duplicate is a correctness problem, not a
 *  cosmetic one.
 *
 *  Rows cannot be removed. Every reader is lockless because an index that was
 *  once valid stays valid; removal would invalidate that and needs its own
 *  design. */
int ai_engine_register(const ai_model_desc_t *desc);

/** Rows added at run time so far, and the ceiling. */
uint32_t ai_engine_dyn_count(void);
uint32_t ai_engine_dyn_capacity(void);

/** Create the inference task (idle until ai_engine_start). Call after the
 *  sensor sources exist; safe to call once from the display bring-up. */
bool ai_engine_init(void);

/** Activate a model by registry index and begin inferring. Stops any model
 *  already running first. Returns false on a bad index or init failure. */
bool ai_engine_start(uint32_t index);

/** Stack the inference task actually got, in words; 0 if it was never created.
 *  Reported on the Edge AI page so a heap squeeze is visible, not silent. */
uint32_t ai_engine_stack_words(void);

/** Samples accepted by the model, and successful model initialisations.
 *  Shown on the page while no verdict exists yet, so a stall is legible. */
uint32_t ai_engine_feeds(void);
uint32_t ai_engine_dq_ok(void);
uint32_t ai_engine_dq_calls(void);

/** Set the accelerometer feed interval (20 = 50 Hz model rate, 100 = 10 Hz
 *  dashboard). Eva Kit drives a local LVGL timer; AI Kit sends an IPC rate
 *  request to CM33. Callable from any CM55 task. */
void ai_engine_set_sensor_rate(uint32_t interval_ms);

/** Restore the default sensor cadence after a model session. */
void ai_engine_resume_sensor(void);
uint32_t ai_engine_inits(void);

/** MODEL_INIT_RECOVERY diagnostics. init_calls == 0 means the inference task
 *  never reached the cold-load (task absent, or no select/start landed);
 *  init_calls > 0 with last_init_rc != 0 means the model's own init() failed
 *  with that code; last_init_rc == 0x7FFFFFFF means init() was never called. */
uint32_t ai_engine_init_calls(void);

/** How many model init() calls RETURNED. Less than ai_engine_init_calls() means
 *  the inference task went into one and did not come out. */
uint32_t ai_engine_init_returns(void);

/** NPU cycles accumulated so far. A coarse "is the NPU working" reading only —
 *  the middleware adds to it on the timeout path as well, so it does NOT prove
 *  any particular inference completed. Use ai_engine_stale_drops() for that. */
uint64_t ai_engine_npu_cycles(void);

/** Verdicts discarded because their dequeue ran past the Ethos-U wait bound and
 *  so could only be carrying the previous frame's output tensor.
 *
 *  Read it as "how often a verdict was withheld", not as an NPU health meter.
 *  The measurement is wall clock, which cannot separate "the NPU did not answer"
 *  from "this task did not run": ai_task sits below the GFX task, and a long
 *  render frame or an XIP stall on the shared SMIF can push a perfectly good
 *  dequeue past the threshold. It also stops counting once a stall wedges the
 *  driver, because dequeue then fails outright and never reaches the check.
 *
 *  The signature of a stalling NPU remains ai_engine_dq_ok() frozen while
 *  ai_engine_dq_calls() climbs. Cumulative for the boot — deliberately not
 *  cleared on a model switch, unlike the pipeline counters. */
uint32_t ai_engine_stale_drops(void);

/** Unused words left in the inference task's stack (all-time minimum); 0 if the
 *  task was never created. Read at about 1 Hz — the call scans the stack. */
uint32_t ai_engine_stack_free_words(void);
int32_t  ai_engine_last_init_rc(void);

/** Stop the active model (idempotent). */
void ai_engine_stop(void);

/** Index of the active model, or -1 when idle. */
int ai_engine_active(void);

/** Index of the REQUESTED model (last ai_engine_start), or -1 when stopped.
 * Leads ai_engine_active() by up to one inference-task tick. Guard a
 * default-model fallback on this, never on ai_engine_active(). */
int ai_engine_requested(void);

/** Copy the latest result. Returns false if nothing has been published yet. */
bool ai_engine_snapshot(ai_result_t *out);

/** One model's last verdict. Use this, not ai_engine_snapshot(), whenever
 *  several models may be publishing: that one reports whoever wrote most
 *  recently, so three quiet models can bury a detection before it is read.
 *  False if that model has never published. */
bool ai_engine_snapshot_model(uint32_t index, ai_result_t *out);

/** Registry indices of the ACTIVE SET's members, up to max, whatever sensor
 *  each one reads -- not microphone models. Returns the count. Callers that
 *  iterate this must test desc->sensor before doing anything sensor-specific
 *  with an entry. */
uint32_t ai_engine_set_members(uint8_t *idx, uint32_t max);

/** The one number a summary row shows for a model: the strongest POSITIVE
 *  class, 0..100, with its index written to *which when that is not NULL.
 *
 *  Index 0 is the negative class by registry contract (see class_labels above),
 *  so it is excluded. Lives here, beside the contract it depends on, because
 *  both the Edge AI page and the MicroPython model link need exactly this rule
 *  and two copies of it would drift the first time the contract changed. */
static inline int ai_result_top_positive(const ai_result_t *r, uint8_t *which)
{
    if (which != NULL) { *which = 1u; }
    if ((r == NULL) || (r->class_count < 2u)) { return 0; }
    float   best = r->scores[1];
    uint8_t arg  = 1u;
    /* Bounded by class_count, not the array size: publish() writes only that
     * many floats and the slots above were never set. */
    for (uint8_t c = 2u; (c < r->class_count) && (c < AI_MAX_CLASSES); c++) {
        if (r->scores[c] > best) { best = r->scores[c]; arg = c; }
    }
    if (which != NULL) { *which = arg; }
    int pct = (int)(best * 100.0f + 0.5f);
    if (pct < 0)   { pct = 0; }
    if (pct > 100) { pct = 100; }
    return pct;
}

/** True while the parallel set is still filling its windows. Verdicts are
 *  withheld until then; show it rather than four silent zero bars. */
bool ai_engine_mic_settling(void);

/** Settle progress, 0..100, for a caller that wants to show it moving. */
uint32_t ai_engine_mic_settle_pct(void);

#ifdef __cplusplus
}
#endif

#endif /* AI_ENGINE_H */
