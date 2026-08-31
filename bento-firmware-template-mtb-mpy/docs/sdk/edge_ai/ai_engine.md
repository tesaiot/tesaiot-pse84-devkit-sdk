# ai_engine.h

## Functions (exported by the archive)

### `ai_engine_active`

```c
int ai_engine_active(void);
```

Index of the active model, or -1 when idle.

### `ai_engine_dq_calls`

```c
uint32_t ai_engine_dq_calls(void);
```

_No description in the header._

### `ai_engine_dq_ok`

```c
uint32_t ai_engine_dq_ok(void);
```

_No description in the header._

### `ai_engine_dyn_capacity`

```c
uint32_t ai_engine_dyn_capacity(void);
```

_No description in the header._

### `ai_engine_dyn_count`

```c
uint32_t ai_engine_dyn_count(void);
```

Rows added at run time so far, and the ceiling.

### `ai_engine_feeds`

```c
uint32_t ai_engine_feeds(void);
```

Samples accepted by the model, and successful model initialisations. Shown on the page while no verdict exists yet, so a stall is legible.

### `ai_engine_init`

```c
bool ai_engine_init(void);
```

Create the inference task (idle until ai_engine_start). Idempotent; must precede ai_engine_start(). If it fails, ai_engine_stack_words() returns 0.

### `ai_engine_init_calls`

```c
uint32_t ai_engine_init_calls(void);
```

MODEL_INIT_RECOVERY diagnostics. init_calls == 0 means the inference task never reached the cold-load (task absent, or no select/start landed); init_calls > 0 with last_init_rc != 0 means the model's own init() failed with that code; last_init_rc == 0x7FFFFFFF means init() was never called.

### `ai_engine_init_returns`

```c
uint32_t ai_engine_init_returns(void);
```

How many model init() calls RETURNED. Less than ai_engine_init_calls() means the inference task went into one and did not come out.

### `ai_engine_inits`

```c
uint32_t ai_engine_inits(void);
```

_No description in the header._

### `ai_engine_last_init_rc`

```c
int32_t ai_engine_last_init_rc(void);
```

_No description in the header._

### `ai_engine_mic_settle_pct`

```c
uint32_t ai_engine_mic_settle_pct(void);
```

Settle progress, 0..100, for a caller that wants to show it moving.

### `ai_engine_mic_settling`

```c
bool ai_engine_mic_settling(void);
```

The one number a summary row shows for a model: the strongest POSITIVE class, 0..100, with its index written to *which when that is not NULL. Index 0 is the negative class by registry contract (see class_labels above), so it is excluded. Lives here, beside the contract it depends on, because both the Edge AI page and the MicroPython model link need exactly this rule and two copies of it would drift the first time the contract changed. */ static inline int ai_result_top_positive(const ai_result_t *r, uint8_t *which) { if (which != NULL) { *which = 1u; } if ((r == NULL) || (r->class_count < 2u)) { return 0; } float   best = r->scores[1]; uint8_t arg  = 1u; /* Bounded by class_count, not the array size: only the first class_count scores are valid and the slots above are never set. */ for (uint8_t c = 2u; (c < r->class_count) && (c < AI_MAX_CLASSES); c++) { if (r->scores[c] > best) { best = r->scores[c]; arg = c; } } if (which != NULL) { *which = arg; } int pct = (int)(best * 100.0f + 0.5f); if (pct < 0)   { pct = 0; } if (pct > 100) { pct = 100; } return pct; } /** True for a few seconds after a set starts, while its inputs settle. Verdicts are withheld until it returns false; show it rather than silent zero bars.

### `ai_engine_model`

```c
const ai_model_desc_t *ai_engine_model(uint32_t index);
```

Registry entry (NULL if out of range).

### `ai_engine_model_count`

```c
uint32_t ai_engine_model_count(void);
```

_No description in the header._

### `ai_engine_npu_cycles`

```c
uint64_t ai_engine_npu_cycles(void);
```

NPU cycles accumulated so far. A coarse "is the NPU working" reading only — the middleware adds to it on the timeout path as well, so it does NOT prove any particular inference completed. Use ai_engine_stale_drops() for that.

### `ai_engine_register`

```c
int ai_engine_register(const ai_model_desc_t *desc);
```

Register a model descriptor at run time. Returns its registry index, or -1. Exported by the shipped archive (listed in lib/edge_ai/api.txt). The compile-time route (modules/ai_models/README.md, "Filling a model slot") is the alternative when a model should be part of every build. This is the engine's open extension point: a model added here needs no rebuilt engine, no fixed slot name, and no source from TESAIoT. Fill in an ai_model_desc_t with your own init/enqueue/dequeue/finalize and the model joins the registry, the Edge AI menu and the sets like any built-in one. The compile-time alternative — filling one of the named slots the engine already imports — is in modules/ai_models/README.md, "Filling a model slot". Task context only; never call it from an ISR. The descriptor is COPIED; the pointers inside it are not. `name`, `description` and every `class_labels[]` entry must outlive the boot, and they are read long after this call returns. String literals and static buffers qualify. A stack buffer does not. Rejected if: the descriptor is incomplete, class_count is 0 or above AI_MAX_CLASSES, capacity is exhausted, or the name duplicates a row that is already registered. A name identifies the model on the Edge AI page, in edge_ai.models() and in the threshold overrides, so a duplicate is a correctness problem, not a cosmetic one. To put a registered model in a set, use ai_engine_set_define() with the index this call returns. Rows cannot be removed: an index, once returned, stays valid for the rest of the boot.

### `ai_engine_requested`

```c
int ai_engine_requested(void);
```

Index of the REQUESTED model (last ai_engine_start), or -1 when stopped. Leads ai_engine_active() until the requested model has finished loading, and drops to -1 if its init() fails. Guard a default-model fallback on this, never on ai_engine_active().

### `ai_engine_resume_sensor`

```c
void ai_engine_resume_sensor(void);
```

Restore the default sensor cadence after a model session. Use it on its own, not in the same breath as ai_engine_set_sensor_rate().

### `ai_engine_set_define`

```c
int ai_engine_set_define(uint32_t set_index, const uint8_t *members, uint32_t n);
```

Give a set an explicit membership at run time, replacing the compiled one. Task context only. 1..8 members, all-or-nothing: one bad index refuses the whole definition. INTRUDER, ROOM and MIC only; returns -1 for AI_PARALLEL_ALL. Undefined sets keep their compiled membership exactly. Read the result back with ai_engine_set_members_defined().

### `ai_engine_set_members`

```c
uint32_t ai_engine_set_members(uint8_t *idx, uint32_t max);
```

Registry indices of the ACTIVE SET's members, up to max, whatever sensor each one reads -- not microphone models. Returns the count. Callers that iterate this must test desc->sensor before doing anything sensor-specific with an entry.

### `ai_engine_set_members_defined`

```c
uint32_t ai_engine_set_members_defined(uint32_t set_index, uint8_t *out, uint32_t max);
```

Explicit membership if one was defined, else 0.

### `ai_engine_set_models`

```c
uint32_t ai_engine_set_models(uint32_t set_index, uint8_t *idx, uint32_t max);
```

File Name        : ai_engine.h Description      : BENTO Edge AI engine on CM55 (TFLite-Micro + Ethos-U55). Holds the model registry, runs one model or one model set at a time, and publishes a result snapshot for the UI and the MicroPython model link. Start a model, or a set (a set index in place of a model index), with ai_engine_start(). Read one model's verdict with ai_engine_snapshot_model(); ai_engine_snapshot() returns only the most recent verdict from any model. ai_engine_requested() is what was asked for; ai_engine_active() is what has finished loading. Every inference is timed and published in the snapshot (inference_us), so a model that outgrows its cadence is visible on-screen rather than silent. Target           : PSoC Edge E84, CM55 / #ifndef AI_ENGINE_H #define AI_ENGINE_H #include <stdint.h> #include <stdbool.h> #include <stddef.h>   /* NULL — ai_result_top_positive() below is inline here */ #ifdef __cplusplus extern "C" { #endif #define AI_MAX_CLASSES      (8u) #define AI_MAX_LABEL_LEN    (16u) /** Sensor pipeline a model consumes. */ typedef enum { AI_SENSOR_IMU = 0,      /**< BMI270 accel+gyro via ipc_sensorhub (CM33-owned) */ AI_SENSOR_RADAR,        /**< BGT60TR13C frames from the CM55 radar task       */ AI_SENSOR_MIC,          /**< PDM microphone front-end (audio_pdm.c)                           */ } ai_sensor_t; /** One entry in the compiled-in model registry. */ typedef struct { const char *name;                   /**< UI name, e.g. "Motion"              */ const char *description;            /**< one-line explanation for the page   */ ai_sensor_t sensor; uint8_t     class_count; /* Registry contract: index 0 is the negative class -- the one that means "nothing is happening". It is "unlabelled" in the audio and radar models, "idle" in motion, "normal" in fall. Everything from 1 up is a positive detection. Summary scores depend on this: ai_result_top_positive() (below) takes the maximum over classes 1..class_count-1, so a model whose classes were all positive would read as if the loudest one were always firing. A model that cannot honour this contract must not be registered as if it did; give it an explicit negative class instead. */ const char *class_labels[AI_MAX_CLASSES]; uint32_t    flash_bytes;            /**< weights size, for the UI            */ uint16_t    period_ms;              /**< natural output cadence              */ /* DEEPCRAFT-generated entry points (symbol-prefixed per model so several models can be linked into one image — the generated sources all declare IMAI_* otherwise). */ int (*init)(void); int (*enqueue)(const float *in); int (*dequeue)(float *out); void (*finalize)(void); } ai_model_desc_t; /** Live result, published by the engine, read by the UI and the model link. */ typedef struct { uint8_t  model_index;               /**< active registry index               */ uint8_t  class_count; uint8_t  top_class;                 /**< argmax over scores                  */ uint8_t  running;                   /**< 1 while inference is active         */ float    scores[AI_MAX_CLASSES];    /**< raw model outputs                   */ uint32_t inference_us;              /**< LAST inference time (DWT-measured)  */ uint32_t inference_us_max;          /**< worst case seen since start         */ uint32_t inferences;                /**< total completed                     */ uint32_t seq;                       /**< increments per published result     */ } ai_result_t; /** How many models are compiled in. */ /** Set index: a named group of models that run together. Pass one of these to ai_engine_start() in place of a model index. ai_engine_active() reports it back. ai_engine_set_models() lists the members and ai_engine_set_name() gives the display name. The set indices sit at the top of the uint8 range (252..255), clear of every registry index. The legacy spellings 13/14/15 are still accepted: the firmware translates them to INTRUDER/ROOM/MIC (253/254/255), and ai_engine_active() answers with the translated value, so a caller that sends 13 is told 253. */ #define AI_PARALLEL_MIC  (255) /** MIC       every microphone model at once. ROOM      radar plus the microphone models that suit a room. INTRUDER  motion, radar and the alarm models. Contains an IMU model, so starting it raises the CM33 sensor push rate for the session. */ #define AI_PARALLEL_ROOM      (254) #define AI_PARALLEL_INTRUDER  (253) /** Every registered model at once — the widest watch. */ #define AI_PARALLEL_ALL       (252) /** Lowest pseudo-index in use; anything at or above this is a set, not a model. Set constants: ALL 252, INTRUDER 253, ROOM 254, MIC 255. Starting a set requires an image built with a microphone model; otherwise ai_engine_start() refuses the set index and returns false. */ #define AI_PARALLEL_FIRST     AI_PARALLEL_ALL /* These numbers are also defined in the model-link wire header (ipc_model_link_defs.h), because MicroPython has to resolve a legacy 13/14/15 to the same value before it can confirm a select. The two are checked equal here rather than trusted, and only when the wire header is in the translation unit, so this header still compiles on its own. */ #if defined(MODEL_LINK_SET_MIC) _Static_assert(AI_PARALLEL_ALL      == MODEL_LINK_SET_ALL,      "set index drift"); _Static_assert(AI_PARALLEL_INTRUDER == MODEL_LINK_SET_INTRUDER, "set index drift"); _Static_assert(AI_PARALLEL_ROOM     == MODEL_LINK_SET_ROOM,     "set index drift"); _Static_assert(AI_PARALLEL_MIC      == MODEL_LINK_SET_MIC,      "set index drift"); #endif /** Members of a set, in registry order. Returns how many were written.

### `ai_engine_set_name`

```c
const char *ai_engine_set_name(uint32_t set_index);
```

Human name for a set, or NULL if that index is not one.

### `ai_engine_set_sensor_rate`

```c
void ai_engine_set_sensor_rate(uint32_t interval_ms);
```

Set the accelerometer feed interval (20 = 50 Hz model rate, 100 = 10 Hz dashboard). Callable from any CM55 task; it also resumes the sensor push. Call it on its own: an ai_engine_resume_sensor() issued right after it can cancel it.

### `ai_engine_snapshot`

```c
bool ai_engine_snapshot(ai_result_t *out);
```

Copy the latest result. Returns false if nothing has been published yet.

### `ai_engine_snapshot_model`

```c
bool ai_engine_snapshot_model(uint32_t index, ai_result_t *out);
```

One model's last verdict. Use this, not ai_engine_snapshot(), whenever several models may be publishing: that one reports whoever wrote most recently, so three quiet models can bury a detection before it is read. False if that model has never published.

### `ai_engine_stack_free_words`

```c
uint32_t ai_engine_stack_free_words(void);
```

Unused words left in the inference task's stack (all-time minimum); 0 if the task was never created. Read at about 1 Hz — the call scans the stack.

### `ai_engine_stack_words`

```c
uint32_t ai_engine_stack_words(void);
```

Stack the inference task actually got, in words; 0 if it was never created. Reported on the Edge AI page so a heap squeeze is visible, not silent.

### `ai_engine_stale_drops`

```c
uint32_t ai_engine_stale_drops(void);
```

Verdicts discarded because their dequeue ran past the Ethos-U wait bound and so could only be carrying the previous frame's output tensor. Read it as "how often a verdict was withheld", not as an NPU health meter. The measurement is wall clock, which cannot separate "the NPU did not answer" from "the inference task did not run": a busy display or an XIP stall on the shared SMIF can delay a perfectly good dequeue past the threshold. It also stops counting once a stall wedges the driver, because dequeue then fails outright and never reaches the check. The signature of a stalling NPU remains ai_engine_dq_ok() frozen while ai_engine_dq_calls() climbs. Cumulative for the boot — deliberately not cleared on a model switch, unlike the pipeline counters.

### `ai_engine_start`

```c
bool ai_engine_start(uint32_t index);
```

Activate a model by registry index, or a set by set index, and begin inferring. Stops whatever was running first. Returns false on a bad index, a set index in an image built without a microphone model, or an engine that never started (see ai_engine_init). A model whose own init() fails is reported later: ai_engine_requested() drops back to -1.

### `ai_engine_stop`

```c
void ai_engine_stop(void);
```

Stop the active model (idempotent).

### `ai_engine_unload`

```c
void ai_engine_unload(uint32_t idx);
```

Release a run-time model and free what it owns. Asynchronous, and honoured only while the engine is idle: call ai_engine_stop() and wait for ai_engine_active() == -1 first, or the request is REFUSED. The two counters below are how a caller learns which happened.

### `ai_engine_unload_done`

```c
uint32_t ai_engine_unload_done(void);
```

_No description in the header._

### `ai_engine_unload_refused`

```c
uint32_t ai_engine_unload_refused(void);
```

_No description in the header._

## Enums

### `ai_sensor_t`

```c
typedef enum {
    AI_SENSOR_IMU = 0,      /**< BMI270 accel+gyro via ipc_sensorhub (CM33-owned) */
    AI_SENSOR_RADAR,        /**< BGT60TR13C frames from the CM55 radar task       */
    AI_SENSOR_MIC,          /**< PDM microphone front-end (audio_pdm.c)                           */} ai_sensor_t;
```

## Structs

### `ai_model_desc_t`

```c
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
     * Summary scores depend on this: ai_result_top_positive() (below) takes
     * the maximum over classes 1..class_count-1, so a model whose classes were
     * all positive would read as if the loudest one were always firing. A model
     * that cannot honour this contract must not be registered as if it did;
     * give it an explicit negative class instead. */
    const char *class_labels[AI_MAX_CLASSES];
    uint32_t    flash_bytes;            /**< weights size, for the UI            */
    uint16_t    period_ms;              /**< natural output cadence              */

    /* DEEPCRAFT-generated entry points (symbol-prefixed per model so several
     * models can be linked into one image — the generated sources all declare
     * IMAI_* otherwise). */
    int (*init)(void);
    int (*enqueue)(const float *in);
    int (*dequeue)(float *out);
    void (*finalize)(void);} ai_model_desc_t;
```

### `ai_result_t`

```c
typedef struct {
    uint8_t  model_index;               /**< active registry index               */
    uint8_t  class_count;
    uint8_t  top_class;                 /**< argmax over scores                  */
    uint8_t  running;                   /**< 1 while inference is active         */
    float    scores[AI_MAX_CLASSES];    /**< raw model outputs                   */
    uint32_t inference_us;              /**< LAST inference time (DWT-measured)  */
    uint32_t inference_us_max;          /**< worst case seen since start         */
    uint32_t inferences;                /**< total completed                     */
    uint32_t seq;                       /**< increments per published result     */} ai_result_t;
```

## Constants

| Name | Value |
|---|---|
| `AI_ENGINE_H` | `#include` |
| `AI_MAX_CLASSES` | `(8u)` |
| `AI_MAX_LABEL_LEN` | `(16u)` |
| `AI_PARALLEL_MIC` | `(255)` |
| `AI_PARALLEL_ROOM` | `(254)` |
| `AI_PARALLEL_INTRUDER` | `(253)` |
| `AI_PARALLEL_ALL` | `(252)` |
| `AI_PARALLEL_FIRST` | `AI_PARALLEL_ALL` |
