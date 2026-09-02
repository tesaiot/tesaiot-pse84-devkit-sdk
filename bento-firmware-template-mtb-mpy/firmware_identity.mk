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
# The public SDK repo. This pointed at a private repo until 2026-09-02, left over
# from before the SDK was published — running the release pipeline would have cut a
# release in the wrong place entirely.
FW_RELEASE_REPO       := tesaiot/tesaiot-pse84-devkit-sdk
# Both variants share one firmware version, so a bare "v" prefix would compute the
# same tag for each and the second release would be refused as already published.
# The prefix carries the variant; the version stays the firmware's own.
FW_RELEASE_TAG_PREFIX := fw-mpy-v
# The asset slug must IDENTIFY the product, not merely differ: the relay's catalogue
# identity is (repo, filename), and <BOARD> cannot separate these two because both
# build for KIT_PSE84_AI. Publishing both as app_combined.hex is why the C-only build
# currently reaches nobody.
#
# The -devkit suffix is not decoration. board=KIT_PSE84_AI carries SEVEN SKUs across
# TWO products, because KitProg3 sits on the AI Kit SoM that the Dev Kit is built
# from. So for this board the slug is the ONLY field that names the product, and it
# is what the flash client falls back to while the index is not yet visible — the
# raw.githubusercontent CDN holds a 300 s cache. Without it, an Eval Kit owner is
# shown Dev Kit firmware with only a grey "Board unconfirmed" chip to warn them.
FW_RELEASE_ASSET_SLUG := bento-micropython-devkit
