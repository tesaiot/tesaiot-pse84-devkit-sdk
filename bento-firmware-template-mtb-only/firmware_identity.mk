################################################################################
# firmware_identity.mk — TESAIoT Dev Kit MicroPython Core. FW_BOARD/FW_SKU/FW_UUID are DERIVED.
################################################################################
FW_FAMILY  := CLAW
# MPY when the MicroPython VM ships, MTB for the C-only variant — the SKU is
# how a flashed board tells you what it is, so it must not lie about this.
# BENTO_HAS_MPY is set by variants/<variant>.mk, included before this file.
FW_APP     := $(if $(filter 0,$(BENTO_HAS_MPY)),MTB,MPY)
FW_VARIANT := DEVKIT
FW_VERSION := $(shell sed -n 's/.*BENTOCLAW_VERSION[[:space:]][[:space:]]*"\([0-9][0-9.]*\)".*/\1/p' \
    ../proj_cm55/tesaiot_version/bentoclaw_version_project.h)
FW_RELEASE_REPO       := wiroon/TESAIoT_KIT_PSE84_AI-Micropython-BentoClaw
FW_RELEASE_TAG_PREFIX := v
# No FW_RELEASE_ASSET_SLUG: repo is private — not listed on the flash-service
# catalog yet. Set a slug before the first public catalog release.
