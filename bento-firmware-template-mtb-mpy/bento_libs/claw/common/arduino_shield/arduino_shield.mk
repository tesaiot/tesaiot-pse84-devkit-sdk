# =============================================================================
# arduino_shield.mk — Arduino Uno R3 expansion-header abstraction.
#
# Usage (directly, or via the repo-root lib.mk module expander):
#     BENTO_LIB_MODULES += arduino_shield
#     include $(BENTO_LIBS_DIR)/../lib.mk
#
# This module supplies the header abstraction only. It contains no physical
# pin knowledge — that lives in a board descriptor, which the board overlay
# provides. On the TESAIoT Dev Kit that is arduino_shield_qwa309.c, pulled in
# by kit-tesaiot-pse84-ai/board.mk under BSP_HAS_QWA309_BASEBOARD.
#
# A project that includes this module but installs no board descriptor still
# links and runs; every call returns ARDUINO_ERR_NO_BOARD. That is deliberate:
# a shield driver compiled into a board that has no header should fail at its
# first call with a readable reason, not at link time with a missing symbol.
# =============================================================================

ARDUINO_SHIELD_DIR := $(BENTO_COMMON)/arduino_shield

SOURCES  += $(ARDUINO_SHIELD_DIR)/arduino_shield.c
INCLUDES += $(ARDUINO_SHIELD_DIR)
