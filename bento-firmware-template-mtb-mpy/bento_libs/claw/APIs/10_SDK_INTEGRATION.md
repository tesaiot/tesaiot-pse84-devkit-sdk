# SDK Integration Guide

**BENTO-TESAIoT Static Library SDK — Developer Integration Reference**

---

## 1. SDK Architecture (Approach C)

The BENTO-TESAIoT SDK uses a **hybrid distribution model**:

```
Pre-compiled Static Libraries (.a) — ~90% of codebase hidden:
  libmicropython.a          994 KB   MicroPython core runtime
  libtesaiot_sensors.a      ~50 KB   Sensor drivers (I2C, BMI270, DPS368, etc.)
  libtesaiot_ipc_cm33.a     ~10 KB   IPC framework (CM33_NS)
  libtesaiot_wifi_creds.a   ~15 KB   WiFi credential storage
  libtesaiot_security.a    ~200 KB   OPTIGA Trust M + TESAIoT crypto
  libtesaiot_license.a       ~5 KB   Device licensing

Source Distribution (~10%) — must compile with project:
  mod*.c + mpy_main.c + tacp.c     MicroPython binding modules (~25 files)
  qstrdefs.generated.h              QSTR IDs (must match libmicropython.a)
```

### Why Source for mod*.c?

MicroPython uses a **QSTR (Qualified String) pipeline** — a 2-pass compile process:

1. Scan `mod*.c` for `MP_QSTR_xxx` macros
2. Generate `qstrdefs.generated.h` with unique string IDs
3. Recompile with generated header

Pre-compiling `mod*.c` into a `.a` would break this pipeline. The `mod*.c` files are thin wrappers (~200 lines each) that call into the `.a` libraries.

---

## 2. SDK Directory Structure

```
BENTO-TESAIoT-SDK/
├── lib/
│   ├── KIT_PSE84_AI/Release/
│   │   ├── libmicropython.a
│   │   ├── libtesaiot_sensors.a
│   │   ├── libtesaiot_ipc_cm33.a
│   │   ├── libtesaiot_wifi_creds.a
│   │   ├── libtesaiot_security.a
│   │   └── libtesaiot_license.a
│   └── APP_KIT_PSE84_EVAL_EPC2/Release/
│       └── (same libraries, compiled for Eva Kit BSP)
│
├── include/
│   ├── ipc/
│   │   ├── ipc_communication.h
│   │   ├── ipc_ui_protocol.h
│   │   └── bsp_feature_flags.h
│   ├── sensors/
│   │   ├── sensor_i2c.h
│   │   ├── sensor_bmi270.h
│   │   ├── sensor_dps368.h
│   │   ├── sensor_sht40.h
│   │   ├── sensor_bmm350.h
│   │   ├── sensor_capsense.h
│   │   ├── sensor_potentiometer.h
│   │   └── sensor_auto_task.h
│   ├── security/
│   │   └── tesaiot*.h (9 headers)
│   ├── wifi/
│   │   ├── lfs_wifi_creds.h
│   │   └── qspi_wifi_creds.h
│   └── micropython/genhdr/
│       ├── qstrdefs.generated.h    CRITICAL: must match libmicropython.a
│       ├── moduledefs.h
│       ├── mpversion.h
│       └── root_pointers.h
│
├── mpy/                            Source distribution (compile with project)
│   ├── modui.c
│   ├── modgpio.c
│   ├── modsensors.c
│   ├── modsensors_bmi270.c
│   ├── modsensors_dps368.c
│   ├── modsensors_sht40.c
│   ├── modsensors_bmm350.c
│   ├── modsensors_capsense.c
│   ├── modsensors_potentiometer.c
│   ├── modwifi.c
│   ├── modlcd.c
│   ├── moddsp.c
│   ├── moddsp_filters.c
│   ├── moddsp_imu.c
│   ├── modjoystick.c
│   ├── modmqtt.c
│   ├── modoptiga.c
│   ├── modtesaiot.c
│   ├── mpy_main.c
│   ├── tacp.c
│   ├── sensor_auto_task.c
│   └── sensor_auto_task.h
│
├── integration/
│   ├── bento_sdk.mk               Include in project Makefile
│   └── bento_micropython.mk       Include in Makefile.micropython
│
└── docs/
    └── APIs/                       This documentation set
```

---

## 3. Toolchain Requirements

| Component | Version | Required? | Notes |
|-----------|---------|-----------|-------|
| **ARM GCC** | 14.2.1 | YES | Must match — ABI sensitive |
| **ModusToolbox** | 3.6 | YES | BSP + middleware |
| **FreeRTOS** | 10.6.202 | YES | mtb_shared release |
| **LVGL** | 9.2.0 | YES | CM55 display |
| **MicroPython** | PSoC Edge main | YES | Port-specific |
| **CMSIS** | 6.1.0 | YES | Core headers |

### Compiler Flags (MUST match for all .a files)

```makefile
CFLAGS := -mcpu=cortex-m33 -mthumb -mfloat-abi=softfp -mfpu=fpv5-sp-d16
CFLAGS += -O2 -g -Wall
CFLAGS += -ffunction-sections -fdata-sections
CFLAGS += -DCOMPONENT_FREERTOS -DCOMPONENT_CAT1D -DCOMPONENT_CM33
CFLAGS += -DCOMPONENT_NON_SECURE_DEVICE -DCOMPONENT_SOFTFP
CFLAGS += -DCY_USING_HAL -DCY_RTOS_AWARE
```

**ABI mismatch = link error or float corruption at runtime.**

---

## 4. Integration — Makefile Snippet

### 4.1 Project Makefile (`proj_cm33_ns/Makefile`)

Add to your project's Makefile:

```makefile
# ============================================================
# BENTO-TESAIoT SDK Integration
# ============================================================
BENTO_SDK := ../../../BENTO-TESAIoT-SDK

# Verify SDK exists
$(if $(wildcard $(BENTO_SDK)/lib),,$(error BENTO SDK not found at $(BENTO_SDK)))

# Headers
INCLUDES += $(BENTO_SDK)/include/ipc
INCLUDES += $(BENTO_SDK)/include/sensors
INCLUDES += $(BENTO_SDK)/include/security
INCLUDES += $(BENTO_SDK)/include/wifi

# Static libraries (link order matters for dependencies)
LDFLAGS += -L$(BENTO_SDK)/lib/$(TARGET)/Release
LDLIBS  += -Wl,--whole-archive -lmicropython -Wl,--no-whole-archive
LDLIBS  += -ltesaiot_sensors
LDLIBS  += -ltesaiot_ipc_cm33
LDLIBS  += -ltesaiot_wifi_creds
LDLIBS  += -ltesaiot_security
LDLIBS  += -ltesaiot_license

# MicroPython modules (source — must compile with project)
SEARCH += $(BENTO_SDK)/mpy

# Ignore local copies during migration
CY_IGNORE += mpy/sensor_i2c.c
CY_IGNORE += mpy/sensor_bmi270.c
CY_IGNORE += mpy/sensor_dps368.c
CY_IGNORE += mpy/sensor_sht40.c
CY_IGNORE += mpy/sensor_bmm350.c
CY_IGNORE += mpy/sensor_capsense.c
CY_IGNORE += mpy/sensor_potentiometer.c
```

### 4.2 Makefile.micropython

```makefile
# MicroPython generated headers (MUST match libmicropython.a)
INC += -I$(BENTO_SDK)/include/micropython

# Keep mod*.c as source (QSTR requirement)
MOD_SRC_C += $(wildcard $(BENTO_SDK)/mpy/mod*.c)
MOD_SRC_C += $(BENTO_SDK)/mpy/mpy_main.c
MOD_SRC_C += $(BENTO_SDK)/mpy/tacp.c
MOD_SRC_C += $(BENTO_SDK)/mpy/sensor_auto_task.c
```

---

## 5. Link Order

Static library link order matters when libraries have dependencies:

```
libmicropython.a       (no BENTO dependencies — standalone)
    ↑
libtesaiot_ipc_cm33.a  (depends on: Cy_IPC_Pipe, FreeRTOS)
    ↑
libtesaiot_sensors.a   (depends on: sensor_i2c → cyhal_i2c, FreeRTOS)
    ↑
libtesaiot_wifi_creds.a (depends on: LittleFS, QSPI HAL)
    ↑
libtesaiot_security.a  (depends on: OPTIGA PAL, mbedTLS)
libtesaiot_license.a   (depends on: libtesaiot_security.a)
```

**Rule:** Libraries that are depended upon come AFTER the libraries that depend on them in the link command.

**Exception:** `libmicropython.a` requires `--whole-archive` because MicroPython uses custom linker sections (`.mp_registered_modules`) that would otherwise be stripped by `--gc-sections`.

---

## 6. QSTR Compatibility

### The Problem

MicroPython assigns compile-time integer IDs to every string used in the API:

```c
// In modui.c:
MP_QSTR_Button    →  ID 247  (assigned during libmicropython.a build)
MP_QSTR_value     →  ID 89   (assigned during libmicropython.a build)
```

If `mod*.c` is compiled with different QSTR IDs than `libmicropython.a`, the firmware will **crash at runtime** (wrong string lookups, corrupted module tables).

### The Solution

Always use the `qstrdefs.generated.h` that was generated during the `libmicropython.a` build:

```makefile
# In Makefile.micropython:
INC += -I$(BENTO_SDK)/include/micropython
# This ensures mod*.c uses the SAME QSTR IDs as libmicropython.a
```

### Verification

```bash
# Check QSTR count matches
grep -c "QDEF(" sdk/include/micropython/genhdr/qstrdefs.generated.h
# Should match the count from the libmicropython.a build

# Check for QSTR references in mod*.c not in generated header
grep -roh "MP_QSTR_[a-zA-Z_0-9]*" sdk/mpy/ | sort -u > /tmp/mod_qstrs.txt
grep "QDEF(MP_QSTR_" sdk/include/micropython/genhdr/qstrdefs.generated.h | \
    sed 's/.*QDEF(MP_QSTR_\([^,]*\),.*/\1/' | sort -u > /tmp/gen_qstrs.txt
comm -23 /tmp/mod_qstrs.txt /tmp/gen_qstrs.txt
# Should output NOTHING — any output = missing QSTR = runtime crash
```

---

## 7. Board Variant Handling

Sensor drivers use `bsp_feature_flags.h` for conditional compilation:

```c
// In sensor_dps368.c:
#if BSP_HAS_DPS368
bool dps368_init(void) { /* ... */ }
#endif
```

This means a single `libtesaiot_sensors.a` can work for all boards — unused sensor code compiles to empty objects and is stripped by `--gc-sections`.

**However**, the `bsp_feature_flags.h` used during `.a` compilation must define ALL sensors as available (superset). The project's own `bsp_feature_flags.h` then controls which `mod*.c` bindings are active.

---

## 8. Building the SDK

### Prerequisites

```bash
export PATH="/usr/bin:/bin:/usr/sbin:/sbin"
export PATH="/Applications/mtb-gcc-arm-eabi/14.2.1/gcc/bin:$PATH"
export PATH="/Applications/ModusToolbox/tools_3.6/modus-shell/bin:$PATH"
```

### Build All Libraries

```bash
cd BENTO-TESAIoT-libraries

# Build sensor library
make -f Makefile.sdk sensors PROJECT_ROOT=../KIT_PSE84_AI-MicroPython-AI-Core

# Build IPC library
make -f Makefile.sdk ipc PROJECT_ROOT=../KIT_PSE84_AI-MicroPython-AI-Core

# Build WiFi credentials library
make -f Makefile.sdk wifi_creds PROJECT_ROOT=../KIT_PSE84_AI-MicroPython-AI-Core

# Or build all at once
make -f Makefile.sdk all PROJECT_ROOT=../KIT_PSE84_AI-MicroPython-AI-Core

# Package MicroPython headers
./scripts/package_micropython_headers.sh ../KIT_PSE84_AI-MicroPython-AI-Core

# Verify
make -f Makefile.sdk verify
```

### Verify Library Contents

```bash
# List objects in library
arm-none-eabi-ar -t lib/libtesaiot_sensors.a

# List exported symbols
arm-none-eabi-nm lib/libtesaiot_sensors.a | grep " T "

# Expected:
#   sensor_i2c_init
#   bmi270_init, bmi270_read_accel, bmi270_read_gyro
#   dps368_init, dps368_read_both
#   sht40_init, sht40_read_both
#   bmm350_init, bmm350_read_heading
#   capsense_init, capsense_read
#   potentiometer_init, potentiometer_read_percent
```

---

## 9. Integration Testing Checklist

After integrating the SDK into a project:

```bash
# 1. Build
make build -j

# 2. Compare binary size (should be within 1% of baseline)
ls -la build/APP_*/Release/proj_cm33_ns.elf

# 3. Flash
make program

# 4. MicroPython REPL test
>>> import sensors
>>> sensors.init()
>>> sensors.bmi270.acceleration()
(0.12, -0.34, 9.78)

# 5. IPC test (sensor data flows to CM55 display)
>>> sensors.push()

# 6. WiFi test
>>> import wifi
>>> wifi.scan()

# 7. UI test
>>> import ui
>>> btn = ui.Button("Test", x=100, y=100)
>>> ui.poll()

# 8. Security test
>>> import optiga
>>> optiga.init()
>>> optiga.uid()
```

---

## 10. Troubleshooting

| Problem | Cause | Fix |
|---------|-------|-----|
| `undefined reference to sensor_i2c_init` | .a not linked | Check `LDFLAGS` path + `LDLIBS` |
| `multiple definition of sensor_i2c_init` | Source + .a both linked | Add source files to `CY_IGNORE` |
| `cannot find -ltesaiot_sensors` | Wrong library path | Verify `BENTO_SDK` path depth |
| REPL works but `import sensors` crashes | QSTR mismatch | Rebuild mod*.c with matching `qstrdefs.generated.h` |
| Float values corrupted | ABI mismatch | All .a must use `-mfloat-abi=softfp` |
| Linker warning: orphan section | Missing linker section | Add `.ipc_sharedmem` to linker script |
| Binary 2x larger than expected | `--whole-archive` on wrong lib | Only use `--whole-archive` for `libmicropython.a` |
| Missing MicroPython module | QSTR not in generated header | Rebuild `libmicropython.a` with new module |

---

## 11. Rollback Plan

Every integration step is reversible:

1. Remove `LDFLAGS`/`LDLIBS` entries from Makefile
2. Remove `CY_IGNORE` entries (return to local source)
3. Build normally — binary size should match baseline

**Rule:** Never delete local source files until the .a integration is verified on hardware.

---

## 12. Version Compatibility Matrix

| Component | Version | Locked to |
|-----------|---------|-----------|
| ARM GCC | 14.2.1 | ModusToolbox 3.6 |
| FreeRTOS | 10.6.202 | mtb_shared release |
| LVGL | 9.2.0 | mtb_shared release |
| MicroPython | PSoC Edge main | Port release |
| OPTIGA Trust M | 5.3.0 | mtb_shared release |
| ifx-mbedtls | 3.6.400 | mtb_shared release |
| lwIP | 2.1.2 | mtb_shared STABLE |
| CMSIS | 6.1.0 | mtb_shared release |

**SDK must document this version matrix.** Mismatched versions = undefined behavior.
