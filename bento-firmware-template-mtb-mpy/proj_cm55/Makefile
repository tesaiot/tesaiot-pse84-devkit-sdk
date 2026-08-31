################################################################################
# \file Makefile
# \version 1.0
#
# \brief
# Makefile for configuring the Cortex-M55 core project
# Phase 1: Minimal FreeRTOS stub
# Phase 2: LVGL + Display (Waveshare 4.3" DSI)
# Phase 14: WiFi (CYW55513)
# Phase 14.5: TESAIoT + OPTIGA Trust M (secure credentials)
# Phase 15-16: WiFi/MQTT IPC proxy, Sensor Dashboard
#
################################################################################

include ../common.mk

################################################################################
# BSP Feature Flags (conditional compilation per TARGET)
################################################################################
-include ../bsps/TARGET_$(TARGET)/bsp_features.mk

# Page Component defaults (if not set in bsp_features.mk, all pages ON)
ENABLE_PAGE_MOTION      ?= 1
ENABLE_PAGE_ENVIRON     ?= 1
ENABLE_PAGE_CONTROLS    ?= 1
ENABLE_PAGE_JOYSTICK    ?= 1
ENABLE_PAGE_SMART_CARD  ?= 1
ENABLE_PAGE_HSM         ?= 1
ENABLE_PAGE_BENTOCLAW   ?= 1
ENABLE_PAGE_BENTO_BUDDY ?= 0
ENABLE_PAGE_ANIMATION   ?= 1
ENABLE_PAGE_FACE_ID     ?= 1
ENABLE_PAGE_SMART_WATCH ?= 1
ENABLE_PAGE_SPECTRUM    ?= 1
ENABLE_PAGE_WIFI_CONNECT ?= 1
ENABLE_PAGE_TESAIOT_CONNECT ?= 1
ENABLE_PAGE_GAME_SNAKE    ?= 0
ENABLE_PAGE_GAME_FLAPPY   ?= 0
ENABLE_PAGE_GAME_PONG     ?= 0
ENABLE_PAGE_GAME_SHOOTER  ?= 0
# QWA309-only page: source is #if BSP_HAS_QWA309_BASEBOARD — registering it
# without the BSP flag would reference missing symbols, so force OFF.
# GPIO & RGB Matrix merges the former Potentiometers + Touch & RGB pages.
ENABLE_PAGE_GPIO_RGB      ?= 1
ifneq ($(BSP_HAS_QWA309_BASEBOARD),1)
ENABLE_PAGE_GPIO_RGB      := 0
endif

# Motor Controller — Adafruit Motor Shield v2 on the QWA309 Arduino header.
# The page talks to the shield over the header I2C, which only exists on the
# QWA309 base board. Off by default even there — most boards have no shield
# plugged in, and a menu that drives absent hardware is noise (removed from
# the default menu 2026-08-28, professor's call). Opt back in with
# `./bento.sh enable motor_ctrl` or ENABLE_PAGE_MOTOR_CTRL=1.
ENABLE_PAGE_MOTOR_CTRL    ?= 0
ifneq ($(BSP_HAS_QWA309_BASEBOARD),1)
ENABLE_PAGE_MOTOR_CTRL    := 0
endif

################################################################################
# Auto-guard: if a page flag=1 but its directory is missing, force to 0.
# Prevents compile errors from #include of non-existent headers when a page
# directory has been removed or not yet created.
################################################################################
_PD:=modules/page-components
$(if $(filter 1,$(ENABLE_PAGE_MOTION)),$(if $(wildcard $(_PD)/motion/.),,$(eval ENABLE_PAGE_MOTION:=0)))
$(if $(filter 1,$(ENABLE_PAGE_ENVIRON)),$(if $(wildcard $(_PD)/environ/.),,$(eval ENABLE_PAGE_ENVIRON:=0)))
$(if $(filter 1,$(ENABLE_PAGE_CONTROLS)),$(if $(wildcard $(_PD)/controls/.),,$(eval ENABLE_PAGE_CONTROLS:=0)))
$(if $(filter 1,$(ENABLE_PAGE_JOYSTICK)),$(if $(wildcard $(_PD)/joystick/.),,$(eval ENABLE_PAGE_JOYSTICK:=0)))
$(if $(filter 1,$(ENABLE_PAGE_SMART_CARD)),$(if $(wildcard $(_PD)/smart_card/.),,$(eval ENABLE_PAGE_SMART_CARD:=0)))
$(if $(filter 1,$(ENABLE_PAGE_HSM)),$(if $(wildcard $(_PD)/hsm/.),,$(eval ENABLE_PAGE_HSM:=0)))
$(if $(filter 1,$(ENABLE_PAGE_BENTOCLAW)),$(if $(wildcard $(_PD)/bentoclaw/.),,$(eval ENABLE_PAGE_BENTOCLAW:=0)))
$(if $(filter 1,$(ENABLE_PAGE_BENTO_BUDDY)),$(if $(wildcard $(_PD)/bento_buddy/.),,$(eval ENABLE_PAGE_BENTO_BUDDY:=0)))
$(if $(filter 1,$(ENABLE_PAGE_ANIMATION)),$(if $(wildcard $(_PD)/animation/.),,$(eval ENABLE_PAGE_ANIMATION:=0)))
$(if $(filter 1,$(ENABLE_PAGE_FACE_ID)),$(if $(wildcard $(_PD)/face_id/.),,$(eval ENABLE_PAGE_FACE_ID:=0)))
$(if $(filter 1,$(ENABLE_PAGE_SMART_WATCH)),$(if $(wildcard $(_PD)/smart_watch/.),,$(eval ENABLE_PAGE_SMART_WATCH:=0)))
$(if $(filter 1,$(ENABLE_PAGE_SPECTRUM)),$(if $(wildcard $(_PD)/spectrum_analyzer/.),,$(eval ENABLE_PAGE_SPECTRUM:=0)))
$(if $(filter 1,$(ENABLE_PAGE_WIFI_CONNECT)),$(if $(wildcard $(_PD)/wifi_connect/.),,$(eval ENABLE_PAGE_WIFI_CONNECT:=0)))
$(if $(filter 1,$(ENABLE_PAGE_TESAIOT_CONNECT)),$(if $(wildcard $(_PD)/tesaiot_connect/.),,$(eval ENABLE_PAGE_TESAIOT_CONNECT:=0)))
$(if $(filter 1,$(ENABLE_PAGE_GPIO_RGB)),$(if $(wildcard $(_PD)/gpio_rgb/.),,$(eval ENABLE_PAGE_GPIO_RGB:=0)))
$(if $(filter 1,$(ENABLE_PAGE_MOTOR_CTRL)),$(if $(wildcard $(_PD)/motor_ctrl/.),,$(eval ENABLE_PAGE_MOTOR_CTRL:=0)))

################################################################################
# Basic Configuration
################################################################################

APPNAME=proj_cm55

VERBOSE=

################################################################################
# Advanced Configuration
################################################################################

# Phase 1: FreeRTOS
COMPONENTS+=FREERTOS RTOS_AWARE

# Phase 2: Display (GFXSS)
COMPONENTS+=GFXSS

# Phase 14: WiFi (CYW55513 via SDIO)
# BSP provides: WIFI_INTERFACE_SDIO SM 55500A1 HCI-UART MURATA-2FY
COMPONENTS+=LWIP MBEDTLS
COMPONENTS+=WCM SECURE_SOCKETS

# Phase 14.5: OPTIGA Trust M — set to 1 to enable (requires Trust M hardware)
# WARNING: OPTIGA PAL I2C may conflict with display I2C (FT5406 touch).
#          Disable when testing display/WiFi without Trust M.
ENABLE_OPTIGA ?= 0

ifeq ($(ENABLE_OPTIGA), 1)
COMPONENTS+=OPTIGA_CYHAL
DEFINES+=ENABLE_OPTIGA
# OPTIGA PAL from BENTO kit-pse84-ai (MTB auto-discovers COMPONENT_OPTIGA_CYHAL/)
SEARCH+=$(BENTO_BOARD)
endif

DISABLE_COMPONENTS=

CORE=CM55
CORE_NAME=CM55_0

################################################################################
# Phase 14.5: TESAIoT + OPTIGA Trust M Integration
# Controlled by ENABLE_OPTIGA flag (0=disabled, 1=enabled)
################################################################################

# OPTIGA Trust M library path (always defined for CY_IGNORE)
OPTIGA_LIB_PATH=$(CY_GETLIBS_SHARED_PATH)/$(CY_GETLIBS_SHARED_NAME)/optiga-trust-m/release-v5.3.0

ifeq ($(ENABLE_OPTIGA), 1)
# OPTIGA Trust M core source files (compiled directly for CM55)
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

# OPTIGA Trust M include directories
INCLUDES+=$(OPTIGA_LIB_PATH)/include \
          $(OPTIGA_LIB_PATH)/include/common \
          $(OPTIGA_LIB_PATH)/include/pal \
          $(OPTIGA_LIB_PATH)/include/cmd \
          $(OPTIGA_LIB_PATH)/include/comms \
          $(OPTIGA_LIB_PATH)/include/ifx_i2c

# TESAIoT library headers — from BENTO kit-pse84-ai
INCLUDES+=$(BENTO_BOARD)/tesaiot/include

# TESAIoT IP-protected sources (compiled directly for CM55 — no pre-built .a)
SOURCES+=$(BENTO_BOARD)/tesaiot/src/tesaiot_license.c
SOURCES+=$(BENTO_BOARD)/tesaiot/src/tesaiot_crypto.c
SOURCES+=$(BENTO_BOARD)/tesaiot/src/tesaiot_license_data.c

# TESAIoT runtime modules (OPTIGA manager) — from BENTO kit-pse84-ai
INCLUDES+=$(BENTO_BOARD)/modules/tesaiot
SEARCH+=$(BENTO_BOARD)/modules/tesaiot

# ifx-mbedtls library include (entropy_poll.h etc.)
INCLUDES+=$(CY_GETLIBS_SHARED_PATH)/$(CY_GETLIBS_SHARED_NAME)/ifx-mbedtls/release-v3.6.400/library

endif  # ENABLE_OPTIGA

# Always ignore optiga internals (even when disabled, prevents MTB auto-discovery errors)
CY_IGNORE+=$(OPTIGA_LIB_PATH)/external/mbedtls
CY_IGNORE+=$(OPTIGA_LIB_PATH)/pal
# AWS SDK coreHTTP not needed (only MQTT); skip missing core_http_config.h
CY_IGNORE+=$(SEARCH_aws-iot-device-sdk-embedded-C)/libraries/standard/coreHTTP

# When OPTIGA is disabled, ignore all OPTIGA-related sources to prevent MTB auto-discovery
ifneq ($(ENABLE_OPTIGA), 1)
CY_IGNORE+=$(OPTIGA_LIB_PATH)
CY_IGNORE+=$(BENTO_BOARD)/modules/tesaiot
CY_IGNORE+=$(BENTO_BOARD)/tesaiot
endif

SOURCES+=
INCLUDES+=

################################################################################
# USB Host HID Joystick (Logitech F310 via SEGGER emUSB-Host)
################################################################################

COMPONENTS+=USBH_BASE
DEFINES+=ENABLE_USB_HOST=1
DEFINES+=USBH_DISABLE_STANDARD_OUTPUT

# The two emUSB-Host configuration files are NOT stored in this repository.
#
# emUSB-Host is SEGGER's, licensed to Cypress for object-code redistribution
# only, so we may not ship its source. Infineon ships the two templates inside
# the asset at export/Config/ for exactly this purpose -- the asset's .cyignore
# excludes that directory so MTB will not compile them in place -- and the
# emUSB-Host API reference says the templates are "automatically copied into
# your project". So they are fetched, then copied out, then given the three
# changes that are ours. See materialize_emusb_config.sh for what those are.
#
# This runs at parse time, not as a PREBUILD: MTB enumerates sources while
# parsing, so files that appear later would never be compiled. Same $(shell)
# gate that common.mk uses for the mtb_shared patches, and gated on the same
# BENTO_ASSET_GATE so `make getlibs` -- which is what fetches the asset -- is
# not blocked by the asset being absent.
ifeq ($(BENTO_ASSET_GATE),1)
BENTO_EMUSB_REPORT := $(shell ../materialize_emusb_config.sh $(BENTO_WORKSPACE) imports/emusb-host/Config 2>&1 | tr '\n' '~')
ifneq ($(BENTO_EMUSB_REPORT),)
$(error $(subst ~,$(newline),$(BENTO_EMUSB_REPORT)))
endif
endif

INCLUDES+=imports/emusb-host/Config
INCLUDES+=imports/emusb-host/Config/COMPONENT_PSE84
include $(BENTO_COMMON)/usb_hid_joystick/usb_hid_joystick.mk
include $(BENTO_COMMON)/modules/usb_ccid_smartcard/usb_ccid_smartcard.mk

################################################################################
# Radar Presence Detection (BGT60TR13C, SPI, 200 Hz) — AI Kit only
################################################################################

ifeq ($(BSP_HAS_RADAR),1)
INCLUDES+=$(BENTO_BOARD)/libraries/tesaiot-radar
SEARCH+=$(BENTO_BOARD)/libraries/tesaiot-radar

# We own the BGT60 platform layer. The vendor's xensiv_bgt60trxx_edge.c ends
# both of its SPI entry points in an unbounded spin on a flag that only the SCB
# interrupt clears; lose that interrupt once and the radar task never returns,
# taking the sensor's own stall watchdog with it because that watchdog sits at
# the bottom of the loop that is stuck. Measured on hardware 2026-08-09: loop
# counter frozen, phase parked on the FIFO read, recovery attempts 0, and only a
# power cut brought it back. bento_bgt60trxx_platform.c is that file with the
# two spins bounded. Both cannot be linked -- they define the same symbols.
CY_IGNORE+=$(SEARCH_sensor-xensiv-bgt60trxx)/xensiv_bgt60trxx_edge.c
endif

################################################################################
# Phase 2: LVGL Display Module
################################################################################

# LVGL display module includes
# Per-project firmware version header (bentoclaw_version_project.h — picked up by the
# shared bentoclaw_version.h via __has_include; single source for on-screen version +
# CM33 boot banner + .fw_identity record + GitHub release tag).
INCLUDES+=tesaiot_version
INCLUDES+=modules/lvgl_display/core

# Real game sprites for the MicroPython firmware (ui.Sprite + bentogame pixel
# art): strong impls of the three weak hooks in shared claw ipc_ui/widget_mgr
# plus the sprite assets, rsynced from the Game project (see the module header).
# ui.Sprite / bentogame sprite engine — strong overrides of the weak hooks in
# the archived widget manager, driven from MicroPython's modui over IPC. An
# MPY feature by definition: with no VM nobody can reach it, so mtb-only
# neither compiles it nor ships it (the weak fallbacks in the archive stand).
ifneq ($(BENTO_HAS_MPY),0)
INCLUDES+=modules/game_sprites_mpy
INCLUDES+=modules/game_sprites_mpy/game_sprites
SOURCES+=modules/game_sprites_mpy/game_sprite_engine.c
SOURCES+=modules/game_sprites_mpy/game_sprite_registry.c
SOURCES+=$(wildcard modules/game_sprites_mpy/game_sprites/*.c)
endif
INCLUDES+=modules/lvgl_display/controller
# QWA309 base-board CM55-local sensor polling (pots x4 + CapSense-4000T)
INCLUDES+=modules/cm55_sensor_poll

# Single strong ipc_ui_ext_dispatch() that fans UI-band extension opcodes out to
# each project feature handler (DFR0522 RGB matrix + audio SFX). Always compiled
# so exactly one strong override of the shared weak hook exists; the feature
# handlers it chains to are weak-defaulted, so any feature combination links.
INCLUDES+=modules/ipc_ui_ext

# Motor Shield v2 service: single owner of the shield on this core, shared by
# the Motor Controller page and by MicroPython's IPC bridge. Present whenever
# the Arduino header exists, independently of whether the page is built --
# `from shield import motor_v2` must work without opening a screen.
ifeq ($(BSP_HAS_ARDUINO_HEADER),1)
ifeq ($(ENABLE_PAGE_MOTOR_CTRL),1)
INCLUDES+=modules/motor_service
endif
# Shield stack from the shared library. arduino_shield is the header
# abstraction; shield_motor_v2 is the PCA9685 + TB6612 driver; the QWA309
# descriptor supplies this board's pin table and its PDL half the I2C.
INCLUDES+=$(BENTO_COMMON)/arduino_shield
INCLUDES+=$(BENTO_COMMON)/shield_motor_v2
SOURCES+=$(BENTO_COMMON)/arduino_shield/arduino_shield.c
SOURCES+=$(BENTO_COMMON)/shield_motor_v2/pca9685.c
SOURCES+=$(BENTO_COMMON)/shield_motor_v2/motor_shield_v2.c
SOURCES+=$(BENTO_LIBS_DIR)/kit-tesaiot-pse84-ai/arduino_shield_qwa309.c
SOURCES+=$(BENTO_LIBS_DIR)/kit-tesaiot-pse84-ai/arduino_shield_qwa309_hal.c
endif

# QWA309 base-board DFR0522 16x8 RGB matrix (I2C 0x10 on the display bus).
# Runs on CM55 (owner of the display SCB). Auto-globbed .c files are excluded
# via CY_IGNORE when the base board is absent, so flag=0 builds are unaffected.
# NOTE: keep the flag value bare (1) — trailing whitespace silently breaks ifeq.
INCLUDES+=modules/dfr0522_rgb
ifneq ($(BSP_HAS_QWA309_BASEBOARD),1)
CY_IGNORE+=modules/dfr0522_rgb
endif

# QWA309 / AI Kit audio: TLV320DAC3100 codec (U28, DAC + Class-D amp) + bento
# audio/SFX engine, out the J8 speaker connector. TDM0 pins (P12.3 WS / P21.1 SD
# / P21.2 SCK / P21.3 MCK) are already routed by the AI Kit design.modus.
# ui.sfx()/ui.tone() then produce sound (chiptune DDS + PCM one-shots on CM55).
# The SD-file players (wav/mp3/tsf/midi) are excluded — SFX path is RAM/QSPI only.
ifeq ($(BSP_HAS_AUDIO_CODEC),1)
INCLUDES+=modules/audio_player
DEFINES+=BENTO_AUDIO_NO_FILE_SOURCES
# The TLV320DAC3100 driver is fetched, not vendored: deps/audio-codec-
# tlv320dac3100.mtb. We used to carry a byte-identical copy taken from an MTB
# code example whose licence permits "copy, modify, and compile" and no
# distribution; Infineon later re-released the same driver as a standalone
# Apache-2.0 asset with an SPDX tag on every file, which is the one we now use.
# No SOURCES+= or INCLUDES+= is needed -- MTB auto-discovers an asset's .c and
# puts its directory on the include path via SEARCH_MTB_MK, so bento_audio.c's
# #include "mtb_tlv320dac3100.h" resolves unchanged.
#
# The asset makes the DAC processing block overridable and defaults it to
# PRB_P1; the copy we carried hardcoded PRB_P17. Keep P17 -- changing the
# interpolation/filter chain is not part of a licence fix.
DEFINES+=TLV320DAC3100_PROCESSING_BLOCK=17
CY_IGNORE+=modules/audio_player/bento_wav.c
CY_IGNORE+=modules/audio_player/bento_mp3.c
CY_IGNORE+=modules/audio_player/bento_tsf.c
CY_IGNORE+=modules/audio_player/bento_midi.c
CY_IGNORE+=modules/audio_player/bento_sfx_table.c
else
# Flag off: keep MTB auto-glob from compiling the audio tree -- including the
# fetched asset, which auto-discovery would otherwise still compile.
CY_IGNORE+=modules/audio_player
CY_IGNORE+=$(SEARCH_audio-codec-tlv320dac3100)
endif

# Waveshare 4.3" DSI (800x480) + FT5406 touch (direct, no IPC)
DEFINES+=MTB_DISPLAY_W4P3INCH_RPI MTB_CTP_FT5406
DEFINES+=CM55_TOUCH_INPUT_ENABLE=1

# Board-specific I2C display controller mapping
DEFINES+=USE_KIT_PSE84_AI
# BMI270 at +/-8g so the DEEPCRAFT motion model sees un-clipped gestures.
# Must match proj_cm33_ns/Makefile (both cores share the wire scale). Other
# boards leave this undefined and stay at +/-2g.
DEFINES+=BENTO_BMI270_ACCEL_8G

# Ignore stale lv_port_indev.c in modules/ — proj_cm55/lv_port_indev.c is canonical
CY_IGNORE+=modules/lvgl_display/core/lv_port_indev.c

# Ignore old raw RGB565 ID card image — replaced by PNG-compressed img_id_card_png.c
CY_IGNORE+=modules/page-components/smart_card/img_id_card.c

# Ignore unused display/touch drivers that may be in mtb_shared
CY_IGNORE+=$(SEARCH_display-dsi-waveshare-7-0-lcd-c)
CY_IGNORE+=$(SEARCH_touch-ctp-gt911)
CY_IGNORE+=$(SEARCH_display-tft-ek79007ad3)
CY_IGNORE+=$(SEARCH_touch-ctp-ili2511)

# Ignore unused LVGL middleware files (reduce compile time)
# Custom overrides (BENTO has its own versions in modules/lvgl_display/core/)
CY_IGNORE+=$(SEARCH_lvgl)/src/draw/vg_lite/lv_draw_vg_lite.c $(SEARCH_lvgl)/src/draw/vg_lite/lv_vg_lite_utils.c
CY_IGNORE+=$(SEARCH_lvgl)/src/draw/vg_lite/lv_draw_vg_lite_img.c $(SEARCH_lvgl)/src/core/lv_refr.c
# VG-Lite ThorVG simulator (using real VG-Lite GPU) — moved in 9.5
CY_IGNORE+=$(SEARCH_lvgl)/src/debugging/vg_lite_tvg
# ThorVG library — ENABLED for Lottie Animation support
# CY_IGNORE+=$(SEARCH_lvgl)/src/libs/thorvg
# ThorVG rapidjson MSVC-only headers (active in 9.5, were .disabled in 9.4)
CY_IGNORE+=$(SEARCH_lvgl)/src/libs/thorvg/rapidjson/msinttypes
# SIMD blend (using ARM-2D + Helium ASM instead)
CY_IGNORE+=$(SEARCH_lvgl)/src/draw/sw/blend/helium $(SEARCH_lvgl)/src/draw/sw/blend/neon
CY_IGNORE+=$(SEARCH_lvgl)/src/draw/sw/blend/riscv_v
# Draw backends not used on PSoC E84
CY_IGNORE+=$(SEARCH_lvgl)/src/draw/dma2d
CY_IGNORE+=$(SEARCH_lvgl)/src/draw/espressif
CY_IGNORE+=$(SEARCH_lvgl)/src/draw/eve
CY_IGNORE+=$(SEARCH_lvgl)/src/draw/nema_gfx
CY_IGNORE+=$(SEARCH_lvgl)/src/draw/opengles
CY_IGNORE+=$(SEARCH_lvgl)/src/draw/nxp
CY_IGNORE+=$(SEARCH_lvgl)/src/draw/renesas
CY_IGNORE+=$(SEARCH_lvgl)/src/draw/nanovg
CY_IGNORE+=$(SEARCH_lvgl)/src/draw/snapshot
CY_IGNORE+=$(SEARCH_lvgl)/src/draw/convert/helium $(SEARCH_lvgl)/src/draw/convert/neon
# Display drivers not used
CY_IGNORE+=$(SEARCH_lvgl)/src/drivers/display/ft81x
CY_IGNORE+=$(SEARCH_lvgl)/src/drivers/display/lovyan_gfx
CY_IGNORE+=$(SEARCH_lvgl)/src/drivers/display/nv3007
CY_IGNORE+=$(SEARCH_lvgl)/src/drivers/display/nxp_elcdif
CY_IGNORE+=$(SEARCH_lvgl)/src/drivers/display/st_ltdc
CY_IGNORE+=$(SEARCH_lvgl)/src/drivers/draw
CY_IGNORE+=$(SEARCH_lvgl)/src/drivers/opengles
CY_IGNORE+=$(SEARCH_lvgl)/src/drivers/uefi
# Libraries not used
CY_IGNORE+=$(SEARCH_lvgl)/src/libs/FT800-FT813
CY_IGNORE+=$(SEARCH_lvgl)/src/libs/frogfs
CY_IGNORE+=$(SEARCH_lvgl)/src/libs/gltf
CY_IGNORE+=$(SEARCH_lvgl)/src/libs/gstreamer
CY_IGNORE+=$(SEARCH_lvgl)/src/libs/svg
CY_IGNORE+=$(SEARCH_lvgl)/src/libs/vg_lite_driver
CY_IGNORE+=$(SEARCH_lvgl)/src/libs/libwebp
CY_IGNORE+=$(SEARCH_lvgl)/src/libs/nanovg
# Others / font / debugging not used (paths restructured in 9.5)
CY_IGNORE+=$(SEARCH_lvgl)/src/font/font_manager
CY_IGNORE+=$(SEARCH_lvgl)/src/debugging/test
CY_IGNORE+=$(SEARCH_lvgl)/src/others/translation
CY_IGNORE+=$(SEARCH_lvgl)/src/stdlib/uefi
# Widgets not used
CY_IGNORE+=$(SEARCH_lvgl)/src/widgets/3dtexture
CY_IGNORE+=$(SEARCH_lvgl)/src/widgets/arclabel
# Tests, demos, examples
CY_IGNORE+=$(SEARCH_lvgl)/tests $(SEARCH_lvgl)/demos $(SEARCH_lvgl)/examples

# Ignore CM55 retarget-io driver (CM33_NS owns UART for MicroPython REPL)
CY_IGNORE+=driver
# Keep imported Face runtime sources out of the stable SensorHub image.
CY_IGNORE+=modules/face_id_real

################################################################################
# Edge AI runtime (BENTO_HAS_EDGE_AI) — TFLite-Micro + Ethos-U55
#
# ml-middleware provides mtb_ml_model_* used by DEEPCRAFT-generated models;
# ml-tflite-micro ships the prebuilt interpreter (COMPONENT_U55/TOOLCHAIN_GCC_ARM
# exists, so our GCC toolchain is supported). Model weights and the tensor
# arena MUST live in SOCMEM for U55 DMA reach — see CY_ML_*_MEM below.
# Config mirrors Infineon's mtb-example-psoc-edge-ml-deepcraft-deploy-motion.
################################################################################
ifeq ($(BENTO_HAS_EDGE_AI),1)
# ONE model per firmware, chosen at build time (the kit loads one model at a
# time, so each model ships as its own image). This sidesteps two hard problems
# that a multi-model image has: (1) MTB_ML_DATA_T is a single global type, so
# int8 and float32 models cannot coexist; (2) runtime model switching over the
# best-effort IPC link is not yet reliable. Each variant carries exactly one
# model and its matching ML data-type component.
#
#   make build                         (default) combo — the full six-model menu
#   make build EDGE_AI_MODEL=motion    IMU int8, no DSP
#   make build EDGE_AI_MODEL=radar     radar float32, no DSP
#   make build EDGE_AI_MODEL=fall      IMU        (agent-added)
#   make build EDGE_AI_MODEL=gesture   radar      (agent-added)
#   make build EDGE_AI_MODEL=audio     PDM int8 net + f32 CMSIS-DSP front end
#
# Default = combo: a plain build must carry the menu this board ships with.
# It used to default to a single model, so anyone rebuilding without passing
# the flag — including the release pipeline — silently produced a one-model
# image while the board's Edge AI page advertises six. Same lesson as the NES
# menu. Opt down explicitly when a single-model image is what you want.
EDGE_AI_MODEL ?= combo
# The models compiled into THIS image. Single-model images carry one; "combo"
# carries every int8 model (motion + audio) with the Edge AI menu switching
# between them at run time (they share MTB_ML_DATA_T = int8; float32 radar cannot
# join). Everything below (source selection, ML component, DSP, weight section)
# derives from this list, so adding a model to a combo is a one-word change here.
# The combo carries every model, int8 AND float32 together. Verified against the
# prebuilt TFLM library: libtensorflow-microlite.a is selected by NPU+toolchain
# only (COMPONENT_U55/TOOLCHAIN_GCC_ARM) and contains BOTH the int8 and float
# kernels, so the ML_INT8x8/ML_FLOAT32 components gate nothing but the
# middleware's MTB_ML_DATA_T pointer typedef (its run path is runtime-typed).
# The float32 radar net therefore executes correctly in this int8-typed image
# (on CPU float kernels; the int8 nets use the U55). The on-board radar failure
# that once looked like a kernel mismatch was actually an unbounded seqlock spin
# in radar_ai_frame_snapshot (reader prio 4 starving writer prio 3 — fixed with
# bounded retries in Claw radar_task.c).
ifeq ($(EDGE_AI_MODEL),combo)
# cough/alarm/siren are Infineon DEEPCRAFT audio READY-MODELS, each shipped as a
# prebuilt eval library (modules/ai_models/*_lib_eval.a) — no source. cough +
# alarm are int8, siren is float32; all three carry a self-contained float IMAI_*
# API and their own FFT/log-mel front end, and reuse the audio PDM path, so they
# co-reside with motion/audio/radar in the combo. Every ready-model .a exports
# the SAME unprefixed IMAI_* symbols, so each committed .a is objcopy-prefixed to
# a unique per-model namespace (recipe under "ready-model" below) and linked via
# LDLIBS (each guarded on its model being in AI_MODELS).
AI_MODELS := motion audio radar cough alarm siren
else
AI_MODELS := $(EDGE_AI_MODEL)
endif
DEFINES+=TF_LITE_STATIC_MEMORY
DEFINES+=ML_DEEPCRAFT_CM55
# Model weights (readonly) default to .cy_socmem_data, whose flash LMA sits
# below the .fw_identity wall at 0x60900000. A large model (audio, ~103 KB)
# overruns that wall, so it places its weights in .ml_weights instead — the CM55
# .ld loads that section into the free 2.5 MB tail of m55_nvm ABOVE the wall and
# copies it to SOCMEM at boot. The tensor arena stays in .cy_socmem_data.
# The large audio weights go above the .fw_identity wall; if audio is in this
# image (single or combo) every model's weights use .ml_weights (they all fit in
# the 2.5 MB tail), otherwise the small models stay in .cy_socmem_data.
ifneq ($(filter audio,$(AI_MODELS)),)
DEFINES+=CY_ML_MODEL_MEM=.ml_weights
else
DEFINES+=CY_ML_MODEL_MEM=.cy_socmem_data
endif
# Tensor arena placement. Normally the arenas (audio 46 KB + motion 23 KB + radar
# 22 KB) stay in .cy_socmem_data below the wall. But the 6-model combo also links
# three ready-model eval .a's (cough + alarm + siren), whose ~37 KB of FFT/cbuffer
# .text below the wall raises the .cy_socmem_data LOAD address until its ~125 KB
# (USB config + those arenas) crosses the .fw_identity wall at 0x60900000
# ("section .fw_identity LMA overlaps .cy_socmem_data"). Move the arenas up into
# .ml_weights for the combo — same above-wall mechanism the weights use — which
# frees ~90 KB below the wall. Single-model and the 4-model combo fit as before.
ifeq ($(EDGE_AI_MODEL),combo)
DEFINES+=CY_ML_ARENA_MEM=.ml_weights
else
DEFINES+=CY_ML_ARENA_MEM=.cy_socmem_data
endif
DEFINES+=BENTO_HAS_EDGE_AI=1
# One EDGE_AI_MODEL_<name> per model in the image, so every translation unit
# (ai_engine.c, audio_pdm.c, the generated model_*.c) gates on the same compile
# defines. A combo image defines several (e.g. EDGE_AI_MODEL_motion AND
# EDGE_AI_MODEL_audio); a single image defines exactly one.
DEFINES+=$(foreach m,$(AI_MODELS),EDGE_AI_MODEL_$(m))
COMPONENTS+=U55 ML_TFLM

# --- Bound the Ethos-U55 completion wait --------------------------------------
# ethosu_wait() blocks on the NPU completion interrupt with
# ETHOSU_SEMAPHORE_WAIT_INFERENCE, which ethosu_driver.h:45-47 defaults to
# ETHOSU_SEMAPHORE_WAIT_FOREVER (UINT64_MAX). ml-middleware maps that onto
# cy_rtos_get_semaphore(sem, 0xFFFFFFFF) == portMAX_DELAY, so one missed NPU
# completion blocks the inference task for good: CM55 keeps drawing, inference
# never resumes, and machine.reset() cannot help because it resets CM33 only.
# That is the "power cycle to get Edge AI back" symptom.
#
# ethosu_driver.c is compiled inside this project (COMPONENTS+=U55 above, object
# under build/.../third_party/COMPONENT_U55/), and the macro is #ifndef-guarded,
# so a project define reaches it.
#
# What the bound actually buys, stated exactly: on timeout the driver's own
# recovery is dead code — ethosu_driver.c:563 tests `ret < 0`, but
# mtb_ml_sem_take (mtb_ml_rtos.c:144-160) returns cy_rslt_t, and CY_RTOS_TIMEOUT
# is positive. So job.result is never set to TIMEOUT and ethosu_wait returns 0
# with a stale output tensor. The inference is wrong for that frame — and the
# task is ALIVE and still switchable, instead of gone until the board is
# unplugged. A wrong verdict that recovers beats a dead engine that does not.
DEFINES+=ETHOSU_SEMAPHORE_WAIT_INFERENCE=2000ULL
INCLUDES+=modules/ai_models
INCLUDES+=modules/page-components/edge_ai

# Compile ONLY the selected model's generated source. Every model_<name>.c in
# modules/ai_models hard-#errors unless its own ML data-type COMPONENT
# (ML_INT8x8 / ML_FLOAT32) is defined, and exactly one such component exists per
# image (MTB_ML_DATA_T is a single global type). Ignore every model_*.c except
# model_$(EDGE_AI_MODEL).c so a second model dropped in the directory never
# breaks the default build. ai_engine.c stays; it is not a model_*.c.
AI_MODEL_KEEP := $(foreach m,$(AI_MODELS),modules/ai_models/model_$(m).c)
CY_IGNORE+=$(filter-out $(AI_MODEL_KEEP),$(wildcard modules/ai_models/model_*.c))

# audio_pdm.c is the PDM microphone front end, needed when ANY mic model is in
# the image — Baby Cry (audio) or the Cough eval ready-model (cough). It is not a
# model_*.c, so the KEEP filter above does not cover it; exclude it only when no
# mic model is present.
ifeq ($(filter audio cough alarm siren,$(AI_MODELS)),)
CY_IGNORE+=modules/ai_models/audio_pdm.c
endif

# --- per-model data type + DSP -------------------------------------------------
# CMSIS_DSP is NOT pulled in for the raw-sample models: GCC 14.2.1 ICEs on the
# CMSIS-DSP f16 Helium intrinsics (arm_clip_f16.c "unrecognizable insn"), which
# is why Infineon's own examples $(error) on GCC Release. Only the audio variant
# needs an FFT front end and must CY_IGNORE those f16/q15 sources.
# ML data-type component — exactly one per image (MTB_ML_DATA_T is global).
# The single-radar image uses ML_FLOAT32 (its network is float32); every other
# image, INCLUDING the combo that carries radar beside the int8 motion/audio
# nets, uses ML_INT8x8. That mix is safe because the middleware run path is
# runtime-typed (raw memcpy of input->bytes + switch on output_type_size);
# model_radar.c carries the documented guard/cast deviation for it. The audio
# model is int8 (its wrapper quantizes the f32 features before mtb_ml_model_run).
ifeq ($(EDGE_AI_MODEL),radar)
COMPONENTS+=ML_FLOAT32
else
COMPONENTS+=ML_INT8x8
endif

# --- DEEPCRAFT audio ready-models (prebuilt eval .a, multi-.a coexistence) -----
# Infineon ships DEEPCRAFT ready-models as prebuilt eval libraries (no source):
#   cough  -> cough_lib_eval.a   (int8,    classes {unlabelled, cough})
#   alarm  -> alarm_lib_eval.a   (int8,    classes {unlabelled, alarm})   [Infineon alarm_siren_lib_eval.a]
#   siren  -> siren_lib_eval.a   (float32, classes {unlabelled, sirens})
# Each exports the generic Imagimob entry points wired in ai_engine.c's *_ROW.
# They carry their OWN FFT/log-mel front end, so they need NO project CMSIS-DSP;
# they pull mtb_ml_* (ml-middleware source) + libm (resolved from the C library).
# int8 nets use the U55; the siren float net runs on the CPU float kernels (the
# prebuilt TFLM lib carries both), exactly like the float32 radar net does in the
# int8-typed combo — no extra ML_FLOAT32 COMPONENT is needed for a self-contained
# ready-model .a.
#
# SYMBOL-COLLISION PROBLEM + FIX (the reason each committed .a is post-processed):
# EVERY pristine ready-model .a exports the SAME unprefixed public API
# (IMAI_init/_enqueue/_dequeue/_finalize) AND the same internal defined globals
# (mtb_init, runs, adjust_sensitivity, number_of_runs, conf_config, conf_exit,
# IMAI_AED_*, IMAI_mtb_models[_count][_profile_log]). Linking two pristine .a's
# fails with "multiple definition of IMAI_init". To co-reside THREE, each .a is
# objcopy-processed into a UNIQUE per-model namespace so `nm` shows all three
# disjoint (verified: zero shared defined globals). The .a's mtb_init rename also
# clears its old clash with model_radar.c's leaked global mtb_init.
#
# Recipe — regenerate a committed <model>_lib_eval.a from the pristine
# ready-model .a (Infineon deploy-ready-model, CONFIG_Debug/TOOLCHAIN_GCC_ARM):
#  1. Build a redefine-syms map <suffix>.map. For the 4 public entries:
#        IMAI_init IMAI_<MODEL>_init   (+ enqueue/dequeue/finalize)
#     For EVERY other defined global S in the .a (nm '[TDBRGCSW]'), append the
#     model suffix:  S  S_<suffix>   e.g.  mtb_init mtb_init_cough
#  2. objcopy MUST run on the EXTRACTED objects, not the archive: archive-mode
#     objcopy silently refuses to rename a section to a name that already exists
#     (so a 2nd/3rd .rodata._Kxx -> .ml_weights is dropped). Extract, process each
#     member, re-archive:
#        arm-none-eabi-ar x <pristine>.a                       # -> library.o model.o
#        for o in library.o model.o; do
#          arm-none-eabi-objcopy --redefine-syms=<suffix>.map \
#            --rename-section .cy_socmem_data=.ml_weights \
#            --rename-section .rodata._K11=.ml_weights \
#            --rename-section .rodata._K24=.ml_weights \   # alarm: _K23 ; siren: none
#            $o $o.new && mv $o.new $o
#        done
#        arm-none-eabi-ar rcs <model>_lib_eval.a library.o model.o
#  3. Weights (.cy_socmem_data: cough ~117 KB, alarm ~74 KB, siren ~136 KB) and the
#     FFT const tables (.rodata._K11/_K24/_K23) default BELOW the .fw_identity wall
#     at 0x60900000 and overran it in the combo, so ALL are renamed to .ml_weights
#     — the CM55 .ld loads that into the free m55_nvm tail ABOVE the wall and
#     copies it to SOCMEM at boot (same mechanism model_audio.c uses via
#     CY_ML_MODEL_MEM=.ml_weights). Verify: `objdump -h` shows ONLY .ml_weights
#     (no .cy_socmem_data / .rodata._K*), and the three .a's share no defined global.
# NO READY-MODEL ARCHIVE SHIPS WITH THIS SDK. The three named above are
# Imagimob evaluation binaries and are not redistributable, so cough/alarm/siren
# are EMPTY SLOTS in a released package: modules/ai_models/ai_model_slots.c
# holds a weak "slot not filled" stub for every slot, the image links and boots,
# and an unfilled model reports that it did not load while every other model
# runs. See modules/ai_models/README.md, "Filling a model slot".
#
# Drop a real archive into modules/ai_models/ as <model>_lib*.a — a Ready Model
# you have licensed, a DEEPCRAFT Studio export, or your own — and the rules
# below link it with no edit here. The wildcard is evaluated at parse time, so a
# newly added .a needs a fresh `make`, not a clean.
AI_MODEL_LIB_MODELS := $(strip $(foreach m,$(AI_MODELS),\
                         $(if $(wildcard ./modules/ai_models/$(m)_lib*.a),$(m))))
AI_MODEL_LIBS       := $(foreach m,$(AI_MODEL_LIB_MODELS),\
                         $(wildcard ./modules/ai_models/$(m)_lib*.a))

# EDGE_AI_IGNORE_MODEL_LIBS=1 builds the configuration a CUSTOMER gets — every
# slot unfilled — even on a bench where the evaluation archives are present.
# Without it this tree can never compile its own stub path (the three .a files
# are committed here, so the wildcard always matches and always drops the
# stubs), and an error in that path would ship unbuilt. Use it in CI:
#
#     make build EDGE_AI_IGNORE_MODEL_LIBS=1
#
ifeq ($(EDGE_AI_IGNORE_MODEL_LIBS),1)
AI_MODEL_LIB_MODELS :=
AI_MODEL_LIBS       :=
endif

LDLIBS  += $(AI_MODEL_LIBS)
# Compile a slot's stub OUT when a real archive fills it. This is not belt and
# braces, it is the load-bearing half: the stub is a WEAK definition, and GNU ld
# does not extract an archive member to replace a definition it already has,
# even a weak one (measured, arm-none-eabi-ld 14.2.1). Left in the link the stub
# would win silently and the model would never run. A model supplied as a .c or
# .o needs no such rule — a strong definition in an object file does override a
# weak one.
DEFINES += $(foreach m,$(AI_MODEL_LIB_MODELS),AI_SLOT_PROVIDED_$(m))

# A model archive in this directory is linked whether or not the rules above
# name it: core-make collects every .a under the application into
# MTB_CORE__SEARCH_APP_LIBS and puts it on the link
# (mtb_shared/core-make/release-v3.8.0/make/core/search_filter_v1.mk:49-53,
# consumed by build_v2.mk:212 and ninja.mk:171).
#
# That matters here. An archive NOT matching <model>_lib*.a still reaches the
# linker but does NOT switch its slot's stub off, so the weak stub wins and the
# model silently never runs — the exact failure the AI_SLOT_PROVIDED_ rule
# exists to prevent. Refuse the build rather than let it happen quietly.
#
# Only the NAMING CONVENTION is checked, not membership of AI_MODELS: an archive
# for a model this image does not build is simply unused, which is fine and must
# not block a legitimate subset build such as AI_MODELS="motion radar".
AI_MODEL_LIB_STRAY := $(filter-out $(wildcard ./modules/ai_models/*_lib*.a),\
                        $(wildcard ./modules/ai_models/*.a))
ifneq ($(AI_MODEL_LIB_STRAY),)
$(error Edge AI: $(AI_MODEL_LIB_STRAY) does not match <model>_lib*.a. MTB will \
link it anyway, but its slot stub would stay in the build and silently win over \
it. Rename it to <model>_lib.a, where <model> is the slot it fills. See \
modules/ai_models/README.md, "Filling a model slot")
endif

ifneq ($(filter audio,$(AI_MODELS)),)
# audio's FFT front end: pull CMSIS-DSP but CY_IGNORE the f16/q15 Helium sources
# that ICE GCC 14.2.1 (arm_clip_f16.c "unrecognizable insn"). The Baby Cry model
# only uses the f32 subset (arm_rfft_fast_f32 / arm_cmplx_mag_f32 / arm_vlog_f32
# / arm_scale_f32 / arm_mult_f32 / arm_dot_prod_f32 / arm_clip_f32), so dropping
# every *_f16.c and *_q15.c leaves a cleanly-compiling front end.
#
# CMSIS_DSP is a COMPONENT of the Infineon `cmsis` library (release-v6.1.0);
# there is NO getlibs `cmsis-dsp` here, so $(SEARCH_cmsis-dsp) was empty and the
# old glob ignored nothing (the ICE would have slipped through). The real
# sources live under $(SEARCH_cmsis)/COMPONENT_CMSIS_DSP/Source/*/ — verified
# against mtb_shared (arm_clip_f16.c is there).
COMPONENTS+=CMSIS_DSP
# ARM_MATH_AUTOVECTORIZE is the real fix for GCC 14.2.1 on CMSIS-DSP: it disables
# the hand-written Helium (MVE) intrinsic code paths and lets the compiler
# autovectorize plain C instead. Those intrinsic paths are exactly what ICE'd
# ("unrecognizable insn" in arm_clip_f16.c) and what hit GCC-14 vector type
# errors in the f32 files (arm_mat_ldlt_f32.c: int32x4_t vs uint32x4_t). This is
# precisely how Infineon's own deploy-audio reference builds CMSIS-DSP on this
# toolchain (proj_cm55/Makefile: ARM_MATH_HELIUM ARM_MATH_DSP ARM_MATH_AUTOVECTORIZE).
DEFINES+=ARM_MATH_HELIUM ARM_MATH_DSP ARM_MATH_AUTOVECTORIZE
# Belt-and-braces + faster compile: the Baby Cry model uses only the f32 subset,
# so drop the f16/q15 sources entirely (also removes any residual scalar-f16
# risk). Inline $(SEARCH_cmsis) — it is defined in libs/mtb.mk (included by
# start.mk AFTER this file's body); CY_IGNORE is recursively expanded, so the
# $(wildcard) resolves later, once SEARCH_cmsis and the files both exist.
CY_IGNORE+=$(wildcard $(SEARCH_cmsis)/COMPONENT_CMSIS_DSP/Source/*/*_f16.c)
CY_IGNORE+=$(wildcard $(SEARCH_cmsis)/COMPONENT_CMSIS_DSP/Source/*/*_q15.c)
endif
else
CY_IGNORE+=$(SEARCH_ml-middleware)
CY_IGNORE+=$(SEARCH_ml-tflite-micro)
CY_IGNORE+=modules/ai_models
CY_IGNORE+=modules/page-components/edge_ai
endif

################################################################################
# Page Component CY_IGNORE (exclude disabled page sub-directories)
################################################################################
ifneq ($(ENABLE_PAGE_MOTION),1)
CY_IGNORE+=modules/page-components/motion
endif
ifneq ($(ENABLE_PAGE_ENVIRON),1)
CY_IGNORE+=modules/page-components/environ
endif
ifneq ($(ENABLE_PAGE_CONTROLS),1)
CY_IGNORE+=modules/page-components/controls
endif
ifneq ($(ENABLE_PAGE_JOYSTICK),1)
CY_IGNORE+=modules/page-components/joystick
endif
ifneq ($(ENABLE_PAGE_SMART_CARD),1)
CY_IGNORE+=modules/page-components/smart_card
endif
ifneq ($(ENABLE_PAGE_HSM),1)
CY_IGNORE+=modules/page-components/hsm
endif
ifneq ($(ENABLE_PAGE_BENTOCLAW),1)
CY_IGNORE+=modules/page-components/bentoclaw
endif
ifneq ($(ENABLE_PAGE_BENTO_BUDDY),1)
CY_IGNORE+=modules/page-components/bento_buddy
endif
ifneq ($(ENABLE_PAGE_ANIMATION),1)
CY_IGNORE+=modules/page-components/animation
endif
ifneq ($(ENABLE_PAGE_FACE_ID),1)
CY_IGNORE+=modules/page-components/face_id
endif
ifneq ($(ENABLE_PAGE_SMART_WATCH),1)
CY_IGNORE+=modules/page-components/smart_watch
endif
ifneq ($(ENABLE_PAGE_SPECTRUM),1)
CY_IGNORE+=modules/page-components/spectrum_analyzer
endif
ifneq ($(ENABLE_PAGE_WIFI_CONNECT),1)
CY_IGNORE+=modules/page-components/wifi_connect
endif
ifneq ($(ENABLE_PAGE_TESAIOT_CONNECT),1)
CY_IGNORE+=modules/page-components/tesaiot_connect
endif
ifneq ($(ENABLE_PAGE_GPIO_RGB),1)
CY_IGNORE+=modules/page-components/gpio_rgb
endif
ifneq ($(ENABLE_PAGE_MOTOR_CTRL),1)
CY_IGNORE+=modules/page-components/motor_ctrl
endif
# Games — individual CY_IGNORE per game; game_common excluded only if ALL games off
ifneq ($(ENABLE_PAGE_GAME_SNAKE),1)
CY_IGNORE+=modules/page-components/games/page_game_snake.c
endif
ifneq ($(ENABLE_PAGE_GAME_FLAPPY),1)
CY_IGNORE+=modules/page-components/games/page_game_flappy.c
endif
ifneq ($(ENABLE_PAGE_GAME_PONG),1)
CY_IGNORE+=modules/page-components/games/page_game_pong.c
endif
ifneq ($(ENABLE_PAGE_GAME_SHOOTER),1)
CY_IGNORE+=modules/page-components/games/page_game_shooter.c
endif
# Exclude game_common if ALL games are disabled
ENABLE_ANY_GAME:=$(or $(filter 1,$(ENABLE_PAGE_GAME_SNAKE)),$(filter 1,$(ENABLE_PAGE_GAME_FLAPPY)),$(filter 1,$(ENABLE_PAGE_GAME_PONG)),$(filter 1,$(ENABLE_PAGE_GAME_SHOOTER)))
ifeq ($(ENABLE_ANY_GAME),)
CY_IGNORE+=modules/page-components/games
endif

################################################################################
# IPC Communication (CM33 ↔ CM55 via Cypress IPC Pipe)
################################################################################

SOURCES+=$(wildcard $(BENTO_COMMON)/shared/source/COMPONENT_CM55/*.c)
INCLUDES+=$(BENTO_COMMON)/shared/include

# Shared Waveshare 4.3" cold-boot backlight bring-up (deterministic power cycle,
# modeled on Linux rpi-panel-attiny-regulator.c). ONE implementation for every
# project driving this panel — common/display/ws_panel_power.{c,h}. MUST be here
# at conditional depth 0 (an earlier attempt inside the ENABLE_OPTIGA ifeq was
# silently skipped). SEARCH auto-discovers the .c; INCLUDES adds the header -I.
SEARCH+=$(BENTO_COMMON)/display
INCLUDES+=$(BENTO_COMMON)/display
# BENTO shared modules (source of truth: BENTO-TESAIoT-libraries/common/modules/)
INCLUDES+=$(BENTO_COMMON)/modules/ipc_lcd
# ipc_lcd: compiled into libbento_ipc.a, not from source

# Thai text rendering library — PUA-based cluster shaping for LVGL bitmap
# fonts. Single include pulls in: L1 thai_to_pua(), L2 thai_label_set_text(),
# and 5 NotoSansThai bitmap fonts (14/16/20/24/28 px) with pre-composed
# PUA glyphs for 2-tier diacritic stacks. See thai_text/README.md.
include $(BENTO_COMMON)/thai_text/thai_text.mk

# proj_cm55 root for lv_port_indev.h (needed by ipc_sensorhub touch IPC handlers)
INCLUDES+=.

INCLUDES+=$(BENTO_COMMON)/modules/ipc_sensorhub
# ipc_sensorhub: compiled into libbento_ipc.a, not from source

INCLUDES+=$(BENTO_COMMON)/modules/ipc_ui
# ipc_ui: compiled into libbento_ipc.a, not from source

# Page Components — core (always included)
INCLUDES+=modules/page-components/_core

# Page Component sub-directories — conditional INCLUDES for header resolution
ifeq ($(ENABLE_PAGE_MOTION),1)
INCLUDES+=modules/page-components/motion
endif
ifeq ($(ENABLE_PAGE_ENVIRON),1)
INCLUDES+=modules/page-components/environ
endif
ifeq ($(ENABLE_PAGE_CONTROLS),1)
INCLUDES+=modules/page-components/controls
endif
ifeq ($(ENABLE_PAGE_GPIO_RGB),1)
INCLUDES+=modules/page-components/gpio_rgb
endif
ifeq ($(ENABLE_PAGE_MOTOR_CTRL),1)
INCLUDES+=modules/page-components/motor_ctrl
endif
ifeq ($(ENABLE_PAGE_JOYSTICK),1)
INCLUDES+=modules/page-components/joystick
endif
ifeq ($(ENABLE_PAGE_SMART_CARD),1)
INCLUDES+=modules/page-components/smart_card
endif
ifeq ($(ENABLE_PAGE_HSM),1)
INCLUDES+=modules/page-components/hsm
endif
ifeq ($(ENABLE_PAGE_BENTOCLAW),1)
INCLUDES+=modules/page-components/bentoclaw
endif
ifeq ($(ENABLE_PAGE_BENTO_BUDDY),1)
INCLUDES+=modules/page-components/bento_buddy
# Lottie runtime renderer + baked-in default TESAIoT BENTO pack.
# Source files live under the shared ble_nus module tree (next to
# nus_protocol/nus_commands) because they reference bento_buddy_state_t.
SOURCES+=$(BENTO_COMMON)/ble_nus/character_lottie.c
SOURCES+=$(BENTO_COMMON)/ble_nus/character_lottie_default.c
INCLUDES+=$(BENTO_COMMON)/ble_nus
endif
ifeq ($(ENABLE_PAGE_ANIMATION),1)
INCLUDES+=modules/page-components/animation
endif
ifeq ($(ENABLE_PAGE_FACE_ID),1)
INCLUDES+=modules/page-components/face_id
INCLUDES+=modules/page-components/face_id/demo/face_id_native
endif
ifeq ($(ENABLE_PAGE_SMART_WATCH),1)
INCLUDES+=modules/page-components/smart_watch
INCLUDES+=modules/page-components/smart_watch/demo/smartwatch_round
endif
ifeq ($(ENABLE_PAGE_SPECTRUM),1)
INCLUDES+=modules/page-components/spectrum_analyzer
endif
ifeq ($(ENABLE_PAGE_WIFI_CONNECT),1)
INCLUDES+=modules/page-components/wifi_connect
INCLUDES+=modules/page-components/wifi_connect/demo/wifi_connect_native
endif
ifeq ($(ENABLE_PAGE_TESAIOT_CONNECT),1)
INCLUDES+=modules/page-components/tesaiot_connect
endif
ifneq ($(ENABLE_ANY_GAME),)
INCLUDES+=modules/page-components/games
endif

INCLUDES+=$(BENTO_COMMON)/modules/wifi_manager
SEARCH+=$(BENTO_COMMON)/modules/wifi_manager

INCLUDES+=$(BENTO_COMMON)/modules/ipc_service
# ipc_service: compiled into libbento_ipc.a, not from source

# Page demo sources — gated individually
ifeq ($(ENABLE_PAGE_SMART_WATCH),1)
SMARTWATCH_DEMO_DIR:=modules/page-components/smart_watch/demo/smartwatch_round
SMARTWATCH_DEMO_SOURCES:=$(wildcard $(SMARTWATCH_DEMO_DIR)/*.c)
SOURCES+=$(SMARTWATCH_DEMO_SOURCES)
endif
ifeq ($(ENABLE_PAGE_FACE_ID),1)
FACEID_NATIVE_DEMO_DIR:=modules/page-components/face_id/demo/face_id_native
FACEID_NATIVE_DEMO_SOURCES:=$(wildcard $(FACEID_NATIVE_DEMO_DIR)/*.c)
SOURCES+=$(FACEID_NATIVE_DEMO_SOURCES)
endif
ifeq ($(ENABLE_PAGE_WIFI_CONNECT),1)
WIFI_NATIVE_DEMO_DIR:=modules/page-components/wifi_connect/demo/wifi_connect_native
WIFI_NATIVE_DEMO_SOURCES:=$(wildcard $(WIFI_NATIVE_DEMO_DIR)/*.c)
SOURCES+=$(WIFI_NATIVE_DEMO_SOURCES)
endif

################################################################################
# Defines
################################################################################

# Basic defines
DEFINES+=CY_RETARGET_IO_CONVERT_LF_TO_CRLF
DEFINES+=CY_RTOS_AWARE

# Phase 14: WiFi + mbedTLS defines
MBEDTLSFLAGS = MBEDTLS_USER_CONFIG_FILE='"mbedtls_user_config.h"'
DEFINES+=$(MBEDTLSFLAGS) CYBSP_WIFI_CAPABLE
DEFINES+=MBEDTLS_CONFIG_FILE='"mbedtls/mbedtls_config.h"'
DEFINES+=CY_WIFI_COUNTRY=WHD_COUNTRY_UNITED_STATES

# Phase 14.5: OPTIGA Trust M + TESAIoT defines (only when enabled)
ifeq ($(ENABLE_OPTIGA), 1)
OPTIGAFLAGS=OPTIGA_LIB_EXTERNAL='"optiga_lib_config_mtb.h"'
DEFINES+=$(OPTIGAFLAGS)
DEFINES+=TESAIOT_DEBUG_LEVEL=3
DEFINES+=CY_CRYPTO_HAL_DISABLE
# PSA crypto config for mbedTLS + OPTIGA SE driver
DEFINES+=MBEDTLS_PSA_CRYPTO_CONFIG_FILE='"configs/ifx_psa_crypto_config.h"'
endif

# MQTT event thread timeout (prevent deadlock)
DEFINES+=CY_MQTT_RECEIVE_DATA_TIMEOUT_MS=100

# Dev-only: LVGL performance monitor (CPU% + FPS overlay at bottom-right)
# Enable: make build BENTO_DEV_PERF_MONITOR=1
# Disable: omit the flag (default)
ifeq ($(BENTO_DEV_PERF_MONITOR),1)
DEFINES+=BENTO_DEV_PERF_MONITOR
endif

################################################################################
# BSP Feature Flag Defines (passed to C code as BSP_HAS_XXX=1)
################################################################################
ifeq ($(BSP_HAS_BMI270),1)
DEFINES+=BSP_HAS_BMI270=1
endif
ifeq ($(BENTO_HAS_MODEL_LINK),1)
DEFINES+=BENTO_HAS_MODEL_LINK=1
INCLUDES+=modules/deepcraft_task
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
ifeq ($(BSP_HAS_RADAR),1)
DEFINES+=BSP_HAS_RADAR=1
endif
ifeq ($(BSP_HAS_CAPSENSE),1)
DEFINES+=BSP_HAS_CAPSENSE=1
endif
ifeq ($(BSP_HAS_POTENTIOMETER),1)
DEFINES+=BSP_HAS_POTENTIOMETER=1
endif
ifeq ($(BSP_HAS_AUDIO_CODEC),1)
DEFINES+=BSP_HAS_AUDIO_CODEC=1
endif
ifeq ($(BSP_HAS_PDM_MIC),1)
DEFINES+=BSP_HAS_PDM_MIC=1
endif
ifeq ($(BSP_HAS_QWA309_BASEBOARD),1)
DEFINES+=BSP_HAS_QWA309_BASEBOARD=1
endif

# Page Component DEFINES (always pass value — 0 or 1 — to C preprocessor)
DEFINES+=ENABLE_PAGE_MOTION=$(ENABLE_PAGE_MOTION)
DEFINES+=ENABLE_PAGE_ENVIRON=$(ENABLE_PAGE_ENVIRON)
DEFINES+=ENABLE_PAGE_CONTROLS=$(ENABLE_PAGE_CONTROLS)
DEFINES+=ENABLE_PAGE_JOYSTICK=$(ENABLE_PAGE_JOYSTICK)
DEFINES+=ENABLE_PAGE_SMART_CARD=$(ENABLE_PAGE_SMART_CARD)
DEFINES+=ENABLE_PAGE_HSM=$(ENABLE_PAGE_HSM)
DEFINES+=ENABLE_PAGE_BENTOCLAW=$(ENABLE_PAGE_BENTOCLAW)
DEFINES+=ENABLE_PAGE_BENTO_BUDDY=$(ENABLE_PAGE_BENTO_BUDDY)
DEFINES+=ENABLE_PAGE_ANIMATION=$(ENABLE_PAGE_ANIMATION)
DEFINES+=ENABLE_PAGE_FACE_ID=$(ENABLE_PAGE_FACE_ID)
DEFINES+=ENABLE_PAGE_SMART_WATCH=$(ENABLE_PAGE_SMART_WATCH)
DEFINES+=ENABLE_PAGE_SPECTRUM=$(ENABLE_PAGE_SPECTRUM)
DEFINES+=ENABLE_PAGE_WIFI_CONNECT=$(ENABLE_PAGE_WIFI_CONNECT)
DEFINES+=ENABLE_PAGE_TESAIOT_CONNECT=$(ENABLE_PAGE_TESAIOT_CONNECT)
DEFINES+=ENABLE_PAGE_GPIO_RGB=$(ENABLE_PAGE_GPIO_RGB)
DEFINES+=ENABLE_PAGE_MOTOR_CTRL=$(ENABLE_PAGE_MOTOR_CTRL)
DEFINES+=ENABLE_PAGE_GAME_SNAKE=$(ENABLE_PAGE_GAME_SNAKE)
DEFINES+=ENABLE_PAGE_GAME_FLAPPY=$(ENABLE_PAGE_GAME_FLAPPY)
DEFINES+=ENABLE_PAGE_GAME_PONG=$(ENABLE_PAGE_GAME_PONG)
DEFINES+=ENABLE_PAGE_GAME_SHOOTER=$(ENABLE_PAGE_GAME_SHOOTER)

CFLAGS+=
CXXFLAGS+=-fno-rtti -fno-exceptions
ASFLAGS+=
LDFLAGS+=
LDLIBS+=
LINKER_SCRIPT=
PREBUILD=
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

# --- BENTO protected library: Core IPC ------------------------------------
# These five translation units ship as a prebuilt, symbol-obfuscated archive
# instead of source. Delete this block and restore the SEARCH+= lines above to
# build them from source again.
#
# emoji_assets/ stays a source directory: it is generated LVGL image data, not
# IP, and the archive deliberately does not contain it.
SEARCH   += $(BENTO_COMMON)/modules/ipc_lcd/emoji_assets
BENTO_IPC_DIST := ../lib/ipc_core
INCLUDES += $(BENTO_IPC_DIST)/include
# LDLIBS, and it must be set BEFORE start.mk: MTB consumes LDLIBS while
# including that file, so the same line after it never reaches the linker.
LDLIBS   += $(BENTO_IPC_DIST)/COMPONENT_CM55/COMPONENT_HARDFP/TOOLCHAIN_GCC_ARM/libbento_ipc.a

# ai_engine.c ships as libbento_edge_ai.a. It holds the inference task, the
# model registry and the parallel-feed router.
#
# Its header stays in the tree and must: page_edge_ai.c dereferences
# ai_model_desc_t and calls ai_result_top_positive(). What the archive removes
# is the implementation, not the interface.
#
# The archive does NOT define printf() or puts(). It did until 2026-08-30, as
# strong globals in the same member that carries every ai_engine_* symbol, which
# meant this library quietly beat libc in your link: your own printf would not
# link at all ("multiple definition"), and if you did not define one, every
# printf on this core silently did nothing.
#
# CM55 still needs those calls muted — cy_retarget_io_init() is never called
# here (CY_IGNORE+=driver above), so newlib stdio reaches a _write that takes an
# uninitialised mutex and aborts. That mute now lives in open source you can
# read and change: bento_cm55_stdio.c in this directory, WEAK so your own
# printf wins with no edit. See that file's header for how to get real output.
#
# CY_IGNORE rather than a SEARCH change: modules/ai_models is on INCLUDES for
# the model_*.c that still compile, so the directory has to stay visible.
BENTO_EDGE_AI_DIST := ../lib/edge_ai
CY_IGNORE += modules/ai_models/ai_engine.c
LDLIBS    += $(BENTO_EDGE_AI_DIST)/COMPONENT_CM55/COMPONENT_HARDFP/TOOLCHAIN_GCC_ARM/libbento_edge_ai.a

# Five CM55 sources ship as lib/cm55_core/…/libbento_cm55.a. Per FILE, not per
# directory: their siblings in the same folders stay open, and MTB discovers
# sources by walking the tree, so only a file-level CY_IGNORE removes one.
#
# What they are, and why these five out of 131: hsm_provision_ui.c operates the
# OPTIGA CSR and Protected Update flow; page_edge_ai.c holds the measured
# per-model confidence calibration; deepcraft_task.c is the cross-core model
# link and its starvation analysis; tesaiot_display.c owns the GFX task and the
# boot ordering that keeps this silicon alive; wifi_saved.c holds the OPTIGA
# slot map. The other 126 are LVGL layout, fonts, images and model weights.
BENTO_CM55_DIST := ../lib/cm55_core
INCLUDES  += $(BENTO_CM55_DIST)/include
CY_IGNORE += modules/page-components/hsm/hsm_provision_ui.c
CY_IGNORE += modules/page-components/edge_ai/page_edge_ai.c
CY_IGNORE += modules/deepcraft_task/deepcraft_task.c
CY_IGNORE += modules/lvgl_display/controller/tesaiot_display.c
CY_IGNORE += modules/page-components/wifi_connect/demo/wifi_connect_native/wifi_saved.c
LDLIBS    += $(BENTO_CM55_DIST)/COMPONENT_CM55/COMPONENT_HARDFP/TOOLCHAIN_GCC_ARM/libbento_cm55.a

include $(CY_TOOLS_DIR)/make/start.mk
