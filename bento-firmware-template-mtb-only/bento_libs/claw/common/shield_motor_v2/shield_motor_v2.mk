# =============================================================================
# shield_motor_v2.mk — Adafruit Motor Shield v2 driver
#                      (PCA9685 16-channel PWM + 2x TB6612FNG H-bridge).
#
# Usage (directly, or via the repo-root lib.mk module expander):
#     BENTO_LIB_MODULES += arduino_shield shield_motor_v2
#     include $(BENTO_LIBS_DIR)/../lib.mk
#
# Depends on arduino_shield for the header abstraction (I2C accessor, and the
# capability check that refuses the two servo pins on a board that cannot drive
# them). List arduino_shield first; lib.mk includes fragments in order.
#
# Both translation units here are free of platform API by design, so the same
# sources build for CM33_NS, for CM55, and for the host test harness in test/.
# Run those with:  make -C $(BENTO_COMMON)/shield_motor_v2/test
# =============================================================================

SHIELD_MOTOR_V2_DIR := $(BENTO_COMMON)/shield_motor_v2

SOURCES  += $(SHIELD_MOTOR_V2_DIR)/pca9685.c
SOURCES  += $(SHIELD_MOTOR_V2_DIR)/motor_shield_v2.c
INCLUDES += $(SHIELD_MOTOR_V2_DIR)
