/*******************************************************************************
 * File Name: page_gpio_rgb.c
 *
 * Description: GPIO & RGB Matrix page — QWA309 base-board I/O hub.
 *              TESAIoT Dev Kit only (BSP_HAS_QWA309_BASEBOARD).
 *
 *              Single page that merges the former Potentiometers and
 *              Touch & RGB pages:
 *
 *                TOP   - pot ADC state and the CapSense link state
 *                          (answer at I2C 0x08, protocol version).
 *
 *                LEFT  — "GPIO Inputs"
 *                          - 4 vertical bars, VR1-4 with their pins, each
 *                            shown in mV (raw x 1800 / 4095, 1.8 V full
 *                            scale) and raw 0-4095, read CM55-locally via
 *                            cm55_pot_read_all() (no IPC hop).
 *                          - 4 button indicators: CapSense BTN0/BTN1 (from the
 *                            IPC snapshot) + the two base-board push-buttons
 *                            on P17.5/P17.7 (read here on CM55 by PDL,
 *                            active-low pull-up), labelled with their pins.
 *                          - SW1-SW4 (two push-buttons, two slide switches)
 *                            via the CapSense controller, shown only when its
 *                            firmware speaks the 0x0D/0x0E snapshot protocol
 *                            (cm55_capsense_info()).
 *                          - CapSense slider bar (0-100 %).
 *
 *                BOTTOM - static notes on what the E84 can and cannot read
 *                          (scroll down).
 *
 *                RIGHT — "RGB Matrix 16x8" (DFR0522 @ I2C 0x10)
 *                          - Large on-screen preview of the current fill color.
 *                          - 8 color swatches; tapping one fills the physical
 *                            panel via the DFR0522 driver (GFX-task context, so
 *                            no IPC hop).
 *                          - Interaction: the P17.5 button cycles to the
 *                            next color and fills the matrix; the P17.7
 *                            button clears it. Their printed labels come
 *                            from QWA309_BTN_LABEL_P17_5 / _P17_7.
 *
 *              All draw + I2C happens in the GFX/LVGL task, which owns the
 *              display I2C bus the DFR0522 shares.
 *
 *******************************************************************************/

#include "page_gpio_rgb.h"

#if BSP_HAS_QWA309_BASEBOARD

#include "page_manager.h"
#include "tesaiot_ui_theme.h"
#include "tesaiot_ui_helpers.h"
#include "cm55_sensor_poll.h"
#include "cm55_capsense_frame.h"
#include "dfr0522_rgb.h"
#include "lvgl.h"

#include "cy_pdl.h"
#include "gpio_pse84_bga_220.h"

#include <stdio.h>
#include <string.h>

/*******************************************************************************
 * Constants
 *******************************************************************************/
#define POT_RAW_MAX        (4095)
/* The pot wipers swing between GND and the 1.8 V analog rail (QWA309
 * schematic V3.1, sheet 19), and the SAR reads that range as 0-4095. */
#define POT_FULL_SCALE_MV  (1800)

/* Printed labels of the two base-board push-buttons, by the pin each one
 * pulls low: SW6 on P17.5 (upper button) and SW5 on P17.7 (lower button). */
#define QWA309_BTN_LABEL_P17_5   "SW6"
#define QWA309_BTN_LABEL_P17_7   "SW5"

/* Base-board push-buttons: active-low tactile switches; the page sets
 * internal pull-ups. Cy_GPIO_Read() == 0 means pressed.
 * Index 0 = P17.5 (next color), index 1 = P17.7 (clear). */
#define SW_COUNT           (2U)

/* Printed knob label and the pin its wiper is on, in printed order; the
 * SAR channel for each is s_pot_adc_ch[] in cm55_sensor_poll.c. */
static const char    *s_pot_names[QWA309_POT_COUNT]  = {
    "VR1 P15.5", "VR2 P15.4", "VR3 P15.6", "VR4 P15.7"
};
static const uint32_t s_pot_colors[QWA309_POT_COUNT] = {
    UI_COLOR_ACCENT_GREEN,
    UI_COLOR_ACCENT_BLUE,
    UI_COLOR_ACCENT_ORANGE,
    UI_COLOR_SENSOR_TOUCH,
};

/* Four button indicators: [0]=BTN0 [1]=BTN1 (CapSense)
 * [2]=P17.5 button [3]=P17.7 button (GPIO) */
#define IND_COUNT          (4U)
static const char *s_ind_names[IND_COUNT] = {
    "BTN0", "BTN1",
    QWA309_BTN_LABEL_P17_5 " P17.5", QWA309_BTN_LABEL_P17_7 " P17.7"
};

/* Shown below the panels; the content area scrolls to reach it. */
static const char s_notes_text[] =
    "SW1-SW4 are read by the CapSense controller (PSoC 4000T), not the E84; "
    "the E84 sees them only through its I2C answer at 0x08, protocol 0x0D or "
    "0x0E. The power and function slide switches cannot be read by "
    "firmware. This page reconfigures P17.5 as an input with a pull-up; the "
    "USB-host VBUS enable shares that pin. The CapSense controller is probed "
    "once at start-up: after switching SW12 ON, restart the board.";

/*******************************************************************************
 * Color swatch table (DFR0522 palette 0..7)
 *******************************************************************************/
typedef struct {
    const char     *name;
    dfr0522_color_t color;
    uint32_t        swatch_hex;   /* on-screen swatch / preview color */
} rgb_swatch_def_t;

static const rgb_swatch_def_t s_swatches[] = {
    { "Off",    DFR0522_COLOR_OFF,    0x3A3A3A },
    { "Red",    DFR0522_COLOR_RED,    0xF44336 },
    { "Green",  DFR0522_COLOR_GREEN,  0x4CAF50 },
    { "Yellow", DFR0522_COLOR_YELLOW, 0xFFEB3B },
    { "Blue",   DFR0522_COLOR_BLUE,   0x448AFF },
    { "Purple", DFR0522_COLOR_PURPLE, 0x9C27B0 },
    { "Cyan",   DFR0522_COLOR_CYAN,   0x00BCD4 },
    { "White",  DFR0522_COLOR_WHITE,  0xF8FAFC },
};
#define NUM_SWATCHES  (sizeof(s_swatches) / sizeof(s_swatches[0]))

/*******************************************************************************
 * Module-Static Context
 *******************************************************************************/
typedef struct {
    /* Left — pots */
    lv_obj_t *pot_bar[QWA309_POT_COUNT];
    lv_obj_t *pot_value[QWA309_POT_COUNT];
    lv_obj_t *status_label;
    lv_obj_t *caps_link_label;
    /* Left — button indicators + SW1-SW4 + slider */
    lv_obj_t *ind[IND_COUNT];
    lv_obj_t *ind_label[IND_COUNT];
    lv_obj_t *sw14_label;
    lv_obj_t *slider_bar;
    lv_obj_t *slider_label;
    /* Right — RGB matrix */
    lv_obj_t *preview;
    lv_obj_t *preview_label;
    lv_obj_t *rgb_status;
    /* State */
    uint8_t   color_idx;        /* current DFR0522 fill color index (0..7) */
    bool      sw_prev[SW_COUNT];/* previous debounced SW state (edge detect) */
    bool      hw_ready;         /* SW GPIO initialized */
    /* Last-rendered values — widgets update ONLY on change. Repainting
     * every tick invalidates all the labels/pills each snapshot and the
     * whole page visibly flickers. */
    uint16_t  shown_pot[QWA309_POT_COUNT];   /* 0xFFFF = never drawn */
    int8_t    shown_live;                    /* -1 unknown, 0 settling, 1 live */
    uint8_t   shown_ind;                     /* bit i = ind[i] lit; 0xFF unknown */
    uint8_t   shown_slider;                  /* 0xFF never drawn, or a SLIDER_SHOWN_* */
    uint8_t   shown_link;                    /* link state drawn; 0xFF unknown */
    uint8_t   shown_proto;                   /* protocol in that text */
    uint8_t   shown_sw14;                    /* SW1-SW4 bits drawn; 0xFF unknown,
                                              * or a SW14_SHOWN_* text */
} page_gpio_rgb_ctx_t;

/* shown_slider values while a text is drawn instead of a position. */
#define SLIDER_SHOWN_UNTOUCHED  (0xFEU)   /* 0x0D/0x0E slider, nobody touching */
#define SLIDER_SHOWN_NO_LINK    (0xFDU)   /* no CapSense answer: no value at all */

/* CapSense link states drawn in caps_link_label. */
enum {
    LINK_NO_ANSWER = 0U,  /* no answer to the start-up probe */
    LINK_READ_FAIL,       /* answered once; the last read failed */
    LINK_OK_LEGACY,       /* reads succeed; legacy 3-byte firmware */
    LINK_OK_SNAPSHOT,     /* reads succeed; protocol 0x0D/0x0E */
    LINK_OK_FW_ERROR,     /* protocol 0x0D/0x0E, controller error flag set */
};

/* shown_sw14 values while a text is drawn instead of switch states. */
#define SW14_SHOWN_NEEDS_0D     (0xFEU)   /* legacy firmware answers */
#define SW14_SHOWN_NO_LINK      (0xFDU)   /* no answer / last read failed */

/* Pot ADC jitter deadband (counts). The 120-px bar shows ~34 counts per
 * pixel, so +/-5 counts of SAR noise is invisible — don't repaint for it. */
#define POT_DEADBAND  (6)

static page_gpio_rgb_ctx_t s_ctx;

static char s_value_buf[QWA309_POT_COUNT][16];
static char s_raw_buf[QWA309_POT_COUNT][16];
static char s_slider_buf[32];
static char s_link_buf[80];
static char s_sw14_buf[64];
static char s_hint_buf[64];

/*******************************************************************************
 * P17.5/P17.7 push-buttons — CM55-local PDL read (active-low, internal pull-up)
 *******************************************************************************/
static void sw_gpio_init(void)
{
    Cy_GPIO_Pin_FastInit(P17_5_PORT, P17_5_PIN,
                         CY_GPIO_DM_PULLUP, 1UL, HSIOM_SEL_GPIO);
    Cy_GPIO_Pin_FastInit(P17_7_PORT, P17_7_PIN,
                         CY_GPIO_DM_PULLUP, 1UL, HSIOM_SEL_GPIO);
    s_ctx.hw_ready = true;
}

/* Return true when the given button index (0=P17.5, 1=P17.7) is pressed. */
static bool sw_read(uint8_t idx)
{
    if (idx == 0U) {
        return (Cy_GPIO_Read(P17_5_PORT, P17_5_PIN) == 0U);
    }
    return (Cy_GPIO_Read(P17_7_PORT, P17_7_PIN) == 0U);
}

/*******************************************************************************
 * Apply a palette color to the physical matrix + on-screen preview
 *******************************************************************************/
static void apply_color(uint8_t idx)
{
    if (idx >= NUM_SWATCHES) idx = 0U;
    const rgb_swatch_def_t *sw = &s_swatches[idx];
    s_ctx.color_idx = idx;

    bool ok = (sw->color == DFR0522_COLOR_OFF) ? dfr0522_clear()
                                               : dfr0522_fill((uint8_t)sw->color);

    if (s_ctx.preview) {
        lv_obj_set_style_bg_color(s_ctx.preview,
                                  lv_color_hex(sw->swatch_hex), LV_PART_MAIN);
    }
    if (s_ctx.preview_label) {
        lv_label_set_text(s_ctx.preview_label, sw->name);
        lv_obj_set_style_text_color(s_ctx.preview_label,
            lv_color_hex(sw->color == DFR0522_COLOR_WHITE ||
                         sw->color == DFR0522_COLOR_YELLOW ||
                         sw->color == DFR0522_COLOR_OFF
                             ? 0x111827 : 0xF8FAFC), LV_PART_MAIN);
    }
    if (s_ctx.rgb_status) {
        if (ok) {
            lv_label_set_text_fmt(s_ctx.rgb_status, "Matrix: %s", sw->name);
            lv_obj_set_style_text_color(s_ctx.rgb_status,
                                        lv_color_hex(UI_COLOR_ACCENT_GREEN),
                                        LV_PART_MAIN);
        } else {
            lv_label_set_text(s_ctx.rgb_status, "Matrix: I2C error");
            lv_obj_set_style_text_color(s_ctx.rgb_status,
                                        lv_color_hex(UI_COLOR_ACCENT_ORANGE),
                                        LV_PART_MAIN);
        }
    }
}

/*******************************************************************************
 * Swatch click — fill the physical matrix (GFX task context)
 *******************************************************************************/
static void swatch_click_cb(lv_event_t *e)
{
    uint32_t idx = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    apply_color((uint8_t)idx);
}

/*******************************************************************************
 * Effect buttons — marquee + built-in DFR0522 animations (GFX task context)
 *******************************************************************************/
typedef struct {
    const char *label;
    uint32_t    accent_hex;
} fx_btn_def_t;

static const fx_btn_def_t s_fx_btns[] = {
    { "TESAIoT", 0x00BCD4 },   /* marquee, cyan */
    { "Stars",   0xF8FAFC },
    { "Wave",    0x448AFF },
    { "Pacman",  0xFFEB3B },
};
#define NUM_FX_BTNS  (sizeof(s_fx_btns) / sizeof(s_fx_btns[0]))

static void effect_click_cb(lv_event_t *e)
{
    uint32_t id = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    bool ok = false;

    switch (id) {
    case 0U: ok = dfr0522_scroll("TESAIoT", 7U, DFR0522_COLOR_CYAN, 70U); break;
    case 1U: ok = dfr0522_effect(DFR0522_FX_STARS, 0U);  break;
    case 2U: ok = dfr0522_effect(DFR0522_FX_WAVE, 0U);   break;
    case 3U: ok = dfr0522_effect(DFR0522_FX_PACMAN, 0U); break;
    default: return;
    }

    if (ok) {
        /* The panel is animating — the solid-color preview no longer
         * describes it. Show the effect name in its accent color, and park
         * color_idx on Off so the next P17.5 press starts the palette over. */
        s_ctx.color_idx = 0U;
        if (s_ctx.preview) {
            lv_obj_set_style_bg_color(s_ctx.preview,
                                      lv_color_hex(0x14171F), LV_PART_MAIN);
        }
        if (s_ctx.preview_label) {
            lv_label_set_text(s_ctx.preview_label, s_fx_btns[id].label);
            lv_obj_set_style_text_color(s_ctx.preview_label,
                                        lv_color_hex(s_fx_btns[id].accent_hex),
                                        LV_PART_MAIN);
        }
    }

    if (s_ctx.rgb_status) {
        if (ok) {
            lv_label_set_text_fmt(s_ctx.rgb_status, "Matrix: %s effect",
                                  s_fx_btns[id].label);
            lv_obj_set_style_text_color(s_ctx.rgb_status,
                                        lv_color_hex(UI_COLOR_ACCENT_GREEN),
                                        LV_PART_MAIN);
        } else {
            lv_label_set_text(s_ctx.rgb_status, "Matrix: effect failed");
            lv_obj_set_style_text_color(s_ctx.rgb_status,
                                        lv_color_hex(UI_COLOR_ACCENT_ORANGE),
                                        LV_PART_MAIN);
        }
    }
}

/*******************************************************************************
 * Small helper: create a rounded on/off indicator pill with a centered label
 *******************************************************************************/
static void indicator_create(lv_obj_t *parent, uint8_t i)
{
    s_ctx.ind[i] = lv_obj_create(parent);
    lv_obj_set_size(s_ctx.ind[i], 88, 46);
    lv_obj_set_style_radius(s_ctx.ind[i], UI_RADIUS_MD, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ctx.ind[i], lv_color_hex(UI_COLOR_BG_CARD),
                              LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ctx.ind[i], 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ctx.ind[i],
                                  lv_color_hex(UI_COLOR_SENSOR_TOUCH),
                                  LV_PART_MAIN);
    lv_obj_clear_flag(s_ctx.ind[i], LV_OBJ_FLAG_SCROLLABLE);

    s_ctx.ind_label[i] = lv_label_create(s_ctx.ind[i]);
    lv_label_set_text(s_ctx.ind_label[i], s_ind_names[i]);
    lv_obj_set_style_text_font(s_ctx.ind_label[i], UI_FONT_CAPTION,
                               LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ctx.ind_label[i],
                                lv_color_hex(UI_COLOR_TEXT_SECONDARY),
                                LV_PART_MAIN);
    lv_obj_center(s_ctx.ind_label[i]);
}

/*******************************************************************************
 * page_gpio_rgb_create
 *******************************************************************************/
lv_obj_t *page_gpio_rgb_create(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));
    memset(s_ctx.shown_pot, 0xFF, sizeof(s_ctx.shown_pot));
    s_ctx.shown_live   = -1;
    s_ctx.shown_ind    = 0xFF;
    s_ctx.shown_slider = 0xFF;
    s_ctx.shown_link   = 0xFF;
    s_ctx.shown_proto  = 0U;
    s_ctx.shown_sw14   = 0xFF;

    lv_obj_t *scr = lv_obj_create(NULL);
    page_manager_t *pm = pm_get_instance();
    lv_obj_t *content = pm_create_page_with_header(scr, pm,
                            "GPIO & RGB Matrix", UI_COLOR_ACCENT_GREEN);

    /* Content becomes a padded vertical stack */
    lv_obj_set_layout(content, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(content, UI_SPACE_LG, LV_PART_MAIN);
    lv_obj_set_style_pad_row(content, UI_SPACE_SM, LV_PART_MAIN);
    /* The notes below the panels do not fit on one screen: let the content
     * scroll vertically to reach them. */
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);

    /* Header status row: pot ADC ("Pots: live" / "Pots: ADC settling") and
     * the CapSense link state. */
    lv_obj_t *status_row = tesaiot_row_create(content, UI_SPACE_LG);
    s_ctx.status_label = lv_label_create(status_row);
    lv_label_set_text(s_ctx.status_label, "Pots: ADC settling");
    lv_obj_set_style_text_color(s_ctx.status_label,
                                lv_color_hex(UI_COLOR_ACCENT_ORANGE),
                                LV_PART_MAIN);
    lv_obj_set_style_text_font(s_ctx.status_label, UI_FONT_CAPTION,
                               LV_PART_MAIN);

    s_ctx.caps_link_label = lv_label_create(status_row);
    lv_obj_set_flex_grow(s_ctx.caps_link_label, 1);
    lv_label_set_long_mode(s_ctx.caps_link_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_ctx.caps_link_label, "CapSense 0x08: ...");
    lv_obj_set_style_text_color(s_ctx.caps_link_label,
                                lv_color_hex(UI_COLOR_TEXT_SECONDARY),
                                LV_PART_MAIN);
    lv_obj_set_style_text_font(s_ctx.caps_link_label, UI_FONT_CAPTION,
                               LV_PART_MAIN);

    lv_obj_t *main_row = tesaiot_row_create(content, UI_SPACE_LG);

    /***************************************************************************
     * LEFT panel — GPIO Inputs (pots + buttons + slider)
     ***************************************************************************/
    {
        lv_obj_t *left = tesaiot_col_create(main_row, UI_SPACE_MD);
        lv_obj_set_width(left, 400);

        lv_obj_t *title = lv_label_create(left);
        lv_label_set_text(title, "GPIO Inputs");
        lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_ACCENT_GREEN),
                                    LV_PART_MAIN);
        lv_obj_set_style_text_font(title, UI_FONT_H3, LV_PART_MAIN);

        /* --- Four potentiometer vertical bars --- */
        lv_obj_t *pot_row = tesaiot_row_create(left, UI_SPACE_SM);
        for (uint8_t i = 0; i < QWA309_POT_COUNT; i++) {
            lv_obj_t *col = tesaiot_col_create(pot_row, UI_SPACE_XS);
            lv_obj_set_flex_grow(col, 1);
            lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START,
                                  LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            lv_obj_t *name = lv_label_create(col);
            lv_label_set_text(name, s_pot_names[i]);
            lv_obj_set_style_text_color(name, lv_color_hex(s_pot_colors[i]),
                                        LV_PART_MAIN);
            lv_obj_set_style_text_font(name, UI_FONT_CAPTION, LV_PART_MAIN);

            s_ctx.pot_bar[i] = lv_bar_create(col);
            lv_obj_set_size(s_ctx.pot_bar[i], 34, 96);    /* vertical bar */
            lv_bar_set_range(s_ctx.pot_bar[i], 0, POT_RAW_MAX);
            lv_bar_set_value(s_ctx.pot_bar[i], 0, LV_ANIM_OFF);
            lv_obj_set_style_radius(s_ctx.pot_bar[i], UI_RADIUS_SM,
                                    LV_PART_MAIN);
            lv_obj_set_style_bg_color(s_ctx.pot_bar[i],
                                      lv_color_hex(UI_COLOR_BG_CARD),
                                      LV_PART_MAIN);
            lv_obj_set_style_bg_color(s_ctx.pot_bar[i],
                                      lv_color_hex(s_pot_colors[i]),
                                      LV_PART_INDICATOR);

            /* Two lines: millivolts, then the raw 12-bit count. */
            s_ctx.pot_value[i] = lv_label_create(col);
            lv_label_set_text(s_ctx.pot_value[i], "---- mV\nraw ----");
            lv_obj_set_style_text_align(s_ctx.pot_value[i],
                                        LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            lv_obj_set_style_text_color(s_ctx.pot_value[i],
                                        lv_color_hex(UI_COLOR_TEXT_SECONDARY),
                                        LV_PART_MAIN);
            lv_obj_set_style_text_font(s_ctx.pot_value[i], UI_FONT_CAPTION,
                                       LV_PART_MAIN);
        }

        /* --- Four button indicators: BTN0 BTN1 + the P17.5/P17.7 buttons --- */
        lv_obj_t *ind_row = tesaiot_row_create(left, UI_SPACE_SM);
        for (uint8_t i = 0; i < IND_COUNT; i++) {
            indicator_create(ind_row, i);
        }

        /* --- SW1-SW4 via the CapSense controller --- */
        s_ctx.sw14_label = lv_label_create(left);
        lv_obj_set_width(s_ctx.sw14_label, LV_PCT(100));
        lv_label_set_long_mode(s_ctx.sw14_label, LV_LABEL_LONG_WRAP);
        lv_label_set_text(s_ctx.sw14_label, "SW1-SW4: ...");
        lv_obj_set_style_text_color(s_ctx.sw14_label,
                                    lv_color_hex(UI_COLOR_TEXT_SECONDARY),
                                    LV_PART_MAIN);
        lv_obj_set_style_text_font(s_ctx.sw14_label, UI_FONT_CAPTION,
                                   LV_PART_MAIN);

        s_ctx.slider_bar = lv_bar_create(left);
        lv_obj_set_size(s_ctx.slider_bar, 360, 16);
        lv_bar_set_range(s_ctx.slider_bar, 0, 100);
        lv_bar_set_value(s_ctx.slider_bar, 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(s_ctx.slider_bar,
                                  lv_color_hex(UI_COLOR_BG_CARD), LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_ctx.slider_bar,
                                  lv_color_hex(UI_COLOR_SENSOR_TOUCH),
                                  LV_PART_INDICATOR);

        s_ctx.slider_label = lv_label_create(left);
        lv_label_set_text(s_ctx.slider_label, "CapSense slider: -- %");
        lv_obj_set_style_text_color(s_ctx.slider_label,
                                    lv_color_hex(UI_COLOR_TEXT_SECONDARY),
                                    LV_PART_MAIN);
        lv_obj_set_style_text_font(s_ctx.slider_label, UI_FONT_CAPTION,
                                   LV_PART_MAIN);
    }

    /***************************************************************************
     * RIGHT panel — DFR0522 RGB matrix preview + swatches
     ***************************************************************************/
    {
        lv_obj_t *right = tesaiot_col_create(main_row, UI_SPACE_MD);
        lv_obj_set_flex_grow(right, 1);

        lv_obj_t *title = lv_label_create(right);
        lv_label_set_text(title, "RGB Matrix 16x8");
        lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_ACCENT_BLUE),
                                    LV_PART_MAIN);
        lv_obj_set_style_text_font(title, UI_FONT_H3, LV_PART_MAIN);

        /* Live preview of the current fill color */
        s_ctx.preview = lv_obj_create(right);
        lv_obj_set_size(s_ctx.preview, LV_PCT(100), 92);
        lv_obj_set_style_radius(s_ctx.preview, UI_RADIUS_MD, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_ctx.preview,
                                  lv_color_hex(s_swatches[0].swatch_hex),
                                  LV_PART_MAIN);
        lv_obj_set_style_border_width(s_ctx.preview, 2, LV_PART_MAIN);
        lv_obj_set_style_border_color(s_ctx.preview,
                                      lv_color_hex(UI_COLOR_TEXT_SECONDARY),
                                      LV_PART_MAIN);
        lv_obj_clear_flag(s_ctx.preview, LV_OBJ_FLAG_SCROLLABLE);

        s_ctx.preview_label = lv_label_create(s_ctx.preview);
        lv_label_set_text(s_ctx.preview_label, s_swatches[0].name);
        lv_obj_set_style_text_font(s_ctx.preview_label, UI_FONT_H3,
                                   LV_PART_MAIN);
        lv_obj_set_style_text_color(s_ctx.preview_label,
                                    lv_color_hex(0x111827), LV_PART_MAIN);
        lv_obj_center(s_ctx.preview_label);

        /* 2 rows x 4 swatches */
        for (int r = 0; r < 2; r++) {
            lv_obj_t *sw_row = tesaiot_row_create(right, UI_SPACE_SM);
            for (int c = 0; c < 4; c++) {
                int idx = r * 4 + c;
                const rgb_swatch_def_t *sw = &s_swatches[idx];

                lv_obj_t *swatch = lv_obj_create(sw_row);
                lv_obj_set_size(swatch, 80, 58);
                lv_obj_set_flex_grow(swatch, 1);
                lv_obj_set_style_radius(swatch, UI_RADIUS_MD, LV_PART_MAIN);
                lv_obj_set_style_bg_color(swatch, lv_color_hex(sw->swatch_hex),
                                          LV_PART_MAIN);
                lv_obj_set_style_border_width(swatch, 2, LV_PART_MAIN);
                lv_obj_set_style_border_color(swatch,
                                              lv_color_hex(UI_COLOR_TEXT_SECONDARY),
                                              LV_PART_MAIN);
                lv_obj_clear_flag(swatch, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_add_flag(swatch, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_add_event_cb(swatch, swatch_click_cb, LV_EVENT_CLICKED,
                                    (void *)(uintptr_t)idx);

                lv_obj_t *name = lv_label_create(swatch);
                lv_label_set_text(name, sw->name);
                lv_obj_set_style_text_font(name, UI_FONT_CAPTION, LV_PART_MAIN);
                lv_obj_set_style_text_color(name,
                    lv_color_hex(sw->color == DFR0522_COLOR_WHITE ||
                                 sw->color == DFR0522_COLOR_YELLOW
                                     ? 0x111827 : 0xF8FAFC), LV_PART_MAIN);
                lv_obj_center(name);
            }
        }

        /* Effect buttons: marquee + stars + wave + pacman */
        lv_obj_t *fx_row = tesaiot_row_create(right, UI_SPACE_SM);
        for (uint32_t i = 0; i < NUM_FX_BTNS; i++) {
            lv_obj_t *btn = lv_btn_create(fx_row);
            lv_obj_set_size(btn, 80, 42);
            lv_obj_set_flex_grow(btn, 1);
            lv_obj_set_style_radius(btn, UI_RADIUS_MD, LV_PART_MAIN);
            lv_obj_set_style_bg_color(btn, lv_color_hex(UI_COLOR_BG_CARD),
                                      LV_PART_MAIN);
            lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_width(btn, 2, LV_PART_MAIN);
            lv_obj_set_style_border_color(btn,
                                          lv_color_hex(s_fx_btns[i].accent_hex),
                                          LV_PART_MAIN);
            lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
            lv_obj_add_event_cb(btn, effect_click_cb, LV_EVENT_CLICKED,
                                (void *)(uintptr_t)i);

            lv_obj_t *lbl = lv_label_create(btn);
            lv_label_set_text(lbl, s_fx_btns[i].label);
            lv_obj_set_style_text_font(lbl, UI_FONT_CAPTION, LV_PART_MAIN);
            lv_obj_set_style_text_color(lbl,
                                        lv_color_hex(s_fx_btns[i].accent_hex),
                                        LV_PART_MAIN);
            lv_obj_center(lbl);
        }

        s_ctx.rgb_status = lv_label_create(right);
        snprintf(s_hint_buf, sizeof(s_hint_buf),
                 "Tap a color  -  %s next  -  %s clear",
                 QWA309_BTN_LABEL_P17_5, QWA309_BTN_LABEL_P17_7);
        lv_label_set_text(s_ctx.rgb_status, s_hint_buf);
        lv_obj_set_style_text_color(s_ctx.rgb_status,
                                    lv_color_hex(UI_COLOR_TEXT_SECONDARY),
                                    LV_PART_MAIN);
        lv_obj_set_style_text_font(s_ctx.rgb_status, UI_FONT_CAPTION,
                                   LV_PART_MAIN);
    }

    /* Notes, below the panels (scroll down) */
    {
        lv_obj_t *notes = lv_label_create(content);
        lv_obj_set_width(notes, LV_PCT(100));
        lv_label_set_long_mode(notes, LV_LABEL_LONG_WRAP);
        lv_label_set_text_static(notes, s_notes_text);
        lv_obj_set_style_text_color(notes,
                                    lv_color_hex(UI_COLOR_TEXT_SECONDARY),
                                    LV_PART_MAIN);
        lv_obj_set_style_text_font(notes, UI_FONT_CAPTION, LV_PART_MAIN);
    }

    /* Bring the base-board push-buttons up (idempotent, GFX-task context) */
    sw_gpio_init();

    return scr;
}

/*******************************************************************************
 * page_gpio_rgb_render
 *******************************************************************************/
/* Repaint indicator pill i only when its lit state actually changed —
 * bit i of shown_ind caches the on-screen state. */
static void ind_apply(uint8_t i, bool lit)
{
    if (s_ctx.ind[i] == NULL) return;

    uint8_t bit = (uint8_t)(1U << i);
    bool shown = (s_ctx.shown_ind != 0xFFU) && ((s_ctx.shown_ind & bit) != 0U);
    if (s_ctx.shown_ind != 0xFFU && shown == lit) return;

    lv_obj_set_style_bg_color(s_ctx.ind[i],
        lv_color_hex(lit ? UI_COLOR_ACCENT_GREEN : UI_COLOR_BG_CARD),
        LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ctx.ind_label[i],
        lv_color_hex(lit ? 0x111827 : UI_COLOR_TEXT_SECONDARY),
        LV_PART_MAIN);
}

/* CapSense link line: redrawn only when the state (or protocol) changes. */
static void render_caps_link(const cm55_capsense_info_t *caps)
{
    if (s_ctx.caps_link_label == NULL) return;

    uint8_t state;
    if (!caps->answered) {
        state = LINK_NO_ANSWER;
    } else if (!caps->live) {
        state = LINK_READ_FAIL;
    } else if (!caps->switches_valid) {
        state = LINK_OK_LEGACY;
    } else if (caps->fw_error) {
        state = LINK_OK_FW_ERROR;
    } else {
        state = LINK_OK_SNAPSHOT;
    }
    if (state == s_ctx.shown_link && caps->proto == s_ctx.shown_proto) return;
    s_ctx.shown_link  = state;
    s_ctx.shown_proto = caps->proto;

    uint32_t color = UI_COLOR_ACCENT_GREEN;
    switch (state) {
    case LINK_NO_ANSWER:
        snprintf(s_link_buf, sizeof(s_link_buf),
                 "CapSense: no answer at 0x08 - check SW12");
        color = UI_COLOR_ACCENT_ORANGE;
        break;
    case LINK_READ_FAIL:
        snprintf(s_link_buf, sizeof(s_link_buf),
                 "CapSense 0x08: no reply on the last read, retrying");
        color = UI_COLOR_ACCENT_ORANGE;
        break;
    case LINK_OK_LEGACY:
        snprintf(s_link_buf, sizeof(s_link_buf),
                 "CapSense 0x08: OK, legacy 3-byte firmware");
        break;
    case LINK_OK_FW_ERROR:
        snprintf(s_link_buf, sizeof(s_link_buf),
                 "CapSense 0x08: protocol 0x%02X, controller reports an error",
                 (unsigned)caps->proto);
        color = UI_COLOR_ACCENT_ORANGE;
        break;
    default:
        snprintf(s_link_buf, sizeof(s_link_buf),
                 "CapSense 0x08: OK, protocol 0x%02X", (unsigned)caps->proto);
        break;
    }
    lv_label_set_text(s_ctx.caps_link_label, s_link_buf);
    lv_obj_set_style_text_color(s_ctx.caps_link_label, lv_color_hex(color),
                                LV_PART_MAIN);
}

/* SW1-SW4 line: states only from a 0x0D/0x0E controller, otherwise why not. */
static void render_sw14(const cm55_capsense_info_t *caps)
{
    if (s_ctx.sw14_label == NULL) return;

    bool    valid = caps->live && caps->switches_valid;
    uint8_t now;
    if (valid) {
        now = (uint8_t)(caps->switches & 0x0FU);
    } else if (caps->live) {
        now = SW14_SHOWN_NEEDS_0D;
    } else {
        now = SW14_SHOWN_NO_LINK;
    }
    if (now == s_ctx.shown_sw14) return;
    s_ctx.shown_sw14 = now;

    if (now == SW14_SHOWN_NEEDS_0D) {
        snprintf(s_sw14_buf, sizeof(s_sw14_buf),
                 "SW1-SW4: needs CapSense firmware 0x0D or 0x0E");
    } else if (now == SW14_SHOWN_NO_LINK) {
        snprintf(s_sw14_buf, sizeof(s_sw14_buf),
                 "SW1-SW4: no CapSense link");
    } else {
        snprintf(s_sw14_buf, sizeof(s_sw14_buf),
                 "SW1 %s  SW2 %s  SW3 %s  SW4 %s",
                 (now & CAPS_SW_SW1)    ? "pressed" : "released",
                 (now & CAPS_SW_SW2)    ? "pressed" : "released",
                 (now & CAPS_SW_SW3_ON) ? "ON" : "OFF",
                 (now & CAPS_SW_SW4_ON) ? "ON" : "OFF");
    }
    lv_label_set_text(s_ctx.sw14_label, s_sw14_buf);
    lv_obj_set_style_text_color(s_ctx.sw14_label,
        lv_color_hex(valid ? UI_COLOR_TEXT_PRIMARY : UI_COLOR_TEXT_SECONDARY),
        LV_PART_MAIN);
}

void page_gpio_rgb_render(sensorhub_snapshot_t *snap)
{
    /* Every widget below updates ONLY when its value really changed —
     * unconditional lv_label_set_text/style writes invalidate the whole
     * page every snapshot and it visibly flickers. */

    /* --- Potentiometers (CM55-local, fresher than the IPC snapshot) --- */
    uint16_t raw[QWA309_POT_COUNT] = {0};
    uint8_t  n = cm55_pot_read_all(raw);
    int8_t   live = (n == QWA309_POT_COUNT) ? 1 : 0;

    if (s_ctx.status_label && live != s_ctx.shown_live) {
        s_ctx.shown_live = live;
        lv_label_set_text(s_ctx.status_label,
                          live ? "Pots: live" : "Pots: ADC settling");
        lv_obj_set_style_text_color(s_ctx.status_label,
            lv_color_hex(live ? UI_COLOR_ACCENT_GREEN : UI_COLOR_ACCENT_ORANGE),
            LV_PART_MAIN);
    }

    if (live) {
        for (uint8_t i = 0; i < QWA309_POT_COUNT; i++) {
            if (s_ctx.pot_bar[i] == NULL) continue;

            int32_t delta = (int32_t)raw[i] - (int32_t)s_ctx.shown_pot[i];
            if (delta < 0) delta = -delta;
            /* End stops always snap exactly to 0 / 4095 — the deadband must
             * not leave the display frozen a few counts short of the rail. */
            bool endstop = (raw[i] == 0U || raw[i] == POT_RAW_MAX) &&
                           (raw[i] != s_ctx.shown_pot[i]);
            if (s_ctx.shown_pot[i] != 0xFFFFU && delta < POT_DEADBAND &&
                !endstop) continue;

            s_ctx.shown_pot[i] = raw[i];
            lv_bar_set_value(s_ctx.pot_bar[i], raw[i], LV_ANIM_OFF);
            /* Rounded to the nearest millivolt. */
            uint32_t mv = ((uint32_t)raw[i] * POT_FULL_SCALE_MV +
                           (POT_RAW_MAX / 2U)) / POT_RAW_MAX;
            snprintf(s_value_buf[i], sizeof(s_value_buf[i]), "%u mV",
                     (unsigned)mv);
            snprintf(s_raw_buf[i], sizeof(s_raw_buf[i]), "raw %u",
                     (unsigned)raw[i]);
            lv_label_set_text_fmt(s_ctx.pot_value[i], "%s\n%s",
                                  s_value_buf[i], s_raw_buf[i]);
        }
    }

    /* --- CapSense link state and SW1-SW4 (CM55-local) --- */
    cm55_capsense_info_t caps;
    (void)cm55_capsense_info(&caps);
    render_caps_link(&caps);
    render_sw14(&caps);

    /* --- Indicator pills: BTN0/BTN1 (CapSense) + P17.5/P17.7 (GPIO) --- */
    uint8_t ind_now = (s_ctx.shown_ind == 0xFFU) ? 0U : s_ctx.shown_ind;

    /* CapSense values only while the controller answers. Without a link the
     * hub snapshot may still hold the last sample, and showing it would be a
     * stale value, so the pills are released and the slider shows no value. */
    if (snap != NULL && snap->has_capsense && caps.live) {
        bool cap[2] = { snap->capsense.btn0_pressed != 0,
                        snap->capsense.btn1_pressed != 0 };
        for (uint8_t i = 0; i < 2U; i++) {
            ind_apply(i, cap[i]);
            ind_now = (uint8_t)(cap[i] ? (ind_now | (1U << i))
                                       : (ind_now & ~(1U << i)));
        }

        if (s_ctx.slider_bar) {
            uint8_t slider = snap->capsense.slider;
            if (slider > 100U) slider = 100U;
            /* A 0x0D/0x0E controller says when nobody touches the slider;
             * show that instead of a position. */
            uint8_t shown = (caps.switches_valid && !caps.slider_touched)
                                ? SLIDER_SHOWN_UNTOUCHED : slider;
            if (shown != s_ctx.shown_slider) {
                s_ctx.shown_slider = shown;
                lv_bar_set_value(s_ctx.slider_bar,
                                 (shown == SLIDER_SHOWN_UNTOUCHED) ? 0 : slider,
                                 LV_ANIM_OFF);
                if (shown == SLIDER_SHOWN_UNTOUCHED) {
                    snprintf(s_slider_buf, sizeof(s_slider_buf),
                             "CapSense slider: not touched");
                } else {
                    snprintf(s_slider_buf, sizeof(s_slider_buf),
                             "CapSense slider: %u %%", (unsigned)slider);
                }
                lv_label_set_text(s_ctx.slider_label, s_slider_buf);
            }
        }
    } else {
        /* Feed gone — release the CapSense pills instead of latching the
         * last state green forever, and clear the slider. */
        for (uint8_t i = 0; i < 2U; i++) {
            ind_apply(i, false);
            ind_now = (uint8_t)(ind_now & ~(1U << i));
        }
        if (s_ctx.slider_bar && s_ctx.shown_slider != SLIDER_SHOWN_NO_LINK) {
            s_ctx.shown_slider = SLIDER_SHOWN_NO_LINK;
            lv_bar_set_value(s_ctx.slider_bar, 0, LV_ANIM_OFF);
            lv_label_set_text(s_ctx.slider_label, "CapSense slider: --");
        }
    }

    /* --- Base-board P17.5/P17.7 buttons (CM55-local GPIO) + matrix --- */
    if (s_ctx.hw_ready) {
        for (uint8_t i = 0; i < SW_COUNT; i++) {
            bool pressed = sw_read(i);
            uint8_t slot = (uint8_t)(2U + i);   /* ind[2]=P17.5, ind[3]=P17.7 */

            ind_apply(slot, pressed);
            ind_now = (uint8_t)(pressed ? (ind_now | (1U << slot))
                                        : (ind_now & ~(1U << slot)));

            /* Rising edge → drive the matrix */
            if (pressed && !s_ctx.sw_prev[i]) {
                if (i == 0U) {
                    /* P17.5 button → cycle to the next palette color and fill */
                    apply_color((uint8_t)((s_ctx.color_idx + 1U) % NUM_SWATCHES));
                } else {
                    /* P17.7 button → clear the matrix (palette index 0 = Off) */
                    apply_color(0U);
                }
            }
            s_ctx.sw_prev[i] = pressed;
        }
    }

    s_ctx.shown_ind = ind_now;
}

/*******************************************************************************
 * page_gpio_rgb_destroy
 *******************************************************************************/
void page_gpio_rgb_destroy(void)
{
    /* The effect/marquee runs on a driver-owned LVGL timer that outlives
     * this page — leaving it armed keeps animating the panel (and hammering
     * the shared display I2C bus) from every other screen. */
    (void)dfr0522_effect(DFR0522_FX_NONE, 0U);

    memset(&s_ctx, 0, sizeof(s_ctx));
}

#endif /* BSP_HAS_QWA309_BASEBOARD */
