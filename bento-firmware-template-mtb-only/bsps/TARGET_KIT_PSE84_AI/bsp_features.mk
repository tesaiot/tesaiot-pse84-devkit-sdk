################################################################################
# BSP Feature Flags: KIT_PSE84_AI (PSoC Edge AI Dev Kit)
#
# Included by Makefile.micropython and proj_cm55/Makefile to enable/disable
# BSP-specific sensor drivers and modules.
#
# Usage:
#   -include ../bsps/TARGET_$(TARGET)/bsp_features.mk
################################################################################

# On-board sensors
BSP_HAS_BMI270=1
BSP_HAS_BMM350=1

# AI Dev Kit specific sensors
BSP_HAS_DPS368=1
BSP_HAS_SHT40=1
BSP_HAS_RADAR=1

# TESAIoT Dev Kit (QWA309 base board) variant.
# When 1, this AI-Kit SoM is mated to the QWA309 training base board and pulls in
# the base-board glue (pots x4, buttons x2, CapSense-4000T over I2C, CAN, DFR0522
# RGB matrix, Arduino-header I/O) from the kit-tesaiot-pse84-ai overlay in
# BENTO-TESAIoT-Claw-libraries. See TESAIoT_PLAN/2026-7/TESAIoT_DevKit_Merge/.
BSP_HAS_QWA309_BASEBOARD=1

# Optional peripherals (QWA309 base board provides these).
# NOTE: values must be a bare 1/0 with NO trailing spaces — the cm55 Makefile
# uses ifeq($(VAR),1) which is whitespace-sensitive; inline comments here would
# leave trailing spaces and silently drop the INCLUDES/CFLAGS for the feature.
# CapSense: external PSoC 4000T over header I2C @0x08
BSP_HAS_CAPSENSE=1
# Potentiometers: VR1-4 on AutAnalog SAR ch4-7 (P15.4-7)
BSP_HAS_POTENTIOMETER=1
# Audio: on-board TLV320DAC3100 (U28) DAC+amp → J8 speaker; SFX/tone like Eva.
# Needs a speaker wired to J8 — hardware-verified 2026-07-17 (ui.tone/ui.sfx audible,
# codec answers I2C 0x18). Turning this off also drops ui.tone/ui.sfx from MicroPython
# (modui.c is #if'd on it via Makefile.micropython), so the REPL loses the API entirely.
BSP_HAS_AUDIO_CODEC=1

# USB Host (SEGGER emUSB-Host, USB-HS port)
BSP_HAS_USB_HOST=1

# Audio
BSP_HAS_PDM_MIC=1

################################################################################
# Page Components (1=enabled, 0=disabled)
# Core pages (Home, Dashboard, Playground) are always ON.
################################################################################
# Eva-style Controls page retired on the TESAIoT Dev Kit — replaced by the
# QWA309-native Potentiometers + Touch & RGB pages below.
# QWA309 base board page (bare values — see note above)
# Merged GPIO & RGB Matrix page (pots + CapSense + SW5/SW6 + DFR0522 matrix)
# Arduino Uno R3 expansion header (J7/J8/J9/J10) and the Motor Controller page
# that drives an Adafruit Motor Shield v2 on it. Bare values, no trailing
# space — a trailing space breaks the ifeq comparisons downstream.
BSP_HAS_ARDUINO_HEADER=1
# Off since 2026-08-28 (professor's call): most boards have no Motor Shield
# plugged into the QWA309 header, and a menu that drives absent hardware is
# noise. This hard `=` overrides the ?= default in proj_cm55/Makefile, so the
# switch must be HERE. Re-enable per build: ENABLE_PAGE_MOTOR_CTRL=1 on make.
# USB CCID is gated off in proj_cm55/main.c (ENABLE_USB_CCID=0 — CCID tasks
# starve the joystick HID stream). Without CCID the Smart Card page is dead UI.

# Games (disabled by default — enable individually)

# DEEPCraft model link (bento_link + deepcraft engine + CM55 stub task)
BENTO_HAS_MODEL_LINK=1

# Edge AI demos (TFLite-Micro + Ethos-U55 NPU on CM55)
BENTO_HAS_EDGE_AI=1

# --- generated from .config by tools/genfeatures.py — do not edit ---
# menuconfig edits .config; this block mirrors it for the build.
ENABLE_PAGE_GPIO_RGB=1
ENABLE_PAGE_MOTOR_CTRL=0
ENABLE_PAGE_HSM=1
ENABLE_PAGE_SMART_WATCH=1
ENABLE_PAGE_ANIMATION=1
ENABLE_PAGE_JOYSTICK=1
ENABLE_PAGE_BENTOCLAW=1
ENABLE_PAGE_WIFI_CONNECT=1
ENABLE_PAGE_MOTION=1
ENABLE_PAGE_ENVIRON=1
ENABLE_PAGE_CONTROLS=0
ENABLE_PAGE_FACE_ID=0
ENABLE_PAGE_SPECTRUM=0
ENABLE_PAGE_SMART_CARD=0
ENABLE_PAGE_BENTO_BUDDY=0
ENABLE_PAGE_TESAIOT_CONNECT=0
ENABLE_PAGE_GAME_SNAKE=0
ENABLE_PAGE_GAME_FLAPPY=0
ENABLE_PAGE_GAME_PONG=0
ENABLE_PAGE_GAME_SHOOTER=0
# --- end generated ---
