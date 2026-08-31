#!/bin/bash
# ============================================================================
# TESAIoT Library Builder v3.0.0
# Creates universal libtesaiot.a (works with any device via license config)
#
# Usage: ./build_library.sh
#
# This script builds a UNIVERSAL library that works with any OPTIGA Trust M
# device. The device-specific license is configured at compile time via
# tesaiot_license_config.h in the customer's project.
# ============================================================================

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Paths
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TESAIOT_DIR="$(dirname "$SCRIPT_DIR")"
PROJECT_ROOT="$(dirname "$TESAIOT_DIR")"
BUILD_DIR="${TESAIOT_DIR}/build"
OUTPUT_DIR="${TESAIOT_DIR}/lib"
INCLUDE_DIR="${TESAIOT_DIR}/include"

# Project build directory (where MTB compiled the .o files)
PROJECT_BUILD_DIR="${PROJECT_ROOT}/proj_cm33_ns/build/Debug/local"

# Toolchain
GCC_PATH="/Applications/mtb-gcc-arm-eabi/14.2.1/gcc/bin"
AR="${GCC_PATH}/arm-none-eabi-ar"
STRIP="${GCC_PATH}/arm-none-eabi-strip"

echo ""
echo -e "${GREEN}╔══════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║          TESAIoT Library Builder v3.0.0                      ║${NC}"
echo -e "${GREEN}╠══════════════════════════════════════════════════════════════╣${NC}"
echo -e "${GREEN}║  Universal Library with ECDSA License Key System             ║${NC}"
echo -e "${GREEN}╚══════════════════════════════════════════════════════════════╝${NC}"
echo ""

# Check if project was built
if [ ! -d "$PROJECT_BUILD_DIR" ]; then
    echo -e "${RED}Error: Project not built${NC}"
    echo ""
    echo "Please build the main project first:"
    echo "  cd proj_cm33_ns && make build"
    echo ""
    exit 1
fi

# Create directories
mkdir -p "${BUILD_DIR}"
mkdir -p "${OUTPUT_DIR}"
mkdir -p "${INCLUDE_DIR}"

# ============================================================================
# Step 1: Verify headers
# ============================================================================
echo -e "${YELLOW}[1/4] Verifying headers...${NC}"

# Headers are maintained in tesaiot/include/ (source of truth)
# No copy needed - just verify they exist
HEADER_COUNT=0
for header in "${INCLUDE_DIR}/"*.h; do
    if [ -f "$header" ]; then
        HEADER_COUNT=$((HEADER_COUNT + 1))
    fi
done

if [ $HEADER_COUNT -eq 0 ]; then
    echo -e "${RED}Error: No headers found in ${INCLUDE_DIR}/${NC}"
    exit 1
fi

echo "      Found ${HEADER_COUNT} headers in: ${INCLUDE_DIR}/"

# ============================================================================
# Step 2: Copy pre-built object files
# ============================================================================
echo ""
echo -e "${YELLOW}[2/4] Copying pre-built object files...${NC}"

OBJECT_FILES=""
OBJECT_COUNT=0

# List of tesaiot object files to include
# NOTE: tesaiot_license.o is INCLUDED (verification logic is IP-protected)
#       Customer provides license data via tesaiot_license_data.c (extern variables)
TESAIOT_OBJECTS=(
    "tesaiot_license.o"
    "tesaiot_crypto.o"
    "tesaiot_mqtt.o"
    "tesaiot_optiga.o"
    "tesaiot_optiga_manager.o"
    "tesaiot_optiga_trust_m.o"
    "tesaiot_protected_update_isolated.o"
    "tesaiot_protected_update_workflow.o"
    "tesaiot_sntp_client.o"
)

for obj in "${TESAIOT_OBJECTS[@]}"; do
    # Special case: objects from tesaiot/src/ are in external/ build path
    if [ "$obj" = "tesaiot_license.o" ] || [ "$obj" = "tesaiot_crypto.o" ]; then
        EXTERNAL_PATH="${PROJECT_ROOT}/proj_cm33_ns/build/Debug/external${PROJECT_ROOT}/tesaiot/src/${obj}"
        if [ -f "$EXTERNAL_PATH" ]; then
            /bin/cp -f "$EXTERNAL_PATH" "${BUILD_DIR}/"
            echo "      Copied: ${obj} (from tesaiot/src/)"
            OBJECT_FILES="${OBJECT_FILES} ${BUILD_DIR}/${obj}"
            OBJECT_COUNT=$((OBJECT_COUNT + 1))
        else
            echo -e "      ${YELLOW}Warning: ${obj} not found at ${EXTERNAL_PATH}${NC}"
        fi
    elif [ -f "${PROJECT_BUILD_DIR}/${obj}" ]; then
        /bin/cp -f "${PROJECT_BUILD_DIR}/${obj}" "${BUILD_DIR}/"
        echo "      Copied: ${obj}"
        OBJECT_FILES="${OBJECT_FILES} ${BUILD_DIR}/${obj}"
        OBJECT_COUNT=$((OBJECT_COUNT + 1))
    else
        echo -e "      ${YELLOW}Warning: ${obj} not found${NC}"
    fi
done

if [ $OBJECT_COUNT -eq 0 ]; then
    echo -e "${RED}Error: No object files found!${NC}"
    echo "Make sure the project was built successfully."
    exit 1
fi

# ============================================================================
# Step 3: Strip debug symbols (production build)
# ============================================================================
echo ""
echo -e "${YELLOW}[3/4] Stripping debug symbols (production build)...${NC}"

for obj_file in ${OBJECT_FILES}; do
    ${STRIP} --strip-debug "${obj_file}" 2>/dev/null || true
    echo "      Stripped: $(basename ${obj_file})"
done

# ============================================================================
# Step 4: Create static library
# ============================================================================
echo ""
echo -e "${YELLOW}[4/4] Creating static library...${NC}"

rm -f "${OUTPUT_DIR}/libtesaiot.a"
${AR} rcs "${OUTPUT_DIR}/libtesaiot.a" ${OBJECT_FILES}
echo "      Created: ${OUTPUT_DIR}/libtesaiot.a"

# Generate SHA256 checksum for integrity verification
CHECKSUM=$(shasum -a 256 "${OUTPUT_DIR}/libtesaiot.a" | cut -d' ' -f1)
echo "${CHECKSUM}  libtesaiot.a" > "${OUTPUT_DIR}/libtesaiot.a.sha256"
echo "      Checksum: ${OUTPUT_DIR}/libtesaiot.a.sha256"

# ============================================================================
# Summary
# ============================================================================
echo ""
echo -e "${GREEN}╔══════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║                    BUILD COMPLETE                            ║${NC}"
echo -e "${GREEN}╠══════════════════════════════════════════════════════════════╣${NC}"
echo -e "${GREEN}║ Library:  tesaiot/lib/libtesaiot.a                           ║${NC}"
echo -e "${GREEN}║ Headers:  tesaiot/include/                                   ║${NC}"
echo -e "${GREEN}║ Version:  3.0.0 (TESAIoT + Developer Crypto Utilities)       ║${NC}"
echo -e "${GREEN}║ Objects:  ${OBJECT_COUNT} modules                                         ║${NC}"
echo -e "${GREEN}║                                                              ║${NC}"
echo -e "${GREEN}║ SHA256: ${CHECKSUM:0:40}...                  ║${NC}"
echo -e "${GREEN}╚══════════════════════════════════════════════════════════════╝${NC}"
echo ""
echo "Library contents:"
${AR} -t "${OUTPUT_DIR}/libtesaiot.a"
echo ""
echo "Library size: $(ls -lh "${OUTPUT_DIR}/libtesaiot.a" | awk '{print $5}')"
echo ""
echo -e "${CYAN}TESAIoT Library v3.0.0 Features:${NC}"
echo "  - Universal library (works with any OPTIGA Trust M device)"
echo "  - License verification logic IP-protected (in library)"
echo "  - License data binding via customer-compiled object"
echo "  - ECDSA P-256 signature verification (mbedTLS)"
echo "  - Hardware-bound to OPTIGA Trust M UID"
echo "  - Debug symbols stripped (production build)"
echo ""
echo -e "${CYAN}Customer Usage:${NC}"
echo "  1. Get device UID from Menu 1 (tesaiot_print_device_uid)"
echo "  2. Register on TESAIoT Server with UID"
echo "  3. Download tesaiot_license_config.h (UID + License Key)"
echo "  4. Place in tesaiot/include/ folder"
echo "  5. Build project - tesaiot_license_data.c compiled with your config"
echo "  6. License verified at runtime!"
echo ""
