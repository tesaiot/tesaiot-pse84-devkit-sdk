# Third-party software in this template

This template compiles code that other people wrote. This file records, per
component, **where it came from and what its licence actually permits** — so
that anyone redistributing this tree can check the answer rather than infer it.

## How to read a licence in this tree

Most vendored files here carry a per-file banner. **Do not decide anything from
the banner alone.** Two banners are common and both are misleading in this tree:

- *"Cypress hereby grants you a personal, non-exclusive, non-transferable
  license to copy, modify, and compile…"* — no redistribution right in the text.
- *"(c) Infineon Technologies AG… any use, reproduction, modification… is
  prohibited"* — stricter still.

Neither is reliable, and the failure runs in **both** directions:

- `optiga-trust-m`'s repository LICENSE is **MIT**. Several PAL files in this
  tree wear the Cypress banner while being *token-identical* to that MIT code —
  the banner was stamped across a directory by whoever assembled the PAL, and it
  does not describe the licence.
- `emusb-host`'s repository LICENSE **is** the Cypress EULA. There the banner
  and the repository agree, and the restriction is real.

**The governing instrument is the upstream repository's root LICENSE**, for the
tag that `deps/*.mtb` or `libs/*.mtb` pins. That is what the table below records.
Where a file's origin could not be traced to a repository, this file says
"not established" rather than guessing in either direction.

## Fetched components

These are **not** in this package. `make getlibs` brings them down from the
upstream repository, pinned to the tag shown. The upstream copy is the
authoritative one; link to it rather than to any copy here.

| Component | Upstream | Tag | Licence |
|---|---|---|---|
| optiga-trust-m | https://github.com/Infineon/optiga-trust-m | `release-v5.3.0` | MIT |
| audio-codec-tlv320dac3100 | https://github.com/Infineon/audio-codec-tlv320dac3100 | `release-v1.0.0` | Apache-2.0 |
| lvgl | https://github.com/lvgl/lvgl | `v9.5.0` | MIT |
| emusb-host | https://github.com/Infineon/emusb-host | `release-v2.2.0` | Cypress EULA — **binary only** |
| ifx-mbedtls | https://github.com/Infineon/ifx-mbedtls | `release-v3.6.400` | Apache-2.0 |
| mtb-dsl-pse8xxgp | https://github.com/Infineon/mtb-dsl-pse8xxgp | `release-v1.2.0` | Apache-2.0 |
| freertos | https://github.com/cypresssemiconductorco/freertos | `release-v10.6.201/202` | MIT |
| lwip | https://github.com/lwip-tcpip/lwip | `STABLE-2_1_2_RELEASE` | BSD-3-Clause |
| wifi-host-driver | https://github.com/cypresssemiconductorco/wifi-host-driver | `release-v5.0.8` | Apache-2.0 |
| ml-middleware | https://github.com/Infineon/ml-middleware | `release-v3.1.0` | Apache-2.0 |
| sensor-xensiv-bgt60trxx | https://github.com/Infineon/sensor-xensiv-bgt60trxx | `release-v2.0.0` | Apache-2.0 |

The full pinned set is `proj_cm33_s/libs/*.mtb`, `proj_cm33_ns/{deps,libs}/*.mtb`
and `proj_cm55/deps/*.mtb` — 41 assets. Each carries its own LICENSE inside
`mtb_shared/<asset>/<tag>/` once fetched.

### emUSB-Host — fetched *and* copied out

emUSB-Host is SEGGER's USB stack, licensed to Cypress for **object-code**
redistribution only. Its source may not travel in this package, and none of it
does. The two configuration files the build needs
(`usbh_config.c`, `usbh_config_io.c`) are copied out of the fetched asset's
`export/Config/` at the start of every `proj_cm55` build by
`materialize_emusb_config.sh`, which also applies the three changes that are
ours. Infineon ships those templates for exactly this purpose — the asset's
`.cyignore` excludes `export/Config` so ModusToolbox will not compile them where
they sit. See README §7.1.

## DEEPCRAFT™ Ready Models — Siren, Cough and Factory Alarm, by Imagimob AB

Three of the audio slots in `proj_cm55/modules/ai_models/` were demonstrated
with prebuilt archives that are **not in this package**. They are **DEEPCRAFT™
Ready Models** — **Siren Detection**, **Cough Detection** and **Factory Alarm
Detection** — authored by **Imagimob AB, an Infineon Technologies company**, and
published by Infineon for PSoC™ Edge. TESAIoT wrote none of them, and they are
the reason this kit can demonstrate real audio Edge AI at all: each arrives
already trained on an acoustic corpus we do not have, already quantised, and
already Vela-compiled for the Ethos-U55 NPU.

Go to the source rather than to our copy:

- **DEEPCRAFT™ Ready Models** —
  https://www.infineon.com/design-resources/embedded-software/deepcraft-edge-ai-solutions/deepcraft-ready-models
- **Upstream code example** —
  https://github.com/Infineon/mtb-example-psoc-edge-ml-deepcraft-deploy-ready-model
  (`release-v1.4.1`), named by this project's `proj_cm55/Makefile:596`
- **DEEPCRAFT™ Studio**, where they are delivered and where you can train your
  own —
  https://www.infineon.com/design-resources/embedded-software/deepcraft-edge-ai-solutions/deepcraft-studio
- **The licence** —
  https://developer.imagimob.com/legal/ai-model-evaluation-license-agreement

They are listed apart from the table above because their upstream is a code
example rather than a library asset, and because their licence permits less
than the rest of that table does.

**TESAIoT's position.** We hold no rights in these models and pass none on. We
reference them under the Imagimob AI Model Evaluation License Agreement and
abide by its terms; the use here is research and teaching, **not commercial
deployment**. Redistribution stays prohibited, so they are fetched from Infineon
rather than shipped. Anyone who wants those three slots filled in a product
takes one of the two routes below, and both lead to Infineon and Imagimob.

| Component | Path | Upstream | Licence | Redistributable? |
|---|---|---|---|---|
| DEEPCRAFT™ Ready Models — Siren, Cough, Factory Alarm (© Imagimob AB, an Infineon Technologies company) | `proj_cm55/modules/ai_models/{siren,cough,alarm}_lib_eval.a` — **absent from this package**; the slots fall through to the weak stubs in `ai_model_slots.c` | https://github.com/Infineon/mtb-example-psoc-edge-ml-deepcraft-deploy-ready-model @ `release-v1.4.1`, `proj_cm55/ready_models/CONFIG_Debug/TOOLCHAIN_GCC_ARM/`. Product page: infineon.com → DEEPCRAFT™ Ready Models | **Imagimob AI Model Evaluation License Agreement**, the example's `LICENSE_Imagimob.txt` (SHA-256 `516ef9bf…c86135`, retrieved 2026-08-29). *Not* an open-source licence, and *not* the Infineon EULA that covers the example's own code — that EULA's §3 defers to this agreement. | **No.** §2.1 grants a **60-day, non-renewable** licence for **evaluation only**; §2.2(c) prohibits distributing, commercially using, publicly performing or publicly displaying the AI Model without separate written permission from Imagimob; §10.3 requires all copies to be deleted when the period ends. |

### What you may and may not do

- **Evaluate them on your own kit** — yes, for 60 days, to decide whether to
  license them (§2.1).
- **Ship a product that links them** — no (§2.2(a), §2.2(c)). That is a breach
  by the shipper, not by us; Infineon's EULA does not cover it.
- **Redistribute the archives** — no (§2.2(c)).
- **Commercial use of any kind** — no (§2.2(c)).
- **Keep them past the evaluation period** — no; delete every copy (§10.3).

### They are metered, and Infineon says so

Infineon's own README for that example says Ready Models *"are intended
specifically for testing purposes and come with a limited number of
inferences"*, and the licence reserves the right to embed *"mechanisms that
limit functionality"*. That mechanism is present and active in the three
archives here: each stops returning results after a fixed number of inferences.
If you fetch them from Infineon and fill the slots, a build eventually goes quiet
on them. That is the meter working as documented, not a fault in the model.

### The two lawful routes to production

1. **Buy the non-evaluation model** from Imagimob/Infineon — §2.1 names this
   route in its own text. No meter, no 60-day clock. Start at the Ready Models
   product page above.
2. **Train your own in DEEPCRAFT™ Studio** — a model you train yourself carries
   no evaluation limit, and Imagimob's published licensing metrics make
   production use on Infineon MCUs free. **That "free" row covers a model you
   train yourself; do not read it onto these Ready Models.**

This template is built for route 2, and route 2 runs through Infineon's tool
rather than through us. `proj_cm55/modules/ai_models/model_{audio,motion,radar}.c`
are DEEPCRAFT™ Studio exports with their weights in-tree — **Imagimob's work too,
not TESAIoT's**; see §"DEEPCRAFT™ Studio generated model code" below — and what
they show you is the shape a Studio export takes once it lands in this tree.
`proj_cm55/modules/ai_models/README.md` §"Adding a model" is the procedure for
putting your own beside them. The SDK's Edge AI chapters E1–E4 and the
`libbento_edge_ai.a` reference apply unchanged to a model you trained yourself.

### Our one alteration, disclosed

`proj_cm55/Makefile:596-620` documents an `objcopy` pass that renames the global
symbols and sections inside each archive. The reason is mundane: all three Ready
Models export the same Imagimob `IMAI_*` entry points, so without renaming no
two of them can be linked into one image, and the kit's Edge AI page offers all
three side by side. Byte-for-byte the archives are otherwise upstream's — the
difference from `release-v1.4.1` is exactly the longer symbol strings; no code,
no weights, no behaviour. §2.2(d) prohibits altering the AI Model, so we state
it here rather than leave it to be found. Infineon and Imagimob are welcome to
tell us they would rather it were done differently, and we will follow whatever
they prefer.

Full clause quotations and the provenance evidence are in
`THIRD_PARTY_NOTICES.md` §2.4.

## DEEPCRAFT™ Studio generated model code, by Imagimob AB

The three models this template **does** ship — motion, audio and radar — are not
TESAIoT's work either. Nobody here trained them, authored them or owns them.
They are **DEEPCRAFT™ Studio exports**: C source emitted by Imagimob's ImagiNet
compiler from a model built in Infineon's Edge AI tool, and every one of the six
files says so in its own opening lines.

> `Copyright © 2023- Imagimob AB, All Rights Reserved.`
>
> `Generated at <date>. Any changes will be lost.`

**Imagimob AB is an Infineon Technologies company**, and **DEEPCRAFT™ Studio** is
Infineon's Edge AI development tool. The network, the feature pipeline, the
quantised weights and the generated C are all products of that tool. What is
TESAIoT's in `proj_cm55/modules/ai_models/` is the plumbing around them — the
slot ABI in `ai_model_slot.h`, the weak stubs in `ai_model_slots.c`, and the
prebuilt engine that calls the four entry points. Nothing inside the generated
files is ours. **The PDM front end beside them is not ours either** — it is
ported from an Infineon example; see the `audio_pdm` row in the table below.

| Component | Path | Origin | Licence | Redistributable? |
|---|---|---|---|---|
| DEEPCRAFT™ Studio generated model code — motion, audio, radar (© Imagimob AB, an Infineon Technologies company) | `proj_cm55/modules/ai_models/model_{motion,audio,radar}.{c,h}` | DEEPCRAFT™ Studio / ImagiNet Compiler. Per file, lines 2 and 5: motion `5.6.3587.65534`, generated 2025-09-25; audio `5.5.3417.65534`, generated 2025-08-20; radar `5.8.4292`, generated 2026-02-20 | **`All Rights Reserved`** — a reservation stated in the files, with no grant written into the source. Not an open-source licence, and **not** covered by this template's Apache-2.0 `LICENSE` | Ask Infineon and Imagimob. TESAIoT grants nothing here and can grant nothing here. |

**TESAIoT's position.** We hold no rights in these three models and pass none on.
They came out of Infineon's tool and they carry Imagimob's copyright. Anything
generated by DEEPCRAFT™ Studio, or derived from a DEEPCRAFT™ model, is Imagimob's
and Infineon's. The Apache-2.0 grant in this template's `LICENSE` covers the code
this project wrote; **it does not reach inside these files**, and nothing here
may be read as TESAIoT licensing them to anyone.

Our use is **research and teaching**, and **not commercial**. Note what this
section does *not* say: no licence grant accompanies these files. The header
reserves all rights and states nothing further, so there are no terms here to use
them under, and this section **credits** Imagimob and Infineon rather than passing
anything on. If you need a grant — for a product, or for anything else — obtain
it from Infineon and Imagimob directly.

Go to the source rather than to our copy:

- **DEEPCRAFT™ Studio**, which generates this code and where you can train your
  own —
  https://www.infineon.com/design-resources/embedded-software/deepcraft-edge-ai-solutions/deepcraft-studio
- **DEEPCRAFT™ Edge AI solutions**, the product family —
  https://www.infineon.com/design-resources/embedded-software/deepcraft-edge-ai-solutions
- **Infineon's PSoC™ Edge DEEPCRAFT™ example**, where the Ready Models and their
  licence live —
  https://github.com/Infineon/mtb-example-psoc-edge-ml-deepcraft-deploy-ready-model

One practical rule follows from the copyright line: **never add an SPDX header or
a TESAIoT copyright notice to these files.** A tag written over Imagimob's notice
would be a false licence claim. `THIRD_PARTY_NOTICES.md` §2.2 and §4.3 carry the
full entry.


## Vendored components

Present in this package as source.

| Component | Path | Origin | Licence | Redistributable? |
|---|---|---|---|---|
| BSP `TARGET_KIT_PSE84_AI` | `bsps/TARGET_KIT_PSE84_AI/` | github.com/Infineon/TARGET_KIT_PSE84_AI v1.2.0.495 (`props.json:3`) | **Apache-2.0** (`LICENSE`; 67 of 87 source files carry `SPDX-License-Identifier: Apache-2.0`, e.g. `cybsp.c:13`) | **Yes, with attribution.** The `EULA` file beside it does not govern the source: its §3 carves out components under their own open-source licence, and every source file states Apache-2.0. |
| LVGL fork | `proj_cm55/modules/lvgl_display/core/` | lvgl v9.5.0, `src/core/` + `src/draw/vg_lite/` | **MIT** (`LICENCE.txt`, "Copyright (c) 2025 LVGL Kft") | **Yes, with attribution.** `LICENSE-LVGL.txt` in that directory carries the upstream notice verbatim. The four forked files differ from upstream only by `LV_ATTRIBUTE_FAST_MEM` removals and a three-line workaround for the `hign_r` typo in Infineon's VG-Lite header. |
| DEEPCRAFT™ Studio model code | `proj_cm55/modules/ai_models/model_{motion,audio,radar}.{c,h}` | DEEPCRAFT™ Studio / ImagiNet Compiler export — see §"DEEPCRAFT™ Studio generated model code" above | **© Imagimob AB, an Infineon Technologies company — `All Rights Reserved`.** Not Apache-2.0, and not TESAIoT's to license | Ask Infineon and Imagimob. Research and teaching use only here; not for commercial use. |
| PDM audio front end | `proj_cm55/modules/ai_models/audio_pdm.{c,h}` | **Ported from Infineon's ModusToolbox™ example mtb-example-psoc-edge-ml-deepcraft-deploy-audio**, its file proj_cm55/audio.c — stated by `audio_pdm.h:5-7`. The data path is faithful to that reference (`audio_pdm.c:10-14`), and the decimation it configures is the reference's because that is what the model was trained against (`audio_pdm.h:15-18`). https://github.com/Infineon/mtb-example-psoc-edge-ml-deepcraft-deploy-audio | **Not established.** The file carries no licence header of its own; Infineon's DEEPCRAFT™ code examples carry the Infineon EULA in their own repository | Establish it upstream first. **This file is not TESAIoT's own work** — the AI Kit BSP adaptation and the build guards are ours, the data path is Infineon's. |
| OPTIGA PAL | `bento_libs/claw/kit-pse84-ai/COMPONENT_OPTIGA_CYHAL/` | optiga-trust-m PAL, release not established | **MIT** for everything traced | **Yes, with attribution** — see the caveat below. |
| OPTIGA / TESAIoT glue | `bento_libs/claw/kit-pse84-ai/modules/tesaiot/` | mixed; see below | mixed | Yes |

### The OPTIGA files, per file

Nine of the twelve PAL files are MIT on evidence. Four are byte-equal (modulo an
`#include` path) to MIT code in the pinned `optiga-trust-m` asset:

| File | Upstream counterpart in `mtb_shared/optiga-trust-m/release-v5.3.0/` | Difference |
|---|---|---|
| `pal_os_datastore.c` | `extras/pal/esp32_freertos/pal_os_datastore.c` | include path only |
| `pal_os_lock.c` | `extras/pal/xmc4800_freertos/pal_os_lock.c` | include path only |
| `pal_os_memory.c` | `extras/pal/esp32_freertos/pal_os_memory.c` | include path + `stdio.h` |
| `pal_os_timer.c` | `extras/pal/xmc4800_freertos/pal_os_timer.c` | include order only |

> **Three of those four carry the Cypress "copy, modify, and compile" banner
> anyway.** The banner is demonstrably misapplied: the code is MIT, in the
> repository whose root LICENSE is MIT. The banners have deliberately **not**
> been edited here — correcting another party's copyright notice is a decision
> for the copyright holder's counterparty, not for a build script — but nothing
> in this tree should be treated as Cypress-restricted on the strength of those
> three banners.

The asset ships **no PSoC/CYHAL PAL** (`extras/pal/` covers esp32, xmc4800,
linux, windows, zephyr and a template — no psoc6, no PSE84). So the
platform-specific PAL files here cannot simply be replaced by a fetch. These
could not be traced to a repository and their origin is **not established**:
`pal_ifx_i2c_config.c`, `pal_os_event.c`, `pal_psoc_gpio_mapping.h`,
`pal_psoc_i2c_mapping.h`, and the PSoC-specific bodies of `pal.c`, `pal_gpio.c`,
`pal_i2c.c`, `pal_logger.c`.

Three files in `modules/tesaiot/` are **ours** despite carrying a Cypress banner,
and the banner is wrong on each:

- `optiga_psa_se.h` / `optiga_psa_se.c` — no counterpart exists anywhere in the
  optiga-trust-m asset (`optiga_psa_register` appears zero times in it).
  optiga-trust-m ships no PSA secure-element driver at all. The API shape comes
  from mbedTLS's public `psa/crypto_se_driver.h`.
- `optiga_trust_helpers.c` (4,720 lines) and `.h` (300 lines) — both carry an
  explicit "TESAIoT Platform Extensions" block naming the author. Of 57
  functions, 2 are verbatim from the MIT `examples/utilities/optiga_trust.c`
  (~144 lines, ~3%); the rest have no upstream analogue.

## Known problems — read before publishing

1. **`proj_cm55/modules/ai_models/{siren,cough,alarm}_lib_eval.a` may not be
   published — settled 2026-08-29, and this is now a blocker, not a question.**
   These are Infineon DEEPCRAFT™ Ready Models, copyright Imagimob AB, an
   Infineon Technologies company, under the **Imagimob AI Model Evaluation
   License Agreement** (`LICENSE_Imagimob.txt` in
   `Infineon/mtb-example-psoc-edge-ml-deepcraft-deploy-ready-model`). It grants
   60 days of internal evaluation and **expressly prohibits distribution**
   (§2.2(c)). They **were** linked by the default `EDGE_AI_MODEL=combo` preset
   and were present in every release zip built before this was settled. **They
   are excluded from this package now**, and the absence is verified against the
   finished zip on every cut rather than assumed from the exclude list. The
   preset still names all six models, but a model archive is put on `LDLIBS`
   only when it is present on disk, so in this package those three slots fall
   through to the weak stubs instead. Filling those
   slots in a product requires written permission from Imagimob/Infineon, the
   purchase of the non-evaluation models, or a model of your own from DEEPCRAFT™
   Studio. Credit, links, the full permission statement and the two production
   routes are in §"DEEPCRAFT™ Ready Models" above; the clause quotations and
   provenance are in `THIRD_PARTY_NOTICES.md` §2.4.

2. **`bento_libs/claw/kit-pse84-ai/libraries/ifx_face_id/{ifx_face_id.a,.h}`**
   (3.1 MB) carry a Cypress grant with no distribution right. Nothing in this
   template compiles or links them — `grep ifx_face_id` over every Makefile
   returns nothing — so they are dead weight as well as a restriction.
   **Excluded from the release packages since 2026-08-29**, at
   `package_rsync_filters()` in `bento-release.sh`, and verified absent from the
   zip after packing rather than merely excluded before it. They remain in the
   development tree, which is why the check matches on filename and runs on
   every cut. Note that only `proj_cm33_ns/Makefile:85` CY_IGNOREs this tree;
   on CM55 it is invisible for the unrelated reason that `ENABLE_OPTIGA ?= 0`
   (`proj_cm55/Makefile:109`) leaves `SEARCH+=$(BENTO_BOARD)` switched off, so
   an `ENABLE_OPTIGA=1` build in the development tree would auto-discover and
   link the archive with no warning.

2b. **`bento_libs/claw/kit-pse84-ai/application_code/`** (1.5 MB, 13 files —
   `one_detection.h`, `font_16x36.h`, `image_resize.c`, `lcd_draw.c` and the
   rest) is the same Infineon face-ID demo's other half, and this document
   missed it until 2026-08-29. It is CY_IGNOREd at `proj_cm33_ns/Makefile:84`
   and referenced by nothing. No file in it carries an SPDX grant; they carry
   the Infineon banner *"you may use this Software only as provided in the
   license agreement accompanying the software package from which you obtained
   this Software. If no license agreement applies, then any use, reproduction,
   modification, translation, or compilation of this Software is prohibited
   without the express written permission of Infineon."* No such agreement
   accompanies it here. **Excluded from the release packages, same mechanism.**
   Twelve other files in the template carry that same banner and are **not**
   excluded, because they are live build inputs: ten in
   `proj_cm55/modules/lvgl_display/core/` and two in `proj_cm55/`. That the
   LVGL fork files carry an Infineon prohibition banner while the "Vendored
   components" table above records them as MIT / LVGL Kft is an unresolved
   contradiction, and it is for counsel, not for a packaging script.

2c. **Lottie animation JSON — `Welcome.json`** is excluded from the release
   packages: `THIRD_PARTY_NOTICES.md` §3.1 establishes no licence for it and
   says plainly that it should not be published. Nothing includes it.
   `lottie_assets_tmp.h` (156 KB, included by no file) is excluded with it
   because it embeds the same unestablished bytes a second time.
   **`success_checkmark.json` is still published**, and honesty requires
   saying why rather than letting the exclusion imply otherwise: the raw
   `.json` is unreferenced, but its bytes are also in `lottie_assets.h`, which
   `page_animation.c` compiles and the shipped image uses. Excluding the file
   would remove the copy nobody reads and keep the copy that ships. Closing it
   needs the in-house regeneration §3.1 recommends. **This is an open blocker.**

3. **BSP prebuilt binaries with no notice**: `sec_api_link.o` (×4 toolchains)
   and `PSE84_SMIF.FLM` (×2). Whether the Apache-2.0 LICENSE or the EULA covers
   these is **not established**.

## Attribution obligations to satisfy when publishing

MIT and Apache-2.0 both require the notice to travel with the code.

- **MIT** (optiga-trust-m, LVGL, FreeRTOS): retain the copyright line and the
  permission notice. LVGL's is at
  `proj_cm55/modules/lvgl_display/LICENSE-LVGL.txt`.
- **Apache-2.0** (BSP, ifx-mbedtls, wifi-host-driver, ml-middleware and most
  fetched assets): ship the licence text and retain `NOTICE` content. The BSP's
  own copy is `bsps/TARGET_KIT_PSE84_AI/LICENSE`.
- Single-header libraries embed their notice in the file itself and are
  satisfied by shipping the file: `tsf.h` (MIT, Schelling + Folta), `jsmn.h`
  (MIT), `minimp3.h` (CC0-1.0, no obligation).

**Resolved 2026-08-29.** Every release package now carries, at its root:

| File | What it is |
|---|---|
| `LICENSE` | Apache-2.0, holder `Thai Embedded Systems Association (TESA)` |
| `NOTICE` | the Apache-2.0 §4(d) notice, and the list of components deliberately absent from the package |
| `THIRD_PARTY.md` | this file — provenance, and what each upstream LICENSE permits |
| `THIRD_PARTY_NOTICES.md` | the notice texts that must travel with a distribution |

Until that date the package contained only this file, so the two references to
`THIRD_PARTY_NOTICES.md` above pointed at nothing a recipient could open, and
the components documented only in that file — Twemoji's CC-BY 4.0 attribution,
Noto Sans Thai's OFL §2, the BSD binary-form clauses of littlefs, the BMI270
config blob and lwIP, and MicroPython, jQuery and CMSIS — shipped with no
notice travelling with them. `bento-release.sh` now stages all four and fails
the cut if any is missing, or if any path either document cites cannot be
resolved inside the package.

`LICENSE-BINARY.md` is deliberately **not** in the package. It carries a
"DRAFT — NOT YET IN FORCE" banner and states that it must not be published or
relied upon until approved. The prebuilt `lib/libbento_*.a` it would govern do
ship, so their terms are unsettled; `NOTICE` says so rather than implying a
grant. Approving that document, or writing another, is open work.
