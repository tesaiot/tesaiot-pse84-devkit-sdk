/* sdk-example: core=cm55 variant=both group=edge_ai
 * id:      cm55/edge_ai/02_model_registry
 * title:   Browse the model registry
 * teaches: enumerate every model the image carries, read its descriptor, and see how much run-time room is left
 * apis:    ai_engine_model_count, ai_engine_model, ai_engine_dyn_count, ai_engine_dyn_capacity
 * entry:   example_edge_ai_model_registry
 */
/*
 * Everything the engine can run is a row in one flat registry, and this is how
 * you read it. There is no separate "list the models" call: model_count() plus
 * model(i) IS the list, and every other API in this module takes the index you
 * find here.
 *
 * THE REGISTRY HAS TWO HALVES AND ONE NUMBERING
 *
 *   [0 .. count-dyn)      compiled in. Linked into the image, no flash cost at
 *                         run time to activate -- switching model is a pointer
 *                         swap, not a reload.
 *   [count-dyn .. count)  added at run time by ai_engine_register() or by the
 *                         staged loader. See 05_register_model and 06_staged_load.
 *
 * ai_engine_model_count() spans BOTH. There is no second call for dynamic rows
 * and no gap between the halves: a dynamic row is an ordinary index everywhere
 * -- ai_engine_start(), the parallel sets, the model link. dyn_count() and
 * dyn_capacity() answer a different question, "how much room is left", not
 * "where do I look".
 *
 * ROWS ARE NEVER REMOVED. Every reader in the engine is lockless because an
 * index that was once valid stays valid for the life of the boot. Releasing a
 * run-time model (ai_engine_unload) frees what the model owns and leaves the
 * row; dyn_count() does not go back down. Budget capacity per BOOT, not per use.
 *
 * WHAT EACH FIELD IS ACTUALLY FOR
 *
 *   sensor       which pipeline feeds it, and therefore whether starting it
 *                needs a CM33 rate request (IMU) or not (radar, mic).
 *   class_count  how many of scores[] are written. Slots above it are never
 *                set -- read them and you are reading whatever was there.
 *   class_labels index 0 is the NEGATIVE class by registry contract: idle,
 *                normal, unlabelled. 1 and up are detections. Every summary
 *                percentage in this SDK is a maximum over 1..class_count-1.
 *   period_ms    the model's natural output cadence -- how often it CAN speak,
 *                which is the floor for any polling you build around it.
 *   flash_bytes  weights size, for a UI that wants to show the cost.
 *
 * Nothing here starts anything, so this is safe to call at any time, including
 * while a model is running.
 */

#include <stdbool.h>
#include <stdint.h>

#include "../sdk_examples.h"
#include "ai_engine.h"

/* The on-screen log is a fixed buffer. A registry larger than this still
 * reports its count and its capacity honestly -- it just stops listing. */
#define LIST_MAX  (8u)

static const char *sensor_name(ai_sensor_t s)
{
    switch (s) {
        case AI_SENSOR_IMU:   return "IMU";
        case AI_SENSOR_RADAR: return "radar";
        case AI_SENSOR_MIC:   return "mic";
        default:              return "?";
    }
}

int example_edge_ai_model_registry(lv_obj_t *parent)
{
    (void)parent;   /* a listing goes to the log, not the screen */

    const uint32_t count = ai_engine_model_count();
    const uint32_t dyn   = ai_engine_dyn_count();
    const uint32_t cap   = ai_engine_dyn_capacity();

    if (count == 0u) {
        sdk_example_logf("registry empty. This image was built without Edge AI"
                         " -- rebuild with BENTO_HAS_EDGE_AI=1");
        return SDK_EX_UNAVAILABLE;
    }

    /* count already includes the dynamic rows, so the compiled half is the
     * difference. Guarded because a caller must never assume it: if a future
     * engine ever reported them separately, this subtraction is where that
     * would show up as a wrong number rather than a crash. */
    const uint32_t compiled = (dyn <= count) ? (count - dyn) : count;

    sdk_example_logf("%lu model(s): %lu compiled + %lu registered at run time",
                     (unsigned long)count, (unsigned long)compiled,
                     (unsigned long)dyn);
    sdk_example_logf("run-time rows: %lu of %lu used, %lu free this boot",
                     (unsigned long)dyn, (unsigned long)cap,
                     (unsigned long)((dyn < cap) ? (cap - dyn) : 0u));

    const uint32_t shown = (count < LIST_MAX) ? count : LIST_MAX;
    for (uint32_t i = 0u; i < shown; i++) {
        const ai_model_desc_t *d = ai_engine_model(i);
        if (d == NULL) {
            /* Out of range is the only documented NULL, and i is in range, so
             * this is a registry that changed under us. Say so; do not skip. */
            sdk_example_logf("[%lu] NULL descriptor -- the registry moved",
                             (unsigned long)i);
            continue;
        }

        sdk_example_logf("[%lu] %s | %s | %u class | %lu KB | %ums%s",
                         (unsigned long)i,
                         (d->name != NULL) ? d->name : "(unnamed)",
                         sensor_name(d->sensor),
                         (unsigned)d->class_count,
                         (unsigned long)(d->flash_bytes / 1024u),
                         (unsigned)d->period_ms,
                         (i >= compiled) ? " [run-time]" : "");

        /* Labels, with class 0 marked, because that one is not a detection. */
        for (uint8_t c = 0u; (c < d->class_count) && (c < AI_MAX_CLASSES); c++) {
            const char *l = (d->class_labels[c] != NULL) ? d->class_labels[c]
                                                         : "(null)";
            sdk_example_logf("      %u %s%s", (unsigned)c, l,
                             (c == 0u) ? "  <- negative class" : "");
        }
    }

    if (shown < count) {
        sdk_example_logf("... %lu more row(s) not listed",
                         (unsigned long)(count - shown));
    }

    sdk_example_logf("index into ai_engine_start() with any of these; sets use"
                     " the pseudo-indices 252..255 instead");
    return SDK_EX_OK;
}
