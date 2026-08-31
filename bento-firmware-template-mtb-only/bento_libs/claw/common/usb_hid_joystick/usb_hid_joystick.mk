# =============================================================================
# usb_hid_joystick.mk — Makefile inclusion for the BENTO USB HID joystick
# library (Logitech F310, DirectInput mode).
#
# Usage from a project's proj_cm55/Makefile:
#     include $(BENTO_COMMON)/usb_hid_joystick/usb_hid_joystick.mk
#
# Library layout (mirrors thai_text/):
#   api/    public headers (hid_f310_report.h, hid_f310_parser.h, usb_hid_joystick.h)
#   src/    implementation
#     hid_f310_parser.c       L1 — pure decoder, libc only, host-testable
#     usb_adapter/
#       usb_hid_joystick.c    L2 — SEGGER emUSB-Host glue, FreeRTOS tasks
#
# EXTERN DEPENDENCIES (must remain defined in the consuming project's
# usbh_config.c — verify with `nm proj_cm55.elf | grep -E
# 'usbh_isr_count|usbh_port_power_count'` after any link-order or
# symbol-visibility change):
#   - extern volatile uint32_t usbh_isr_count;
#   - extern volatile uint32_t usbh_port_power_count;
# Guards against the 2026-03-10 incident class where Eva Kit Controls/HSM
# pages disappeared after a stale-.o re-link silently dropped USB symbols.
# =============================================================================

# Overridable so trees whose BENTO_COMMON points elsewhere (the game trees
# during the fork wind-down) can consume this module in place:
#     USB_HID_JOYSTICK_DIR := $(BENTO_CLAW_LIBS)/common/usb_hid_joystick
#     include $(USB_HID_JOYSTICK_DIR)/usb_hid_joystick.mk
USB_HID_JOYSTICK_DIR ?= $(BENTO_COMMON)/usb_hid_joystick

# Public API headers
INCLUDES += $(USB_HID_JOYSTICK_DIR)/api

# L1 core (host-testable, no USB deps). Compiled into every consumer.
SOURCES  += $(USB_HID_JOYSTICK_DIR)/src/hid_f310_parser.c

# L2 USB adapter (SEGGER emUSB-Host + FreeRTOS). Opt-out via
# `USB_HID_JOYSTICK_SKIP_L2 := 1` BEFORE including this file if you only
# want the renderer-agnostic L1 core (non-BENTO consumers).
ifneq ($(USB_HID_JOYSTICK_SKIP_L2),1)
    SOURCES += $(USB_HID_JOYSTICK_DIR)/src/usb_adapter/usb_hid_joystick.c
endif
