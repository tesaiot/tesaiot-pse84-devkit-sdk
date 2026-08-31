################################################################################
# \file common.mk
# \version 1.0
#
# \brief
# Settings shared across all projects.
# PSoC Edge MicroPython + AI Integration Project
#
################################################################################

MTB_TYPE=PROJECT

# Target board/hardware (BSP).
TARGET?=KIT_PSE84_AI

# Toolchain
TOOLCHAIN=GCC_ARM

# Build configuration (Debug/Release)
CONFIG=Release

# Config file for postbuild sign and merge operations.
#
# Secure boot (Extended Boot OEM verification).
# See TESAIoT_PLAN/2026-7/PSoC_Edge_Security_Integration/
#
#   SECURE_BOOT=0 (default) : unchanged behaviour — CM33_S image gets MCUboot
#                             metadata only and stays UNSIGNED.
#   SECURE_BOOT=1           : CM33_S image is signed with the OEM RoT key, as
#                             required once a device is provisioned secure_boot=true.
#
# Only the literal values 0 and 1 are accepted. Anything else (true/yes/on/...)
# is a hard error rather than a silent fall-through to an UNSIGNED build — see
# PROGRESS.md [F-b]. $(strip) makes 'SECURE_BOOT=1 ' behave as 1, per the
# workspace's known trailing-whitespace trap.
#
# Override the signing key with:
#   make SECURE_BOOT=1 SECURE_BOOT_KEY=/abs/path/to/oem_private_key_0.pem
# ---------------------------------------------------------------------------
# BENTO_VARIANT — which firmware personality this tree builds.
#
#   mtb-mpy (default) : ModusToolbox + MicroPython. Unchanged behaviour.
#                           The REPL, the TACP host link that the BENTO IDE
#                           flashes through, /main.py, and the ~30 Python
#                           extension modules.
#   mtb-only          : ModusToolbox only. Plain C on FreeRTOS, no VM.
#
# Both build from one checkout, into the same build/ directory. Switching
# BENTO_VARIANT therefore needs a clean first — see rule 1 in CLAUDE.md, which
# this workspace learned the hard way: ninja keys objects by timestamp, and a
# variant switch changes no timestamps at all.
#
# What actually differs is narrower than the directory layout suggests, and it
# is worth writing down because it is the thing that will surprise a
# contributor: CM55 is identical in both — the whole UI, Edge AI and radar
# image references zero MicroPython symbols. On CM33_NS, 8 of 522 objects touch
# the VM. WiFi association and the entire OPTIGA CSR / Protected Update path
# are plain C in both variants and are reached from main() without Python.
#
# What mtb-only still owes is measured rather than estimated. Linking the CM33_NS
# objects without libmicropython.a leaves NINE undefined symbols, and they fall
# into three groups:
#
#   storage / config  tesaiot_config_get, _get_ptr, _set_field,
#                     _set_field_nosave — LittleFS is mounted by executing a
#                     Python source string, and the config store (broker,
#                     device id, TLS mode, WiFi credentials) persists the same
#                     way, so both need a C implementation.
#   MQTT session      tesaiot_mqtt_connect, tesaiot_mqtt_disconnect,
#                     tesaiot_bridge_mqtt_connected. tesaiot_mqtt.c is compiled
#                     into libmicropython.a, so the session goes with the VM
#                     even though nothing in it is Python. Moving that file
#                     into SOURCES is most of the work.
#   tasks             mpy_task_entry, sensor_auto_task_create — main() starts
#                     both unconditionally today.
#
# The variant refuses to build until those exist, rather than producing a board
# that boots to silence.
#
# Only the literal values are accepted. An unrecognised value is a hard error
# rather than a silent fall-through, for the same reason SECURE_BOOT is strict
# below: a typo must not quietly select the wrong firmware. $(strip) makes
# 'BENTO_VARIANT=mtb-only ' behave as mtb-only, per the workspace's trailing-
# whitespace trap.
BENTO_VARIANT?=mtb-mpy

ifeq ($(filter mtb-mpy mtb-only,$(strip $(BENTO_VARIANT))),)
$(error BENTO_VARIANT must be 'mtb-mpy' or 'mtb-only', got '$(BENTO_VARIANT)')
endif

# Variant settings live in one file per variant so a contributor changes one
# place, and both this Makefile and Makefile.micropython read the same values.
# Board layer first, variant layer second: a variant states only its
# differences from the board, which is the layering MicroPython's own ports use
# (ports/stm32/Makefile reads mpconfigboard.mk then mpconfigvariant.mk).
include $(dir $(lastword $(MAKEFILE_LIST)))variants/$(strip $(BENTO_VARIANT)).mk

SECURE_BOOT?=0

ifeq ($(filter 0 1,$(strip $(SECURE_BOOT))),)
$(error SECURE_BOOT must be 0 or 1, got '$(SECURE_BOOT)'. There is no 'true'/'yes' — \
        an unrecognised value would otherwise produce an UNSIGNED build. \
        Signed build: SECURE_BOOT=1 ./build.sh ai clean)
endif

ifeq ($(strip $(SECURE_BOOT)),1)
COMBINE_SIGN_JSON?=configs/secure_boot_with_extended_boot.json
# Absolute path: the postbuild runs edgeprotecttools with CWD=configs/, while this
# file is parsed with CWD=proj_*/ — only an absolute path is correct for both. It is
# derived from this makefile's own location, so it survives being invoked from anywhere.
# NOTE: ':=' is required. With '=' / '?=' the MAKEFILE_LIST lookup would be deferred until
# recipe time, by which point the last entry is fw_identity.mk (included below) and the key
# path would silently resolve into BENTO-TESAIoT-Claw-libraries instead of this project.
_SECURE_BOOT_APP_DIR:=$(abspath $(dir $(lastword $(MAKEFILE_LIST))))
SECURE_BOOT_KEY?=$(_SECURE_BOOT_APP_DIR)/security/keys/oem_private_key_0.pem
# MTB_COMBINE_SIGN_ARGS is the pass-through hook that signcombinemkgen emits into
# build/$(notdir $(COMBINE_SIGN_JSON)).mk; it resolves {{OEM_SIGNING_KEY}} in the JSON.
MTB_COMBINE_SIGN_ARGS+=-s OEM_SIGNING_KEY "$(SECURE_BOOT_KEY)"
# Positive confirmation in the build log: "did I actually get a signed build?" must be
# answerable without reading the postbuild output. Verify with security/verify_signed_image.sh.
$(info SECURE_BOOT=1 — CM33_S will be SIGNED with $(SECURE_BOOT_KEY))
else
COMBINE_SIGN_JSON?=configs/boot_with_extended_boot.json
endif
# Which directory is the ModusToolbox workspace — the one holding mtb_shared
# and the MicroPython port.
#
# A customer unpacks this template as a direct child of their workspace, so the
# workspace is ../.. from a project directory. In the release repository the
# same tree sits one level deeper, under template/, so it is ../../.. there.
#
# The test is bento-release.sh, which exists only at the root of the release
# repository. It was the presence of mtb_shared until 2026-08-28, and that
# could not work: mtb_shared does not exist before the first getlibs, so the
# probe was asking about a directory that had not been created yet — and once
# getlibs had created one at the overshot depth, the probe preferred that stray
# copy permanently. A marker that is checked in, and never generated, cannot
# fail that way.
#
# Still overridable, and that is the documented escape for any other layout:
#
#     make build BENTO_WORKSPACE=/path/to/workspace
#
BENTO_WORKSPACE ?= $(strip $(if $(wildcard ../../bento-release.sh),../../..,../..))
# ---------------------------------------------------------------------------
# Third-party assets this firmware needs PATCHED, and getlibs will not patch.
#
# Six assets under mtb_shared carry local changes — eleven files — and
# `make getlibs` fetches the pristine upstream of every one of them. Only one
# stops the build. The other ten change behaviour silently: without the
# cy_tls.c change the OPTIGA key is never bound to the TLS session, so mTLS
# falls back to a software key and the board looks healthy until the broker
# rejects it.
#
# Checked by SHA-256, not by existence. A file being present says nothing about
# WHICH version it is, and GNU patch defaults to fuzz factor 2 — it will ignore
# two lines of context at each end to find somewhere to apply a hunk, and exit
# 0. Its own CAVEATS say the result is correct only when applied to exactly the
# version the diff came from, and Yocto's patch-fuzz QA check exists because
# "it is entirely possible for an incorrectly patched file to still compile
# without errors". So the check compares digests against
# third_party_patches/PATCHED.sha256.
#
# The gate must not fire on the goals a customer needs BEFORE they can patch
# anything. getlibs is the obvious one — the assets have to exist before they
# can be patched, so blocking it is circular, and it is step one in the package
# README. clean and help have the same problem for the same reason.
define newline


endef

BENTO_ASSET_GATE_GOALS := getlibs clean help printlibs get_app_info \
                          uninstall config config_bt config_ezpd
BENTO_ASSET_GATE := $(if $(filter $(BENTO_ASSET_GATE_GOALS),$(MAKECMDGOALS)),,1)

BENTO_ASSET_SUMS ?= ../third_party_patches/PATCHED.sha256

ifeq ($(BENTO_ASSET_GATE),1)
ifneq ($(wildcard $(BENTO_ASSET_SUMS)),)
BENTO_ASSET_REPORT := $(shell ../verify_asset_patches.sh $(BENTO_WORKSPACE) $(BENTO_ASSET_SUMS) 2>&1 | tr '\n' '~')
ifneq ($(BENTO_ASSET_REPORT),)
$(error $(subst ~,$(newline),$(BENTO_ASSET_REPORT)))
endif
endif
endif

BENTO_LIBS_DIR = ../bento_libs/claw
BENTO_COMMON   = $(BENTO_LIBS_DIR)/common
BENTO_BOARD    = $(BENTO_LIBS_DIR)/kit-pse84-ai

# Firmware identity: derive SKU/UUID + generate the on-device config header, then
# wire it in. Guarded on BENTO_LIBS_DIR so bootstrap contexts (var unset) skip it.
ifneq ($(BENTO_LIBS_DIR),)
-include ../firmware_identity.mk
include $(BENTO_LIBS_DIR)/common/shared/fw_identity/fw_identity.mk
endif

include ../common_app.mk
