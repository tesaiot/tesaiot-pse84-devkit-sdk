/*******************************************************************************
 * dfr0522_rgb.h — CM55-side DFRobot DFR0522 16x8 RGB LED matrix driver.
 *
 * The DFR0522 sits at I2C 0x10 on the HEADER I2C bus (P17.0/P17.1). On the
 * AI Kit that bus is the DISPLAY / touch SCB, owned by CM55 (same controller
 * the CapSense-4000T read path in cm55_sensor_poll.c uses via
 * DISPLAY_I2C_CONTROLLER_HW + disp_touch_i2c_controller_context). The panel
 * driver therefore MUST run on CM55, not CM33_NS.
 *
 * Wire frame (from the tested dfr0522_rgb_matrix reference):
 *   [0x02, function, color, x, y, 0..(pad to 51 bytes)]
 *   function: 0x01 = clear, 0x08 = set pixel, 0x09 = fill
 *   color   : 0..7 (see dfr0522_color_t)
 *   panel   : 16 wide (x 0..15) x 8 tall (y 0..7)
 *
 * C API — call ONLY from the GFX/LVGL task context (the owner of the display
 * I2C controller). The MicroPython-facing `rgbmatrix` module on CM33_NS reaches
 * these functions over the IPC UI pipe (opcode IPC_CMD_UI_RGB_MATRIX), so the
 * actual I2C transaction always executes here in GFX-task context.
 *
 * Gated by BSP_HAS_QWA309_BASEBOARD.
 *******************************************************************************/

#ifndef DFR0522_RGB_H
#define DFR0522_RGB_H

#include "bsp_feature_flags.h"

#if BSP_HAS_QWA309_BASEBOARD

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DFR0522_WIDTH    (16U)
#define DFR0522_HEIGHT   (8U)

typedef enum {
    DFR0522_COLOR_OFF    = 0U,
    DFR0522_COLOR_RED    = 1U,
    DFR0522_COLOR_GREEN  = 2U,
    DFR0522_COLOR_YELLOW = 3U,
    DFR0522_COLOR_BLUE   = 4U,
    DFR0522_COLOR_PURPLE = 5U,
    DFR0522_COLOR_CYAN   = 6U,
    DFR0522_COLOR_WHITE  = 7U,
} dfr0522_color_t;

/* Turn every pixel off. Returns true on I2C success. */
bool dfr0522_clear(void);

/* Fill the whole 16x8 panel with a solid color (1..7). Returns true on success. */
bool dfr0522_fill(uint8_t color);

/* Set a single pixel (x 0..15, y 0..7) to color (0..7). Returns true on success. */
bool dfr0522_pixel(uint8_t x, uint8_t y, uint8_t color);

/*******************************************************************************
 * Frame-oriented game API. All maintain a shadow framebuffer and write only
 * CHANGED pixels to the panel (the wire cost is ~1.2 ms per 51-byte pixel
 * command — a naive full repaint is ~150 ms of display-bus time; a typical
 * game frame changes a handful of pixels and stays invisible at 30 fps).
 * Call ONLY from GFX-task context, like the ops above.
 *******************************************************************************/

/* Paint a full 128-px frame from a 64-byte 4bpp buffer (IPC_RGB_BLIT packing:
 * byte = frame[y*8 + (x>>1)], even x = low nibble). Diff-flushed. */
bool dfr0522_blit(const uint8_t frame[64]);

/* Render value (0..9999) as right-aligned 3x5 digits. Diff-flushed. */
bool dfr0522_score(uint16_t value, uint8_t color);

/* Bottom-row progress bar: lit pixels = value*16/max. Diff-flushed. */
bool dfr0522_bar(uint8_t value, uint8_t max, uint8_t color);

/* Start a C-side marquee (3x5 font) stepping every period_ms on an LVGL
 * timer — survives a busy Python game loop. len 0 (or period 0) stops it.
 * Any other draw op (clear/fill/pixel/blit/score/bar) also stops it. */
bool dfr0522_scroll(const char *text, uint8_t len, uint8_t color, uint16_t period_ms);

/*******************************************************************************
 * Built-in animated effects — same LVGL-timer/diff-flush machinery as the
 * marquee. Any other draw op (and the marquee) stops a running effect, and
 * starting an effect stops the marquee. Call from GFX-task context only.
 *******************************************************************************/
typedef enum {
    DFR0522_FX_NONE   = 0U,   /* stop whatever effect is running */
    DFR0522_FX_STARS  = 1U,   /* twinkling stars (white→cyan→blue fade-out) */
    DFR0522_FX_WAVE   = 2U,   /* travelling sine wave with a dim trail */
    DFR0522_FX_PACMAN = 3U,   /* pacman chased by a ghost, eating dots */
} dfr0522_fx_t;

/* Start effect fx stepping every period_ms (period 0 → per-effect default).
 * DFR0522_FX_NONE stops the current effect and clears the panel. */
bool dfr0522_effect(dfr0522_fx_t fx, uint16_t period_ms);

#ifdef __cplusplus
}
#endif

#endif /* BSP_HAS_QWA309_BASEBOARD */

#endif /* DFR0522_RGB_H */
