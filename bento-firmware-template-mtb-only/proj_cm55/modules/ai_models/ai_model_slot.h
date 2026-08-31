/* ai_model_slot.h — the model slot ABI.
 *
 * The Edge AI engine (lib/edge_ai/libbento_edge_ai.a) holds a registry of model
 * rows. Each row is four function pointers, and the engine imports those four
 * functions BY NAME from outside the archive. That import is the seam: a model
 * is anything that exports the four entry points for its slot. The engine never
 * needs rebuilding to accept a different model in a slot it already knows.
 *
 * The four entry points are Imagimob's IPWIN streaming queue ABI, which is what
 * DEEPCRAFT Studio emits, so a Studio export drops straight in:
 *
 *     int  <SLOT>_init(void);                     0 on success
 *     int  <SLOT>_enqueue(const float *data_in);  one input frame in
 *     int  <SLOT>_dequeue(float *data_out);       one score vector out
 *     void <SLOT>_finalize(void);
 *
 * Return codes are IPWIN_RET_* below. enqueue/dequeue return NODATA (-1) while
 * the model is still filling its window; that is normal, not an error.
 *
 * The slot names this build's engine imports, and where each is filled:
 *
 *   AIM_MOTION_*   modules/ai_models/model_motion.c   (ships, DEEPCRAFT export)
 *   AIM_AUDIO_*    modules/ai_models/model_audio.c    (ships, DEEPCRAFT export)
 *   AIM_RADAR_*    modules/ai_models/model_radar.c    (ships, DEEPCRAFT export)
 *   IMAI_COUGH_*   UNFILLED
 *   IMAI_ALARM_*   UNFILLED
 *   IMAI_SIREN_*   UNFILLED
 *
 * Every slot also has a weak "not filled" definition in ai_model_slots.c, which
 * is compiled unconditionally. The prebuilt engine references all 24 names
 * whatever AI_MODELS selects, so without that a subset build such as
 * AI_MODELS="motion radar" leaves 16 undefined references. A real model
 * overrides the weak definition of the slot it fills.
 *
 * The AIM_/IMAI_ split is history, not meaning: the registry row fixes the name
 * and the engine imports exactly that. lib/edge_ai/consumer_must_provide.txt is
 * the authoritative list, read off the shipped binary.
 *
 * See README.md, "Filling a model slot", for the procedure and for the one
 * linker rule that matters when your model arrives as a .a rather than a .c.
 */
#pragma once

/* Imagimob IPWIN return codes. The generated model headers (model_audio.h and
 * friends) define these too, with the same values; the guards let a slot file
 * include either or both. */
#ifndef IPWIN_RET_SUCCESS
#define IPWIN_RET_SUCCESS    0
#define IPWIN_RET_NODATA    -1
#define IPWIN_RET_ERROR     -2
#define IPWIN_RET_STREAMEND -3
#endif

/* Declare a slot's four entry points. */
#define AI_MODEL_SLOT_DECLARE(SLOT)                    \
    int  SLOT##_init(void);                            \
    int  SLOT##_enqueue(const float *data_in);         \
    int  SLOT##_dequeue(float *data_out);              \
    void SLOT##_finalize(void)

/* Emit a weak "slot not filled" implementation of a slot.
 *
 * init() returns IPWIN_RET_ERROR, which the engine reads as "this model did not
 * load": it declines to make the model active, records the code in
 * ai_engine_last_init_rc(), and every other model in the image keeps running.
 * Nothing faults and nothing is silently wrong — the model simply reports no
 * result.
 *
 * WHAT_NAME is a short literal naming the slot. It is emitted into .rodata so
 * an unfilled slot is visible in a built image without a debugger:
 *
 *     strings <your>.elf | grep BENTO-MODEL-SLOT
 *
 * These definitions are weak, so a real model compiled into the SAME link as an
 * object file overrides them with no further action. A real model arriving as a
 * static ARCHIVE does NOT: GNU ld will not extract an archive member to replace
 * a definition it already has, even a weak one (measured, arm-none-eabi-ld
 * 14.2.1). The project Makefile therefore drops this file from the build
 * whenever it finds a matching <model>_lib*.a to link. Do not rely on weakness
 * alone against an archive.
 */
#define AI_MODEL_SLOT_UNFILLED(SLOT, WHAT_NAME)                               \
    __attribute__((weak)) int SLOT##_init(void) {                             \
        static const char *volatile marker =                                  \
            "BENTO-MODEL-SLOT unfilled: " WHAT_NAME;                          \
        (void)marker;                                                         \
        return IPWIN_RET_ERROR;                                               \
    }                                                                         \
    __attribute__((weak)) int SLOT##_enqueue(const float *data_in) {          \
        (void)data_in; return IPWIN_RET_ERROR;                                \
    }                                                                         \
    __attribute__((weak)) int SLOT##_dequeue(float *data_out) {               \
        (void)data_out; return IPWIN_RET_NODATA;                              \
    }                                                                         \
    __attribute__((weak)) void SLOT##_finalize(void) { }                      \
    struct ai_model_slot_##SLOT##_needs_a_semicolon
