/* sdk-example: core=cm55 variant=both group=io
 * id:      cm55/io/01_rgb_matrix
 * title:   Draw on the DFR0522 16x8 RGB matrix
 * teaches: the panel is on the DISPLAY I2C bus, so it belongs to the GFX task and to no other; the eight draw ops, what each one costs on the wire, and which of them cancel an animation
 * apis:    dfr0522_clear, dfr0522_fill, dfr0522_pixel, dfr0522_blit, dfr0522_score, dfr0522_bar, dfr0522_scroll, dfr0522_effect
 * entry:   example_cm55_rgb_matrix
 */
/*******************************************************************************
 * io/01 — the QWA309 base board's 16x8 RGB LED matrix.
 *
 * WHERE THE PANEL IS, AND WHY THAT DECIDES THE CORE
 * -------------------------------------------------
 * The DFR0522 answers at I2C 0x10 on the HEADER bus, P17.0 (SCL) / P17.1 (SDA).
 * On this board that bus is the DISPLAY / touch SCB, and CM55 owns it — the
 * same controller the CapSense read path uses through
 * DISPLAY_I2C_CONTROLLER_HW + disp_touch_i2c_controller_context.
 *
 *   proj_cm55/modules/dfr0522_rgb/dfr0522_rgb.h:4-8   address, pins, ownership
 *   bento_libs/claw/kit-tesaiot-pse84-ai/arduino_shield_qwa309.c:74-75
 *                                                     A4 = P17.1 SDA,
 *                                                     A5 = P17.0 SCL
 *   proj_cm55/Makefile:306-309  (reference: proj_cm55/Makefile:267-269)
 *                                                     INCLUDES+=modules/dfr0522_rgb,
 *                                                     CY_IGNORE'd unless
 *                                                     BSP_HAS_QWA309_BASEBOARD=1
 *
 * So the driver runs HERE. `import rgbmatrix` in MicroPython on CM33_NS does
 * not touch the bus at all: it sends IPC_CMD_UI_RGB_MATRIX and the transaction
 * still executes in this task. There is no second path, and adding one would
 * put two masters on the display bus.
 *
 * WHAT EACH CALL COSTS, AND THE 51-VERSUS-5 TRAP
 * ----------------------------------------------
 * Every op is one I2C write starting at register 0x02:
 *
 *     [0x02, FUNC, COLOR, PIX_X, PIX_Y]
 *      FUNC  1 = clear, 8 = set pixel, 9 = fill   (dfr0522_rgb.c:14-25)
 *
 * DFRobot's Arduino library always ships its whole 50-byte buffer, and the
 * header block of dfr0522_rgb.h:10-11 still describes that padded shape. The
 * driver does NOT do that. RGB_TX_FULL_FRAME is 0 and the wire length is
 * RGB_TX_SIZE = 5 (dfr0522_rgb.c:88-95). The panel acts on the command when
 * the transaction ends, so stopping after PIX_Y is complete and valid
 * (dfr0522_rgb.c:24-25).
 *
 * The reason is animation, not tidiness: one effect frame issues one command
 * per CHANGED pixel — 24 for a wave step, up to 35 for pacman — so the 46
 * bytes of zero-fill turned ~120 bytes of real work into ~1.8 KB of wire time
 * per frame, on the bus the display is on. Cutting it ~10x is what makes the
 * effects fluid; verified on a Dev Kit 2026-07-31 (dfr0522_rgb.c:27-42).
 *
 *   clear / fill        one 5-byte command
 *   pixel               one 5-byte command per pixel
 *   blit / score / bar  DIFF-flushed: only changed pixels are sent at all
 *
 * There is NO full-frame upload on this part. The BITMAP register selects a
 * picture already stored in the panel, not an arbitrary bitmap, so repainting
 * is always per-pixel (dfr0522_rgb.c:44-46). That is why the diff exists and
 * why this example never paints the whole panel a pixel at a time: it draws
 * one pixel to show the primitive, then hands whole frames to blit().
 *
 * ANIMATIONS ARE EXCLUSIVE
 * ------------------------
 * dfr0522_scroll() and dfr0522_effect() run on an LVGL timer inside the
 * driver, so they keep stepping while a Python game loop is busy. They are
 * mutually exclusive with each other and with every plain draw op: any of
 * clear / fill / pixel / blit / score / bar stops a running marquee or effect,
 * and starting an effect stops the marquee (dfr0522_rgb.h:79-98). That is why
 * the buttons below are a radio group in behaviour even though they are not
 * one on screen — pressing a second one cancels the first, by design.
 *
 * NOT BLOCKING
 * ------------
 * Each button handler issues at most a handful of frames and returns. Nothing
 * here waits on the bus.
 *******************************************************************************/

#include "../sdk_examples.h"

#include <string.h>

#include "bsp_feature_flags.h"

#if BSP_HAS_QWA309_BASEBOARD
#include "dfr0522_rgb.h"
#endif

#if BSP_HAS_QWA309_BASEBOARD

/* One 4bpp frame in the packing dfr0522_blit() expects:
 *   byte = frame[y * 8 + (x >> 1)], even x in the LOW nibble.
 * 16 wide x 8 tall / 2 pixels per byte = 64 bytes (dfr0522_rgb.h:69-71). */
#define BLIT_BYTES   (64u)

static uint8_t s_frame[BLIT_BYTES];

static void frame_set(uint8_t x, uint8_t y, uint8_t colour)
{
    const unsigned i = (unsigned)y * 8u + (unsigned)(x >> 1);
    if (i >= BLIT_BYTES) {
        return;
    }
    if ((x & 1u) == 0u) {
        s_frame[i] = (uint8_t)((s_frame[i] & 0xF0u) | (colour & 0x0Fu));
    } else {
        s_frame[i] = (uint8_t)((s_frame[i] & 0x0Fu) | (uint8_t)((colour & 0x0Fu) << 4));
    }
}

/* A diagonal band, drawn as a frame rather than as 128 pixel commands. */
static void frame_build_band(void)
{
    memset(s_frame, 0, sizeof(s_frame));
    for (uint8_t y = 0u; y < DFR0522_HEIGHT; y++) {
        for (uint8_t x = 0u; x < DFR0522_WIDTH; x++) {
            const uint8_t band = (uint8_t)(((x + y) / 2u) % 7u);
            frame_set(x, y, (uint8_t)(band + 1u));      /* 1..7, never OFF */
        }
    }
}

static void say(const char *what, bool ok)
{
    sdk_example_logf("%-28s %s", what, ok ? "ok" : "I2C FAILED");
}

static void on_clear(lv_event_t *e)
{
    (void)e;
    sdk_example_log_clear();
    say("dfr0522_clear()", dfr0522_clear());
    sdk_example_logf("  one 5-byte command; also stops a marquee or effect");
}

static void on_fill(lv_event_t *e)
{
    (void)e;
    sdk_example_log_clear();
    say("dfr0522_fill(CYAN)", dfr0522_fill((uint8_t)DFR0522_COLOR_CYAN));
    say("dfr0522_pixel(0,0,RED)",
        dfr0522_pixel(0u, 0u, (uint8_t)DFR0522_COLOR_RED));
    sdk_example_logf("  colours are 0..7, 0 = OFF (dfr0522_color_t)");
    sdk_example_logf("  panel is %ux%u — x 0..%u, y 0..%u",
                     (unsigned)DFR0522_WIDTH, (unsigned)DFR0522_HEIGHT,
                     (unsigned)DFR0522_WIDTH - 1u, (unsigned)DFR0522_HEIGHT - 1u);
}

static void on_frame(lv_event_t *e)
{
    (void)e;
    sdk_example_log_clear();
    frame_build_band();
    say("dfr0522_blit(64-byte frame)", dfr0522_blit(s_frame));
    say("dfr0522_score(1234, WHITE)",
        dfr0522_score(1234u, (uint8_t)DFR0522_COLOR_WHITE));
    say("dfr0522_bar(7, 10, GREEN)",
        dfr0522_bar(7u, 10u, (uint8_t)DFR0522_COLOR_GREEN));
    sdk_example_logf("  all three are diff-flushed: only changed pixels go out");
}

static void on_scroll(lv_event_t *e)
{
    (void)e;
    sdk_example_log_clear();
    static const char msg[] = "BENTO";
    say("dfr0522_scroll(\"BENTO\")",
        dfr0522_scroll(msg, (uint8_t)(sizeof(msg) - 1u),
                       (uint8_t)DFR0522_COLOR_YELLOW, 120u));
    sdk_example_logf("  runs on the driver's own LVGL timer, 120 ms/step");
    sdk_example_logf("  len 0 or period 0 stops it; so does any draw op");
}

static void on_effect(lv_event_t *e)
{
    (void)e;
    sdk_example_log_clear();
    say("dfr0522_effect(FX_PACMAN)", dfr0522_effect(DFR0522_FX_PACMAN, 0u));
    sdk_example_logf("  period 0 = the effect's own default step");
    sdk_example_logf("  DFR0522_FX_NONE stops it AND clears the panel");
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, lv_event_cb_t cb,
                        int32_t x, int32_t y)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, 150, 44);          /* 44 px: the minimum touch target */
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    return btn;
}

int example_cm55_rgb_matrix(lv_obj_t *parent)
{
    lv_obj_t *caption = lv_label_create(parent);
    lv_label_set_text(caption,
                      "DFR0522 16x8 @ I2C 0x10 on the display bus (P17.0/P17.1)");
    lv_obj_set_style_text_color(caption, lv_color_hex(0xD8E0F0), 0);
    lv_obj_set_pos(caption, 10, 8);

    (void)button(parent, "clear",  on_clear,   10, 40);
    (void)button(parent, "fill",   on_fill,   170, 40);
    (void)button(parent, "frames", on_frame,  330, 40);
    (void)button(parent, "scroll", on_scroll,  10, 92);
    (void)button(parent, "effect", on_effect, 170, 92);

    /* Prove the panel is really there before claiming anything: one clear is
     * one transaction, and its return value is the ACK. */
    const bool present = dfr0522_clear();

    sdk_example_logf("panel present: %s", present ? "yes" : "NO ACK at 0x10");
    if (!present) {
        sdk_example_logf("  the usual cause is the base-board power switch,");
        sdk_example_logf("  not a missing panel. Nothing else on this bus");
        sdk_example_logf("  was disturbed — the display kept its own frames.");
        return SDK_EX_UNAVAILABLE;
    }
    sdk_example_logf("tap a button; every op runs in THIS task, on the");
    sdk_example_logf("display SCB, and returns before the next frame is due");
    return SDK_EX_OK;
}

#else  /* !BSP_HAS_QWA309_BASEBOARD */

int example_cm55_rgb_matrix(lv_obj_t *parent)
{
    (void)parent;
    sdk_example_logf("No QWA309 base board in this build.");
    sdk_example_logf("  dfr0522_rgb.h is empty unless BSP_HAS_QWA309_BASEBOARD,");
    sdk_example_logf("  and proj_cm55/Makefile CY_IGNOREs the whole module,");
    sdk_example_logf("  so there is no driver to call rather than a driver");
    sdk_example_logf("  that would fail. Build for the TESAIoT Dev Kit.");
    return SDK_EX_UNAVAILABLE;
}

#endif /* BSP_HAS_QWA309_BASEBOARD */
