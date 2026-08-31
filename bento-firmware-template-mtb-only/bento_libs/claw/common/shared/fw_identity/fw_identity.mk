################################################################################
# fw_identity.mk — generate the on-device identity config header + wire it in.
#
# Included by a project's common_app.mk AFTER that project's firmware_identity.mk.
# Reads FW_FAMILY / FW_APP / FW_VARIANT / FW_VERSION (author-set) + TARGET (build), derives
# BOARD / SKU / UUID, and writes a build-time header fw_id_config.h that fw_identity.c includes.
#
# Using a generated header (not -D string defines) avoids make/shell quote-escaping and is
# inspectable. If FW_FAMILY is empty (project not opted in) this is a no-op — fw_identity.c
# then falls back to an explicit UNSET identity, so nothing breaks.
#
# See TESAIoT_PLAN/Firmware_Identity/{00_Firmware_Identity_Contract,01_Spec_Firmware_Team}.md
################################################################################

ifneq ($(FW_FAMILY),)

FW_ID_SHARED   := $(BENTO_LIBS_DIR)/common/shared
FW_ID_SCRIPTS  := $(FW_ID_SHARED)/scripts
FW_ID_GEN_DIR  := $(abspath ../.fw_identity_gen)

# Release flag: set by the release pipeline (RELEASE_BUILD=1); local/debug builds -> 0.
FW_ID_RELEASE  ?= $(if $(RELEASE_BUILD),1,0)

# Generate the config header at parse time (deterministic; regenerated every build).
_FW_ID_MK := $(shell mkdir -p $(FW_ID_GEN_DIR) && \
  python3 $(FW_ID_SCRIPTS)/gen_fw_id_config.py \
    --family  "$(FW_FAMILY)"  --app "$(FW_APP)" --variant "$(FW_VARIANT)" \
    --version "$(FW_VERSION)" --target "$(TARGET)" --release "$(FW_ID_RELEASE)" \
    > $(FW_ID_GEN_DIR)/fw_id_config.h 2> $(FW_ID_GEN_DIR)/gen.err \
  && echo OK || echo FAIL)

ifeq ($(_FW_ID_MK),FAIL)
$(warning fw_identity: gen_fw_id_config.py failed — see $(FW_ID_GEN_DIR)/gen.err; \
          fw_identity.c will use the UNSET fallback)
else
INCLUDES += $(FW_ID_GEN_DIR)
# Tell CM33_NS main.c the record is embedded, so it moves the CM55 boot vector address past it
# (the record shifts __Vectors from m55_nvm+0x400 to +0x800). All cores see this bare -D; only
# main.c consumes it. Non-opted builds never define it → they keep the stock boot address (safe).
DEFINES += FW_IDENTITY_PRESENT
endif

endif # FW_FAMILY
