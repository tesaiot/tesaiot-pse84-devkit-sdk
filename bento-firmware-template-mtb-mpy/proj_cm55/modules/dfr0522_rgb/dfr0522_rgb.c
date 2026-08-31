/*******************************************************************************
 * dfr0522_rgb.c — CM55-side DFRobot DFR0522 16x8 RGB matrix I2C driver.
 *
 * Reuses the display I2C controller (DISPLAY_I2C_CONTROLLER_HW +
 * disp_touch_i2c_controller_context) exactly like cm55_sensor_poll.c does for
 * the CapSense-4000T read path — this is the SCB CM55 owns for the display /
 * touch / header I2C bus (P17.0/P17.1) on the AI Kit.
 *
 * Whole body gated by BSP_HAS_QWA309_BASEBOARD.
 *
 *==============================================================================
 * PANEL PROTOCOL — the panel is a REGISTER FILE, not a fixed-size packet
 *==============================================================================
 * I2C 7-bit address 0x10. Registers are written sequentially from the first
 * address in the transaction (DFRobot_RGBPanel.h):
 *
 *   0x02 FUNC    1 = clear, 8 = set pixel, 9 = fill
 *   0x03 COLOR   0..7 (see dfr0522_color_t)
 *   0x04 PIX_X   0..15
 *   0x05 PIX_Y   0..7
 *   0x06 BITMAP  built-in picture index (display(picIndex) in the Arduino lib)
 *   0x07 STR     text buffer for the panel's own string renderer
 *
 * The panel acts on the command when the transaction ends, so a write that
 * stops after PIX_Y is complete and valid.
 *
 * KNOWHOW — why every command here is 5 bytes, not 51 (2026-07-31):
 *   DFRobot's Arduino library always ships its entire 50-byte buffer, and the
 *   reference driver this file came from (qwa309-training-base rgb_panel.c)
 *   copied that shape: register byte + 50 bytes, of which 46 were zero-fill
 *   for BITMAP/STR — registers clear/fill/pixel never touch.
 *
 *   That padding is invisible for a one-off fill, but it decides whether
 *   animation is possible at all: a frame issues ONE command per changed
 *   pixel (24 for a wave step, up to 35 for pacman), so the padding turned
 *   ~120 bytes of real work into ~1.8 KB of wire time per frame. The panel
 *   visibly painted pixel-by-pixel and the GFX task blocked for longer than
 *   an LVGL frame. Writing only FUNC..PIX_Y cut the wire cost ~10x and is
 *   what makes the effects fluid — verified on a TESAIoT Dev Kit 2026-07-31.
 *
 *   Flip RGB_TX_FULL_FRAME to 1 to restore the padded writes if a future
 *   panel revision turns out to need them.
 *
 * NO FULL-FRAME UPLOAD EXISTS. The BITMAP register selects a picture already
 * stored in the panel, not an arbitrary bitmap, so repainting is always
 * per-pixel. Frame-oriented ops therefore diff against a shadow framebuffer
 * (see below) and write only what changed — that pairing (small command +
 * small diff) is the whole performance story.
 *
 * BUS SHARING. This SCB is shared with the display/touch controller, and
 * CM33_NS claims it while touch is paused (OPTIGA, tesaiot_config). The
 * animation timers skip a frame when lv_port_indev_touch_paused() is true
 * rather than colliding on the bus.
 *******************************************************************************/

#include "dfr0522_rgb.h"

#if BSP_HAS_QWA309_BASEBOARD

#include "cybsp.h"              /* CYBSP_I2C_DISPLAY_CONTROLLER_* (via cycfg) */
#include "display_i2c_config.h"
#include "cy_scb_i2c.h"
#include "lv_port_indev.h"      /* lv_port_indev_touch_paused() — bus handoff */
#include "lvgl.h"               /* lv_timer for the C-side marquee (GFX task) */
#include "ipc_ui_protocol.h"    /* IPC_RGB_SCROLL_TEXT_MAX + blit frame layout */
#include <string.h>
#include <stdio.h>

/* Anti-copy-paste guard: this file implements canonical opcode-map v2 sub-ops. */
_Static_assert(IPC_UI_OPMAP_VERSION == 2, "wrong tree's ipc_ui_protocol.h (opcode map mismatch)");

/*******************************************************************************
 * Shared display I2C controller — extern from display init (same symbol
 * cm55_sensor_poll.c uses).
 *******************************************************************************/
extern cy_stc_scb_i2c_context_t disp_touch_i2c_controller_context;

#define RGB_I2C_HW          DISPLAY_I2C_CONTROLLER_HW
#define RGB_I2C_CTX         (&disp_touch_i2c_controller_context)

#define RGB_I2C_ADDRESS     (0x10U)
#define RGB_CMD_REGISTER    (0x02U)
#define RGB_FUNC_CLEAR      (0x01U)
#define RGB_FUNC_PIXEL      (0x08U)
#define RGB_FUNC_FILL       (0x09U)
#define RGB_BYTE_TIMEOUT_MS (5U)

/* Wire length per command — see the KNOWHOW block at the top of this file
 * before changing it. 1 restores DFRobot's padded 50-byte payload. */
#define RGB_TX_FULL_FRAME   (0)
#if RGB_TX_FULL_FRAME
#define RGB_TX_SIZE         (51U)
#else
#define RGB_TX_SIZE         (5U)   /* [reg, FUNC, COLOR, PIX_X, PIX_Y] */
#endif

/*******************************************************************************
 * Low-level command write — RGB_TX_SIZE bytes ([reg, FUNC, COLOR, PIX_X,
 * PIX_Y]; 51 when RGB_TX_FULL_FRAME=1).
 ******************************************************************************/
static bool dfr0522_write(uint8_t function, uint8_t color, uint8_t x, uint8_t y)
{
    uint8_t tx[RGB_TX_SIZE];
    cy_en_scb_i2c_status_t status;
    cy_en_scb_i2c_status_t stop_status;

    memset(tx, 0, sizeof(tx));
    tx[0] = RGB_CMD_REGISTER;
    tx[1] = function;
    tx[2] = color;
    tx[3] = x;
    tx[4] = y;

    status = Cy_SCB_I2C_MasterSendStart(RGB_I2C_HW, RGB_I2C_ADDRESS,
                                        CY_SCB_I2C_WRITE_XFER,
                                        RGB_BYTE_TIMEOUT_MS, RGB_I2C_CTX);
    if (status == CY_SCB_I2C_SUCCESS) {
        for (uint32_t i = 0U; i < RGB_TX_SIZE; i++) {
            status = Cy_SCB_I2C_MasterWriteByte(RGB_I2C_HW, tx[i],
                                                RGB_BYTE_TIMEOUT_MS, RGB_I2C_CTX);
            if (status != CY_SCB_I2C_SUCCESS) {
                break;
            }
        }
    }

    stop_status = Cy_SCB_I2C_MasterSendStop(RGB_I2C_HW, RGB_BYTE_TIMEOUT_MS,
                                            RGB_I2C_CTX);
    if ((status == CY_SCB_I2C_SUCCESS) && (stop_status != CY_SCB_I2C_SUCCESS)) {
        status = stop_status;
    }
    return (status == CY_SCB_I2C_SUCCESS);
}

/*******************************************************************************
 * Shadow framebuffer — one byte per pixel (color 0..7), row-major.
 * All draw paths keep it in sync so the frame ops can write only CHANGED
 * pixels; mixing pixel/fill/blit/score/bar calls stays coherent.
 *******************************************************************************/
#define RGB_PIXELS  (DFR0522_WIDTH * DFR0522_HEIGHT)   /* 128 */

static uint8_t s_shadow[RGB_PIXELS];
static bool    s_shadow_valid = false;   /* false until first known-good state */

/* Marquee state (LVGL timer, GFX-task context) */
static lv_timer_t *s_scroll_timer = NULL;
static char        s_scroll_text[IPC_RGB_SCROLL_TEXT_MAX];
static uint8_t     s_scroll_len = 0;
static uint8_t     s_scroll_color = DFR0522_COLOR_WHITE;
static int16_t     s_scroll_offset = 0;

static void dfr0522_scroll_stop(void)
{
    if (s_scroll_timer) {
        lv_timer_delete(s_scroll_timer);
        s_scroll_timer = NULL;
    }
    s_scroll_len = 0;
}

/* Stop BOTH animation engines (marquee + built-in effects) — every direct
 * draw op goes through this so exactly one animation owns the panel. */
static void dfr0522_fx_stop(void);   /* effects engine, bottom of file */
static void dfr0522_anim_stop(void)
{
    dfr0522_scroll_stop();
    dfr0522_fx_stop();
}

bool dfr0522_clear(void)
{
    dfr0522_anim_stop();
    bool ok = dfr0522_write(RGB_FUNC_CLEAR, DFR0522_COLOR_OFF, 0U, 0U);
    if (ok) {
        memset(s_shadow, DFR0522_COLOR_OFF, sizeof(s_shadow));
        s_shadow_valid = true;
    }
    return ok;
}

bool dfr0522_fill(uint8_t color)
{
    if (color < DFR0522_COLOR_RED || color > DFR0522_COLOR_WHITE) {
        return false;
    }
    dfr0522_anim_stop();
    bool ok = dfr0522_write(RGB_FUNC_FILL, color, 0U, 0U);
    if (ok) {
        memset(s_shadow, color, sizeof(s_shadow));
        s_shadow_valid = true;
    }
    return ok;
}

bool dfr0522_pixel(uint8_t x, uint8_t y, uint8_t color)
{
    if (x >= DFR0522_WIDTH || y >= DFR0522_HEIGHT ||
        color > DFR0522_COLOR_WHITE) {
        return false;
    }
    dfr0522_anim_stop();
    bool ok = dfr0522_write(RGB_FUNC_PIXEL, color, x, y);
    if (ok && s_shadow_valid) {
        s_shadow[(uint32_t)y * DFR0522_WIDTH + x] = color;
    }
    return ok;
}

/*******************************************************************************
 * Diff-flush a full 128-byte target frame against the shadow.
 * First frame after boot (shadow unknown): clear the panel, then paint only
 * the non-off pixels — bounded, known-good state.
 *******************************************************************************/
/* One-shot wire-cost report: the first flush that actually moves pixels
 * prints the measured per-command time, so the animation budget is a
 * measured number instead of an assumption. */
static bool s_wire_reported = false;

static bool dfr0522_flush_target(const uint8_t *target)
{
    if (!s_shadow_valid) {
        if (!dfr0522_write(RGB_FUNC_CLEAR, DFR0522_COLOR_OFF, 0U, 0U)) {
            return false;
        }
        memset(s_shadow, DFR0522_COLOR_OFF, sizeof(s_shadow));
        s_shadow_valid = true;
    }

    bool measure = !s_wire_reported;
    uint32_t t0 = 0;
    if (measure) {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
        t0 = DWT->CYCCNT;
    }

    uint32_t written = 0;
    for (uint32_t i = 0; i < RGB_PIXELS; i++) {
        if (target[i] != s_shadow[i]) {
            uint8_t x = (uint8_t)(i % DFR0522_WIDTH);
            uint8_t y = (uint8_t)(i / DFR0522_WIDTH);
            if (!dfr0522_write(RGB_FUNC_PIXEL, target[i], x, y)) {
                return false;   /* shadow stays truthful up to the failure */
            }
            s_shadow[i] = target[i];
            written++;
        }
    }

    if (measure && written > 0U) {
        uint32_t us = (uint32_t)(((uint64_t)(DWT->CYCCNT - t0) * 1000000ULL) /
                                 SystemCoreClock);
        s_wire_reported = true;
        printf("[RGB] wire: %lu px in %lu us (%lu us/px, %u B/cmd)\r\n",
               (unsigned long)written, (unsigned long)us,
               (unsigned long)(us / written), (unsigned)RGB_TX_SIZE);
    }
    return true;
}

/* Unpack an IPC_RGB_BLIT 4bpp frame into a 128-byte target. */
static void dfr0522_unpack_4bpp(const uint8_t frame[64], uint8_t *target)
{
    for (uint32_t y = 0; y < DFR0522_HEIGHT; y++) {
        for (uint32_t x = 0; x < DFR0522_WIDTH; x++) {
            uint8_t b = frame[y * (DFR0522_WIDTH / 2U) + (x >> 1)];
            uint8_t c = (x & 1U) ? (uint8_t)(b >> 4) : (uint8_t)(b & 0x0FU);
            target[y * DFR0522_WIDTH + x] = (c > DFR0522_COLOR_WHITE)
                                            ? DFR0522_COLOR_OFF : c;
        }
    }
}

bool dfr0522_blit(const uint8_t frame[64])
{
    if (frame == NULL) {
        return false;
    }
    dfr0522_anim_stop();
    uint8_t target[RGB_PIXELS];
    dfr0522_unpack_4bpp(frame, target);
    return dfr0522_flush_target(target);
}

/*******************************************************************************
 * 3x5 font — digits 0-9 + A-Z subset used by the marquee. Each glyph is
 * 5 rows x 3 cols, one byte per row (bits 2..0 = left..right).
 *******************************************************************************/
static const uint8_t s_font_digits[10][5] = {
    {7,5,5,5,7}, {2,6,2,2,7}, {7,1,7,4,7}, {7,1,7,1,7}, {5,5,7,1,1},
    {7,4,7,1,7}, {7,4,7,5,7}, {7,1,2,2,2}, {7,5,7,5,7}, {7,5,7,1,7},
};
static const uint8_t s_font_alpha[26][5] = {
    {7,5,7,5,5}, {6,5,6,5,6}, {7,4,4,4,7}, {6,5,5,5,6}, {7,4,7,4,7},
    {7,4,7,4,4}, {7,4,5,5,7}, {5,5,7,5,5}, {7,2,2,2,7}, {1,1,1,5,7},
    {5,6,4,6,5}, {4,4,4,4,7}, {5,7,7,5,5}, {6,5,5,5,5}, {7,5,5,5,7},
    {7,5,7,4,4}, {7,5,5,7,3}, {6,5,6,5,5}, {7,4,7,1,7}, {7,2,2,2,2},
    {5,5,5,5,7}, {5,5,5,5,2}, {5,5,7,7,5}, {5,5,2,5,5}, {5,5,7,2,2},
    {7,1,2,4,7},
};

/* Lowercase set. Uppercase fills all 5 rows; lowercase keeps its body in the
 * bottom 3 (rows 2-4) with ascenders/descenders where the letter needs one,
 * so mixed-case text like "TESAIoT" reads with real case contrast instead of
 * silently rendering as all caps. */
static const uint8_t s_font_lower[26][5] = {
    {0,0,6,5,7}, {4,4,6,5,7}, {0,0,7,4,7}, {1,1,3,5,7}, {0,0,7,6,7},
    {3,4,7,4,4}, {0,7,5,7,3}, {4,4,6,5,5}, {2,0,2,2,2}, {1,0,1,1,6},
    {4,5,6,6,5}, {6,2,2,2,7}, {0,0,7,7,5}, {0,0,6,5,5}, {0,0,7,5,7},
    {0,7,5,7,4}, {0,7,5,7,1}, {0,0,6,4,4}, {0,0,3,2,6}, {2,7,2,2,3},
    {0,0,5,5,7}, {0,0,5,5,2}, {0,0,5,7,7}, {0,0,5,2,5}, {0,5,5,3,6},
    {0,0,7,2,7},
};

/* Paint one glyph column-slice into target at panel column px (may be off
 * panel — clipped). Glyph rows render at y=1..5. */
static void font_render_col(uint8_t *target, int16_t px, char ch, uint8_t glyph_col, uint8_t color)
{
    if (px < 0 || px >= (int16_t)DFR0522_WIDTH || glyph_col > 2U) {
        return;
    }
    const uint8_t *rows = NULL;
    if (ch >= '0' && ch <= '9')      rows = s_font_digits[ch - '0'];
    else if (ch >= 'A' && ch <= 'Z') rows = s_font_alpha[ch - 'A'];
    else if (ch >= 'a' && ch <= 'z') rows = s_font_lower[ch - 'a'];
    if (rows == NULL) {
        return;   /* space / unsupported char = blank column */
    }
    for (uint32_t r = 0; r < 5U; r++) {
        if (rows[r] & (uint8_t)(1U << (2U - glyph_col))) {
            target[(r + 1U) * DFR0522_WIDTH + (uint32_t)px] = color;
        }
    }
}

bool dfr0522_score(uint16_t value, uint8_t color)
{
    if (color < DFR0522_COLOR_RED || color > DFR0522_COLOR_WHITE) {
        return false;
    }
    dfr0522_anim_stop();
    if (value > 9999U) {
        value = 9999U;
    }

    char digits[4];
    int ndigits = 0;
    uint16_t v = value;
    do {
        digits[ndigits++] = (char)('0' + (v % 10U));
        v /= 10U;
    } while (v > 0U && ndigits < 4);

    uint8_t target[RGB_PIXELS];
    memset(target, DFR0522_COLOR_OFF, sizeof(target));
    /* Right-aligned, 4 columns per digit (3 glyph + 1 gap): rightmost digit
     * ends at column 14, leaving a 1-px margin. */
    int16_t px = (int16_t)(DFR0522_WIDTH - 2);
    for (int d = 0; d < ndigits; d++) {          /* least-significant first */
        for (int col = 2; col >= 0; col--) {
            font_render_col(target, (int16_t)(px - (2 - col)), digits[d], (uint8_t)col, color);
        }
        px -= 4;
    }
    return dfr0522_flush_target(target);
}

bool dfr0522_bar(uint8_t value, uint8_t max, uint8_t color)
{
    if (max == 0U || color < DFR0522_COLOR_RED || color > DFR0522_COLOR_WHITE) {
        return false;
    }
    dfr0522_anim_stop();
    if (value > max) {
        value = max;
    }
    uint32_t lit = ((uint32_t)value * DFR0522_WIDTH) / max;

    /* Only the bottom row changes — start from the current shadow (or a
     * blank frame on first use) so the rest of the panel is untouched. */
    uint8_t target[RGB_PIXELS];
    if (s_shadow_valid) {
        memcpy(target, s_shadow, sizeof(target));
    } else {
        memset(target, DFR0522_COLOR_OFF, sizeof(target));
    }
    for (uint32_t x = 0; x < DFR0522_WIDTH; x++) {
        target[(DFR0522_HEIGHT - 1U) * DFR0522_WIDTH + x] =
            (x < lit) ? color : DFR0522_COLOR_OFF;
    }
    return dfr0522_flush_target(target);
}

/*******************************************************************************
 * Marquee — steps one column per timer tick, renders through the same
 * diff-flush path (a step changes only the glyph columns, cheap on the wire).
 *******************************************************************************/
#define SCROLL_CHAR_COLS  (4)   /* 3 glyph cols + 1 gap */

static void dfr0522_scroll_tick(lv_timer_t *timer)
{
    (void)timer;
    if (s_scroll_len == 0U) {
        return;
    }
    /* CM33_NS owns this SCB while touch is paused (OPTIGA / tesaiot config).
     * Skip the frame rather than collide on the bus — the animation just
     * pauses for the duration of the handoff. */
    if (lv_port_indev_touch_paused()) {
        return;
    }

    uint8_t target[RGB_PIXELS];
    memset(target, DFR0522_COLOR_OFF, sizeof(target));

    int16_t total_cols = (int16_t)s_scroll_len * SCROLL_CHAR_COLS;
    for (int16_t c = 0; c < total_cols; c++) {
        int16_t px = (int16_t)DFR0522_WIDTH + (int16_t)(c - s_scroll_offset);
        if (px < -1 || px >= (int16_t)DFR0522_WIDTH) {
            continue;
        }
        uint8_t glyph_col = (uint8_t)(c % SCROLL_CHAR_COLS);
        if (glyph_col > 2U) {
            continue;   /* gap column */
        }
        font_render_col(target, px, s_scroll_text[c / SCROLL_CHAR_COLS],
                        glyph_col, s_scroll_color);
    }

    (void)dfr0522_flush_target(target);

    s_scroll_offset++;
    if (s_scroll_offset > total_cols + (int16_t)DFR0522_WIDTH) {
        s_scroll_offset = 0;   /* wrap: text re-enters from the right */
    }
}

bool dfr0522_scroll(const char *text, uint8_t len, uint8_t color, uint16_t period_ms)
{
    if (len == 0U || period_ms == 0U || text == NULL) {
        dfr0522_anim_stop();   /* documented "stop the animation" idiom */
        return true;
    }
    if (color < DFR0522_COLOR_RED || color > DFR0522_COLOR_WHITE) {
        return false;
    }
    if (len > IPC_RGB_SCROLL_TEXT_MAX) {
        len = IPC_RGB_SCROLL_TEXT_MAX;
    }

    dfr0522_fx_stop();   /* marquee takes over from any built-in effect */

    memcpy(s_scroll_text, text, len);
    s_scroll_len    = len;
    s_scroll_color  = color;
    s_scroll_offset = 0;

    if (s_scroll_timer) {
        lv_timer_set_period(s_scroll_timer, period_ms);
    } else {
        s_scroll_timer = lv_timer_create(dfr0522_scroll_tick, period_ms, NULL);
    }
    return (s_scroll_timer != NULL);
}

/*******************************************************************************
 * Built-in effects engine — one LVGL timer, per-effect frame builders that
 * fill a 128-byte target repainted through the same diff-flush path.
 *******************************************************************************/
static lv_timer_t  *s_fx_timer = NULL;
static dfr0522_fx_t s_fx_id    = DFR0522_FX_NONE;
static uint16_t     s_fx_step  = 0;
static uint32_t     s_fx_rng   = 0x2F6E2B1Fu;

static void dfr0522_fx_stop(void)
{
    if (s_fx_timer) {
        lv_timer_delete(s_fx_timer);
        s_fx_timer = NULL;
    }
    s_fx_id = DFR0522_FX_NONE;
}

static uint32_t fx_rand(void)
{
    uint32_t x = s_fx_rng;          /* xorshift32 */
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_fx_rng = x;
    return x;
}

/* ── Stars: spawn at random, each star ages white → cyan → blue → off ──── */
#define FX_STAR_MAX  12U
#define FX_STAR_LIFE 6U
static struct {
    uint8_t x, y, age;
    bool    live;
} s_stars[FX_STAR_MAX];

static void fx_stars_frame(uint8_t *target)
{
    /* Spawn up to 2 new stars per tick into free slots */
    for (uint32_t spawn = 0; spawn < 2U; spawn++) {
        if ((fx_rand() & 3U) == 0U) continue;      /* ~75% spawn chance */
        for (uint32_t i = 0; i < FX_STAR_MAX; i++) {
            if (!s_stars[i].live) {
                s_stars[i].live = true;
                s_stars[i].age  = 0;
                s_stars[i].x    = (uint8_t)(fx_rand() % DFR0522_WIDTH);
                s_stars[i].y    = (uint8_t)(fx_rand() % DFR0522_HEIGHT);
                break;
            }
        }
    }

    for (uint32_t i = 0; i < FX_STAR_MAX; i++) {
        if (!s_stars[i].live) continue;
        uint8_t c = (s_stars[i].age < 2U) ? DFR0522_COLOR_WHITE
                  : (s_stars[i].age < 4U) ? DFR0522_COLOR_CYAN
                                          : DFR0522_COLOR_BLUE;
        target[(uint32_t)s_stars[i].y * DFR0522_WIDTH + s_stars[i].x] = c;
        if (++s_stars[i].age >= FX_STAR_LIFE) {
            s_stars[i].live = false;
        }
    }
}

/* ── Wave: travelling sine (cyan crest over a blue trail) ──────────────── */
/* round(3.5 + 3.2*sin(2*pi*k/32)) precomputed for k = 0..31, range 0..7 */
static const uint8_t s_fx_sine[32] = {
    3, 4, 5, 5, 6, 6, 6, 7, 7, 7, 6, 6, 6, 5, 5, 4,
    3, 3, 2, 2, 1, 1, 1, 0, 0, 0, 1, 1, 1, 2, 2, 3,
};

static void fx_wave_frame(uint8_t *target)
{
    for (uint32_t x = 0; x < DFR0522_WIDTH; x++) {
        uint32_t k  = (x * 2U + s_fx_step) & 31U;
        uint32_t kt = (x * 2U + s_fx_step + 29U) & 31U;   /* trail = -3 steps */
        target[(uint32_t)s_fx_sine[kt] * DFR0522_WIDTH + x] = DFR0522_COLOR_BLUE;
        target[(uint32_t)s_fx_sine[k]  * DFR0522_WIDTH + x] = DFR0522_COLOR_CYAN;
    }
}

/* ── Pacman: chased across the panel by a ghost, eating a dot trail ────── */
/* 5x5 sprites, one byte per row, bits 4..0 = left..right */
static const uint8_t s_fx_pac_closed[5] = { 0x0E, 0x1F, 0x1F, 0x1F, 0x0E };
static const uint8_t s_fx_pac_open[5]   = { 0x0E, 0x1C, 0x18, 0x1C, 0x0E };
static const uint8_t s_fx_ghost[5]      = { 0x0E, 0x15, 0x1F, 0x1F, 0x15 };

static void fx_sprite5(uint8_t *target, int16_t px, const uint8_t rows[5],
                       uint8_t color)
{
    for (uint32_t r = 0; r < 5U; r++) {
        for (uint32_t c = 0; c < 5U; c++) {
            if ((rows[r] & (uint8_t)(1U << (4U - c))) == 0U) continue;
            int16_t x = (int16_t)(px + (int16_t)c);
            if (x < 0 || x >= (int16_t)DFR0522_WIDTH) continue;
            target[(r + 1U) * DFR0522_WIDTH + (uint32_t)x] = color;
        }
    }
}

/* Steps per lap. pac_x runs -7..(TRACK-8); the ghost trails 7 columns
 * behind, so the lap must be long enough for its right edge (pac_x-3) to
 * leave the panel before the wrap, or a red column pops off mid-frame. */
#define FX_PAC_TRACK  31

static void fx_pacman_frame(uint8_t *target)
{
    int16_t pac_x = (int16_t)(s_fx_step % FX_PAC_TRACK) - 7;   /* -7..22 */

    /* Dot trail ahead of pacman (row 3, every 3rd column) */
    for (uint32_t x = 0; x < DFR0522_WIDTH; x++) {
        if ((x % 3U) == 1U && (int16_t)x >= pac_x + 5) {
            target[3U * DFR0522_WIDTH + x] = DFR0522_COLOR_WHITE;
        }
    }

    fx_sprite5(target, (int16_t)(pac_x - 7), s_fx_ghost, DFR0522_COLOR_RED);
    fx_sprite5(target, pac_x,
               (s_fx_step & 1U) ? s_fx_pac_open : s_fx_pac_closed,
               DFR0522_COLOR_YELLOW);
}

static void dfr0522_fx_tick(lv_timer_t *timer)
{
    (void)timer;
    /* Same bus handoff as the marquee: CM33_NS owns the SCB while touch
     * is paused, so skip this frame instead of colliding. */
    if (lv_port_indev_touch_paused()) {
        return;
    }

    uint8_t target[RGB_PIXELS];
    memset(target, DFR0522_COLOR_OFF, sizeof(target));

    switch (s_fx_id) {
    case DFR0522_FX_STARS:  fx_stars_frame(target);  break;
    case DFR0522_FX_WAVE:   fx_wave_frame(target);   break;
    case DFR0522_FX_PACMAN: fx_pacman_frame(target); break;
    default:                return;
    }

    (void)dfr0522_flush_target(target);
    s_fx_step++;
}

bool dfr0522_effect(dfr0522_fx_t fx, uint16_t period_ms)
{
    if (fx == DFR0522_FX_NONE) {
        return dfr0522_clear();   /* stops both engines + blanks the panel */
    }
    if (fx > DFR0522_FX_PACMAN) {
        return false;
    }

    dfr0522_scroll_stop();

    /* Step periods are chosen for motion, and stay comfortably above the
     * wire cost of one frame (see RGB_TX_SIZE — a 5-byte command puts a
     * 24-pixel wave step in the low milliseconds, so the panel is idle
     * between steps rather than still painting the previous one). */
    if (period_ms == 0U) {
        period_ms = (fx == DFR0522_FX_STARS)  ? 110U
                  : (fx == DFR0522_FX_WAVE)   ? 60U
                                              : 90U;    /* pacman */
    }

    s_fx_id   = fx;
    s_fx_step = 0;
    s_fx_rng ^= lv_tick_get() | 1U;
    if (fx == DFR0522_FX_STARS) {
        memset(s_stars, 0, sizeof(s_stars));
    }

    if (s_fx_timer) {
        lv_timer_set_period(s_fx_timer, period_ms);
    } else {
        s_fx_timer = lv_timer_create(dfr0522_fx_tick, period_ms, NULL);
    }
    return (s_fx_timer != NULL);
}

#endif /* BSP_HAS_QWA309_BASEBOARD */
