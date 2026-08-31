# Model slots — the 24 names in `consumer_must_provide.txt` that are models

`consumer_must_provide.txt` lists every symbol this archive references and does
not define. Most are the board's: FreeRTOS, the PDL, the ML middleware, libc.
Twenty-four are not. They are **model slots**, and they are the extension point
of this library.

    AIM_MOTION_init  AIM_MOTION_enqueue  AIM_MOTION_dequeue  AIM_MOTION_finalize
    AIM_AUDIO_*      AIM_RADAR_*
    IMAI_COUGH_*     IMAI_ALARM_*        IMAI_SIREN_*

The engine holds a registry of model rows; each row is those four function
pointers, imported by name from outside this archive. **A model is anything
that exports the four entry points for its slot.** You do not need this
archive's source, and it does not need rebuilding, to put a different model in
a slot it already knows.

    int  <SLOT>_init(void);                    /* 0 on success               */
    int  <SLOT>_enqueue(const float *data_in); /* one input frame in         */
    int  <SLOT>_dequeue(float *data_out);      /* one score vector out       */
    void <SLOT>_finalize(void);

That is Imagimob's IPWIN streaming ABI, which is what DEEPCRAFT™ Studio emits,
so a Studio export drops in with a four-line rename. `_enqueue`/`_dequeue`
return -1 (`NODATA`) while the model fills its window; that is normal.

The ABI, the tool and the models are Infineon's. **Imagimob AB is an Infineon
Technologies company**, DEEPCRAFT™ Studio is Infineon's Edge AI tool, and this
archive contributes the registry and the feed router around them — nothing
inside a model.

## What the template already supplies

The BENTO firmware template fills all 24 in `proj_cm55/modules/ai_models/`:

- `model_motion.c`, `model_audio.c`, `model_radar.c` — **DEEPCRAFT™ Studio
  exports, © Imagimob AB, an Infineon Technologies company.** They are not
  TESAIoT's: nobody here trained them or owns them, and the Apache-2.0 grant on
  this project's own code does not reach inside them. Their headers reserve all
  rights and grant nothing, so no terms accompany them here — they are credited,
  not licensed on. Used for research and teaching, not commercially; a grant
  comes from Infineon and Imagimob, not from us. See
  `THIRD_PARTY_NOTICES.md` §2.2 and §4.3, and
  https://www.infineon.com/design-resources/embedded-software/deepcraft-edge-ai-solutions/deepcraft-studio
- `ai_model_slots.c` — a weak "slot not filled" definition for **all 24
  symbols**, compiled unconditionally. Three slots (cough, alarm, siren) are
  empty: they were demonstrated with DEEPCRAFT™ Ready Models — also Imagimob's,
  published by Infineon — which are licensed for evaluation only and are not
  redistributable, so no copy ships. Their `init()` returns -2, so the image
  links and boots, those three report that they did not load, and every other
  model runs. Credit and the licence citations: `THIRD_PARTY_NOTICES.md` §2.4.

  It covers all six slots rather than only the empty three because this archive
  references all 24 names whatever `AI_MODELS` the consumer selects; a partial
  selection would otherwise leave undefined references.

So the archive links out of the box against the template, with nothing to
download. If you are integrating it into your own project instead, these 24
names are yours to define.

## Or skip the slots entirely

`ai_engine_register(const ai_model_desc_t *desc)` adds a registry row at run
time, from your own code. No slot name, no rebuilt archive, no involvement from
TESAIoT: fill in a descriptor with your four functions and the model joins the
menu and the sets like one of the models already in the tree. `ai_engine_dyn_count()` and
`ai_engine_dyn_capacity()` report the rows added so far and the ceiling. The
contract — task context only, and the descriptor is copied while the strings
inside it are not — is above the declaration in `include/ai_engine.h`.

Check before you rely on it: `grep ai_engine_register api.txt`. An archive cut
before this symbol was exported carries it under an internal name and will not
link against it; the slot routes above and below work on every build.

## One linker rule, and it bites

The stubs are **weak**, so a real model compiled into the same link as a `.c`
or `.o` overrides them with no further action.

A real model that arrives as a **static archive** does not. GNU ld will not
extract an archive member to replace a definition it already has, even a weak
one — measured on arm-none-eabi-ld 14.2.1. The stub wins, silently: no warning,
no undefined reference, and the model never runs. When your model is a `.a`,
**compile that slot's stub out.** The template's
`proj_cm55/Makefile` does this automatically for any
`modules/ai_models/<model>_lib*.a` it finds, by defining
`AI_SLOT_PROVIDED_<model>`.

ModusToolbox links every `.a` under the application whether or not the Makefile
names it, so an archive that does not match that pattern is linked *without*
switching the stub off, and the stub wins. The template refuses to build in that
case rather than let it happen quietly. And renaming a vendor's evaluation
archive does not change its licence: an archive you did not author is still not
yours to redistribute.

Full procedure, with the sensor feed and score vector each slot expects:
`proj_cm55/modules/ai_models/README.md`, "Filling a model slot".
