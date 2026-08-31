/* sdk-example: core=cm55 variant=both group=display
 * id:      cm55/display/08_sprites
 * title:   Animate an image sprite
 * teaches: build an lv_image_dsc_t with real transparency, create a sprite through the handle table, and swap frames without churning the object
 * apis:    ui_widget_mgr_create_sprite, ui_widget_mgr_set_sprite_image, ui_widget_mgr_set_position, ui_widget_mgr_get_object, ipc_ui_set_container
 * entry:   example_ipc_core_sprites
 */
/*******************************************************************************
 * ipc_core/09_sprites — one object, many frames.
 *
 * A sprite is not a widget type you can ask ui_widget_mgr_create() for: there
 * is no room in ipc_ui_create_t for a picture, and the pixels are project
 * local symbols the other core must never name. So sprites get their own
 * constructor, ui_widget_mgr_create_sprite(), which takes a descriptor
 * pointer directly and hands back an ordinary handle. From that moment
 * set_position, set_visible and delete all work on it as usual.
 *
 * THE TWO FAILURE RETURNS ARE DIFFERENT PROBLEMS
 * ----------------------------------------------
 *   -1  the 64-slot handle table is full
 *   -2  no container bound, a NULL descriptor, OR no sprite engine on this
 *       board — game_sprite_create() is a weak hook whose default returns
 *       NULL (see ipc_core/08_override_hooks). A Claw board with no strong
 *       override gets -2 forever and there is nothing wrong with it.
 * Reporting -2 as "sprite failed" and stopping is the mistake; report which.
 *
 * FRAME SWAPS ARE CHEAP, OBJECT CHURN IS NOT
 * ------------------------------------------
 * ui_widget_mgr_set_sprite_image() reuses the object and only changes the
 * source, and the engine skips the repaint when the descriptor pointer has not
 * changed. Deleting and recreating a sprite per frame instead is what makes an
 * LVGL game flicker: it invalidates the whole area twice per tick and
 * reallocates a handle each time.
 *
 * The type check inside that call is not defensive noise. A handle whose slot
 * has been recycled into, say, a Label would otherwise be handed to
 * lv_image_set_src(), which reads a mistyped struct and can free a garbage
 * pointer — CM55 heap corruption from two lines of script.
 *
 * THE DESCRIPTOR
 * --------------
 * RGB565A8, which is what the shipped game artwork uses: a colour plane of
 * w*h*2 bytes followed by an alpha plane of w*h bytes, stride = w*2 (the
 * colour plane's row length). Alpha is what lets the sprite sit over a page
 * without a rectangle of background around it. Built in RAM here so the file
 * carries no hex blob; real assets come out of LVGL's image converter as a
 * const array plus LV_IMAGE_DECLARE().
 *
 * NOT BLOCKING
 * ------------
 * run() creates one sprite and one lv_timer. The sprite's own delete event
 * stops the timer, so nav-away or another example ends the animation.
 *******************************************************************************/

#include "../sdk_examples.h"

#include <string.h>

#include "ipc_ui.h"
#include "ipc_ui_protocol.h"
#include "ui_widget_mgr.h"

#define SPR_W          (16)
#define SPR_H          (16)
#define SPR_FRAMES     (2)
#define ANIM_PERIOD_MS (70)

/* One bit per pixel, MSB is x = 0. Two frames of the same creature, wings up
 * and wings out. */
static const uint16_t s_frame_mask[SPR_FRAMES][SPR_H] = {
    {   /* wings up */
        0x0000, 0x0180, 0x03C0, 0x07E0, 0x0FF0, 0x1FF8, 0x3FFC, 0x3FFC,
        0x1FF8, 0x0FF0, 0x07E0, 0x03C0, 0x0180, 0x0000, 0x0000, 0x0000,
    },
    {   /* wings out */
        0x0000, 0x0180, 0x03C0, 0x07E0, 0x0FF0, 0x1FF8, 0xFFFF, 0xFFFF,
        0x1FF8, 0x0FF0, 0x07E0, 0x03C0, 0x0180, 0x0000, 0x0000, 0x0000,
    },
};

/* Colour plane then alpha plane, per LV_COLOR_FORMAT_RGB565A8. */
static uint8_t        s_pixels[SPR_FRAMES][SPR_W * SPR_H * 3];
static lv_image_dsc_t s_dsc[SPR_FRAMES];

static int         s_sprite = -1;
static lv_timer_t *s_timer;
static uint32_t    s_tick;
static int16_t     s_x;
static int16_t     s_dx = 4;
static int16_t     s_x_max = 300;

static void build_frames(void)
{
    static const uint16_t body = 0xFD40u;   /* amber, RGB565            */
    static const uint16_t edge = 0xC2A0u;   /* darker rim               */

    for (int f = 0; f < SPR_FRAMES; f++) {
        uint8_t *colour = s_pixels[f];
        uint8_t *alpha  = s_pixels[f] + (SPR_W * SPR_H * 2);

        for (int y = 0; y < SPR_H; y++) {
            for (int x = 0; x < SPR_W; x++) {
                const bool on = (s_frame_mask[f][y] & (0x8000u >> x)) != 0u;
                /* A pixel on the outline of the shape gets the rim colour;
                 * this is only here so the two frames are legible at 16 px. */
                const bool rim = on && ((x == 0) || (x == SPR_W - 1) ||
                                        ((s_frame_mask[f][y] & (0x8000u >> (x + 1))) == 0u) ||
                                        ((s_frame_mask[f][y] & (0x8000u >> (x - 1))) == 0u));
                const uint16_t c = rim ? edge : body;
                const int      i = (y * SPR_W + x);
                colour[i * 2]     = (uint8_t)(c & 0xFFu);
                colour[i * 2 + 1] = (uint8_t)(c >> 8);
                alpha[i]          = on ? 0xFFu : 0x00u;   /* transparent bg */
            }
        }

        memset(&s_dsc[f], 0, sizeof(s_dsc[f]));
        s_dsc[f].header.magic  = LV_IMAGE_HEADER_MAGIC;
        s_dsc[f].header.cf     = LV_COLOR_FORMAT_RGB565A8;
        s_dsc[f].header.w      = SPR_W;
        s_dsc[f].header.h      = SPR_H;
        s_dsc[f].header.stride = SPR_W * 2;      /* colour plane row bytes */
        s_dsc[f].data_size     = sizeof(s_pixels[f]);
        s_dsc[f].data          = s_pixels[f];
    }
}

static void container_deleted_cb(lv_event_t *e)
{
    (void)e;
    ipc_ui_set_container(NULL);
}

static void sprite_deleted_cb(lv_event_t *e)
{
    (void)e;
    if (s_timer != NULL) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    s_sprite = -1;
}

static void anim_cb(lv_timer_t *t)
{
    (void)t;
    s_tick++;

    /* Frame swap every third tick — roughly 5 flaps a second. The engine
     * ignores a swap to the frame already showing, so calling this every tick
     * would be correct too, just wasteful of the comparison. */
    ui_widget_mgr_set_sprite_image(s_sprite, &s_dsc[(s_tick / 3u) % SPR_FRAMES]);

    s_x = (int16_t)(s_x + s_dx);
    if (s_x >= s_x_max) { s_x = s_x_max; s_dx = (int16_t)-s_dx; }
    if (s_x <= 10)      { s_x = 10;      s_dx = (int16_t)-s_dx; }

    /* A shallow bob so the motion reads as flight rather than a slide. */
    const int16_t bob = (int16_t)(((s_tick / 2u) % 8u) < 4u ? 0 : 6);
    ui_widget_mgr_set_position(s_sprite, s_x, (int16_t)(120 + bob));
}

int example_ipc_core_sprites(lv_obj_t *parent)
{
    ipc_ui_set_container(parent);
    lv_obj_remove_event_cb(parent, container_deleted_cb);
    lv_obj_add_event_cb(parent, container_deleted_cb, LV_EVENT_DELETE, NULL);

    if (s_timer != NULL) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }

    build_frames();
    s_tick = 0;
    s_x = 10;
    s_dx = 4;

    const int32_t cw = lv_obj_get_content_width(parent);
    s_x_max = (int16_t)((cw > (SPR_W + 40)) ? (cw - SPR_W - 20) : 100);

    lv_obj_t *caption = lv_label_create(parent);
    lv_label_set_text(caption, "ui_widget_mgr_create_sprite() + set_sprite_image()");
    lv_obj_set_style_text_color(caption, lv_color_hex(0xD8E0F0), 0);
    lv_obj_set_pos(caption, 10, 10);

    /* The descriptor pointer must outlive the sprite: the engine stores it,
     * it does not copy the pixels. Statics here; const flash arrays in a real
     * project. A descriptor on the stack is a use-after-return. */
    s_sprite = ui_widget_mgr_create_sprite(&s_dsc[0], s_x, 120);
    if (s_sprite == -1) {
        sdk_example_logf("handle table full (%d slots) - delete something first",
                         UI_MAX_WIDGETS);
        return SDK_EX_BUSY;
    }
    if (s_sprite < 0) {
        /* -2. Three causes, and only one of them is a bug in this file. */
        sdk_example_logf("create_sprite returned -2");
        sdk_example_logf("  container bound: %s",
                         ui_widget_mgr_needs_container() ? "NO" : "yes");
        sdk_example_logf("  most likely: no strong game_sprite_create() on this");
        sdk_example_logf("  board. The library default returns NULL. See");
        sdk_example_logf("  ipc_core/08_override_hooks for the override.");
        return SDK_EX_UNAVAILABLE;
    }

    lv_obj_t *spr_obj = ui_widget_mgr_get_object(s_sprite);
    if (spr_obj == NULL) {
        return SDK_EX_REFUSED;
    }
    lv_obj_add_event_cb(spr_obj, sprite_deleted_cb, LV_EVENT_DELETE, NULL);

    s_timer = lv_timer_create(anim_cb, ANIM_PERIOD_MS, NULL);
    if (s_timer == NULL) {
        sdk_example_logf("lv_timer_create failed - sprite will not animate");
        return SDK_EX_REFUSED;
    }

    sdk_example_logf("sprite handle %d, %d frames of %dx%d RGB565A8",
                     s_sprite, SPR_FRAMES, SPR_W, SPR_H);
    sdk_example_logf("  descriptor: %u bytes each (colour plane + alpha plane)",
                     (unsigned)sizeof(s_pixels[0]));
    sdk_example_logf("  animating every %d ms, sweeping x 10..%d",
                     ANIM_PERIOD_MS, (int)s_x_max);
    return SDK_EX_STARTED;
}
