# =============================================================================
# usb_hid_joystick — standalone CMake build for use outside BENTO.
#
# Targets:
#   hid_f310_parser   (static)  Always built. L1: pure decoder, libc only.
#   test_hid_f310     (exe)     Host-side unit tests; CTest registers it.
#
# The L2 USB adapter (src/usb_adapter/usb_hid_joystick.c) intentionally is
# NOT exposed here — it depends on SEGGER emUSB-Host (proprietary) and on
# FreeRTOS. BENTO consumes it via usb_hid_joystick.mk; external integrators
# wanting USB host support must add it explicitly with their own SEGGER
# headers in scope.
#
# Usage:
#   mkdir build && cd build
#   cmake .. && cmake --build . && ctest
# =============================================================================
cmake_minimum_required(VERSION 3.15)
project(usb_hid_joystick
        VERSION 0.1.0
        DESCRIPTION "Logitech F310 HID joystick parser + emUSB-Host adapter"
        LANGUAGES C)

# ---- L1 core: pure parser, libc only ----------------------------------------
add_library(hid_f310_parser STATIC
    src/hid_f310_parser.c
)
target_include_directories(hid_f310_parser
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/api>
)
target_compile_features(hid_f310_parser PUBLIC c_std_99)
set_target_properties(hid_f310_parser PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---- Host-side unit tests ---------------------------------------------------
enable_testing()
add_executable(test_hid_f310_parser tests/test_hid_f310_parser.c)
target_link_libraries(test_hid_f310_parser PRIVATE hid_f310_parser)
add_test(NAME hid_f310_parser_unit COMMAND test_hid_f310_parser)

# ---- Install rule (optional; for distro packaging) --------------------------
include(GNUInstallDirs)
install(TARGETS hid_f310_parser
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES api/hid_f310_report.h api/hid_f310_parser.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
