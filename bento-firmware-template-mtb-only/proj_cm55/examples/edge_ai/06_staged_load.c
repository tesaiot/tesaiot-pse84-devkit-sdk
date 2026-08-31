/* sdk-example: core=cm55 variant=both group=edge_ai
 * id:      cm55/edge_ai/06_staged_load
 * title:   Load a model staged by the host
 * teaches: check a staging manifest yourself before handing the address to the loader, then read the loader's verdict
 * apis:    ai_model_staged_load, ai_model_staged_count, ai_model_staged_rejects, ai_model_staged_last_rc, ai_model_staged_heap_free, ai_engine_model, ai_engine_model_count
 * entry:   example_edge_ai_staged_load
 */
/*
 * A model downloaded on CM33 reaches CM55 as bytes in shared memory: a manifest
 * at a 32-byte-aligned address, the .tflite flatbuffer just after it. One call
 * turns that into a registry row. This file is about what you do BEFORE that
 * call.
 *
 * WHY VALIDATE WHEN THE LOADER ALREADY DOES
 *
 * Two reasons, and neither is distrust of the loader.
 *
 *  1. mtb_ml_model_init() validates NOTHING -- the shipped tensorflow-microlite
 *     archive exports no Verify* symbol at all. A malformed flatbuffer becomes
 *     pointer arithmetic inside AllocateTensors on the core that owns the
 *     display, and this core's fault handler masks interrupts and blinks until
 *     somebody pulls power. The CRC below is what stands between a corrupted
 *     transfer and that. Check it before the bytes are handed over, not after.
 *  2. The loader answers with one small negative number. Your checks can say
 *     WHICH field was wrong and what it held, which is the difference between
 *     a fix and a guess. Run both: yours for the diagnosis, the loader's as the
 *     authority.
 *
 * WHAT A CRC IS AND IS NOT. It answers "did this arrive intact". It answers
 * nothing about who sent it -- anyone can recompute a CRC over anything. The
 * staging format carries an ECDSA P-256 signature in a trailer after the
 * flatbuffer for that question, and its verdict is reported on a separate
 * field with a separate set of codes. Passing CRC is not provenance.
 *
 * THE FIELDS NOTHING DOWNSTREAM CAN CHECK FOR YOU
 *
 *   sample_rate_hz    the rate the model was TRAINED at. This firmware feeds
 *                     one rate. A mismatch is not a degraded model, it is a
 *                     window covering the wrong span of time.
 *   axis_convention   the IMU feed negates X and Y to match the training rig.
 *                     A model trained on a differently mounted board runs,
 *                     produces confident output, and is wrong.
 *   frame_bytes       the IMU path builds SIX floats on its stack and passes
 *                     that. A nine-channel export satisfies every other check
 *                     -- including window_depth * frame_bytes == in_elements
 *                     * 4, which is an identity between three declared numbers
 *                     and is satisfied by a consistent lie -- and then reads 36
 *                     bytes out of a 24-byte array, fifty times a second. It
 *                     does not crash. It is quietly wrong, which is worse.
 *
 * ONE PIPELINE SHAPE ONLY. The loader builds "slide a fixed window, quantize,
 * infer, dequantize". IMU models are that shape. Audio and radar models are
 * not -- their feature front ends are ordered DSP code and coefficient tables
 * that no .tflite carries -- and nothing can detect the difference from the
 * file. Load IMU models only; the sensor check below refuses what it can see.
 *
 * WHERE THE ADDRESS COMES FROM
 *
 *     make build ENABLE_PAGE_EXAMPLES=1 \
 *                CFLAGS+=-DTESAIOT_EXAMPLE_STAGE_ADDR=0x26301A40
 *
 * In a real application it arrives in the CTRL message that asked for the load;
 * it is not a constant you keep anywhere.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../sdk_examples.h"
#include "ai_engine.h"
#include "ai_model_staged.h"
#include "ipc_model_stage_defs.h"

/* No default address is a real address. 0 means "not configured", and the
 * example says so rather than reading page zero. */
#ifndef TESAIOT_EXAMPLE_STAGE_ADDR
#define TESAIOT_EXAMPLE_STAGE_ADDR  (0u)
#endif

/* The TFLite file identifier, bytes 4..7 of a flatbuffer. Cheapest possible
 * proof that this is a model and not, say, the JSON that described it. */
#define TFL3_OFFSET   (4u)
#define TFL3_BYTES    (4u)

/* The model CRC walks the whole flatbuffer bit by bit, which is real work on a
 * few hundred KB. Doing it in run() would hold the GFX task through a frame;
 * this one-shot timer lets the validation log reach the screen first. It still
 * runs ON the GFX task -- there is no supported way to move a task-context
 * loader call off it -- so keep models the size the format was measured with. */
#define DEFER_MS      (50u)

static struct {
    lv_timer_t *timer;
    uint32_t    addr;
    bool        running;
} s;

static const char *loader_rc_text(int32_t rc)
{
    switch (rc) {
        case  0:  return "loaded";
        case -1:  return "bad manifest: magic, version, CRC, bounds or not TFL3";
        case -2:  return "no free slot, OR a duplicate model name";
        case -3:  return "model init failed: arena too small, or heap exhausted";
        case -4:  return "sensor not supported by this loader (IMU only)";
        case -5:  return "axis convention absent or not the one applied here";
        case -6:  return "training sample rate is not the rate fed here";
        case -7:  return "frame width is not the six floats supplied here";
        default:  return "unrecognised loader code";
    }
}

/* Everything the manifest can be checked against without touching the model
 * bytes. Cheap: a few hundred bytes of CRC and some integer compares. */
static bool check_header(const ai_stage_header_t *h)
{
    if (h->magic != AI_STAGE_MAGIC) {
        sdk_example_logf("magic 0x%08lX, expected 0x%08lX -- this is not a"
                         " staging buffer", (unsigned long)h->magic,
                         (unsigned long)AI_STAGE_MAGIC);
        return false;
    }
    if (h->version != AI_STAGE_VERSION) {
        sdk_example_logf("manifest version %lu, this firmware speaks %lu",
                         (unsigned long)h->version,
                         (unsigned long)AI_STAGE_VERSION);
        return false;
    }

    /* header_crc32 covers every byte above itself, so offsetof IS the length.
     * ai_stage_crc32() is static inline in the shared header precisely so both
     * cores compute this from one implementation. */
    const uint32_t want = ai_stage_crc32(0u, (const uint8_t *)h,
                                         (uint32_t)offsetof(ai_stage_header_t,
                                                            header_crc32));
    if (want != h->header_crc32) {
        sdk_example_logf("header CRC 0x%08lX, manifest says 0x%08lX -- the"
                         " manifest is corrupt; do not trust any field in it",
                         (unsigned long)want, (unsigned long)h->header_crc32);
        return false;
    }

    if (h->model_bytes == 0u) {
        sdk_example_logf("model_bytes is 0 -- nothing was staged");
        return false;
    }
    if ((h->model_offset % AI_STAGE_ALIGN) != 0u) {
        sdk_example_logf("model_offset %lu is not a multiple of %u. The NPU"
                         " rejects a command stream off that boundary, and the"
                         " stream is padded relative to the flatbuffer base",
                         (unsigned long)h->model_offset,
                         (unsigned)AI_STAGE_ALIGN);
        return false;
    }
    if ((h->class_count == 0u) || (h->class_count > AI_STAGE_MAX_CLASSES)) {
        sdk_example_logf("class_count %u outside 1..%u",
                         (unsigned)h->class_count,
                         (unsigned)AI_STAGE_MAX_CLASSES);
        return false;
    }

    /* --- the four fields whose wrongness nothing downstream can detect --- */
    if (h->sensor != AI_STAGE_SENSOR_IMU) {
        sdk_example_logf("sensor %u: this loader builds a windowed IMU pipeline"
                         " only. Audio and radar front ends are C code no"
                         " .tflite carries", (unsigned)h->sensor);
        return false;
    }
    if (h->axis_convention != AI_STAGE_AXES_DEEPCRAFT_XY_NEG) {
        sdk_example_logf("axis_convention %u, this firmware applies %u (X and Y"
                         " negated). %s", (unsigned)h->axis_convention,
                         (unsigned)AI_STAGE_AXES_DEEPCRAFT_XY_NEG,
                         (h->axis_convention == AI_STAGE_AXES_UNDECLARED)
                             ? "Undeclared is refused, not assumed"
                             : "A mismatch runs and is silently wrong");
        return false;
    }
    if (h->sample_rate_hz != (uint32_t)AI_STAGE_IMU_FEED_HZ) {
        sdk_example_logf("trained at %lu Hz; this firmware feeds %u Hz. The"
                         " window would cover the wrong span of time",
                         (unsigned long)h->sample_rate_hz,
                         (unsigned)AI_STAGE_IMU_FEED_HZ);
        return false;
    }
    if (h->frame_bytes != (uint32_t)AI_STAGE_IMU_FRAME_BYTES) {
        sdk_example_logf("frame_bytes %lu; the IMU path supplies %u floats =="
                         " %u bytes. A wider frame reads past the caller's"
                         " array on every sample",
                         (unsigned long)h->frame_bytes,
                         (unsigned)AI_STAGE_IMU_FRAME_FLOATS,
                         (unsigned)AI_STAGE_IMU_FRAME_BYTES);
        return false;
    }

    /* An identity between three DECLARED numbers, so it proves consistency and
     * nothing more -- the frame_bytes test above is the one that protects the
     * copy. Worth running: an inconsistent manifest is a broken packer. */
    if ((h->window_depth * h->frame_bytes) != (h->in_elements * 4u)) {
        sdk_example_logf("geometry inconsistent: depth %lu x %lu bytes != %lu"
                         " floats", (unsigned long)h->window_depth,
                         (unsigned long)h->frame_bytes,
                         (unsigned long)h->in_elements);
        return false;
    }
    return true;
}

static void load_cb(lv_timer_t *t)
{
    (void)t;

    const ai_stage_header_t *h =
        (const ai_stage_header_t *)(uintptr_t)s.addr;
    const uint8_t *model = (const uint8_t *)h + h->model_offset;

    /* --- the expensive check, and the TFL3 identifier -------------------- */
    const uint32_t crc = ai_stage_crc32(0u, model, h->model_bytes);
    if (crc != h->model_crc32) {
        sdk_example_logf("model CRC 0x%08lX, manifest says 0x%08lX. The"
                         " transfer is damaged -- NOT handing this to the"
                         " loader", (unsigned long)crc,
                         (unsigned long)h->model_crc32);
        goto done;
    }
    if ((model[TFL3_OFFSET + 0u] != (uint8_t)'T') ||
        (model[TFL3_OFFSET + 1u] != (uint8_t)'F') ||
        (model[TFL3_OFFSET + 2u] != (uint8_t)'L') ||
        (model[TFL3_OFFSET + 3u] != (uint8_t)'3')) {
        sdk_example_logf("no 'TFL3' identifier at byte %u of the flatbuffer --"
                         " intact, but not a TFLite model",
                         (unsigned)TFL3_OFFSET);
        goto done;
    }
    sdk_example_logf("CRC ok over %lu bytes, TFL3 present. Calling the loader",
                     (unsigned long)h->model_bytes);

    /* --- the loader's own verdict ---------------------------------------- */
    const uint32_t heap_before = ai_model_staged_heap_free();
    const int      rc          = ai_model_staged_load(s.addr);

    if (rc < 0) {
        /* last_rc is the same code on a durable field, which is what an
         * off-core diagnosis reads. rejects counts them, so a refused load
         * cannot be mistaken for a completed one. */
        sdk_example_logf("load refused: %d -- %s", rc,
                         loader_rc_text(ai_model_staged_last_rc()));
        sdk_example_logf("%lu staged, %lu rejected this boot; heap free %lu B",
                         (unsigned long)ai_model_staged_count(),
                         (unsigned long)ai_model_staged_rejects(),
                         (unsigned long)ai_model_staged_heap_free());
        goto done;
    }

    {
        const uint32_t         heap_after = ai_model_staged_heap_free();
        const ai_model_desc_t *d          = ai_engine_model((uint32_t)rc);

        sdk_example_logf("loaded as row [%d] of %lu; last_rc %ld (%s)", rc,
                         (unsigned long)ai_engine_model_count(),
                         (long)ai_model_staged_last_rc(),
                         loader_rc_text(ai_model_staged_last_rc()));
        sdk_example_logf("'%s', %u class; %lu staged, %lu rejected this boot",
                         ((d != NULL) && (d->name != NULL)) ? d->name : "?",
                         (d != NULL) ? (unsigned)d->class_count : 0u,
                         (unsigned long)ai_model_staged_count(),
                         (unsigned long)ai_model_staged_rejects());
        sdk_example_logf("CM55 heap %lu -> %lu B. The slot and the row are"
                         " held for the whole boot", (unsigned long)heap_before,
                         (unsigned long)heap_after);
        sdk_example_logf("ai_engine_start(%d) will run it like any other row",
                         rc);
    }

done:
    if (s.timer != NULL) {
        lv_timer_delete(s.timer);
        s.timer = NULL;
    }
    s.running = false;
}

int example_edge_ai_staged_load(lv_obj_t *parent)
{
    (void)parent;   /* the verdict is a set of numbers; the log carries them */

    if (s.running) {
        sdk_example_logf("a load is already in flight");
        return SDK_EX_BUSY;
    }

    const uint32_t addr = (uint32_t)TESAIOT_EXAMPLE_STAGE_ADDR;

    if (addr == 0u) {
        sdk_example_logf("no staging address. Rebuild with"
                         " CFLAGS+=-DTESAIOT_EXAMPLE_STAGE_ADDR=0x...");
        sdk_example_logf("in an application it arrives in the CTRL message"
                         " that asked for the load, not as a constant");
        sdk_example_logf("%lu staged, %lu rejected so far; heap free %lu B",
                         (unsigned long)ai_model_staged_count(),
                         (unsigned long)ai_model_staged_rejects(),
                         (unsigned long)ai_model_staged_heap_free());
        return SDK_EX_UNAVAILABLE;
    }

    /* Alignment first: every read below dereferences this pointer, and the
     * staging code aligns its own buffer at run time precisely because the
     * linker section it sits in does not guarantee it. */
    if ((addr % AI_STAGE_ALIGN) != 0u) {
        sdk_example_logf("0x%08lX is not %u-byte aligned. The NPU requires it"
                         " of the flatbuffer and the manifest sits ahead of it",
                         (unsigned long)addr, (unsigned)AI_STAGE_ALIGN);
        return SDK_EX_REFUSED;
    }

    sdk_example_logf("staging buffer at 0x%08lX; heap free %lu B",
                     (unsigned long)addr,
                     (unsigned long)ai_model_staged_heap_free());

    if (!check_header((const ai_stage_header_t *)(uintptr_t)addr)) {
        sdk_example_logf("manifest rejected here -- the loader never saw it");
        return SDK_EX_REFUSED;
    }
    sdk_example_logf("manifest ok: IMU, %u Hz, %u-byte frames",
                     (unsigned)AI_STAGE_IMU_FEED_HZ,
                     (unsigned)AI_STAGE_IMU_FRAME_BYTES);

    s.addr  = addr;
    s.timer = lv_timer_create(load_cb, DEFER_MS, NULL);
    if (s.timer == NULL) {
        sdk_example_logf("no LVGL timer available -- not loading");
        return SDK_EX_UNAVAILABLE;
    }
    s.running = true;

    return SDK_EX_STARTED;
}
