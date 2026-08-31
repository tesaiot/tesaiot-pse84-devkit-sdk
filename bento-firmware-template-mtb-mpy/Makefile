################################################################################
# \file Makefile
# \version 1.0
#
# \brief
# Top-level application make file.
# PSoC Edge MicroPython + AI Integration Project
#
# Architecture:
#   CM33_S  - Secure boot (TrustZone)
#   CM33_NS - MicroPython scripting + peripherals (bare-metal)
#   CM55    - WiFi + Display + Camera + AI + Radar (FreeRTOS)
#
################################################################################

################################################################################
# Basic Configuration
################################################################################

MTB_TYPE=APPLICATION

MTB_PROJECTS=proj_cm33_s proj_cm33_ns proj_cm55

################################################################################
# Paths
################################################################################

include common_app.mk

include $(CY_TOOLS_DIR)/make/application.mk
