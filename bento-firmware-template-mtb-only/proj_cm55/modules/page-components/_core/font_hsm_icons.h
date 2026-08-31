/*******************************************************************************
 * File Name: font_hsm_icons.h
 *
 * Description: Custom LVGL font — FontAwesome security icons at 28px.
 *              Lock (U+F023), Unlock (U+F09C), Key (U+F084),
 *              User-secret (U+F21B), Lock-open (U+F3C1).
 *
 *              Generated with lv_font_conv from FontAwesome5.
 *
 *******************************************************************************/

#ifndef FONT_HSM_ICONS_H
#define FONT_HSM_ICONS_H

#include "lvgl.h"

/* 28px security icons */
extern const lv_font_t font_hsm_icons_28;

/* UTF-8 encoded icon strings for use with lv_label_set_text() */
#define HSM_ICON_LOCK        "\xEF\x80\xA3"   /* U+F023 — closed padlock */
#define HSM_ICON_UNLOCK      "\xEF\x82\x9C"   /* U+F09C — open padlock */
#define HSM_ICON_KEY         "\xEF\x82\x84"   /* U+F084 — key */
#define HSM_ICON_USER_SECRET "\xEF\x88\x9B"   /* U+F21B — user with secret */
#define HSM_ICON_LOCK_OPEN   "\xEF\x8F\x81"   /* U+F3C1 — lock open alt */

#endif /* FONT_HSM_ICONS_H */
