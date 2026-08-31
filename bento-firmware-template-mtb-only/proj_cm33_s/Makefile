################################################################################
# \file Makefile
# \version 1.0
#
# \brief
# Makefile for configuring the Cortex-M33 core secure project
#
################################################################################

include ../common.mk

################################################################################
# Basic Configuration
################################################################################

APPNAME=proj_cm33_s

VERBOSE=

################################################################################
# Advanced Configuration
################################################################################

COMPONENTS+=SECURE_DEVICE

DISABLE_COMPONENTS+=

# Set this application to run at the CM33
CORE=CM33
CORE_NAME=CM33_0

# Set this application to be secure
VCORE_ATTRS=SECURE

APPTYPE=flash

SOURCES=
INCLUDES=
VFP_SELECT+=
CFLAGS+=
CXXFLAGS+=
ASFLAGS+=

LDFLAGS+=

ifeq ($(TOOLCHAIN),ARM)
 LDFLAGS+=--diag_suppress=L6848
endif

LDLIBS+=
LINKER_SCRIPT=
PREBUILD=
POSTBUILD=

################################################################################
# Paths
################################################################################

CY_APP_PATH=

# Derived from BENTO_WORKSPACE, not hardcoded.
#
# This was `../../..`, which is right for our tree — template/ sits inside
# TESAIoT_Firmware_Development_and_Release/, so three levels up from a project
# directory is the workspace. It is wrong for a customer, whose README tells
# them to unpack bento-firmware-template/ as a direct child of their workspace:
# one level shallower, so ../../.. overshoots.
#
# The symptom is not a missing-directory error. `make getlibs` happily CREATES
# mtb_shared at the overshot path — 41 assets in the wrong place, measured
# 2026-08-28 — and the build then fails much later with ninja looking for a
# vendor source in a directory nothing populated. Found by unpacking the
# shipped zip and building it, which is the only way to find it.
CY_GETLIBS_SHARED_PATH=$(BENTO_WORKSPACE)
CY_GETLIBS_SHARED_NAME=mtb_shared

include $(CY_TOOLS_DIR)/make/start.mk
