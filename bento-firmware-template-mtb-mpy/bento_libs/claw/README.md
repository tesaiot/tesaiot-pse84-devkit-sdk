# BENTO-TESAIoT-Libraries

> **STALE — read `../README.md` instead.**
>
> This file still describes the pre-merge world of March 2026: it names
> `KIT_PSE84_AI-MicroPython-AI-Core`, `KIT_PSE84_AI-MicroPython-AI-Game` and
> `KIT_PSE84_EVAL_EPC2-MicroPython-AI-Core`, none of which exist any more, and
> it draws the repository-root directory tree rather than this family's. It
> mentions neither the `claw/`/`game/` split, nor `lib.mk`, nor any module added
> since — `arduino_shield`, `shield_motor_v2`, `ble_nus`, `thai_text`,
> `bento_link`, `deepcraft`.
>
> It is the natural entry point once `BENTO_LIBS_DIR` points here, which is
> exactly why the warning is at the top rather than in a changelog. Kept for its
> module-reference and know-how sections; do not trust its paths or project
> names. Flagged 2026-08-03.


> **BENTO : : Make Anything.**

Centralized shared firmware library for PSoC Edge E84 multi-project workspace by BENTO & TESAIoT.
Single source of truth — edit once, build across all projects with zero manual sync.

## Projects Consuming This Library

| Project | Target Board | Role |
|---------|-------------|------|
| `KIT_PSE84_AI-MicroPython-AI-Core` | AI Kit (KIT_PSE84_AI) | Primary — sensors, AI, MicroPython |
| `KIT_PSE84_AI-MicroPython-AI-Game` | AI Kit (KIT_PSE84_AI) | Game Console variant |
| `KIT_PSE84_EVAL_EPC2-MicroPython-AI-Core` | Eva Kit (APP_KIT_PSE84_EVAL_EPC2) | Touch display, CAPSENSE, audio |

All three projects share the same `mtb_shared/` at workspace root for Infineon BSP/middleware.

## Directory Structure

```
BENTO-TESAIoT-libraries/
├── common/                          # Board-agnostic (ALL 3 projects)
│   ├── shared/                      # IPC communication framework
│   │   ├── include/                 #   ipc_communication.h, ipc_ui_protocol.h, bsp_feature_flags.h
│   │   └── source/
│   │       ├── COMPONENT_CM33/      #   cm33_ipc_communication.c
│   │       └── COMPONENT_CM55/      #   cm55_ipc_communication.c
│   ├── modules/                     # CM55 IPC + connectivity modules
│   │   ├── ipc_lcd/                 #   LCD control + emoji assets
│   │   ├── ipc_sensorhub/           #   Sensor data routing (CM33 <-> CM55)
│   │   ├── ipc_service/             #   Service discovery + sensor snapshot IPC
│   │   ├── ipc_ui/                  #   Widget management (ipc_ui + ui_widget_mgr + icons)
│   │   ├── usb_hid_joystick/        #   F310 joystick via USB Host
│   │   ├── wifi_manager/            #   WiFi SoftAP/STA management
│   │   ├── tesaiot_config/          #   Runtime config store (.tesaiot_config)
│   │   ├── tesaiot_https/           #   HTTPS client for TESAIoT API
│   │   └── tesaiot_mqtt/            #   MQTT + mTLS + publisher/subscriber
│   └── mpy/                        # MicroPython C modules (36 files)
│       ├── mod*.c                   #   12 extension modules (ui, gpio, sensors, wifi, dsp, etc.)
│       ├── sensor_*.c/h             #   7 sensor drivers (BMI270, DPS368, SHT40, BMM350, etc.)
│       ├── modsensors_*.c           #   5 per-sensor MicroPython bindings
│       └── sensor_auto_task.c/h     #   Background sensor polling
│
├── kit-pse84-ai/                    # AI Kit specific (AI-Core + AI-Game only)
│   ├── COMPONENT_OPTIGA_CYHAL/      #   12 files — OPTIGA Trust M PAL
│   ├── modules/tesaiot/             #   4 files — OPTIGA manager + OID config
│   ├── tesaiot/                     #   21 files — TESAIoT licensing & security (v3.0.0)
│   │   ├── include/                 #     Public API headers
│   │   ├── src/                     #     License verification source
│   │   └── lib/                     #     Prebuilt libtesaiot.a
│   └── libraries/
│       ├── tesaiot-radar/           #   3 files — IFX Radar sensor driver
│       ├── camera-dvp-ov7675/       #   4 files — Camera DVP driver (CY_IGNORE'd)
│       ├── tesaiot-camera-hal/      #   4 files — Camera HAL abstraction (CY_IGNORE'd)
│       └── ifx_face_id/            #   2 files — Face ID ML library (CY_IGNORE'd)
│
├── kit-pse84-eval-epc2/             # Eva Kit specific
│   └── mpy/                        #   2 files — eva_controls_auto.c/h
│
├── scripts/                         # Validation & build scripts
│
└── docs/                            # Planning & migration documentation (11 files)
    ├── 00_EXECUTIVE_SUMMARY.md
    ├── 01_CROSS_PROJECT_FILE_INVENTORY.md
    ├── 02_CATEGORIZATION_MATRIX.md
    ├── 03_PROPOSED_DIRECTORY_STRUCTURE.md
    ├── 04_MAKEFILE_INTEGRATION_PLAN.md
    ├── 05_MIGRATION_PHASES.md
    ├── 06_RISK_ANALYSIS.md
    ├── 07_STATIC_LIBRARY_FEASIBILITY.md
    ├── 08_FINAL_ASSESSMENT.md
    ├── PHASE0_BASELINE_REPORT.md
    └── PROGRESS_REPORT.md
```

## How Projects Consume BENTO

### Step 1: Define BENTO Variables (common.mk)

Each project's `common.mk` defines the path variables:

```makefile
BENTO_LIBS_DIR = ../../BENTO-TESAIoT-libraries
BENTO_COMMON   = $(BENTO_LIBS_DIR)/common
BENTO_BOARD    = $(BENTO_LIBS_DIR)/kit-pse84-ai      # AI Kit projects
# or
BENTO_BOARD    = $(BENTO_LIBS_DIR)/kit-pse84-eval-epc2  # Eva Kit project
```

> **Note:** Eva Kit only uses `BENTO_COMMON` modules — `BENTO_BOARD` is intentionally
> undefined for Eva Kit's CM55 Makefile (it has different hardware: no Radar, no OPTIGA).

### Step 2: Add SEARCH + INCLUDES (proj_cm55/Makefile)

ModusToolbox auto-discovers all `.c` files in SEARCH paths:

```makefile
# IPC modules (all projects)
SEARCH+=$(BENTO_COMMON)/modules/ipc_lcd
SEARCH+=$(BENTO_COMMON)/modules/ipc_sensorhub
SEARCH+=$(BENTO_COMMON)/modules/ipc_service
SEARCH+=$(BENTO_COMMON)/modules/ipc_ui
SEARCH+=$(BENTO_COMMON)/modules/usb_hid_joystick
SEARCH+=$(BENTO_COMMON)/modules/wifi_manager

# Headers
INCLUDES+=$(BENTO_COMMON)/modules/ipc_sensorhub
INCLUDES+=$(BENTO_COMMON)/modules/ipc_ui
INCLUDES+=$(BENTO_COMMON)/modules/usb_hid_joystick

# Board-specific (AI Kit only, conditional)
ifeq ($(ENABLE_OPTIGA), 1)
SEARCH+=$(BENTO_BOARD)
SEARCH+=$(BENTO_BOARD)/modules/tesaiot
INCLUDES+=$(BENTO_BOARD)/tesaiot/include
endif

ifeq ($(BSP_HAS_RADAR),1)
SEARCH+=$(BENTO_BOARD)/libraries/tesaiot-radar
INCLUDES+=$(BENTO_BOARD)/libraries/tesaiot-radar
endif
```

### Step 3: Add MicroPython Sources (proj_cm33_ns/Makefile)

```makefile
INCLUDES+=$(BENTO_COMMON)/mpy
```

MicroPython `.c` files are compiled into `libmicropython.a` via `Makefile.micropython`.

### Step 4: CY_IGNORE Stale Local Copies

Any local module directories that were migrated to BENTO must be excluded:

```makefile
CY_IGNORE+=modules/ipc_lcd modules/ipc_sensorhub modules/ipc_service
CY_IGNORE+=modules/ipc_ui modules/wifi_manager modules/usb_hid_joystick
```

## Module Reference

### common/shared/ — IPC Communication Framework (5 files)

Cross-core communication between CM33 and CM55 via IPC Pipe.

| File | Purpose |
|------|---------|
| `ipc_communication.h` | IPC command definitions, pipe setup API |
| `ipc_ui_protocol.h` | UI widget protocol (create, update, event, list) |
| `bsp_feature_flags.h` | Board feature detection macros (`BSP_FEATURE_*`) |
| `cm33_ipc_communication.c` | CM33-side IPC pipe init + message dispatch |
| `cm55_ipc_communication.c` | CM55-side IPC pipe init + message dispatch |

### common/modules/ — IPC + Connectivity Modules (9 modules)

| Module | Files | Purpose |
|--------|-------|---------|
| `ipc_lcd` | 2 + 36 emoji | LCD panel control, brightness, emoji rendering |
| `ipc_sensorhub` | 2 | Route sensor data from CM33 to CM55 display |
| `ipc_service` | 3 | Service discovery, sensor snapshot IPC, WiFi status |
| `ipc_ui` | 5 | MicroPython widget creation/update over IPC (32 widget limit) |
| `usb_hid_joystick` | 2 | F310 DirectInput joystick via SEGGER emUSB-Host |
| `wifi_manager` | 2 | WiFi SoftAP/STA management for CYW55513 |
| `tesaiot_config` | 5 | Runtime config store, IPC handler for CM55 UI sync |
| `tesaiot_https` | 3 | HTTPS client for TESAIoT cloud (TLS 1.2, server auth) |
| `tesaiot_mqtt` | 11 | MQTT lifecycle, mTLS, publisher/subscriber, PSA stubs |

### common/mpy/ — MicroPython C Modules (36 files)

Extension modules compiled into `libmicropython.a` on CM33_NS:

| Module | File | API |
|--------|------|-----|
| `ui` | modui.c | `ui.Button()`, `ui.Label()`, `ui.Chart()`, etc. (14 widget types) |
| `gpio` | modgpio.c | `gpio.led()`, `gpio.read()`, `gpio.pwm()` |
| `sensors` | modsensors.c + 5 submodules | `sensors.init()`, `sensors.read()` (BMI270, DPS368, SHT40, BMM350, CAPSENSE) |
| `wifi` | modwifi.c | `wifi.scan()`, `wifi.connect()`, `wifi.ap()` |
| `dsp` | moddsp.c + 2 submodules | `dsp.fft()`, `dsp.filter()`, `dsp.imu_fusion()` |
| `lcd` | modlcd.c | `lcd.brightness()`, `lcd.emoji()` |
| `joystick` | modjoystick.c | `joystick.read()` (F310 via IPC) |
| `mqtt` | modmqtt.c | `mqtt.connect()`, `mqtt.publish()` |
| `optiga` | modoptiga.c | `optiga.init()`, `optiga.sign()`, `optiga.aes_encrypt()`, etc. (20 APIs) |
| `tesaiot` | modtesaiot.c | `tesaiot.license()`, `tesaiot.device_id()`, `tesaiot.connect()` |
| `bentoclaw` | modbentoclaw.c | 31 EXEC tools, TACP v2 protocol, JSON arg parser (v1.1.0) |

**Board variant handling:** Uses `#if BSP_FEATURE_*` and `#ifdef USE_KIT_PSE84_EVAL_EPC2`
guards for hardware differences (e.g., GPIO pin mapping, sensor availability).

### kit-pse84-ai/ — AI Kit Specific (63 files)

Only consumed by AI-Core and AI-Game projects.

| Component | Files | Purpose |
|-----------|-------|---------|
| `COMPONENT_OPTIGA_CYHAL` | 12 | Hardware abstraction layer for OPTIGA Trust M |
| `modules/tesaiot` | 4 | OPTIGA manager, OID configuration |
| `tesaiot/` | 21 | TESAIoT licensing, crypto, mTLS (v3.0.0) |
| `libraries/tesaiot-radar` | 3 | IFX Radar sensor driver (BGT60LTR11) |
| `libraries/camera-*` | 8 | Camera DVP + HAL (CY_IGNORE'd by default) |
| `libraries/ifx_face_id` | 2 | Face ID ML library (CY_IGNORE'd by default) |

### common/mpy/modoptiga.c — OPTIGA Trust M Security Module

Hardware-backed security via OPTIGA Trust M V3 chip (I2C address 0x30).
Runs on CM33_NS with IPC-based touch pause/resume for SCB0 bus sharing.

| Category | API | Description |
|----------|-----|-------------|
| Lifecycle | `init()`, `deinit()`, `is_ready()` | Open/close OPTIGA application |
| Identity | `uid()` | 27-byte factory UID (hex string) |
| Data | `read_data(oid)`, `write_data(oid, data)` | Read/write OID data objects |
| Metadata | `read_metadata(oid)`, `write_metadata(oid, tlv)` | Read/write OID metadata (TLV format) |
| Random | `random(length)` | Hardware TRNG (8-256 bytes) |
| Hash | `sha256(data)` | Hardware SHA-256 |
| Signature | `sign(digest, key_oid)` | ECDSA P-256 sign (32-byte digest) |
| Key Mgmt | `gen_keypair(key_oid)` | Generate ECC P-256 keypair |
| Key Agreement | `ecdh(peer_pubkey, key_oid)` | ECDH shared secret (accepts raw 65-byte or DER 68-byte) |
| Symmetric | `aes_generate_key(bits)` | Generate AES-128/192/256 key in OID 0xE200 |
| Encryption | `aes_encrypt(data)`, `aes_decrypt(ct, iv)` | AES-CBC with hardware key |
| MAC | `hmac(data, secret_oid)` | HMAC-SHA256 with stored secret |
| KDF | `hkdf(secret_oid, salt, info, length)` | HKDF-SHA256 key derivation |
| Counter | `counter_read(id)`, `counter_increment(id, n)` | Monotonic counters (0-3) |
| Config | `setup()`, `require_setup()`, `is_configured()` | Safe OID metadata configuration |

**Configuration**: `setup()` configures OIDs needed for advanced crypto (AES key 0xE200, HMAC/HKDF secret 0xF1D5 with PRESSEC type, counters 0xE120-E123). Only modifies OIDs still in Creation state (LCS=0x01). `require_setup()` is idempotent — checks first, configures only if needed. Counter OIDs are transitioned to Operational (irreversible) to enable increment.

**Security**: Factory key (0xE0F0) blocked in sign/gen_keypair/ecdh. Trust anchor (0xE0E8) blocked in writes.

**I2C Bus Sharing**: SCB0 shared with CM55 touch (FT5406). IPC commands `TOUCH_PAUSE (0xD6)` / `TOUCH_RESUME (0xD7)` coordinate access. PAL uses 100kHz (BSP divider=31).

### kit-pse84-eval-epc2/ — Eva Kit Specific (2 files)

| File | Purpose |
|------|---------|
| `eva_controls_auto.c/h` | CAPSENSE slider/button to GPIO mapping |

## What Stays LOCAL (Not in BENTO)

These components are intentionally kept in each project's local source tree:

| Component | Reason |
|-----------|--------|
| `sensorhub_ui/` (page_manager, Smart Watch) | PAGE_ID enums differ per project (11 vs 8 pages) |
| `lvgl_display/` | `lv_conf.h` differs (draw thread size, Lottie, ThorVG) |
| `cm55_sensor_poll/` (Eva Kit only) | Eva Kit-specific I2C sensor polling on CM55 |

## Validation

Use the workspace's `clean_build.sh` — `./clean_build.sh all` builds and
symbol-verifies all nine consumers.

The four scripts that used to sit in `claw/scripts/` were deleted on 2026-08-03:
all three copies were byte-identical, every one targeted project directories
that no longer exist, and `validate_bento.sh` checked the frozen pre-merge
snapshot at the repository root rather than this family tree — so it would
have reported PASS forever without validating anything a project builds.


## Build Requirements

- **ModusToolbox 3.6** with GCC ARM 14.2.1
- **PATH setup:**
  ```bash
  export PATH="/usr/bin:/bin:/usr/sbin:/sbin:\
  /Applications/mtb-gcc-arm-eabi/14.2.1/gcc/bin:\
  /Applications/ModusToolbox/tools_3.6/modus-shell/bin:$PATH"
  ```
- **Build command:** `make build -j` from each project root
- **Flash command:** `make program` (one board at a time via KitProg3)

## Architecture Decisions

1. **No symlinks** — incompatible with Dropbox sync; SEARCH paths used instead
2. **No git submodules** — overhead not justified for single-developer workflow
3. **SEARCH + CY_IGNORE pattern** — MTB-native, zero build system hacking
4. **`#ifdef` for board variants** — preferred over separate files (fewer to maintain)
5. **LOCAL for sensorhub_ui** — PAGE_ID enums are fundamentally different per project
6. **LOCAL for lvgl_display** — lv_conf.h diverges on 3 settings, splitting creates include complexity

## File Count Summary

| Location | Files | Size |
|----------|-------|------|
| common/mpy/ | 37 | MicroPython extension modules (incl. modbentoclaw, modtesaiot) |
| common/modules/ | 71 | 9 modules (IPC + TESAIoT config/HTTPS/MQTT) |
| common/shared/ | 8 | IPC framework + bentoclaw_version + TESAIoT defs |
| kit-pse84-ai/ | 64 | Security, radar, camera, OPTIGA, cryptography |
| kit-pse84-eval-epc2/ | 3 | Eva Kit controls + cryptography |
| docs/ | 11 | Planning & migration docs |
| scripts/ | 4 | Validation scripts |
| **Total** | **198** | |

---

## Changelog

### 2026.03-rc.2 (2026-03-29)

- **BentoClaw v1.1.0**: 31/31 EXEC tools implemented — `modbentoclaw.c` (+629 lines)
  - JSON arg parser (`claw_extract_arg()`), IPC sensor snapshot helper (`claw_fetch_sensor_snapshot()`)
  - New tools: `joystick_read`, `trustm_sign`, `rule_set`, `rule_clear`
  - Eva Kit `capsense_read`/`potentiometer_read` via IPC (CM55 owns SCB0)
- **Shared Version Header**: `bentoclaw_version.h` — single version define for CM33 + CM55
- **IPC Service**: Added `IPC_CMD_SENSOR_SNAPSHOT` (0x9B) handler + ISR filter, guarded `cm55_sensor_poll.h` for AI Kit
- **TESAIoT Connectivity Modules** (new):
  - `tesaiot_config/`: Runtime config store (`.tesaiot_config` on QSPI flash)
  - `tesaiot_https/`: HTTPS client for TESAIoT cloud API
  - `tesaiot_mqtt/`: MQTT client + mTLS + publisher/subscriber tasks
- **Cryptography Examples**: Board-specific examples for AI Kit and Eva Kit
- **New Shared Headers**: `ipc_tesaiot_defs.h`, `debug_log_disable.h`
- **MicroPython Modules**: `modtesaiot.c` (connectivity API), `sensor_auto_task` BMI270 cache

### 2026.03-rc.3 (2026-03-29)

- **LFS2 Cache Fix**: readsize 4096→512 reduces per-file cache 16KB→2KB — fixes MemoryError on `open()` with fragmented GC heap
- **AI Kit GC Heap**: 48KB→64KB — required for boot-time WiFi credential read via LFS2
- **WiFi Auto-Connect**: Boot scan timeout 3s→8s for 5GHz AP discovery (iPhone hotspot)
- **WiFi Credential Persistence**: Flush to QSPI from TACP dispatch — saves even when REPL idle
- **GC Collect**: After every BentoClaw EXEC + ASK dispatch — reclaims tool execution garbage
- **Boot GC Management**: gc_collect() before config_init AND WiFi cred read — clean heap for file I/O

### 2026.03-rc.1 (2026-03-10)

- **Playground Loading Optimization (Phase 1)**: Dynamic IPC timer (5ms fast mode) + hidden container in `ipc_ui.c`
- **Boot Debug Suppression**: `[MPY]`, `[LFS_CREDS]`, `[WiFi-Boot]` messages guarded by `#ifdef BOOT_VERBOSE`
- **VFS Mount Print Removed**: Silent QSPI filesystem mount (no more "VFS: LFS2 mounted" on REPL)
- **ui_widget_mgr_get_parent()**: New accessor for hidden container pattern
- **ipc_communication.h**: Fixed duplicate HSM macros + WiFi/Time command conflict
- **Branding**: Updated to "BENTO & TESAIoT"

### v0.9.0-wifi-qspi (2026-03-09)

- WiFi credential QSPI save (LittleFS), 2-phase atomic write
- Boot auto-connect from saved credentials
- OPTIGA Trust M credential storage (CRED_READ/WRITE/ERASE)

---

## License

(c) 2025-2026 BENTO & TESAIoT Foundation Platform

## Engineering know-how

- [GFXSS display know-how](docs/GFXSS_DISPLAY_KNOWHOW.md) — E84 graphics subsystem: the bottom band (SOCMEM contention), cold-boot dark backlight, DC interrupt registers, and the dead ends that are not worth re-walking
