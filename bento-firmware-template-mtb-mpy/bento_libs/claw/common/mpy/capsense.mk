# =============================================================================
# capsense.mk — pick the CapSense backend for this board, and (for the VM
# build) wrap the CapSense MicroPython module sources behind BSP_HAS_CAPSENSE.
#
# Usage:
#   mtb-mpy   proj_cm33_ns/Makefile.micropython
#             include $(BENTO_COMMON)/mpy/capsense.mk
#             -> MOD_SRC_C gets the binding + the selected backend.
#
#   mtb-only  proj_cm33_ns/Makefile, inside the BENTO_HAS_MPY=0 block
#             include $(BENTO_COMMON)/mpy/capsense.mk
#             SOURCES+=$(BENTO_COMMON)/mpy/$(BENTO_CAPSENSE_BACKEND_C)
#             -> no MicroPython binding, backend only.
#
# TWO files implement sensor_capsense.h and exactly one may be linked:
#   sensor_capsense.c      direct I2C read of the 4000T at 0x08 on the CM33
#                          sensor bus (sensor_i2c wrapper).
#   sensor_capsense_ipc.c  IPC_CMD_CONTROLS_STATE round trip to the CM55
#                          cm55_sensor_poll cache (refreshed every 50 ms).
#
# Which one is correct is a BOARD fact, not a variant fact, so it is decided
# ONCE here and read by both build systems. It was previously decided here for
# mtb-mpy and restated by hand in proj_cm33_ns/Makefile for mtb-only, where the
# hand copy named the direct backend unconditionally — so every mtb-only
# package shipped a capsense_read() pointed at the wrong bus on QWA309 boards.
#
# Path convention: BENTO_CAPSENSE_BACKEND_C is a bare filename with no
# directory, because the two consumers want different prefixes — MOD_SRC_C uses
# the `mpy/X.c` relative form resolved by the MicroPython port's VPATH, while
# proj_cm33_ns/Makefile's SOURCES wants the full $(BENTO_COMMON)/mpy/X.c.
# =============================================================================

ifeq ($(BSP_HAS_CAPSENSE),1)

ifeq ($(BSP_HAS_QWA309_BASEBOARD),1)
# QWA309 boards: the 4000T sits on the CM55-owned display/touch bus (P17.0/1),
# not the CM33 sensor bus — the direct-I2C backend would read the wrong bus,
# and pointing CM33 at the display bus would create a two-master hazard. Use
# the IPC snapshot backend. Its CM55 half — cm55_sensor_poll's strong
# cm55_controls_snapshot() override — is compiled in both variants.
BENTO_CAPSENSE_BACKEND_C := sensor_capsense_ipc.c
else
BENTO_CAPSENSE_BACKEND_C := sensor_capsense.c
endif

# The MicroPython half. Guarded so the mtb-only consumer can include this file
# purely for the backend selection: with BENTO_HAS_MPY=0 nothing here adds a
# MOD_SRC_C entry, and in particular nothing names modsensors_capsense.c, which
# that package does not carry. BENTO_HAS_MPY is unset inside
# Makefile.micropython's own sub-make, and `ifneq` treats unset as not-0, so
# the VM build keeps its existing behaviour.
ifneq ($(BENTO_HAS_MPY),0)
MOD_SRC_C += mpy/modsensors_capsense.c
MOD_SRC_C += mpy/$(BENTO_CAPSENSE_BACKEND_C)
endif

endif
