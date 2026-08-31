# =============================================================================
# thai_text.mk — single-line Makefile inclusion for the BENTO Thai text library
#
# Usage from a project's proj_cm55/Makefile (or any core's Makefile):
#     include $(BENTO_COMMON)/thai_text/thai_text.mk
#
# Adds: thai_to_pua() runtime (L1), thai_label_set_text() LVGL adapter (L2),
#       and 5 bitmap fonts (lv_font_noto_thai_{14,16,20,24,28}).
#
# Requires LVGL to be linkable (for L2 + bitmap fonts). If you want only the
# renderer-agnostic L1 core, set THAI_TEXT_SKIP_LVGL=1 BEFORE including this
# file — that drops the adapter + fonts and only compiles thai_shaping.c.
# =============================================================================

THAI_TEXT_DIR := $(BENTO_COMMON)/thai_text

# Public API headers
INCLUDES += $(THAI_TEXT_DIR)/api

# L1 core runtime (also exposes cluster_table.h via src/ include for debug)
INCLUDES += $(THAI_TEXT_DIR)/src
SOURCES  += $(THAI_TEXT_DIR)/src/thai_shaping.c

ifneq ($(THAI_TEXT_SKIP_LVGL),1)
    # L2 LVGL adapter
    SOURCES += $(THAI_TEXT_DIR)/src/lvgl_adapter/thai_lvgl_adapter.c

    # L3 bitmap fonts (one .c per size). Wildcard tolerates regen adding sizes.
    SOURCES += $(wildcard $(THAI_TEXT_DIR)/src/fonts/*.c)
endif
