################################################################################
# kit-tesaiot-pse84-ai/board.mk
#
# QWA309 base-board overlay makefile fragment. A consuming project's
# proj_cm33_ns/Makefile (and Makefile.micropython) / proj_cm55/Makefile can
#   -include $(BENTO_LIBS_DIR)/kit-tesaiot-pse84-ai/board.mk
# to pull the overlay sources. Everything here is guarded by
# BSP_HAS_QWA309_BASEBOARD so a flag=0 build is byte-identical to stock AI Kit.
#
# NOTE: this fragment is deliberately empty of compiled sources until each
# capability wave lands its driver. It exists now so the persona is a real,
# referenceable board layer (per decision D4) and so downstream Makefiles can
# wire the include path once, ahead of the source drops.
################################################################################

TESAIOT_OVERLAY_DIR := $(BENTO_LIBS_DIR)/kit-tesaiot-pse84-ai

ifeq ($(BSP_HAS_QWA309_BASEBOARD),1)

# Include path for QWA309 MicroPython C modules (headers land here per wave).
INCLUDES += $(TESAIOT_OVERLAY_DIR)/mpy

# --- Arduino Uno R3 expansion header ----------------------------------------
# Requires the arduino_shield module, which the consuming project pulls in with
#     BENTO_LIB_MODULES += arduino_shield
# The two files here split deliberately: the descriptor is portable C and is
# covered by host tests, the HAL is PSoC PDL and is target-only.
#
# The HAL borrows the display's SCB5 I2C controller, so it belongs to whichever
# core owns the display -- CM55 on this board. Adding it to a CM33 build would
# link against a context that core does not own.
ifeq ($(BSP_HAS_ARDUINO_HEADER),1)
SOURCES += $(TESAIOT_OVERLAY_DIR)/arduino_shield_qwa309.c
ifeq ($(CORE),CM55)
SOURCES += $(TESAIOT_OVERLAY_DIR)/arduino_shield_qwa309_hal.c
endif
endif

# --- Capability sources (uncommented as each wave lands its driver) ----------
# MicroPython modules (compiled into libmicropython.a via MOD_SRC_C in the
# consuming Makefile.micropython, NOT here — this list is for reference/CM55).
#
# Wave 2:  $(TESAIOT_OVERLAY_DIR)/mpy/modcan.c            # CANFD0 ch1 (CM33_NS)
# Wave 3:  $(TESAIOT_OVERLAY_DIR)/mpy/modrgbmatrix.c      # DFR0522 I2C 0x10
# Wave 3:  $(TESAIOT_OVERLAY_DIR)/mpy/mod_qwa309_header.c # header GPIO/UART/SPI/PWM
# Wave 1:  $(TESAIOT_OVERLAY_DIR)/mpy/tesaiot_controls_auto.c # pots x4 + buttons feed

endif
