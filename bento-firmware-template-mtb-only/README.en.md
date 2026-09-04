# TESAIoT firmware template

> ภาษาไทยอยู่ที่ [README.md](README.md) — Thai version: [README.md](README.md)
<!-- //! [doc-drift-fix] — see docs/template_local_deltas.list; a sync that reverts this file must be refused -->

A complete, working firmware for the **TESAIoT Dev Kit** (PSoC Edge
PSE846GPS2DBZC4A), meant to be copied and turned into your own product.

It boots to a touch Home screen with eighteen menus, runs MicroPython, reads
every sensor on the board, and talks to the TESAIoT cloud. All of that is here
as source you can read and change — except six areas that arrive as prebuilt
libraries. Which six, and why, is spelled out below.

```bash
./bento.sh            # start here
```

---

## 1. What you are looking at

PSoC Edge has three processors, and this firmware uses all of them. It matters
because it decides where your code goes.

| Core | Runs | Typical work |
|---|---|---|
| **CM33_S** | secure boot | you will not touch this |
| **CM33_NS** | MicroPython, WiFi, sensors, cloud | Python API, drivers, connectivity |
| **CM55** | LVGL display, all UI pages | screens, menus, graphics |

The two application cores talk over an IPC mailbox. A sensor is read on one
core and drawn on the other, so a new sensor page usually means a small change
on each side.

## 2. First run

```bash
./bento.sh doctor     # is everything present?

# Dependencies are fetched PER PROJECT. There is no getlibs at the top level,
# and each project declares its own deps/*.mtb — running it only in
# proj_cm33_ns fetches 33 of the 41 assets and the build then stops inside
# ninja on a missing optiga-trust-m file.
for p in proj_cm33_s proj_cm33_ns proj_cm55; do (cd $p && make getlibs); done

./bento.sh build      # ~10 minutes for a clean build of all three cores
./bento.sh flash      # program the board over KitProg
```

**Then power-cycle the board.** A debugger reset is not enough: the display
backlight needs a cold 0→1 edge and stays dark otherwise, which looks exactly
like a failed flash.

**`getlibs` is not optional here, and the order matters.** Some source this
firmware compiles is not in this package at all — it is fetched, then copied out
of the fetched asset by the build. `make getlibs` in `proj_cm55` must therefore
run *before* the first `build`, or the build stops and tells you so. See
§7.1 for what is fetched and why.

**Patched dependencies.** This firmware needs local changes to five assets under
`mtb_shared` — eleven files — that `getlibs` does not provide. One stops the
build, the rest fail silently, including the one that binds the OPTIGA key into
the TLS session. The build refuses to start without them and names what is
missing. The diffs travel with this package, in `third_party_patches/`.

`doctor` checks the toolchain and the two trees this template deliberately does
not carry (see §7). Fix whatever it reports before building; the errors you get
otherwise are long and unhelpful.

## 3. The CLI

`bento.sh` with no arguments opens a menu. Every action is also a subcommand:

| Command | What it does |
|---|---|
| `doctor` | toolchain, archives, and the trees you must supply |
| `menus` | all eighteen menus, whether each is on, and its size on disk |
| `enable <menu>` | turn a menu on |
| `disable <menu>` | turn a menu off — the code stays, it is simply not built |
| `remove <menu>` | delete a menu's sources, after telling you what else to edit |
| `build` / `flash` / `clean` | the build cycle |
| `verify` | check the prebuilt libraries against their signature |

`menus` asks `make` for each flag rather than reading the Makefile, because
several flags are assigned twice — once inside a board conditional and once in
its else branch — and reading the text gives the wrong answer.

## 4. Adding your own screen

Menus live one directory each under `proj_cm55/modules/page-components/`:

```
_core       animation   bento_buddy  bentoclaw   controls    edge_ai
environ     face_id     games        gpio_rgb    hsm         joystick
motion      motor_ctrl  smart_card   smart_watch spectrum_analyzer
tesaiot_connect         wifi_connect
```

Copy `edge_ai` — it is the one page that is wired in completely at every
touch point below, so it is the safe thing to imitate. (`environ` and `motion`
are smaller but are registered with no Home card, which is the broken state
described under this table; do not copy them.) Rename it, and wire it in at
these places:

| File | Add |
|---|---|
| `proj_cm55/modules/page-components/_core/page_manager.h` | your `PAGE_ID_…` in the enum — an explicit value at the end, next free number, never re-numbered (the values are ABI, see the comment at the top of the enum) |
| `proj_cm55/modules/page-components/_core/sensorhub_ui.c` | the `#include "page_<name>.h"` (with the others, ~lines 34-97) and a `pm_register(...)` call |
| `proj_cm55/modules/page-components/_core/page_home.c` | an `s_card_defs[]` entry |
| `proj_cm55/Makefile` — flag default | `ENABLE_PAGE_<NAME> ?= 1` with the others (lines 23-57) |
| `proj_cm55/Makefile` — auto-guard + `CY_IGNORE` | the `$(wildcard …)` line that forces the flag to 0 when the directory is gone (lines 66-81) and the `CY_IGNORE+=modules/page-components/<name>` block (lines 636-700) |
| `proj_cm55/Makefile` — `INCLUDES+=` and `DEFINES+=` | `INCLUDES+=modules/page-components/<name>` (lines 737-798) and `DEFINES+=ENABLE_PAGE_<NAME>=$(ENABLE_PAGE_<NAME>)` (from line 897) |

There is no `./bento.sh add` — the wiring is by hand, and `./bento.sh menus`
only reports flags for directories that already exist.

**All of them, or none.** A page registered with no card exists but cannot be
reached. A card with no registration is silently ignored when tapped —
`pm_navigate()` returns when the page has no `create_cb` — so the symptom is a
card that does nothing, not a crash. A missing `INCLUDES+=` fails the build
with `fatal error: page_<name>.h: No such file or directory`. This is the
single most common mistake when adding a screen.

`_core` is not a menu — it holds the Home screen and the page manager, and
nothing works without it.

### Removing a menu

`./bento.sh disable <menu>` is the safe move: the flag goes to 0, the code is
not compiled, and nothing else has to change. Use `remove` only when you want
the sources gone, and expect to edit the two files it names afterwards.

## 5. Writing Python instead

The board runs MicroPython on CM33_NS. Over the USB serial console:

```python
import sensors
sensors.scan()          # I2C addresses that answered
sensors.read_all()      # every sensor, as a dict

import ui
ui.Button(...)          # draw on the CM55 screen from Python, over IPC

import wifi, tesaiot
wifi.connect("ssid", "password")
tesaiot.config()        # cloud settings
```

To make a script run at boot, write it to the board's filesystem as `/main.py`
from the REPL, or use the TESAIoT tooling if you have it — this template does
not bundle an uploader.

## 6. What is prebuilt, and why

Six areas ship as static libraries in `lib/` rather than as source. Their
source is not in this tree at all — that is the point, and the fact that this
template builds without it is the proof.

| Area | Library | Core |
|---|---|---|
| Bento Buddy BLE agent | `libbento_secure.a` | CM33_NS, off by default |
| HSM screen, Edge AI page, display bring-up | `libbento_cm55.a` | CM55 |
| Edge AI inference and the parallel-feed engine | `libbento_edge_ai.a` | CM55 |
| Core IPC: service, LCD, UI, sensor hub | `libbento_ipc.a` | CM55 |
| TACP protocol, WiFi credential storage | `libbento_mpy.a` | CM33_NS |
| OPTIGA enrolment: CSR and Protected Update | `libbento_hsm.a` | CM33_NS |

Each sits in `lib/<area>/` with its own `include/`, an `api.txt` listing every
symbol it exports, a `consumer_must_provide.txt` listing what it expects from
you, and a `PROVENANCE.txt` recording which project it was built against.

```bash
./bento.sh verify     # ECDSA signature + SHA-256 of every shipped file
```

Read `lib/ipc_core/PROVENANCE.txt` before reusing that one in a different
project: it contains a page-order constant compiled in at build time, and a
project whose menus are in a different order will link cleanly and behave
wrongly.

**The Edge AI *engine* is ours; the Edge AI *models* are not.**
`libbento_edge_ai.a` is the registry, the sensor feed router and the run-time
loader, and that is TESAIoT's work. The models it runs are not.
`proj_cm55/modules/ai_models/model_motion.{c,h}`, `model_audio.{c,h}` and
`model_radar.{c,h}` are **DEEPCRAFT™ Studio** exports, generated by Infineon's
Edge AI tool and copyright **Imagimob AB, an Infineon Technologies company** —
the line in each file reads *"Copyright © 2023- Imagimob AB, All Rights
Reserved."* That is a reservation with no grant of any kind written into the
source, so this template credits them rather than licensing them on. Nobody here
trained them or owns them, the Apache-2.0 grant on this template's own code does
not reach inside them, and they are used here for research and teaching rather
than commercial deployment. If you intend to ship a
product containing them, or anything derived from them or from DEEPCRAFT™
Studio, settle it with Infineon and Imagimob first. Start at
https://www.infineon.com/design-resources/embedded-software/deepcraft-edge-ai-solutions/deepcraft-studio;
the full entry is `THIRD_PARTY.md` and `THIRD_PARTY_NOTICES.md` §2.2, §2.4 and
§4.3, and `proj_cm55/modules/ai_models/README.md` repeats it where the files are.

**What this does and does not give you.** The libraries hide implementation and
internal symbol names. They do not hide the protocols — UUIDs, JSON commands
and format strings are readable in any binary, and a disassembler reads
machine code regardless.

There is no technical control behind that. The OPTIGA-UID licence check
described in §8 is not compiled into any of the three cores: both
`proj_cm33_ns/Makefile:87` and `proj_cm55/Makefile:179-184` `CY_IGNORE` the
directory that holds `tesaiot_license.c`, no `tesaiot_license.o` exists in any
build tree, and `tesaiot_is_licensed` appears in none of the three Release ELFs
(`arm-none-eabi-nm`, checked 2026-08-29). What restricts use is the licence
agreement — a contractual boundary, not an enforced one. The obfuscation raises
the cost of copying; it does not stop it.

## 7. What you must supply

The template carries the application and the board support package. It does not
carry the platform, which is far too large to ship inside a template — the
MicroPython port alone is 154 MB.

```
<your workspace>/
  micropython-psoc-edge-psoc-edge-main/   154 MB   MicroPython port
  mtb_shared/                             1.9 GB   make getlibs
  bento-firmware-template/                         this directory
```

By default the template finds the workspace one level above itself. (Inside the
release repository it sits one directory deeper, and it detects that.) Point it
elsewhere with:

```bash
make build BENTO_WORKSPACE=/path/to/workspace
```

That variable is read by `common.mk`, `proj_cm33_ns/Makefile.micropython` and
`bento.sh`.

### 7.1 Source that is fetched rather than shipped

Some files this firmware compiles are deliberately **not** in this package. They
belong to third parties whose licence permits us to ship binaries but not
source, so the build fetches them from the vendor's own published asset and uses
them from there. Nothing is lost — the asset is the authoritative copy, and it
is one `getlibs` away.

| What | Upstream | Pinned by |
|---|---|---|
| emUSB-Host configuration — `usbh_config.c`, `usbh_config_io.c` | [github.com/Infineon/emusb-host](https://github.com/Infineon/emusb-host) `release-v2.2.0`, files at `export/Config/` | `proj_cm55/deps/emusb-host.mtb` |

emUSB-Host is SEGGER's USB stack. SEGGER licensed it to Cypress for object-code
redistribution only, so its source cannot travel in this package. Infineon ships
the two configuration templates inside the asset for integrators to copy out —
the asset's `.cyignore` excludes `export/Config` precisely so ModusToolbox will
not compile them where they sit — and `materialize_emusb_config.sh` does that
copy at the start of every `proj_cm55` build, then applies the three changes
that are ours (a lower ISR priority, and two counters the joystick driver reads
to tell "no device" from "device present but silent"). That script is the
readable record of exactly what we changed and why.

**Consequence for you:** run `make getlibs` in `proj_cm55` before the first
build. If you do not, the build stops with a message naming the missing asset
rather than a page of undefined references. Re-running it is harmless; the copy
is idempotent and happens on every build, so the fetched asset and your tree
cannot drift apart.

If you are re-packaging this template, note that these files exist on disk after
any build. They are excluded from the package by `bento-release.sh` and the
result is verified there — do not commit them and do not ship them.

You also need **ModusToolbox 3.6 specifically** and the ARM GCC that ships with
it. Newer is not better here: a later Configurator regenerates the BSP
configuration from `design.modus` and emits notices that `-Werror=cpp` turns
into errors in files you never touched. `BENTO_MTB_VERSION` overrides the pin
if you intend to re-validate.

## 8. Licensing your board

`bento_libs/claw/kit-pse84-ai/tesaiot/include/tesaiot_license_config.h` contains
placeholders. To get real values:

```python
import optiga
print(optiga.uid())     # 54 hex characters, unique to your board
```

Send that UID to TESAIoT and you receive a signature for it. Put both in the
header and rebuild.

**Be clear about what this does today.** Filling the header in changes nothing
about how the firmware runs, because the code that would read it is not built.
`tesaiot_license.c` sits in a directory both core Makefiles `CY_IGNORE`
(`proj_cm33_ns/Makefile:87`, `proj_cm55/Makefile:179-184`), the prebuilt
`tesaiot/lib/libtesaiot_license.a` is linked by nothing, and
`tesaiot_is_licensed` is absent from all three Release ELFs (checked with
`arm-none-eabi-nm`, 2026-08-29). `tesaiot.license_verify()` exists in
MicroPython but its handler is compiled out with `ENABLE_OPTIGA=0`
(`proj_cm55/Makefile:109`) and answers "not available".

Register the UID anyway: it is how your board is recorded as licensed, and it is
what the check will read once it is wired in. Just do not treat it as something
that currently stops unlicensed firmware from running.

## 9. When something goes wrong

| Symptom | Cause |
|---|---|
| Linker cannot find a function from `lib/` | `LDLIBS` was set after `include start.mk`. ModusToolbox reads it while including that file; anything later never reaches the linker. |
| Menu is missing from Home | its flag is 0, or `s_card_defs[]` has no entry. `./bento.sh menus` tells you which. |
| Tapping a card does nothing | the card exists but `pm_register` does not — `pm_navigate()` ignores a page with no `create_cb`. See §4. |
| `undefined reference` to something that exists in source | stale object files. `./bento.sh clean`, then build. |
| Screen black, sensors return `[]` after repeated flashing | the shared I2C bus latched up. Unplug USB completely, wait ten seconds, plug back in. A reset button is not enough. |
| MicroPython behaves oddly after removing library sources | the qstr pool moved. See `bento_archived_qstrs.c` — it exists to prevent exactly this. |

## 10. Layout

```
bento.sh                   the CLI
Makefile common.mk         build entry points
bsps/                      board support package
configs/                   signing and boot configuration
proj_cm33_s/               secure boot core
proj_cm33_ns/              MicroPython, WiFi, sensors, cloud
proj_cm55/
  modules/
    page-components/
      _core/               Home screen, page manager, page_id_t  (not optional)
      <menu>/              one directory per menu  <- your screens go here
    lvgl_display/          display driver and LVGL port
    ai_models/           DEEPCRAFT(TM) Studio models, (c) Imagimob AB — see
                         its README.md; not TESAIoT's work
    deepcraft_task/ ...
bento_libs/                shared BENTO libraries, as source
lib/                       the six prebuilt areas, with headers and signature
```

---

Built and verified on a TESAIoT Dev Kit: three cores, `app_combined.hex`
15,939,088 bytes, sensors answering at I2C `0x18`, `0x68`, `0x77`.
