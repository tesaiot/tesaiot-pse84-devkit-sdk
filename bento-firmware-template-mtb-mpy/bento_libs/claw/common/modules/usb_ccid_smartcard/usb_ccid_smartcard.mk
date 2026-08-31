# =============================================================================
# usb_ccid_smartcard.mk — single-line Makefile inclusion for the BENTO
# USB CCID smart card reader driver.
#
# Usage from a project's proj_cm55/Makefile:
#     include $(BENTO_COMMON)/modules/usb_ccid_smartcard/usb_ccid_smartcard.mk
#
# Adds: INCLUDES + SEARCH so MTB's vpath auto-discovers usb_ccid_smartcard.c.
# Behaviour-equivalent replacement for the prior two-line INCLUDES+SEARCH
# block — produces byte-identical ELF symbol tables when wrapping a no-op
# refactor (verified via `nm --size-sort` diff).
#
# Depends on: SEGGER emUSB-Host (USBH stack), brought in by the project's
# COMPONENTS list. This .mk does not re-declare that dependency — it is
# purely a path-organisation wrapper.
# =============================================================================

USB_CCID_SMARTCARD_DIR := $(BENTO_COMMON)/modules/usb_ccid_smartcard

INCLUDES += $(USB_CCID_SMARTCARD_DIR)
SEARCH   += $(USB_CCID_SMARTCARD_DIR)
