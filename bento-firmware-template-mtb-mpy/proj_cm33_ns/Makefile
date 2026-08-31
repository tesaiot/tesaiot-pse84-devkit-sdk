################################################################################
# \file Makefile
# \version 3.0
#
# \brief
# Makefile for configuring the Cortex-M33 non-secure project
# MicroPython scripting engine + WiFi stack + CM55 boot
#
# Architecture:
#   PREBUILD step builds libmicropython.a using MicroPython's own build system
#   MTB compiles BSP/PDL/HAL/retarget-io + main.c + wifi_init.c
#   Final link: MTB objects + libmicropython.a (--whole-archive)
#
# v3.0: Added FreeRTOS + WiFi stack (WHD/WCM/lwIP/mbedTLS)
#        MicroPython runs as a FreeRTOS task
#        WiFi SDIO (SDHC0) accessed directly from CM33_NS (PPC-protected)
#
################################################################################

include ../common.mk

# BSP feature flags shared with proj_cm55 + Makefile.micropython. Without
# this `info.board` reports zero for every BSP_HAS_* (because the native
# CM33_NS code that builds the JSON ack reads them as undefined ⇒ 0)
# and `sensor.stream.start` rejects every id (k_streams gated on
# BSP_HAS_BMI270 etc. in sensor_stream.c).
-include ../bsps/TARGET_$(TARGET)/bsp_features.mk

################################################################################
# Basic Configuration
################################################################################

APPNAME=proj_cm33_ns

VERBOSE=

################################################################################
# Advanced Configuration
################################################################################

# FreeRTOS + WiFi stack
# MicroPython runs as a FreeRTOS task. WiFi (WHD/WCM/lwIP) requires RTOS.
COMPONENTS+=FREERTOS RTOS_AWARE
COMPONENTS+=LWIP MBEDTLS SECURE_SOCKETS
COMPONENTS+=OPTIGA_CYHAL

# Custom mbedTLS config
MBEDTLSFLAGS = MBEDTLS_USER_CONFIG_FILE='"configs/mbedtls_user_config.h"'

# OPTIGA Trust M library config
OPTIGAFLAGS=OPTIGA_LIB_EXTERNAL='"optiga_lib_config_mtb.h"'
OPTIGA_LIB_PATH=$(CY_GETLIBS_SHARED_PATH)/$(CY_GETLIBS_SHARED_NAME)/optiga-trust-m/release-v5.3.0
DISABLE_COMPONENTS=

CORE=CM33
CORE_NAME=CM33_0

# Exclude MicroPython build directories from MTB auto-discovery.
# MTB must NOT try to compile MicroPython sources - they are built separately.
# ENABLE_PAGE_BENTO_BUDDY must be assigned BEFORE the CY_IGNORE ifneq below
# evaluates it — otherwise the variable is empty at parse time and btstack
# gets blanket-excluded even on a flag=1 build. Default kept at 0 (see the
# dedicated block further down for the rationale + memory math).
ENABLE_PAGE_BENTO_BUDDY ?= 0


# Exclude MicroPython build dirs and BMI270 library
# (libmicropython.a provides its own BMI270 driver via sensor_bmi270.c)
CY_IGNORE=mpy build-micropython
CY_IGNORE+=$(SEARCH_BMI270_SensorAPI) $(SEARCH_sensor-motion-bmi270)
# BT libraries: btstack + btstack-integration are fetched (via libs/*.mtb)
# only when the buddy variant needs them. bt-fw for this board lives in
# `bt-fw-mur-cyw55513` (Murata LBEE5HY2FY module, CYW55513 silicon) — the
# same lib WiFi already consumes, so no separate ignore is needed.
ifneq ($(ENABLE_PAGE_BENTO_BUDDY),1)
CY_IGNORE+=$(SEARCH_btstack-integration) $(SEARCH_btstack)
endif
# Exclude OPTIGA internal directories (MTB auto-discovers 168+ .c files otherwise)
CY_IGNORE+=$(OPTIGA_LIB_PATH)/external/mbedtls
CY_IGNORE+=$(OPTIGA_LIB_PATH)/extras
CY_IGNORE+=$(OPTIGA_LIB_PATH)/examples
CY_IGNORE+=$(OPTIGA_LIB_PATH)/tests
# Exclude AI Kit application_code and libraries (CM55-specific, discovered via SEARCH)
CY_IGNORE+=$(BENTO_LIBS_DIR)/kit-pse84-ai/application_code
CY_IGNORE+=$(BENTO_LIBS_DIR)/kit-pse84-ai/libraries
CY_IGNORE+=$(BENTO_LIBS_DIR)/kit-pse84-ai/modules
CY_IGNORE+=$(BENTO_LIBS_DIR)/kit-pse84-ai/tesaiot

# MTB-compiled sources:
#   main.c       - FreeRTOS entry point (creates MicroPython task, boots CM55)
#   wifi_init.c  - SDIO + WCM initialization (lazy, on first wifi.scan/connect)
#   ntp_sync.c   - NTP → RTC sync (needs LwIP headers, must be MTB-compiled)
# driver/retarget_io_init.c is auto-discovered by MTB.
SOURCES+=main.c wifi_init.c ntp_sync.c ipc_hsm_handler.c
# TESAIoT config IPC handler (CM55 → CM33: mode switch, config get/set)
SOURCES+=$(BENTO_COMMON)/modules/tesaiot_config/ipc_tesaiot_handler.c
# TESAIoT MQTT subsystem (needs lwIP + cy_mqtt — MTB provides these includes)
SOURCES+=$(BENTO_COMMON)/modules/tesaiot_mqtt/mqtt_task.c
SOURCES+=$(BENTO_COMMON)/modules/tesaiot_mqtt/mqtt_client_config.c
SOURCES+=$(BENTO_COMMON)/modules/tesaiot_mqtt/publisher_task.c
SOURCES+=$(BENTO_COMMON)/modules/tesaiot_mqtt/subscriber_task.c
# mTLS + OPTIGA Trust M orchestrator (cert read → PSA SE → TLS binding)
SOURCES+=$(BENTO_COMMON)/modules/tesaiot_mqtt/mqtt_mtls_setup.c
# PSA ITS RAM stubs (required by MBEDTLS_PSA_CRYPTO_SE_C — volatile, no flash)
SOURCES+=$(BENTO_COMMON)/modules/tesaiot_mqtt/psa_its_ram_stubs.c
# OPTIGA TLS key binding (secure-sockets cy_tls.c references cy_tls_get_optiga_key_id)
SOURCES+=$(CY_GETLIBS_SHARED_PATH)/$(CY_GETLIBS_SHARED_NAME)/secure-sockets/release-v3.12.1/source/COMPONENT_MBEDTLS/cy_tls_optiga_key.c
# OPTIGA Trust M helpers (cert read, validation, ECDSA sign) + PSA SE driver
# optiga_trust_helpers.c and optiga_psa_se.c stay as source deliberately.
# optiga_trust_helpers.c is three licences in one file: 447 lines derive from
# Infineon's mtb-example-optiga-mqtt-client under the Cypress EULA, 459 from
# optiga-trust-m v5.3.0 under MIT, and the rest is ours. Compiling the whole
# file into a closed archive would put vendor object code inside it. Splitting
# our part out is the right end state and is planned in the header's own
# refactoring note; it is a source change in a tree this release repo only
# reads. optiga_psa_se.c has no copyright header at all while its own .h claims
# Cypress — unresolved, so it stays open.
SOURCES+=$(BENTO_LIBS_DIR)/kit-pse84-ai/modules/tesaiot/optiga_trust_helpers.c
SOURCES+=$(BENTO_LIBS_DIR)/kit-pse84-ai/modules/tesaiot/optiga_psa_se.c
# tesaiot_optiga_manager.c ships in libbento_hsm.a — see the LDLIBS block below.

# OPTIGA Trust M Certificate Lifecycle Management: CSR + Protected Update.
# Sources are carried over verbatim from official_pse84_trustm_mTLS_tesaiot —
# do not edit them here; fix upstream and re-sync.
# See TESAIoT_PLAN/2026-8/HSM_CSR_ProtectedUpdate_MQTTs/.
#
# On for this project since v1.7.0. Enrolment is what the Dev Kit is for, and
# a default of 0 made every build that forgot the flag ship a board with no
# optiga.csr(), no verify_pair() and no protected_update() — the functions
# simply are not linked, so it fails as AttributeError, which reads like a
# broken board rather than a wrong build. release_fw.sh runs a bare
# `make build -j RELEASE_BUILD=1` with no variable pass-through, so a default
# of 0 would have published exactly that image.
# MPY_GC_HEAP_IN_SOCMEM: 0 = GC heap in SRAM (fast, the default everywhere else),
# 1 = GC heap in the shared SOCMEM window, which returns ~64 KB to the C heap and
# makes MicroPython slower. This project sets it to 1 because the mTLS handshake
# needs that heap: with the GC heap in SRAM the C heap had 6,308 bytes free and
# the handshake failed on a malloc. The full rationale, the trade and the
# per-project guidance are in Makefile.micropython — read that before changing
# this. Every boot prints which side of the trade the running image is on.
MPY_GC_HEAP_IN_SOCMEM ?= 1

ENABLE_OPTIGA_CLM ?= 1
ifeq ($(ENABLE_OPTIGA_CLM),1)
# The MQTT side of Protected Update and CSR: request/status publishing and the
# command handlers. Carried over from official_pse84_trustm_mTLS_tesaiot and
# left byte-identical; the compile-time identity macros it expects are supplied
# as runtime accessors by mqtt_client_config.h, because a board here is
# provisioned after it is built.
# tesaiot_optiga_trust_m.c (the MQTT enrolment protocol) and
# tesaiot_protected_update_isolated.c (the offline self-test) ship in
# libbento_hsm.a with tesaiot_optiga_manager.c — see the LDLIBS block below.
#
# Bundle ingest stays as source. The single_bundle apply path was carried from
# the reference subscriber task and 1301 of its 1510 substantive lines are
# still identical to it; that file is under the Cypress EULA and ours carries
# no notice, so it is not ours to close. The claw subscriber dispatches to it
# via a weak symbol.
SOURCES+=$(BENTO_LIBS_DIR)/kit-pse84-ai/modules/tesaiot/tesaiot_pu_ingest.c
DEFINES+=ENABLE_OPTIGA_CLM=1
endif

# IPC shared headers (ipc_communication.h, ipc_response_t, etc.)
INCLUDES+=$(BENTO_COMMON)/shared/include
# TESAIoT config store + IPC handler headers
INCLUDES+=$(BENTO_COMMON)/modules/tesaiot_config
# TESAIoT MQTT headers (mqtt_task.h, mqtt_client_config.h, etc.)
INCLUDES+=$(BENTO_COMMON)/modules/tesaiot_mqtt
# TESAIoT HTTPS POST client (Scenarios 3+4: serverTLS Bearer + APISIX key-auth)
SOURCES+=$(BENTO_COMMON)/modules/tesaiot_https/tesaiot_https.c

# --- mtb-only: what replaces the MicroPython layer ---------------------------
# Under mtb-mpy all of these are compiled into libmicropython.a by
# Makefile.micropython, so adding them here too would double-define. Under
# mtb-only that archive is gone and they are ordinary sources:
#
#   storage_c/            the C lfs2 mount over the same flash window, plus
#                         the byte-compatible /.wifi_creds store
#   tesaiot_config_store  same parser and serialiser; the two I/O blocks
#                         compile against bento_storage when BENTO_HAS_MPY=0
#   tesaiot_mqtt          pure C (its symbol table demands nothing from the
#                         VM); it lived in MOD_SRC_C by drift, not by need
#   sensor_auto_task      pure C except two ISR commands now guarded; also the
#                         only boot-path caller of cm33_ipc_communication_setup
#   cm33_ipc_communication  the IPC pipe bring-up itself
ifeq ($(BENTO_HAS_MPY),0)
SOURCES+=$(BENTO_COMMON)/storage_c/littlefs/lfs2.c
SOURCES+=$(BENTO_COMMON)/storage_c/littlefs/lfs2_util.c
SOURCES+=$(BENTO_COMMON)/storage_c/bento_storage.c
SOURCES+=$(BENTO_COMMON)/storage_c/lfs_wifi_creds_c.c
SOURCES+=$(BENTO_COMMON)/modules/tesaiot_config/tesaiot_config_store.c
SOURCES+=$(BENTO_COMMON)/modules/tesaiot_mqtt/tesaiot_mqtt.c
SOURCES+=$(BENTO_COMMON)/mpy/sensor_auto_task.c
# The I2C sensor drivers sensor_auto_task feeds from. All plain C — they sit
# under mpy/ because the Python sensors module wraps them, but the wrapper is
# modsensors.c, not these.
SOURCES+=$(BENTO_COMMON)/mpy/sensor_i2c.c
SOURCES+=$(BENTO_COMMON)/mpy/sensor_bmi270.c
SOURCES+=$(BENTO_COMMON)/mpy/sensor_bmm350.c
SOURCES+=$(BENTO_COMMON)/mpy/sensor_dps368.c
SOURCES+=$(BENTO_COMMON)/mpy/sensor_sht40.c
# CapSense is the one sensor with TWO interchangeable backends behind
# sensor_capsense.h, and which is correct depends on the board: on a QWA309
# base board the 4000T is on the CM55-owned display/touch bus, so the
# direct-I2C backend reads the wrong bus. capsense.mk owns that decision for
# both variants — do NOT restate it here. This line said sensor_capsense.c
# unconditionally until 2026-08-28, which gave every mtb-only C developer on
# this board a capsense_read() that talks to the wrong I2C bus.
ifeq ($(BSP_HAS_CAPSENSE),1)
include $(BENTO_COMMON)/mpy/capsense.mk
SOURCES+=$(BENTO_COMMON)/mpy/$(BENTO_CAPSENSE_BACKEND_C)
endif
SOURCES+=$(BENTO_COMMON)/mpy/sensor_potentiometer.c
SOURCES+=$(BENTO_COMMON)/shared/source/COMPONENT_CM33/cm33_ipc_communication.c
INCLUDES+=$(BENTO_COMMON)/storage_c
endif
INCLUDES+=$(BENTO_COMMON)/modules/tesaiot_https

# OPTIGA Trust M library core sources (13 files)
SOURCES+=$(OPTIGA_LIB_PATH)/src/crypt/optiga_crypt.c \
         $(OPTIGA_LIB_PATH)/src/util/optiga_util.c \
         $(OPTIGA_LIB_PATH)/src/cmd/optiga_cmd.c \
         $(OPTIGA_LIB_PATH)/src/common/optiga_lib_common.c \
         $(OPTIGA_LIB_PATH)/src/common/optiga_lib_logger.c \
         $(OPTIGA_LIB_PATH)/src/comms/optiga_comms_ifx_i2c.c \
         $(OPTIGA_LIB_PATH)/src/comms/ifx_i2c/ifx_i2c.c \
         $(OPTIGA_LIB_PATH)/src/comms/ifx_i2c/ifx_i2c_config.c \
         $(OPTIGA_LIB_PATH)/src/comms/ifx_i2c/ifx_i2c_data_link_layer.c \
         $(OPTIGA_LIB_PATH)/src/comms/ifx_i2c/ifx_i2c_physical_layer.c \
         $(OPTIGA_LIB_PATH)/src/comms/ifx_i2c/ifx_i2c_presentation_layer.c \
         $(OPTIGA_LIB_PATH)/src/comms/ifx_i2c/ifx_i2c_transport_layer.c \
         $(OPTIGA_LIB_PATH)/extras/pal/pal_crypt_mbedtls.c
INCLUDES+=$(BENTO_COMMON)/mpy
# OPTIGA lib config header (optiga_lib_config_mtb.h) — dir is CY_IGNOREd for .c but needed for .h
INCLUDES+=$(BENTO_LIBS_DIR)/kit-pse84-ai/modules/tesaiot
# TESAIoT config umbrella header (tesaiot_config.h — debug labels, OID config)
INCLUDES+=$(BENTO_LIBS_DIR)/kit-pse84-ai/tesaiot/include

# OPTIGA Trust M library includes (root needed for "include/optiga_*.h" relative paths)
INCLUDES+=$(OPTIGA_LIB_PATH)
INCLUDES+=$(OPTIGA_LIB_PATH)/include \
          $(OPTIGA_LIB_PATH)/include/common \
          $(OPTIGA_LIB_PATH)/include/pal \
          $(OPTIGA_LIB_PATH)/include/cmd \
          $(OPTIGA_LIB_PATH)/include/comms \
          $(OPTIGA_LIB_PATH)/include/ifx_i2c
# SEARCH path for COMPONENT_OPTIGA_CYHAL PAL files (MTB auto-discovery)
SEARCH+=$(BENTO_LIBS_DIR)/kit-pse84-ai

# Defines
DEFINES+=$(MBEDTLSFLAGS) $(OPTIGAFLAGS) CYBSP_WIFI_CAPABLE CY_RTOS_AWARE
# OPTIGA Trust M I2C pin defines (needed for OPTIGA PAL compilation)
# Dev Kit (QWA309 base): mikroBUS/QWIIC I2C = P17.0/P17.1 shared 3V3 bus (SCB5)
DEFINES+=CYBSP_TRUSTM_I2C_SCL=P17_0 CYBSP_TRUSTM_I2C_SDA=P17_1
# Route OPTIGA PAL to the shared 3V3 bus (SCB5) instead of SCB0 (P8.x 1V8)
DEFINES+=OPTIGA_I2C_ON_3V3_BUS
DEFINES+=MBEDTLS_CONFIG_FILE='"mbedtls/mbedtls_config.h"'
DEFINES+=MBEDTLS_PSA_CRYPTO_CONFIG_FILE='"configs/ifx_psa_crypto_config.h"'
DEFINES+=CY_RETARGET_IO_CONVERT_LF_TO_CRLF
# BT feature baud rate (3M for faster firmware download)
DEFINES+=CYBSP_BT_PLATFORM_CFG_BAUD_FEATURE=3000000

# Board-specific defines for conditional compilation
DEFINES+=USE_KIT_PSE84_AI
# BMI270 at +/-8g so the DEEPCRAFT motion model sees un-clipped gestures.
# Must match proj_cm55/Makefile (both cores share the wire scale). Other boards
# leave this undefined and stay at +/-2g.
DEFINES+=BENTO_BMI270_ACCEL_8G
# MQTT event thread: 3KB (default 8KB) — saves 5KB FreeRTOS heap
DEFINES+=CY_MQTT_EVENT_THREAD_STACK_SIZE=3072
# Suppress WHD boot banners (chip ID, WLAN MAC/FW/CLM, WHD VERSION)
DEFINES+=WHD_PRINT_DISABLE

# Propagate BSP feature flags (loaded from bsp_features.mk above) to the
# CM33_NS native compile so info.board / sensor.stream report honestly
# and the streamable-id table in sensor_stream.c is populated.
ifeq ($(BSP_HAS_BMI270),1)
DEFINES+=BSP_HAS_BMI270=1
endif
ifeq ($(BSP_HAS_BMM350),1)
DEFINES+=BSP_HAS_BMM350=1
endif
ifeq ($(BSP_HAS_DPS368),1)
DEFINES+=BSP_HAS_DPS368=1
endif
ifeq ($(BSP_HAS_SHT40),1)
DEFINES+=BSP_HAS_SHT40=1
endif
ifeq ($(BSP_HAS_CAPSENSE),1)
DEFINES+=BSP_HAS_CAPSENSE=1
endif
ifeq ($(BSP_HAS_POTENTIOMETER),1)
DEFINES+=BSP_HAS_POTENTIOMETER=1
endif

# Bento Desktop Buddy variant — BLE NUS + reduced MPY heap.
# When ENABLE_PAGE_BENTO_BUDDY=1, shrink MPY GC heap from 112 KB to 85 KB
# to reserve ~27 KB for the AIROC BLE host stack (NUS GATT + L2CAP buffers).
# See CLAUDE.md section on CM33_NS memory budget (252 KB RAM total).
#
# 2026-05-26 finding (feat/playground-ble-radio-switch branch): on this
# CYW55513 + Murata LBEE5HY2FY hardware, BLE-only operation is NOT in
# fact lighter than BLE+WiFi — the Murata 2FY module needs the WHD SDIO
# bring-up to power-negotiate the radio die, and wiced_bt_stack_init then
# competes with WHD for FreeRTOS heap. Empirically the AIROC controller
# fails BTM_ENABLED_EVT (Malloc failed) with 90 KB heap + 64 KB MPY GC.
# The DualBand TinyPython variant remains the only working BLE+anything
# configuration on this board (MPY heap squeezed to 24 KB to free 40 KB
# for WHD + AIROC). Until a smaller-footprint BT stack or external BLE
# transceiver lands, the Playground line stays WiFi-only by default;
# users wanting Desktop Buddy pairing should flash the DualBand variant.
# See TESAIoT_PLAN/2026-5/PLAYGROUND_PLUS_BLE/PLAN.md for the full
# memory math + Phase 1 implementation that is kept in the tree as a
# checkpoint for any future re-attempt.
ENABLE_PAGE_BENTO_BUDDY ?= 0
DEFINES+=ENABLE_PAGE_BENTO_BUDDY=$(ENABLE_PAGE_BENTO_BUDDY)
ifeq ($(ENABLE_PAGE_BENTO_BUDDY),1)
# Memory: 112 KB -> 85 KB MPY heap, reserves ~27 KB for AIROC BLE host stack.
DEFINES+=MPY_GC_HEAP_SIZE=87040
DEFINES+=BENTO_HAS_BLE_NUS=1
# Project-wide jsmn configuration (applies to every TU that includes jsmn.h).
# JSMN_PARENT_LINKS gates struct member `parent` used by nus_protocol/nus_commands
# for navigating nested prompt/time objects; JSMN_STRICT enforces spec parse.
DEFINES+=JSMN_PARENT_LINKS JSMN_STRICT JSMN_STATIC

# AIROC BLE host stack for CYW55500.
# WICED_BLE  = high-level host API (wiced_bt_stack_init, GATT server, SMP).
# RTOS_AWARE = btstack-integration + retarget-io pick up RTOS mutex/semaphore
#              primitives from cyabs_rtos (required by freertos/cybt_platform).
#
# HCI-UART is NOT listed here — bsp.mk:DEVICE_COMPONENTS already injects
# "HCI-UART 55500 55500A1 MURATA-2FY" so the transport glue is linked
# automatically. Adding it again is redundant and was mixing with the HAL
# code path that this project does not use (pure PDL + mtb-hal-next).
# BLUETOOTH-FREERTOS is a legacy component name (pre-v4 btstack-integration);
# in v6.2.1 the FreeRTOS port lives inside COMPONENT_HCI-UART/freertos, so
# listing it explicitly does nothing.
#
# Trust source: Infineon/mtb-example-psoc-edge-btstack-hello-sensor
# (proj_cm33_ns/Makefile:63 = COMPONENTS+=FREERTOS RTOS_AWARE WICED_BLE).
COMPONENTS+=WICED_BLE RTOS_AWARE

# Nordic UART Service — declared as a library module; the file list lives
# with the module (ble_nus.mk) so every consumer stays in sync by construction.
BENTO_LIB_MODULES += ble_nus
include $(BENTO_LIBS_DIR)/../lib.mk

# AIROC BT stack header paths. getlibs does not auto-register these on the
# AI Kit (manifest does not list them as public), so expose them explicitly:
_BTSTACK_INT_ROOT := $(firstword $(wildcard ../../mtb_shared/btstack-integration/release-v*))
_BTSTACK_ROOT     := $(firstword $(wildcard ../../mtb_shared/btstack/release-v*))
INCLUDES+=$(_BTSTACK_INT_ROOT)
INCLUDES+=$(_BTSTACK_INT_ROOT)/COMPONENT_HCI-UART/include
INCLUDES+=$(_BTSTACK_INT_ROOT)/COMPONENT_HCI-UART/common
INCLUDES+=$(_BTSTACK_ROOT)/wiced_include
INCLUDES+=$(_BTSTACK_ROOT)/include

# Fail fast: if the user flipped the flag but never ran `make getlibs`,
# the btstack-integration and bt-fw blobs will be missing and WICED_BLE
# will not link. Filesystem wildcard is used here (not $(SEARCH_*)) because
# this block is parsed before libs/mtb.mk is included.
ifeq ($(wildcard ../../mtb_shared/btstack-integration/release-v*/.),)
$(error ENABLE_PAGE_BENTO_BUDDY=1 requires btstack-integration. Run 'make getlibs' to fetch the AIROC BLE host stack.)
endif
ifeq ($(wildcard ../../mtb_shared/btstack/release-v*/.),)
$(error ENABLE_PAGE_BENTO_BUDDY=1 requires btstack. Run 'make getlibs' to fetch the AIROC BLE host stack.)
endif
ifeq ($(wildcard ../../mtb_shared/bt-fw-mur-cyw55513/release-v*/.),)
$(error ENABLE_PAGE_BENTO_BUDDY=1 requires bt-fw-mur-cyw55513 (Murata CYW55513 HCD firmware). Run 'make getlibs'.)
endif

# libmicropython.a is compiled separately (Makefile.micropython). When the
# MPY heap size changes via CLI (112 -> 85 KB), the .a caches old size and
# mpy_gc_heap[] would be compiled with stale bytes. Delete the stamp to
# force Makefile.micropython to rebuild.
PREBUILD+=; if [ -f build-micropython/libmicropython.a ]; then \
    rm -f build-micropython/libmicropython.a \
          build/$(TARGET)/Debug/$(APPNAME).elf \
          build/$(TARGET)/Release/$(APPNAME).elf; \
fi
endif

VFP_SELECT+=
CFLAGS+=
CXXFLAGS+=
ASFLAGS+=

# Link libmicropython.a with --whole-archive to ensure all MicroPython
# modules are included (they use registration macros, not direct references).
ifeq ($(BENTO_HAS_MPY),1)
LDFLAGS+=-Wl,--whole-archive $(CURDIR)/build-micropython/libmicropython.a -Wl,--no-whole-archive
endif
LDLIBS+=

LINKER_SCRIPT=

# PREBUILD: Build MicroPython static library before MTB compilation.
# After building, if libmicropython.a is newer than the ELF, delete the ELF
# to force MTB/ninja to re-link. Without this, adding new MicroPython C modules
# would silently produce a stale ELF because ninja doesn't track .a dependencies.
#
# The ELF check below sweeps BOTH Debug and Release. It used to name Debug only,
# while common.mk:20 sets CONFIG=Release — so the condition never held, no ELF
# was ever deleted, and every MicroPython change since then linked stale. Found
# 2026-08-04 when a newly added module compiled into the .a but never appeared
# in the image. The sibling sweep at the MPY-heap PREBUILD above already had it
# right; this one had drifted.
#
# Pass ENABLE_PAGE_BENTO_BUDDY + BSP_HAS_* explicitly so Makefile.micropython
# sees the same flag values as this Makefile — without this, the libmicropython
# build saw flag=0 and skipped exec_python_capture / modbentobuddy.c, which
# nus_commands.c and the radio_scheduler then could not link against.
ifeq ($(BENTO_HAS_MPY),1)
PREBUILD=$(MAKE) -f $(CURDIR)/Makefile.micropython TARGET=$(TARGET) \
    ENABLE_PAGE_BENTO_BUDDY=$(ENABLE_PAGE_BENTO_BUDDY) \
    ENABLE_OPTIGA_CLM=$(ENABLE_OPTIGA_CLM) \
    MPY_GC_HEAP_IN_SOCMEM=$(MPY_GC_HEAP_IN_SOCMEM) \
    BSP_HAS_BMI270=$(BSP_HAS_BMI270) \
    BSP_HAS_BMM350=$(BSP_HAS_BMM350) \
    BSP_HAS_DPS368=$(BSP_HAS_DPS368) \
    BSP_HAS_SHT40=$(BSP_HAS_SHT40) \
    BSP_HAS_CAPSENSE=$(BSP_HAS_CAPSENSE) \
    BSP_HAS_POTENTIOMETER=$(BSP_HAS_POTENTIOMETER) \
    BSP_HAS_QWA309_BASEBOARD=$(BSP_HAS_QWA309_BASEBOARD) && \
    for cfg in Debug Release; do \
        elf=build/$(TARGET)/$$cfg/$(APPNAME).elf; \
        if [ -f "$$elf" ] && [ build-micropython/libmicropython.a -nt "$$elf" ]; then \
            rm -f "$$elf"; \
        fi; \
    done
else
PREBUILD=
endif

POSTBUILD=

################################################################################
# Paths
################################################################################

CY_APP_PATH=

# Derived from BENTO_WORKSPACE, not hardcoded.
#
# This was `../../..`, which is right for our tree — template/ sits inside
# TESAIoT_Firmware_Development_and_Release/, so three levels up from a project
# directory is the workspace. It is wrong for a customer, whose README tells
# them to unpack bento-firmware-template/ as a direct child of their workspace:
# one level shallower, so ../../.. overshoots.
#
# The symptom is not a missing-directory error. `make getlibs` happily CREATES
# mtb_shared at the overshot path — 41 assets in the wrong place, measured
# 2026-08-28 — and the build then fails much later with ninja looking for a
# vendor source in a directory nothing populated. Found by unpacking the
# shipped zip and building it, which is the only way to find it.
CY_GETLIBS_SHARED_PATH=$(BENTO_WORKSPACE)
CY_GETLIBS_SHARED_NAME=mtb_shared

# --- BENTO protected library: TACP + WiFi credential management -----------
# tacp.c, lfs_wifi_creds.c and claw_session.c ship as a prebuilt archive.
# Their removal from the compile list lives in Makefile.micropython; they stay
# in SRC_QSTR there so the MicroPython qstr pool is unchanged.
BENTO_MPY_DIST := ../lib/mpy_secure
INCLUDES += $(BENTO_MPY_DIST)/include
# Follows the VM: every object in it needs the MicroPython runtime.
# libbento_hsm.a below does NOT — it needs zero mp_/qstr symbols and is linked
# in both variants.
ifeq ($(BENTO_HAS_MPY),1)
LDLIBS   += $(BENTO_MPY_DIST)/COMPONENT_CM33/COMPONENT_SOFTFP/TOOLCHAIN_GCC_ARM/libbento_mpy.a
endif

# --- BENTO protected library: OPTIGA enrolment and Protected Update ---------
# tesaiot_optiga_trust_m.c, tesaiot_protected_update_isolated.c and
# tesaiot_optiga_manager.c. tesaiot_pu_ingest.c is deliberately NOT in here and
# still compiles from source above — see the note beside it.
#
# Linked unconditionally, which is safe: an archive member is pulled only when
# something already references it, so with ENABLE_OPTIGA_CLM off nothing is
# extracted. Guarding it on the flag would be equivalent and one more thing to
# keep in step.
BENTO_HSM_DIST := ../lib/tesaiot_hsm
INCLUDES += $(BENTO_HSM_DIST)/include
LDLIBS   += $(BENTO_HSM_DIST)/COMPONENT_CM33/COMPONENT_SOFTFP/TOOLCHAIN_GCC_ARM/libbento_hsm.a

include $(CY_TOOLS_DIR)/make/start.mk
