/* ai_model_slots.c — a weak "slot not filled" definition for every model slot
 * the Edge AI engine imports.
 *
 * WHY THIS FILE IS COMPILED UNCONDITIONALLY
 *
 * lib/edge_ai/libbento_edge_ai.a is prebuilt with the full six-model registry,
 * so it references all 24 slot symbols no matter what AI_MODELS this image
 * selects. Compile only the models in AI_MODELS and the rest are undefined:
 * `AI_MODELS="motion radar"` left 20 undefined references before this file
 * existed. So every slot gets a weak definition here, always, and a real model
 * overrides the ones it fills.
 *
 * The engine reads a non-zero init() as "this model did not load": it declines
 * to activate the model, records the code in ai_engine_last_init_rc(), prunes
 * it from any set it belongs to, and leaves every other model running. An
 * unfilled slot therefore costs a menu entry that reports nothing, not a
 * broken image.
 *
 * HOW A REAL MODEL TAKES A SLOT
 *
 *   As a .c/.o   Drop model_<name>.c into this directory exporting the slot's
 *                four entry points. A strong definition in an object file beats
 *                the weak one here with no further action.
 *
 *   As a .a      Name it <name>_lib*.a in this directory. The Makefile links it
 *                AND defines AI_SLOT_PROVIDED_<name>, which compiles that
 *                slot's stub out of this file. That second half is required,
 *                not tidiness: GNU ld does not extract an archive member to
 *                replace a definition it already has, even a weak one
 *                (measured, arm-none-eabi-ld 14.2.1), so a stub left in the
 *                link would win silently and the model would never run.
 *
 *   At run time  ai_engine_register() — no slot needed at all. See ai_engine.h.
 *
 * Full procedure, with the sensor feed and score vector each slot expects:
 * README.md, "Filling a model slot".
 */

#include "ai_model_slot.h"

/* --- IMU ------------------------------------------------------------------ */
#if !defined(AI_SLOT_PROVIDED_motion)
AI_MODEL_SLOT_UNFILLED(AIM_MOTION, "motion");
#endif

/* --- microphone ----------------------------------------------------------- */
#if !defined(AI_SLOT_PROVIDED_audio)
AI_MODEL_SLOT_UNFILLED(AIM_AUDIO, "audio");
#endif
#if !defined(AI_SLOT_PROVIDED_cough)
AI_MODEL_SLOT_UNFILLED(IMAI_COUGH, "cough");
#endif
#if !defined(AI_SLOT_PROVIDED_alarm)
AI_MODEL_SLOT_UNFILLED(IMAI_ALARM, "alarm");
#endif
#if !defined(AI_SLOT_PROVIDED_siren)
AI_MODEL_SLOT_UNFILLED(IMAI_SIREN, "siren");
#endif

/* --- radar ---------------------------------------------------------------- */
#if !defined(AI_SLOT_PROVIDED_radar)
AI_MODEL_SLOT_UNFILLED(AIM_RADAR, "radar");
#endif
