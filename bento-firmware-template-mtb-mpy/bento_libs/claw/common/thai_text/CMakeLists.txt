# =============================================================================
# thai_text — standalone CMake build for use outside BENTO.
#
# Targets:
#   thai_shape       (static)  Always built. L1: thai_to_pua(), libc-only.
#   thai_lvgl        (static)  Built when THAI_TEXT_LVGL_DIR is set. L2: LVGL adapter.
#
# Usage:
#   mkdir build && cd build
#   cmake ..                                       # L1 only
#   cmake .. -DTHAI_TEXT_LVGL_DIR=/path/to/lvgl    # L1 + L2 adapter
#   cmake --build .
#
# The 5 bitmap font .c files (src/fonts/*) intentionally are NOT linked into
# any library — they must be compiled into the consuming application alongside
# LVGL's own font headers, since lv_font_t layout is LVGL-version-specific.
# =============================================================================
cmake_minimum_required(VERSION 3.15)
project(thai_text
        VERSION 0.4.0
        DESCRIPTION "Thai cluster shaping for bitmap text renderers"
        LANGUAGES C)

# ---- L1 core: renderer-agnostic, no external deps ---------------------------
add_library(thai_shape STATIC
    src/thai_shaping.c
)
target_include_directories(thai_shape
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/api>
    PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)
target_compile_features(thai_shape PUBLIC c_std_99)
set_target_properties(thai_shape PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

# ---- L2 LVGL adapter: optional ---------------------------------------------
if(DEFINED THAI_TEXT_LVGL_DIR)
    if(NOT EXISTS "${THAI_TEXT_LVGL_DIR}/lvgl.h")
        message(FATAL_ERROR
            "THAI_TEXT_LVGL_DIR=${THAI_TEXT_LVGL_DIR} does not contain lvgl.h")
    endif()
    add_library(thai_lvgl STATIC
        src/lvgl_adapter/thai_lvgl_adapter.c
    )
    target_include_directories(thai_lvgl
        PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/api>
                $<BUILD_INTERFACE:${THAI_TEXT_LVGL_DIR}>
    )
    target_link_libraries(thai_lvgl PUBLIC thai_shape)
    set_target_properties(thai_lvgl PROPERTIES POSITION_INDEPENDENT_CODE ON)
    message(STATUS "thai_text: L2 LVGL adapter enabled (LVGL at ${THAI_TEXT_LVGL_DIR})")
else()
    message(STATUS "thai_text: L1 core only (pass -DTHAI_TEXT_LVGL_DIR=... for L2)")
endif()

# ---- Install rule (optional; for distro packaging) -------------------------
include(GNUInstallDirs)
install(TARGETS thai_shape
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES api/thai_shaping.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
if(TARGET thai_lvgl)
    install(TARGETS thai_lvgl
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(FILES api/thai_lvgl_adapter.h
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()
