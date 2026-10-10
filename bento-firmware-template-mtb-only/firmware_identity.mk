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

# No build-machine path in an image. __FILE__ in vendored code (the lwIP and
# sys_arch asserts on CM33_NS, the Ethos-U driver on CM55) carries the
# absolute path of the checkout, a home directory included; the 1.11.0 image
# carried 32 of them. The workspace root -- the directory that holds
# mtb_shared/ and this template, BENTO_WORKSPACE in common.mk, set before this
# file is read -- is mapped away, so those strings start at mtb_shared/... The
# second form catches the "//" paths GCC 14.2.1 otherwise leaves unmapped. The
# library's identity fragment applies the same rule to the library's own
# workspace, which for this template is the template root and does not cover
# mtb_shared/; these names are the template's own so a later sync of that
# fragment cannot redefine them.
ifneq ($(strip $(BENTO_WORKSPACE)),)
TPL_WS_ROOT       := $(abspath $(BENTO_WORKSPACE))
TPL_WS_PREFIX_MAP := -fmacro-prefix-map=$(TPL_WS_ROOT)/= -fmacro-prefix-map=/$(TPL_WS_ROOT)/=
CFLAGS   += $(TPL_WS_PREFIX_MAP)
CXXFLAGS += $(TPL_WS_PREFIX_MAP)
ASFLAGS  += $(TPL_WS_PREFIX_MAP)
endif
