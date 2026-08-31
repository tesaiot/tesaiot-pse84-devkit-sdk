# Edge AI models

## The three empty audio slots

This SDK ships **no** cough, alarm or siren model. Those three slots are covered
by a weak "slot not filled" stub in `ai_model_slots.c`: the image links and
boots, the three models appear in the Edge AI menu and report that they did not
load, and every other model runs normally. "Filling a model slot" below is the
whole procedure for putting a real model in one.

`ai_model_slots.c` carries a weak definition for **all six** slots, not just the
three empty ones, and is compiled whatever `AI_MODELS` you select. The prebuilt
engine references all 24 slot symbols regardless, so a partial selection such as
`AI_MODELS="motion radar"` would otherwise leave 16 undefined references.

They are empty for a licence reason, and the reason is worth reading once.

## Credit: every model in this directory is Infineon's, not ours

Read this before anything else in this file. **TESAIoT trained no model, authored
no model and owns no model.** Six generated files ship here and three archives
were demonstrated here, and every one of them is the work of **Imagimob AB, an
Infineon Technologies company**. The six generated files are DEEPCRAFT™ Studio
exports; the three archives are DEEPCRAFT™ Ready Models, delivered through
DEEPCRAFT™ Studio — a different product, and not something this project can say
was "produced with" the tool.

What is ours in this directory is the plumbing: `ai_model_slot.h`,
`ai_model_slots.c` and the prebuilt engine that calls the four entry points.
**`audio_pdm.c` / `.h` is not on that list.** It is ported from Infineon's
mtb-example-psoc-edge-ml-deepcraft-deploy-audio and its data path is Infineon's —
the file says so at `audio_pdm.h:5-7` and `audio_pdm.c:10-14`. See "What is
here" below and `THIRD_PARTY_NOTICES.md` §2.5.

### The three archives that are not in this package

`siren_lib_eval.a`, `cough_lib_eval.a` and `alarm_lib_eval.a` are the archives
those three slots were demonstrated with. They are **not in this package**, and
they are not ours to put there: they are **DEEPCRAFT™ Ready Models** (Siren
Detection, Cough Detection and Factory Alarm Detection), authored by **Imagimob
AB, an Infineon Technologies company**, and published by Infineon for
PSoC™ Edge. They arrive already trained, already quantised and
already Vela-compiled for the Ethos-U55 NPU, and they are why this kit can
demonstrate real audio Edge AI on day one. TESAIoT wrote none of them.

- Ready Models — https://www.infineon.com/design-resources/embedded-software/deepcraft-edge-ai-solutions/deepcraft-ready-models
- Upstream example — https://github.com/Infineon/mtb-example-psoc-edge-ml-deepcraft-deploy-ready-model (`release-v1.4.1`)
- DEEPCRAFT™ Studio — https://www.infineon.com/design-resources/embedded-software/deepcraft-edge-ai-solutions/deepcraft-studio
- Licence — https://developer.imagimob.com/legal/ai-model-evaluation-license-agreement

**Our position.** TESAIoT references them under the Imagimob AI Model
Evaluation License Agreement and abides by it. The use here is research and
teaching; we hold no rights in these models and pass none on.

**What you may and may not do.** Evaluate them on your own kit — yes, for 60
days (§2.1). Ship a product that links them, redistribute the archives, or put
them to any commercial use — no (§2.2(a), §2.2(c)); delete every copy when the
period ends (§10.3). Infineon's own EULA does not widen this: its §3 defers to
this agreement. They are also metered — Infineon's README for the upstream
example states Ready Models *"come with a limited number of inferences"*, and
each archive here stops returning results after a fixed number. A default build
therefore goes quiet on them eventually; that is the meter, not a fault.

**Two lawful routes to production.** Buy the non-evaluation model from
Imagimob/Infineon (§2.1 names this route), or train your own in DEEPCRAFT™
Studio — a model you train yourself carries no evaluation limit, and Imagimob's
published licensing metrics make production use on Infineon MCUs free. That
"free" row covers a model you train yourself; it does not cover Ready Models.
Route 2 is what "Filling a model slot" below describes. `model_audio.c`,
`model_motion.c` and `model_radar.c` show what a Studio export looks like once it
lands here — they are **not** something we trained; see the next section.

**Our one alteration, disclosed.** The recipe in `../../Makefile` (the comment
block above the ready-model link rules) runs `objcopy` over each archive to
rename its global symbols and sections. All three Ready Models
export the same Imagimob `IMAI_*` entry points, so without that no two of them
link into one image. Nothing else changes — the byte difference from upstream is
exactly the longer symbol strings. §2.2(d) prohibits altering the AI Model, so
it is stated here rather than left to be found. Infineon and Imagimob are welcome
to tell us they would rather it were done differently, and we will follow
whatever they prefer.

Clause quotations and provenance: `THIRD_PARTY.md` and `THIRD_PARTY_NOTICES.md`
§2.4 at the root of this package.

### The three model files that are here

`model_motion.{c,h}`, `model_audio.{c,h}` and `model_radar.{c,h}` **do** ship,
and they are Imagimob's too. Each is a **DEEPCRAFT™ Studio export** — C emitted
by Imagimob's ImagiNet compiler — and each says so in its own first lines:

```
* ImagiNet Compiler <version>
* Copyright © 2023- Imagimob AB, All Rights Reserved.
*
* Generated at <date> UTC. Any changes will be lost.
```

Per file, from lines 2 and 5: motion `5.6.3587.65534`, generated 2025-09-25;
audio `5.5.3417.65534`, generated 2025-08-20; radar `5.8.4292`, generated
2026-02-20.

**TESAIoT did not train these and does not own them.** They came out of
Infineon's tool and they carry Imagimob's copyright, and anything generated by
DEEPCRAFT™ Studio or derived from a DEEPCRAFT™ model is Imagimob's and
Infineon's; any terms for it are theirs to set, and none are set here. The
Apache-2.0 grant in this template's
`LICENSE` covers the code this project wrote and **does not reach inside these
files**. Our use of them is research and teaching, **not commercial deployment**;
if you intend to ship a product containing them or anything derived from them,
settle that with Infineon and Imagimob first.

Two practical rules follow:

- **Never edit these files.** The generator says so on line 5, and the copyright
  line says the rest.
- **Never add an SPDX header or a TESAIoT copyright notice to them.** A tag
  written over Imagimob's notice would be a false licence claim.

Go to the source rather than to our copy:

- DEEPCRAFT™ Studio, which generates this code and where you can train your own —
  https://www.infineon.com/design-resources/embedded-software/deepcraft-edge-ai-solutions/deepcraft-studio
- Infineon's PSoC™ Edge DEEPCRAFT™ example, where the Ready Models and their
  licence live —
  https://github.com/Infineon/mtb-example-psoc-edge-ml-deepcraft-deploy-ready-model

Full entry: `THIRD_PARTY_NOTICES.md` §2.2 and §4.3.

<!-- //! [doc-drift-fix] — see docs/template_local_deltas.list; a sync that reverts this file must be refused -->

This directory holds the models. The engine that runs them ships as
`../../../lib/edge_ai/`, a prebuilt archive — see its `README.md`, `api.txt`
and `consumer_must_provide.txt`.

## What is here

| File | What it is |
|---|---|
| `model_audio.c`, `model_motion.c`, `model_radar.c` | **DEEPCRAFT™ Studio exports, © Imagimob AB, an Infineon Technologies company — not TESAIoT's work.** Generated model code filling three slots; weights are inline arrays placed in `CY_SECTION(CY_ML_MODEL_MEM)`. Do not edit and do not re-license — see "Credit" above. |
| `ai_model_slots.c` | A weak "slot not filled" definition for **every** slot, compiled unconditionally. It is what makes the SDK link with no model archive present, and what makes a partial `AI_MODELS` link at all. |
| `ai_model_slot.h` | The slot ABI: the four entry points a model exports, and the return codes. Read this first. |
| `audio_pdm.c` / `.h` | The PDM microphone front end, and **not TESAIoT's own work**: ported from Infineon's `mtb-example-psoc-edge-ml-deepcraft-deploy-audio` (the file says so at `audio_pdm.h:5-7`), with the data path faithful to that reference (`audio_pdm.c:10-14`) and the decimation the reference's because that is what the model was trained against (`audio_pdm.h:15-18`). The AI Kit BSP adaptation and the build guards are ours. The engine calls into it; it is not part of the archive. Terms are not established — `THIRD_PARTY_NOTICES.md` §2.5. |
| `ai_engine.h` | The engine's public interface. The implementation is in the archive. |

## Choosing which models are built

`AI_MODELS` in `../../Makefile` selects them, and `EDGE_AI_MODEL` picks the
preset. The default is `combo`. Each model costs flash whether or not it is
ever started, so a product build should list only what it uses.

```
make build EDGE_AI_MODEL=motion       # one model
make build AI_MODELS="motion radar"   # an explicit set
```

## Using the engine

```c
#include "ai_engine.h"

ai_engine_init();                          /* once, after the IPC pipe is up */
ai_engine_start(AI_PARALLEL_MIC);          /* or a single model index */

ai_result_t r;
if (ai_engine_snapshot(&r) && r.running) {
    uint8_t which = 0;
    int pct = ai_result_top_positive(&r, &which);    /* strongest POSITIVE class, 0..100 */
    const ai_model_desc_t *m = ai_engine_model(r.model_index);
    /* m->class_labels[which], pct, r.top_class, r.scores[], r.inference_us */
}
```

`ai_result_t` carries raw `scores[]` plus `top_class`, not a label or a single
confidence. `ai_result_top_positive()` (a static inline in `ai_engine.h`)
applies the registry contract — class 0 is the negative class and is skipped —
and is the same rule the Edge AI page and the MicroPython model link use.

`ai_engine_model_count()` and `ai_engine_model()` enumerate the single models;
`ai_engine_set_models()` and `ai_engine_set_name()` describe the sets. From
MicroPython the same surface is `edge_ai.*` — see the IDE's API reference.

## If a model does not respond

`ai_engine_stale_drops()` counts inference results the NPU returned too late to
be trusted; a rising count means the model is not keeping up with its feed.
`ai_engine_dq_calls()` and `ai_engine_dq_ok()` give the attempt and success
counts, and `ai_engine_npu_cycles()` the last measured NPU cost. A model whose
sensor has stopped producing data reports no result rather than a stale one.

## Filling a model slot

The engine holds a registry of model rows. Each row is four function pointers,
and the engine imports those four functions **by name** from outside the
archive. That import is the seam. A model is anything that exports the four
entry points for its slot, so you can fill a slot without touching, rebuilding
or even having the source of `lib/edge_ai/libbento_edge_ai.a`.

The four entry points are Imagimob's IPWIN streaming ABI, which is what
DEEPCRAFT Studio emits:

```c
int  <SLOT>_init(void);                    /* 0 on success                    */
int  <SLOT>_enqueue(const float *data_in); /* one input frame in              */
int  <SLOT>_dequeue(float *data_out);      /* one score vector out            */
void <SLOT>_finalize(void);
```

`enqueue`/`dequeue` return `IPWIN_RET_NODATA` (-1) while the model is still
filling its window. That is normal, not an error. Full definitions are in
`ai_model_slot.h`.

### The slots this build has

| Model name | Slot symbols | Sensor feed | Filled by |
|---|---|---|---|
| `motion` | `AIM_MOTION_*` | BMI270 IMU | `model_motion.c` |
| `audio` | `AIM_AUDIO_*` | PDM mic, 16 kHz f32 | `model_audio.c` |
| `radar` | `AIM_RADAR_*` | 60 GHz radar frames | `model_radar.c` |
| `cough` | `IMAI_COUGH_*` | PDM mic, 16 kHz f32 | *(empty — weak stub)* |
| `alarm` | `IMAI_ALARM_*` | PDM mic, 16 kHz f32 | *(empty — weak stub)* |
| `siren` | `IMAI_SIREN_*` | PDM mic, 16 kHz f32 | *(empty — weak stub)* |

The `AIM_`/`IMAI_` split is history, not meaning: the registry row fixes the
name and the engine imports exactly that. The authoritative list is read off
the shipped binary, in `lib/edge_ai/consumer_must_provide.txt`.

### Route 0 — register a model at run time (no slot needed)

The engine also takes new models at run time. This is the route with no slot
list at all: you supply a descriptor and the engine adds a row.

```c
#include "ai_engine.h"

static int  my_init(void)               { return 0; }
static int  my_enqueue(const float *in) { (void)in;  return 0; }
static int  my_dequeue(float *out)      { out[0] = 0.2f; out[1] = 0.8f; return 0; }
static void my_finalize(void)           { }

/* Static storage: the descriptor is copied, the strings inside it are not,
 * and they are read from an ISR. */
static const ai_model_desc_t my_desc = {
    .name         = "MyModel",
    .description  = "what it detects, one line",
    .sensor       = AI_SENSOR_MIC,
    .class_count  = 2,
    .class_labels = { "idle", "event" },   /* index 0 is the NEGATIVE class */
    .flash_bytes  = 0,
    .period_ms    = 100,
    .init = my_init, .enqueue = my_enqueue,
    .dequeue = my_dequeue, .finalize = my_finalize,
};

void my_model_register(void)
{
    int idx = ai_engine_register(&my_desc);   /* registry index, or -1 */
    if (idx >= 0) { ai_engine_start((uint32_t)idx); }
}
```

Index 0 of `class_labels[]` must be the negative class — the one that means
"nothing is happening". The Edge AI page takes a row's confidence as the
maximum over classes 1 and up, so a model whose classes are all positive
renders as if the loudest one were always firing.

Call it from task context before or after `ai_engine_init()`, never from an
ISR. The row then behaves like a built-in: it appears in the Edge AI menu, it
can join a set, and `ai_engine_start(idx)` runs it. `ai_engine_dyn_count()` and
`ai_engine_dyn_capacity()` report how many run-time rows exist and the ceiling.

Read the contract above the declaration in `ai_engine.h` before you use it —
particularly that the descriptor is copied but the strings inside it are not,
so every string must outlive the boot.

Check before you rely on it: `grep ai_engine_register lib/edge_ai/api.txt`. An
archive cut before this symbol was exported carries it under an internal name
and will not link against it; the compile-time routes below work on every
build.

Use this route when you want a model the registry has no slot for. Use the
compile-time routes below when you are replacing one of the six the board
already ships with.

### Route 1 — a model you trained (recommended)

Train in DEEPCRAFT™ Studio, export for PSoC™ Edge, then:

1. Drop the generated source into this directory as `model_<slot>.c` — e.g.
   `model_cough.c`.
2. Rename its four entry points from `IMAI_*` to the slot's names
   (`IMAI_COUGH_init`, `_enqueue`, `_dequeue`, `_finalize`).
3. Make sure `<slot>` is in `AI_MODELS` (it is, in the default `combo`), and
   `make build`.

Your strong definitions override the weak stubs in `ai_model_slots.c`
automatically — a strong definition in an object file beats a weak one, so there
is nothing else to switch off. A model you trained yourself carries no
evaluation inference limit.

### Route 2 — a model that arrives as a static archive

A licensed Ready Model, or any build system that hands you a `.a`:

1. Put it in this directory named **`<slot>_lib.a`** — e.g. `cough_lib.a`. The
   name matters; see the two warnings below.
2. `make build`.

`proj_cm55/Makefile` links any `./modules/ai_models/<model>_lib*.a` whose model
is in `AI_MODELS`, and defines `AI_SLOT_PROVIDED_<model>`, which compiles that
slot's stub out of `ai_model_slots.c`.

**Why the stub must go, not merely lose.** The stub is a weak definition, and
GNU ld does not extract an archive member to replace a definition it already
has, even a weak one — measured on arm-none-eabi-ld 14.2.1. Leave the stub in
the link beside an archive and the stub wins, with no warning and no linker
error; the model simply never runs. Compiling it out is what makes the override
deterministic.

**Name it correctly or the build stops.** ModusToolbox links every `.a` it finds
under the application
(`core-make .../make/core/search_filter_v1.mk:49-53`), so an archive named
anything else — `deepcraft_cough.a`, `my_model.a` — is still linked but does
*not* switch the stub off, and the stub silently wins. The Makefile refuses to
build when it sees a `.a` here that does not match `<model>_lib*.a`, rather than
let that happen quietly.

> **Licensing — read before you copy anything in.** A DEEPCRAFT™ Ready Model
> archive is licensed to *you* for evaluation and is **not redistributable**.
> Putting one in this directory puts it inside anything you build, zip or
> publish from this tree. The release tooling refuses to package a file named
> `*_lib_eval.a`, but that check keys on the name: an archive you have renamed
> to `cough_lib.a` will not trip it. Renaming a file does not change its
> licence. Do not commit or redistribute a model archive you did not author or
> buy redistribution rights to.

Two archives that both export the unprefixed `IMAI_init`/`_enqueue`/`_dequeue`/
`_finalize` cannot co-reside. Give each a unique namespace with `objcopy
--redefine-syms` before linking more than one — the full recipe, including the
section renames the `.fw_identity` wall requires, is in the comment block above
the ready-model rules in `../../Makefile`.

### Route 3 — any other runtime

TFLite Micro, CMSIS-NN, a hand-written DSP classifier: write the four functions
and put them in a `.c` in this directory. The engine calls them and cares about
nothing else. Feed rates, frame shapes and the score vector each slot expects
are in the table above and in `ai_engine.h`.

### Checking your work

Build the configuration a customer gets — every slot unfilled — even on a
machine that has model archives sitting in this directory:

```bash
make build EDGE_AI_IGNORE_MODEL_LIBS=1
```

Without that flag a tree holding `cough_lib_eval.a` and friends never compiles
its own stub path, so a fault there would not show up until a customer hit it.

```bash
# which slots are still empty, read off the built image
arm-none-eabi-strings proj_cm55/build/*/*/proj_cm55.elf | grep BENTO-MODEL-SLOT

# where a slot resolved to
arm-none-eabi-nm proj_cm55/build/*/*/proj_cm55.elf | grep IMAI_COUGH_init
#   'W' = still the weak stub;  'T' = your model
```

At run time, a slot that is still a stub reports `ai_engine_last_init_rc() ==
-2` (`IPWIN_RET_ERROR`) and never becomes active. The engine keeps every other
model running; it does not fail the image.

### A model the registry has no slot for

Adding `model_keyword.c` alone gives you a file the build compiles and the
engine never calls, because there is no `keyword` row in the registry. The
compile-time rows are inside the archive.

That is what Route 0 is for: `ai_engine_register()` adds the row at run time,
from your own code, with no rebuilt archive and no involvement from TESAIoT.
A new compile-time row is the only thing that still needs a rebuilt engine, and
it buys nothing a registered row does not already give you.
