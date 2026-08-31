/*******************************************************************************
 * File Name: tesaiot_ui_theme.h
 *
 * Description: TESAIoT UI Design System — Premium Warm Dark Theme.
 *              Colors, typography, spacing, and layout proportions.
 *
 *              Visual direction: minimal, elegant, airy, calm contrast.
 *              Golden Ratio (phi = 1.618) for harmonious proportions.
 *              Warm charcoal palette with controlled red accent.
 *              All sizes optimized for 800x480 (4.3" DSI) touchscreen.
 *              Touch targets meet WCAG 2.5.5 AAA (44px minimum).
 *
 *******************************************************************************/

#ifndef TESAIOT_UI_THEME_H
#define TESAIOT_UI_THEME_H

#include "lvgl.h"

/*******************************************************************************
 * Golden Ratio Constants
 *******************************************************************************/
#define UI_PHI           1.618f
#define UI_PHI_INV       0.618f  /* 1/phi */

/*******************************************************************************
 * Display Dimensions (800x480 — Waveshare 4.3" DSI)
 *******************************************************************************/
#define UI_SCREEN_W      800
#define UI_SCREEN_H      480
#define UI_STATUS_BAR_H  32
#define UI_TAB_BAR_H     46
#define UI_CONTENT_H     (UI_SCREEN_H - UI_STATUS_BAR_H)  /* 448 */

/* Golden Ratio horizontal split */
#define UI_PHI_MAJOR_W   494   /* 800 / phi */
#define UI_PHI_MINOR_W   306   /* 800 - 494 */

/* Golden Ratio vertical split (content area) */
#define UI_PHI_MAJOR_H   277   /* 448 / phi */
#define UI_PHI_MINOR_H   171   /* 448 - 277 */

/*******************************************************************************
 * Spacing Scale (phi-based: 4, 6, 10, 16, 26, 42)
 *******************************************************************************/
#define UI_SPACE_XS       4
#define UI_SPACE_SM       6
#define UI_SPACE_MD      10
#define UI_SPACE_LG      16
#define UI_SPACE_XL      26
#define UI_SPACE_XXL     42

/*******************************************************************************
 * Touch Target Sizes (WCAG 2.5.5 AAA compliant)
 *******************************************************************************/
#define UI_TOUCH_MIN     48    /* Minimum touch target (Google MD) */
#define UI_TOUCH_PRIMARY 56    /* Primary action / elderly-friendly */
#define UI_TOUCH_SPACING  8    /* Minimum gap between targets */

/*******************************************************************************
 * Radius Scale (phi-based: 6, 10, 16, 26)
 *******************************************************************************/
#define UI_RADIUS_SM      6
#define UI_RADIUS_MD     10
#define UI_RADIUS_LG     16    /* Cards */
#define UI_RADIUS_XL     26    /* Full-shell frame */

/*******************************************************************************
 * Card Dimensions
 *******************************************************************************/
#define UI_CARD_RADIUS   UI_RADIUS_LG   /* 16 */
#define UI_CARD_BORDER    0              /* No visible border — premium clean */
#define UI_CARD_PAD      UI_SPACE_LG    /* 16px */

/* Standard card sizes for 800x480 */
#define UI_CARD_FULL_W   770   /* 800 - 2*15 margin */
#define UI_CARD_HALF_W   380   /* (770 - 10 gap) / 2 */
#define UI_CARD_THIRD_W  250   /* (770 - 2*10 gap) / 3 */
#define UI_CARD_QUARTER_W 185  /* (770 - 3*10 gap) / 4 */

/*******************************************************************************
 * Background Colors (Warm Dark Premium — depth hierarchy)
 *******************************************************************************/
#define UI_COLOR_BG_DEEP     0x1A120F  /* Screen background (warm charcoal) */
#define UI_COLOR_BG_CARD     0x2A1712  /* Card / surface 1 (warm glass) */
#define UI_COLOR_BG_SURFACE  0x4A2520  /* Elevated surface 2 */
#define UI_COLOR_BG_HOVER    0x6A3A31  /* Hover/active / border subtle */

/*******************************************************************************
 * Text Colors (Warm contrast hierarchy)
 *******************************************************************************/
#define UI_COLOR_TEXT_PRIMARY   0xF5F2EE  /* Primary (warm white) */
#define UI_COLOR_TEXT_SECONDARY 0xCFC6BF  /* Secondary (warm gray) */
#define UI_COLOR_TEXT_DISABLED  0x9A918A  /* Muted / hint */
#define UI_COLOR_TEXT_SUBTLE   0x795533  /* Warm brown (sensor name labels) */
#define UI_COLOR_TEXT_MUTED    0x555555  /* Gray (compass minor ticks, close btn) */

/*******************************************************************************
 * Semantic Accent Colors
 *******************************************************************************/
#define UI_COLOR_ACCENT         0xE1482F  /* Primary accent (warm red) */
#define UI_COLOR_ACCENT_CYAN    0x71C7EC  /* Interactive / info / accent-alt */
#define UI_COLOR_ACCENT_GREEN   0x50D890  /* Success / active / ON */
#define UI_COLOR_ACCENT_ORANGE  0xF2B84B  /* Warning / caution */
#define UI_COLOR_ACCENT_RED     0xE85B5B  /* Error / danger / OFF */
#define UI_COLOR_ACCENT_PURPLE  0xBB86FC  /* AI indicator */
#define UI_COLOR_ACCENT_BLUE    0x448AFF  /* Navigation / links */

/*******************************************************************************
 * Sensor-Specific Colors (consistent across all pages)
 *******************************************************************************/
#define UI_COLOR_SENSOR_IMU     0x4CAF50  /* BMI270 — green */
#define UI_COLOR_SENSOR_BARO    0xFF9800  /* DPS368 — orange */
#define UI_COLOR_SENSOR_ENV     0x2196F3  /* SHT40 — blue */
#define UI_COLOR_SENSOR_MAG     0xE040FB  /* BMM350 — purple */
#define UI_COLOR_SENSOR_TOUCH   0x00BCD4  /* CapSense — teal */
#define UI_COLOR_SENSOR_POT     0x8BC34A  /* Pot — lime */
#define UI_COLOR_SENSOR_RADAR   0xFF1744  /* Radar — red */
#define UI_COLOR_SENSOR_JOY     0xFFD740  /* Joystick — amber */

/*******************************************************************************
 * Typography (Montserrat — Golden Ratio scale from 16px base)
 *******************************************************************************/
#define UI_FONT_STATUS   &lv_font_montserrat_12  /* Status bar / tiny labels */
#define UI_FONT_CAPTION  &lv_font_montserrat_14  /* Captions / meta */
#define UI_FONT_BODY     &lv_font_montserrat_16  /* Default body */
#define UI_FONT_H3       &lv_font_montserrat_20  /* Section headers */
#define UI_FONT_H2       &lv_font_montserrat_24  /* Card titles */
#define UI_FONT_H1       &lv_font_montserrat_28  /* Page titles */
#define UI_FONT_DISPLAY  &lv_font_montserrat_36  /* Hero numbers */
#define UI_FONT_BIG_NUM  &lv_font_montserrat_40  /* Gauge values */

/*******************************************************************************
 * Arc Gauge Defaults
 *******************************************************************************/
#define UI_ARC_SIZE       180
#define UI_ARC_WIDTH       16
#define UI_ARC_START_ANGLE 135  /* 7-o'clock position */
#define UI_ARC_SWEEP       270  /* 270-degree sweep */

/*******************************************************************************
 * Chart Defaults
 *******************************************************************************/
#define UI_CHART_POINTS    72   /* Dense sampling for smooth LINE rendering (VG-Lite) */
#define UI_CHART_LINE_W     2   /* Width >= 2 enables rounded caps/joins */
#define UI_CHART_HEIGHT   160   /* Default chart height */

/*******************************************************************************
 * Compass Defaults (line-based — legacy)
 *******************************************************************************/
#define UI_COMPASS_SIZE   160
#define UI_COMPASS_NEEDLE_W 4
#define UI_COMPASS_NEEDLE_LEN ((UI_COMPASS_SIZE / 2) - 20)

/*******************************************************************************
 * Heading Gauge Defaults (lv_scale round gauge — preferred)
 *
 * Based on lv_example_scale_4 pattern (round outer gauge with needle).
 * Full 360deg sweep, cardinal labels (N/E/S/W), red North section.
 *******************************************************************************/
#define UI_HEADING_GAUGE_DASH_SIZE  160   /* Dashboard compact gauge (px) */
#define UI_HEADING_GAUGE_FULL_SIZE  200   /* Motion tab full gauge (px) */

/*******************************************************************************
 * Apple-Style Compass Defaults (lv_scale rotating dial)
 *
 * Black bg, white tick marks every 10°, labels every 30° (N/30/60/E/...),
 * red North section, fixed pointer at top, rotating dial.
 *******************************************************************************/
#define UI_COMPASS_APPLE_SIZE  260   /* Motion tab Apple compass (px) */

/*******************************************************************************
 * Brand Identity — Home hero tagline
 *
 * Tagline shown after the "BENTO ◉⦿" wordmark on the Home hero. Gated on
 * ENABLE_PAGE_BENTO_BUDDY — the canonical Buddy/Playground build switch
 * (set =1 on the DualBand firmware, =0 on Playground), propagated to the
 * CM55 compile via DEFINES. Keeping it here lets both firmware variants
 * share one theme header instead of hardcoding the string in page_home.c.
 *******************************************************************************/
#if defined(ENABLE_PAGE_BENTO_BUDDY) && (ENABLE_PAGE_BENTO_BUDDY == 1)
    #define HOME_BRAND_TAGLINE  "Your Trust Buddy."
#else
    #define HOME_BRAND_TAGLINE  "Playground : Make Anything."
#endif

#endif /* TESAIOT_UI_THEME_H */
