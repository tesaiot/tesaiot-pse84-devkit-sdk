# TESAIoT Dev Kit — C-only firmware template (mtb-only)

> ภาษาไทย: [README.md](README.md)

The firmware that runs on the **TESAIoT Dev Kit** (Infineon PSoC™ Edge E84 AI
Kit on the TESAIoT QWA309 base board), as a ModusToolbox™ project you can read,
change and ship. Plain C on FreeRTOS across three cores. No MicroPython, no
virtual machine, no scripting layer: what you build is what runs.

---

## 1. What this package is

| | |
|---|---|
| Language | C, FreeRTOS, LVGL on CM55 |
| Toolchain | ModusToolbox 3.6 and the GNU Arm toolchain it installs |
| Cores | CM33 secure (boot only), CM33 non-secure (Wi-Fi, sensors, cloud, OPTIGA), CM55 (display, every UI page, Edge AI on the NPU, radar, USB host) |
| Source | every UI page, every sensor driver, the BSP, the IPC transport, Wi-Fi, MQTT/TLS, storage |
| Prebuilt | five archives in `lib/`, each with headers and `api.txt`; one signed manifest lists the SHA-256 of every file in `lib/` (§6) |

Two things this variant does **not** have, on purpose:

- **No MicroPython.** There is no REPL, no `/main.py` and no Python module.
  Your code is C: FreeRTOS tasks on CM33_NS and LVGL pages on CM55.
- **No IDE console.** The BENTO IDE's console talks to the board through the
  MicroPython link this variant does not contain. The IDE's Remote Flash can
  still program the published C-only hex; your own build goes on with
  `make program` over KitProg3 (§3).

**How to tell it is alive.** With no REPL, the proof of life is a heartbeat on
the KitProg3 USB serial port (115200 8N1): one line every 10 seconds giving
the seconds since boot and the number of FreeRTOS tasks, for example

```
[HB] t=30s tasks=27
```

Config and saved Wi-Fi networks persist through the C storage layer
(`bento_libs/claw/common/storage_c/`), in the same format the MicroPython
variant writes, so a board can move between the two without losing them.

## 2. Getting it: the release zip, or a clone plus `lib/`

**The release zip is the complete package.** Download
`bento-firmware-template-mtb-only.zip` from the `fw-c-only-v1.13.0` release of
[tesaiot/tesaiot-pse84-devkit-sdk](https://github.com/tesaiot/tesaiot-pse84-devkit-sdk/releases),
unzip it, and check the archives before anything else:

```bash
unzip bento-firmware-template-mtb-only.zip
cd bento-firmware-template-mtb-only
(cd lib && ./verify.sh)        # the manifest's ECDSA signature, then the SHA-256 of every file
```

**A git clone is not enough on its own.** The repository tracks the source
but not the five `.a` archives (git ignores `*.a`), so a clone's `lib/` has the
headers and `verify.sh` but none of the archives, and the link fails. If you work from a clone, take `lib/` from the
release zip **of the same version** and verify it there:

```bash
# from the unpacked zip, into your clone
rsync -a bento-firmware-template-mtb-only/lib/ <your-clone>/bento-firmware-template-mtb-only/lib/
(cd <your-clone>/bento-firmware-template-mtb-only/lib && ./verify.sh)
```

Do not mix a `lib/` from one version with the source of another.

## 3. Build and flash

You need ModusToolbox 3.6 (newer Configurators regenerate the BSP and break
the build), bash 4 or newer (on macOS: Homebrew's bash), and about 2 GB for
`mtb_shared`. The template expects this layout and finds the workspace one
level above itself:

```
<your workspace>/
  mtb_shared/                         created by make getlibs
  bento-firmware-template-mtb-only/   this directory
```

**1. Fetch the libraries, per project.** There is no top-level `getlibs`:

```bash
for p in proj_cm33_s proj_cm33_ns proj_cm55; do (cd $p && make getlibs); done
```

**2. Apply the patched dependencies.** Five assets need eleven local changes
that `getlibs` cannot provide; the diffs travel in `third_party_patches/`. The
build refuses to start without them and names what is missing:

```bash
cd ../mtb_shared
( for p in $(cat ../bento-firmware-template-mtb-only/third_party_patches/series); do
    patch -p1 -F0 --forward < "../bento-firmware-template-mtb-only/third_party_patches/$p" || exit 1
  done && shasum -a 256 -c ../bento-firmware-template-mtb-only/third_party_patches/PATCHED.sha256 )
cd ../bento-firmware-template-mtb-only
```

The parentheses keep a failure from closing your terminal. Run it once: on a
second run `patch` reports the patches as previously applied and stops, which
is expected; the `shasum` line on its own then confirms the tree.

**3. Build and program.**

```bash
make build -j          # three cores; "Build complete" for each
make program           # over KitProg3
```

Then **power-cycle the board** (unplug USB, plug it back). A debugger reset is
not enough: the display backlight needs a cold start and stays dark otherwise,
which looks like a failed flash and is not.

`./setup.sh --check` reports what is missing; `./setup.sh --build` runs
step 1 and step 3 for you (apply step 2 by hand first). `./bento.sh` lists
the menus, turns them on and off, and runs `verify`.

**Do not judge a running board through openocd.** Attaching a debugger to a
running board parks it. Watch the heartbeat instead.

## 4. What is on the screen

The board boots to a touch Home screen. Each card opens one page; the source
of every page is under `proj_cm55/modules/page-components/`. The cards, in
order:

1. Sensor Dashboard
2. GPIO & RGB Matrix
3. Edge AI
4. HSM Security
5. Smart Watch
6. Animation
7. BENTO Playground
8. Joystick
9. BENTO Claw
10. Wi-Fi Setting

The pictures below were taken from this release running on a TESAIoT Dev Kit
(800x480). Thin horizontal lines in some of them come from the screen capture,
which read the display memory while a frame was being drawn; the panel itself
shows none. Two values are hidden with a grey block: the secure element's
unique ID and the list of nearby Wi-Fi networks.

**Home.** The card row scrolls sideways; the live sensor strip shows the IMU,
compass heading, temperature, humidity, touch and pot readings.

![Home](assets/readme/01_home.png)
![Home, cards scrolled](assets/readme/14_home_cards_mid.png)
![Home, last cards](assets/readme/15_home_cards_end.png)

**Sensor Dashboard.** IMU charts, magnetometer heading, pressure,
temperature and humidity, CapSense, radar presence and the USB joystick.

![Sensor Dashboard](assets/readme/02_sensor_dashboard.png)

**GPIO & RGB Matrix.** The QWA309 base board: each knob in mV and raw with its
pin, the push-buttons by pin, the CapSense link state, SW1-SW4, and the 16x8
RGB matrix. Scroll down for the notes on what the E84 can and cannot read.

![GPIO & RGB Matrix](assets/readme/03_gpio_rgb.png)
![GPIO & RGB Matrix, scrolled to the notes](assets/readme/03b_gpio_rgb_scrolled.png)

**Edge AI.** Pick a model from the list, load it, and watch the class scores.
The template includes the Siren, Cough and Factory Alarm ready models
(Infineon DEEPCRAFT™ Ready Models by Imagimob AB, evaluation builds) for
non-commercial education and evaluation use, beside the motion, audio and
radar models (`proj_cm55/modules/ai_models/README.md`). The picture shows the
Siren entry selected with the factory-alarm model's description and classes,
a display mix-up to be fixed in the next release.

![Edge AI](assets/readme/04_edge_ai.png)

**HSM Security.** The OPTIGA™ Trust M secure element: chip identity,
certificate and key slots, counters and health. The unique ID is hidden here.

![HSM Security](assets/readme/05_hsm_security.png)

**Smart Watch.** A round watch-face demo with fixed sample values.

![Smart Watch](assets/readme/06_smart_watch.png)

**Animation.** Lottie animations played with ThorVG.

![Animation](assets/readme/07_animation.png)

**BENTO Playground.** An empty screen for your own code to draw on.

![BENTO Playground](assets/readme/08_playground.png)

**Joystick.** A USB game controller on the host port; here waiting for one.

![Joystick](assets/readme/09_joystick.png)

**BENTO Claw.** The agent status screen. In this variant nothing sends it
data, so it shows its idle state.

![BENTO Claw](assets/readme/10_bento_claw.png)

**Wi-Fi Setting.** Scan, join and saved networks. The scanned list is hidden
here.

![Wi-Fi Setting](assets/readme/11_wifi_setting.png)

## 5. The QWA309 base board

Every switch, knob and header on the base board, which pin it reaches, and what
firmware can and cannot read is in the documentation chapter
**J7 — The QWA309 base board: hardware reference**: in this package at
`docs/html/group__j7__qwa309__baseboard.html` (Thai:
`docs/html/th/group__j7__qwa309__baseboard.html`), and on the
[documentation site](https://tesaiot.github.io/tesaiot-pse84-devkit-sdk/).

Read it before you wire anything. Three points from it:

- **Do not press the push-button on P17.5 before the GPIO & RGB Matrix page
  has been opened.** At boot the BSP drives P17.5 high as an output (it is also
  the camera reset line and the USB-host VBUS enable), and the button connects
  it straight to ground. The page reconfigures the pin as an input with a
  pull-up when it first opens.
- **SW1 to SW4 are not wired to the E84.** The CapSense controller on the base
  board reads them and reports them over I2C at address 0x08, and only its
  firmware protocol 0x0D or 0x0E reports them. The link needs SW12 ON and a
  restart. The decoding was tested on the host
  only, because no board with that firmware was available. The GPIO & RGB
  Matrix page says which firmware it found (its on-screen text says "0x0D or
  newer"; it accepts only 0x0D and 0x0E).
- **Printed labels and schematic designators differ** for every user switch.
  The chapter gives both.

### QWA309 base board pinout

Every header, switch and knob of the V3.1 base board in one picture: the
Arduino and mikroBUS pin maps, the user inputs, the power and function
switches, the shared I2C bus and the CapSense controller. The J7 chapter has
the same diagram with a Thai legend, and an SVG version for zooming.

![QWA309 base board pinout, V3.1](assets/readme/qwa309_pinout_v3_1.png)

## 6. What is prebuilt

These areas ship as static libraries in `lib/` rather than as source:

| Area | Library | Core |
|---|---|---|
| Edge AI inference: model registry, model sets, run-time model loading | `libbento_edge_ai.a` | CM55 |
| HSM screen, Edge AI page, display bring-up | `libbento_cm55.a` | CM55 |
| Core IPC: service, LCD, UI, sensor hub | `libbento_ipc.a` | CM55 |
| OPTIGA enrolment: CSR and Protected Update | `libbento_hsm.a` | CM33_NS |

Each sits in `lib/<area>/` with its own `include/`, an `api.txt` listing every
symbol it exports, a `consumer_must_provide.txt` listing what it expects from
you, and a `PROVENANCE.txt`. The API reference for each is under `docs/sdk/`
and in `docs/html/`.

## 7. Where your code goes

| You want to | Start in |
|---|---|
| add a background task, a driver, a cloud message | `proj_cm33_ns/main.c` and `bento_libs/claw/common/` |
| add or change a screen | `proj_cm55/modules/page-components/<page>/`, registered in `_core/sensorhub_ui.c` and carded in `_core/page_home.c` (both, always) |
| read the base-board knobs, buttons and CapSense | `proj_cm55/modules/cm55_sensor_poll/` and the J7 chapter |
| store a setting | `bento_libs/claw/common/storage_c/bento_storage.h` |

The documentation in `docs/html/` (English) and `docs/html/th/` (Thai) walks
through each of these with code taken from this tree.

## 8. Licence

- The TESAIoT source in this package is licensed under the **Apache License
  2.0**: `LICENSE` (Thai translation: `LICENSE-TH.md`), with `NOTICE`.
- The five archives in `lib/` are **not** covered by that grant. `NOTICE` says
  what applies to them.
- Third-party code keeps its own licence: the Infineon BSP and libraries,
  LVGL, littlefs, fonts and the rest are listed in `THIRD_PARTY.md`, with the
  notice texts in `THIRD_PARTY_NOTICES.md`.
- The DEEPCRAFT™ Studio models in `proj_cm55/modules/ai_models/` are copyright
  Imagimob AB, an Infineon Technologies company, all rights reserved. They are
  credited here, not licensed on; settle any product use with Infineon first
  (`proj_cm55/modules/ai_models/README.md`).

## 9. New in 1.12.0

- **GPIO & RGB Matrix page:** the pin beside every input, the knobs in mV
  (0 to 1800) as well as raw, the CapSense link state, and SW1-SW4 when the
  CapSense controller runs firmware protocol 0x0D or 0x0E (decoding tested on
  the host only; needs SW12 ON and a restart).
- **Sensor Dashboard:** the CapSense line is redrawn only when it changes,
  which removes a visible flicker.
- **Start-up:** BMI270, DPS368 and SHT40 initialisation is tried up to four times
  (four attempts in total) before a sensor row is given up for the boot.
- **Documentation:** a new chapter, J7, on the QWA309 base board; this README,
  with pictures of every page and the base-board pinout diagram; and the
  clone-versus-zip instructions above.
- The five archives in `lib/` are unchanged from 1.11.0.
