################################################################################
# \file common_app.mk
# \version 1.0
#
# \brief
# Settings shared across the entire application.
#
################################################################################

################################################################################
# Paths
################################################################################

# Locate ModusToolbox helper tools folders in default installation
# locations for Windows, Linux, and macOS.
CY_WIN_HOME=$(subst \,/,$(USERPROFILE))
CY_TOOLS_PATHS ?= $(wildcard \
    $(CY_WIN_HOME)/ModusToolbox/tools_* \
    $(HOME)/ModusToolbox/tools_* \
    /Applications/ModusToolbox/tools_*)

# If you install ModusToolbox software in a custom location, add the path to its
# "tools_X.Y" folder (where X and Y are the version number of the tools
# folder).
CY_TOOLS_PATHS+=

# The ModusToolbox version this template is validated against.
#
# The stock line here is `lastword $(sort ...)` — newest installed wins. That is
# wrong for a template, and it fails in a way that looks like a code defect
# rather than a tooling one: the build runs device-configurator-cli, which
# REGENERATES bsps/TARGET_$(TARGET)/config/GeneratedSource from design.modus
# using whatever Configurator is newest. Backend 3.80.0 emits a notice that
# 3.60.0 did not — a Wi-Fi host-wake pin constraint — and cycfg_notices.h turns
# every notice into `#warning`, which -Werror=cpp turns into an error. Sixteen
# of them, in a tree the customer has not touched.
#
# This does not show up in our own tree, because there GeneratedSource is
# already newer than design.modus and nothing regenerates. It shows up on the
# first build of a freshly unpacked package, which is exactly the build we do
# not do. Found 2026-08-28 by unpacking the shipped zip and building it.
#
# So pin it. A customer with only a newer ModusToolbox gets a clear error naming
# the version to install, rather than sixteen warnings about a pin they did not
# configure.
BENTO_MTB_VERSION ?= 3.6

CY_TOOLS_DIR=$(lastword $(sort $(wildcard $(filter %tools_$(BENTO_MTB_VERSION),$(CY_TOOLS_PATHS)))))

ifeq ($(CY_TOOLS_DIR),)
$(error ModusToolbox $(BENTO_MTB_VERSION) not found. This template is validated \
        against tools_$(BENTO_MTB_VERSION); a newer Configurator regenerates the \
        BSP config and the build then fails on notices you did not cause. \
        Install it, or override with BENTO_MTB_VERSION=<x.y> and expect to \
        re-validate. Searched: $(CY_TOOLS_PATHS))
endif

# Absolute path to the compiler's "bin" directory.
CY_COMPILER_GCC_ARM_DIR?=

ifeq ($(CY_TOOLS_DIR),)
$(error Unable to find any of the available CY_TOOLS_PATHS -- $(CY_TOOLS_PATHS))
endif
