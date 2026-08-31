# TESAIoT Static Library Makefile
# This creates libtesaiot.a from tesaiot_* source files
#
# Usage (from project root that contains proj_cm33_ns/):
#   make -C path/to/BENTO-TESAIoT-libraries/kit-pse84-ai/tesaiot \
#        PROJECT_ROOT=/path/to/KIT_PSE84_AI-MicroPython-AI-Core
#
#   make clean     - Clean build artifacts
#
# The pre-built lib/libtesaiot.a is distributed for projects without source access.
# Source files (tesaiot_*.c) are proprietary and live in PROJECT_ROOT/proj_cm33_ns/.
#

# Paths — PROJECT_ROOT must point to the AI Kit project that has tesaiot_*.c in proj_cm33_ns/
PROJECT_ROOT ?= $(error PROJECT_ROOT must be set to the AI Kit project root, e.g. /path/to/KIT_PSE84_AI-MicroPython-AI-Core)
SRC_DIR := $(PROJECT_ROOT)/proj_cm33_ns
LICENSE_SRC_DIR := src
BUILD_DIR := build
OUTPUT_DIR := lib
INCLUDE_DIR := include

# Library name
LIB_NAME := libtesaiot.a

# Toolchain (same as project)
GCC_PATH := /Applications/mtb-gcc-arm-eabi/14.2.1/gcc/bin
CC := $(GCC_PATH)/arm-none-eabi-gcc
AR := $(GCC_PATH)/arm-none-eabi-ar

# Compiler flags for PSoC Edge Cortex-M33 (MUST match project: softfp)
CFLAGS := -mcpu=cortex-m33 -mthumb -mfloat-abi=softfp -mfpu=fpv5-sp-d16
CFLAGS += -O2 -g -Wall
CFLAGS += -ffunction-sections -fdata-sections

# MTB Shared path (resolved relative to PROJECT_ROOT)
MTB_SHARED := $(PROJECT_ROOT)/../mtb_shared
BSP_DIR := $(PROJECT_ROOT)/bsps/TARGET_KIT_PSE84_AI
BENTO_COMMON := $(dir $(lastword $(MAKEFILE_LIST)))../../common

# Include paths (comprehensive - from project build response file)
INCLUDES := \
    -I$(SRC_DIR) \
    -I$(INCLUDE_DIR) \
    -I$(BSP_DIR) \
    -I$(BSP_DIR)/config/GeneratedSource \
    -I$(BSP_DIR)/config \
    -I$(MTB_SHARED)/core-lib/release-v1.6.0/include \
    -I$(MTB_SHARED)/mtb-hal-cat1/release-v2.8.0/include \
    -I$(MTB_SHARED)/mtb-hal-cat1/release-v2.8.0/COMPONENT_CAT1D/include \
    -I$(MTB_SHARED)/mtb-dsl-pse8xxgp/release-v1.2.0 \
    -I$(MTB_SHARED)/mtb-dsl-pse8xxgp/release-v1.2.0/pdl/devices/include \
    -I$(MTB_SHARED)/mtb-dsl-pse8xxgp/release-v1.2.0/pdl/devices/include/ip \
    -I$(MTB_SHARED)/mtb-dsl-pse8xxgp/release-v1.2.0/pdl/drivers/include \
    -I$(MTB_SHARED)/mtb-dsl-pse8xxgp/release-v1.2.0/pdl/drivers/third_party/ethernet/include \
    -I$(MTB_SHARED)/mtb-dsl-pse8xxgp/release-v1.2.0/hal/include \
    -I$(MTB_SHARED)/mtb-dsl-pse8xxgp/release-v1.2.0/device-utils/syspm/include \
    -I$(MTB_SHARED)/freertos/release-v10.6.202/Source/include \
    -I$(MTB_SHARED)/freertos/release-v10.6.202/Source/portable/COMPONENT_CM33 \
    -I$(MTB_SHARED)/freertos/release-v10.6.202/Source/portable/COMPONENT_CM33/TOOLCHAIN_GCC_ARM/COMPONENT_FREERTOS_NTZ \
    -I$(MTB_SHARED)/abstraction-rtos/release-v1.12.0/include \
    -I$(MTB_SHARED)/abstraction-rtos/release-v1.12.0/include/COMPONENT_FREERTOS \
    -I$(MTB_SHARED)/lwip/STABLE-2_1_2_RELEASE/src/include \
    -I$(MTB_SHARED)/lwip/STABLE-2_1_2_RELEASE/src/include/lwip \
    -I$(MTB_SHARED)/lwip-freertos-integration/release-v1.2.0/arch \
    -I$(MTB_SHARED)/lwip-network-interface-integration/release-v1.7.0/include \
    -I$(MTB_SHARED)/mqtt/release-v4.6.090/include \
    -I$(MTB_SHARED)/secure-sockets/release-v3.12.1/include \
    -I$(MTB_SHARED)/optiga-trust-m/release-v5.3.0/include \
    -I$(MTB_SHARED)/optiga-trust-m/release-v5.3.0/include/common \
    -I$(MTB_SHARED)/optiga-trust-m/release-v5.3.0/include/cmd \
    -I$(MTB_SHARED)/optiga-trust-m/release-v5.3.0/include/comms \
    -I$(MTB_SHARED)/optiga-trust-m/release-v5.3.0/include/pal \
    -I$(MTB_SHARED)/optiga-trust-m/release-v5.3.0/include/ifx_i2c \
    -I$(MTB_SHARED)/optiga-trust-m/release-v5.3.0/config \
    -I$(MTB_SHARED)/cmsis/release-v6.1.0/Core/Include \
    -I$(MTB_SHARED)/cmsis/release-v6.1.0/Core/Include/m-profile \
    -I$(MTB_SHARED)/ifx-mbedtls/release-v3.6.400/include \
    -I$(MTB_SHARED)/ifx-mbedtls/release-v3.6.400/include/mbedtls \
    -I$(MTB_SHARED)/ifx-mbedtls/release-v3.6.400/include/psa \
    -I$(MTB_SHARED)/ifx-mbedtls/release-v3.6.400/library \
    -I$(MTB_SHARED)/retarget-io/release-v1.9.0/include \
    -I$(MTB_SHARED)/connectivity-utilities/release-v4.5.2 \
    -I$(MTB_SHARED)/wifi-connection-manager/release-v4.1.0/include \
    -I$(MTB_SHARED)/wifi-host-driver/release-v5.0.8/WHD/COMPONENT_WIFI6/inc \
    -I$(MTB_SHARED)/wifi-core-freertos-lwip-mbedtls/release-v3.1.0/configs \
    -I$(MTB_SHARED)/clib-support/release-v1.8.0/include \
    -I$(MTB_SHARED)/mtb-srf/release-v1.1.0/include \
    -I$(MTB_SHARED)/mtb-srf/release-v1.1.0/include/COMPONENT_NON_SECURE_DEVICE \
    -I$(MTB_SHARED)/mtb-srf/release-v1.1.0/include/COMPONENT_NON_SECURE_DEVICE/COMPONENT_MW_MTB_IPC \
    -I$(MTB_SHARED)/mtb-ipc/release-v1.2.0/include \
    -I$(MTB_SHARED)/async-transfer/release-v1.1.0/include \
    -I$(MTB_SHARED)/whd-bsp-integration/release-v2.5.0 \
    -I$(MTB_SHARED)/se-rt-services-utils/release-v1.2.0

# Defines (match project)
DEFINES := \
    -DCOMPONENT_FREERTOS \
    -DCOMPONENT_FREERTOS_NTZ \
    -DCOMPONENT_LWIP \
    -DCOMPONENT_CAT1D \
    -DCOMPONENT_CM33 \
    -DCOMPONENT_CM33_0 \
    -DCOMPONENT_NON_SECURE_DEVICE \
    -DCOMPONENT_PSE84 \
    -DCOMPONENT_SOFTFP \
    -DCOMPONENT_MBEDTLS \
    -DCOMPONENT_OPTIGA_CYHAL \
    -DCY_USING_HAL \
    -DCYBSP_WIFI_CAPABLE \
    -DCY_RTOS_AWARE \
    -DFLASH_BOOT \
    -DCY_PDL_FLASH_BOOT \
    -DMBEDTLS_CONFIG_FILE='"mbedtls/mbedtls_config.h"' \
    -DMBEDTLS_USER_CONFIG_FILE='"mbedtls_user_config.h"' \
    -DOPTIGA_LIB_EXTERNAL='"optiga_lib_config_mtb.h"'

# TESAIoT source files (all tesaiot_*.c from proj_cm33_ns)
TESAIOT_SOURCES := \
    $(SRC_DIR)/tesaiot_csr_workflow.c \
    $(SRC_DIR)/tesaiot_mqtt.c \
    $(SRC_DIR)/tesaiot_optiga.c \
    $(SRC_DIR)/tesaiot_optiga_manager.c \
    $(SRC_DIR)/tesaiot_optiga_trust_m.c \
    $(SRC_DIR)/tesaiot_protected_update_isolated.c \
    $(SRC_DIR)/tesaiot_protected_update_workflow.c \
    $(SRC_DIR)/tesaiot_sntp_client.c

# License source (new version with embedded public key)
LICENSE_SOURCE := $(LICENSE_SRC_DIR)/tesaiot_license.c

# Object files
TESAIOT_OBJECTS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(TESAIOT_SOURCES))
LICENSE_OBJECT := $(BUILD_DIR)/tesaiot_license.o
ALL_OBJECTS := $(TESAIOT_OBJECTS) $(LICENSE_OBJECT)

.PHONY: all clean copy_headers

all: copy_headers $(OUTPUT_DIR)/$(LIB_NAME)
	@echo ""
	@echo "============================================"
	@echo "  TESAIoT Library Built Successfully!"
	@echo "============================================"
	@echo "Library: $(OUTPUT_DIR)/$(LIB_NAME)"
	@echo "Headers: $(INCLUDE_DIR)/"
	@echo ""
	@echo "License System: ECDSA Signature Verification"
	@echo "Public Key: Embedded in library"
	@echo ""
	@echo "Contents:"
	@$(AR) -t $(OUTPUT_DIR)/$(LIB_NAME)
	@echo ""

$(OUTPUT_DIR)/$(LIB_NAME): $(ALL_OBJECTS) | $(OUTPUT_DIR)
	@echo ""
	@echo "Creating static library: $@"
	$(AR) rcs $@ $(ALL_OBJECTS)

# Compile tesaiot_*.c from proj_cm33_ns
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	@echo "Compiling: $<"
	$(CC) $(CFLAGS) $(INCLUDES) $(DEFINES) -c $< -o $@

# Compile license source from tesaiot/src
$(BUILD_DIR)/tesaiot_license.o: $(LICENSE_SRC_DIR)/tesaiot_license.c | $(BUILD_DIR)
	@echo "Compiling: $<"
	$(CC) $(CFLAGS) $(INCLUDES) $(DEFINES) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(OUTPUT_DIR):
	mkdir -p $(OUTPUT_DIR)

# Copy all tesaiot_*.h headers to include directory
copy_headers:
	@echo "Copying headers to $(INCLUDE_DIR)/..."
	@mkdir -p $(INCLUDE_DIR)
	@rsync -a $(SRC_DIR)/tesaiot_*.h $(INCLUDE_DIR)/ 2>/dev/null || true
	@echo "Headers copied."

clean:
	rm -rf $(BUILD_DIR) $(OUTPUT_DIR)/$(LIB_NAME)
