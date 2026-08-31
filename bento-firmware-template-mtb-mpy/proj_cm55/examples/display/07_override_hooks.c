/* sdk-example: core=cm55 variant=both group=display
 * id:      cm55/display/07_override_hooks
 * title:   Override the library's weak hooks with your own
 * teaches: which seven symbols libbento_ipc leaves for you, their exact signatures, and how to tell whose definition the linker chose
 * apis:    cm55_controls_snapshot, game_sprite_create, game_sprite_set, game_sprite_lookup, ipc_ui_ext_clear_all, ipc_ui_ext_dispatch, ipc_ui_platform_diag
 * entry:   example_ipc_core_override_hooks
 */
/*******************************************************************************
 * ipc_core/08_override_hooks — the seven seams in libbento_ipc.
 *
 * The archive defines these seven __attribute__((weak)). A weak definition is
 * a placeholder: if any object in the link provides a STRONG definition of the
 * same name, the linker drops the archive's and keeps yours, with no warning
 * and no flag. That is the entire mechanism, and it is how one board-agnostic
 * library serves a Game console with real sprites, a Dev Kit with an RGB
 * matrix on the display I2C, and a bare Claw board with neither.
 *
 * THE SEVEN, AND WHERE THE SIGNATURE COMES FROM
 * ---------------------------------------------
 * Two are declared in shipped headers. Five are not — the library calls them
 * but never publishes a prototype, so YOU declare them, and a declaration that
 * disagrees with the archive's call site links silently and then corrupts the
 * stack at run time. Every signature below was read out of the library source
 * and the file:line is on the definition:
 *
 *   cm55_controls_snapshot   ipc_communication.h:198  (declared for you)
 *                            weak default: ipc_service.c:862
 *   ipc_ui_ext_dispatch      ipc_ui.h                 (declared for you)
 *                            weak default: ipc_ui.c:81
 *   game_sprite_lookup       NO HEADER  weak default: ipc_ui.c:64
 *   ipc_ui_ext_clear_all     NO HEADER  weak default: ipc_ui.c:93
 *   ipc_ui_platform_diag     NO HEADER  weak default: ipc_ui.c:165
 *   game_sprite_create       NO HEADER  weak default: ui_widget_mgr.c:1206
 *   game_sprite_set          NO HEADER  weak default: ui_widget_mgr.c:1213
 *
 * (BENTO-TESAIoT-libraries/claw/common/modules/ipc_ui/ and .../ipc_service/.
 *  Verified against the shipped archive too: `nm --defined-only libbento_ipc.a`
 *  lists all seven as W.)
 *
 * WHY THESE DEFINITIONS ARE WEAK TOO
 * ----------------------------------
 * In YOUR project you write them strong — that is the point, and it is one
 * word shorter. Here they are weak by default because this file ships beside
 * a template that already defines six of the seven strongly
 * (modules/game_sprites_mpy, modules/dfr0522_rgb, modules/ipc_ui_ext,
 * modules/cm55_sensor_poll), and two strong definitions of one symbol is a
 * link error, not a preference. Build with
 *
 *     -DTESAIOT_EXAMPLE_HOOKS_STRONG
 *
 * to take ownership instead — after removing the project's own definition.
 *
 * WHOSE DEFINITION IS LIVE?
 * -------------------------
 * You cannot ask the linker at run time, but you can ask the code: each
 * definition below records that it ran. run() calls the five that are safe to
 * call and prints, per hook, whether ours answered. ipc_ui_ext_clear_all() is
 * defined and deliberately NOT called — on a Dev Kit the real one stops the
 * RGB matrix marquee and blanks the panel, and an example has no business
 * doing that to a running program.
 *******************************************************************************/

#include "../sdk_examples.h"

#include <string.h>

#include "ipc_communication.h"   /* ipc_controls_state_t, cm55_controls_snapshot */
#include "ipc_ui.h"              /* ipc_ui_ext_dispatch                          */
#include "ipc_ui_protocol.h"     /* BENTO_SPR_*, IPC_CMD_UI_*                     */

#ifdef TESAIOT_EXAMPLE_HOOKS_STRONG
#  define EXAMPLE_HOOK
#else
#  define EXAMPLE_HOOK  __attribute__((weak))
#endif

/*******************************************************************************
 * The five prototypes no shipped header carries. Copy this block verbatim.
 *******************************************************************************/
const lv_image_dsc_t *game_sprite_lookup(int sprite_id);
lv_obj_t             *game_sprite_create(lv_obj_t *parent, const lv_image_dsc_t *dsc);
void                  game_sprite_set(lv_obj_t *sprite, const lv_image_dsc_t *dsc);
void                  ipc_ui_ext_clear_all(void);
uint16_t              ipc_ui_platform_diag(uint32_t *out, uint16_t max_words);

/* Did OUR definition run? One flag per hook, set from inside. */
static volatile bool s_ran_lookup;
static volatile bool s_ran_create;
static volatile bool s_ran_set;
static volatile bool s_ran_controls;
static volatile bool s_ran_diag;
static volatile bool s_ran_dispatch;
static volatile bool s_ran_clear_all;

/*******************************************************************************
 * 1. game_sprite_lookup() — id to compiled descriptor.
 *
 * WHEN TO OVERRIDE: your project has sprite artwork and wants MicroPython's
 * ui.Sprite(ui.SPR_*) or bentogame to reach it. The ids cross the IPC pipe as
 * integers because the descriptors are project-local symbols that CM33 must
 * never name. The library's default returns NULL for every id, which makes a
 * sprite create fail cleanly instead of drawing garbage.
 *
 * The real registry is an array indexed by BENTO_SPR_*, so a renamed asset is
 * a LINK error rather than silent drift. This stand-in has one entry.
 *******************************************************************************/
#define SPR_W   (16)
#define SPR_H   (16)

static uint8_t       s_sprite_pixels[SPR_W * SPR_H * 2];
static lv_image_dsc_t s_sprite_dsc;
static bool          s_sprite_ready;

static void build_sprite(void)
{
    if (s_sprite_ready) {
        return;
    }
    /* A checker, RGB565 little-endian — the byte order LVGL stores for
     * LV_COLOR_FORMAT_RGB565. Real artwork comes out of LVGL's image
     * converter as a const array plus LV_IMAGE_DECLARE(); this one is built
     * in RAM only so the example carries no 512-byte hex blob. */
    for (int y = 0; y < SPR_H; y++) {
        for (int x = 0; x < SPR_W; x++) {
            const uint16_t c = (((x >> 2) ^ (y >> 2)) & 1) ? 0xFD20u : 0x2D7Fu;
            const int      i = (y * SPR_W + x) * 2;
            s_sprite_pixels[i]     = (uint8_t)(c & 0xFFu);
            s_sprite_pixels[i + 1] = (uint8_t)(c >> 8);
        }
    }
    memset(&s_sprite_dsc, 0, sizeof(s_sprite_dsc));
    s_sprite_dsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
    s_sprite_dsc.header.cf     = LV_COLOR_FORMAT_RGB565;
    s_sprite_dsc.header.w      = SPR_W;
    s_sprite_dsc.header.h      = SPR_H;
    s_sprite_dsc.header.stride = SPR_W * 2;      /* what the shipped sprite
                                                  * assets use; no alignment
                                                  * padding at this width    */
    s_sprite_dsc.data_size     = sizeof(s_sprite_pixels);
    s_sprite_dsc.data          = s_sprite_pixels;
    s_sprite_ready = true;
}

EXAMPLE_HOOK const lv_image_dsc_t *game_sprite_lookup(int sprite_id)
{
    s_ran_lookup = true;
    if (sprite_id < 0 || sprite_id >= BENTO_SPR_COUNT) {
        return NULL;                              /* out of range, always NULL */
    }
    build_sprite();
    return &s_sprite_dsc;                         /* one picture, every id     */
}

/*******************************************************************************
 * 2. game_sprite_create() — the sprite engine's constructor.
 *
 * WHEN TO OVERRIDE: whenever you want ui.Sprite / bentogame to render at all.
 * ui_widget_mgr_create_sprite() calls this and treats NULL as "no sprite
 * engine on this board", returning -2. The default returns NULL.
 *
 * Called from the GFX task with the widget manager's container as parent.
 * Return an lv_obj_t that lv_image_set_src() accepts, because
 * game_sprite_set() below will be handed the same object later.
 *******************************************************************************/
EXAMPLE_HOOK lv_obj_t *game_sprite_create(lv_obj_t *parent, const lv_image_dsc_t *dsc)
{
    s_ran_create = true;
    if (parent == NULL || dsc == NULL) {
        return NULL;
    }
    lv_obj_t *img = lv_image_create(parent);
    if (img == NULL) {
        return NULL;
    }
    lv_image_set_src(img, dsc);
    /* A sprite is scenery: it must not scroll the page or swallow taps meant
     * for the widgets underneath it. */
    lv_obj_remove_flag(img, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return img;
}

/*******************************************************************************
 * 3. game_sprite_set() — swap the picture, keep the object.
 *
 * WHEN TO OVERRIDE: same time as create(); they are a pair. Called for every
 * animation frame and every direction change, which is why the equality test
 * below matters: a game loop asks for the same frame most ticks, and
 * lv_image_set_src() invalidates the area whether or not anything changed.
 *******************************************************************************/
EXAMPLE_HOOK void game_sprite_set(lv_obj_t *sprite, const lv_image_dsc_t *dsc)
{
    s_ran_set = true;
    if (sprite == NULL || dsc == NULL) {
        return;
    }
    if (lv_image_get_src(sprite) == dsc) {
        return;                                   /* same frame, no repaint */
    }
    lv_image_set_src(sprite, dsc);
}

/*******************************************************************************
 * 4. cm55_controls_snapshot() — CapSense + pots, answered from CM55.
 *
 * WHEN TO OVERRIDE: your board carries the QWA309 CapSense-4000T and pots.
 * They sit on the CM55-owned display/touch bus, so CM33 must never do direct
 * I2C to them; it asks over IPC_CMD_CONTROLS_STATE instead and ipc_service
 * calls this. Return false and the service answers status 1, "no controls on
 * this board", which is the default.
 *
 * Fill every field you claim. `caps_valid` and `pot_valid` are what tell the
 * caller which half of the struct to believe.
 *******************************************************************************/
EXAMPLE_HOOK bool cm55_controls_snapshot(ipc_controls_state_t *out)
{
    s_ran_controls = true;
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    /* No real controls behind this example, and saying otherwise would put a
     * phantom slider on the Dashboard. Refusing IS the correct answer here. */
    return false;
}

/*******************************************************************************
 * 5. ipc_ui_platform_diag() — append your counters to the UI diagnostics.
 *
 * WHEN TO OVERRIDE: you own a display controller and want its counters in the
 * answer to IPC_CMD_UI_GET_DIAG. ipc_ui writes seven words first — five drop
 * counters, the queue high-water mark, and the fast-drain flag — then calls
 * this with the remaining room. Return HOW MANY WORDS YOU WROTE; write no more
 * than max_words or you overrun the response buffer. The default returns 0.
 *
 * This is the only one of the seven the shipped template does NOT define, so
 * on that firmware the definition below is the live one.
 *******************************************************************************/
#define DIAG_MARKER   (0xB3410008u)   /* so a reader can find our block */

EXAMPLE_HOOK uint16_t ipc_ui_platform_diag(uint32_t *out, uint16_t max_words)
{
    s_ran_diag = true;
    if (out == NULL || max_words < 2u) {
        return 0u;                    /* no room is not an error, it is zero */
    }
    out[0] = DIAG_MARKER;
    out[1] = lv_tick_get();
    return 2u;
}

/*******************************************************************************
 * 6. ipc_ui_ext_dispatch() — service UI-band opcodes the library does not know.
 *
 * WHEN TO OVERRIDE: your project owns a peripheral that must be driven from
 * the GFX task — the DFR0522 RGB matrix on the display I2C, the audio mixer,
 * a motor shield on the header bus. Called from process_ui_command() for any
 * opcode in 0x50..0x6F the built-in switch did not handle. Return true if you
 * handled it; the caller then arms the fast drain, because handled extension
 * traffic is gameplay traffic.
 *
 * 0x6C..0x6E are the reserved free headroom. Everything else in the band is
 * already spoken for, so an override that swallows an unrecognised opcode
 * breaks a feature it has never heard of. Match narrowly, decline by default.
 *
 * A project with more than one feature on this hook overrides it ONCE and
 * chains internally; see the template's modules/ipc_ui_ext/ipc_ui_ext_chain.c.
 *******************************************************************************/
#define EXAMPLE_EXT_OPCODE   (0x6Cu)

EXAMPLE_HOOK bool ipc_ui_ext_dispatch(uint32_t cmd, const uint8_t *data)
{
    s_ran_dispatch = true;
    if (cmd != EXAMPLE_EXT_OPCODE || data == NULL) {
        return false;                 /* not ours */
    }
    /* data points at the raw 128-byte ipc_msg_t.data[]. GFX-task context, so
     * touching the display or the display I2C bus is legal here and only
     * here. Keep it short: this runs inside the UI drain's 4 ms budget. */
    return true;
}

/*******************************************************************************
 * 7. ipc_ui_ext_clear_all() — reset your peripheral for the next program.
 *
 * WHEN TO OVERRIDE: same peripherals as the hook above. Called on
 * IPC_CMD_UI_CLEAR_ALL, which means a new MicroPython script is starting, so
 * the previous student's scrolling banner must stop and the panel must blank.
 * The default does nothing.
 *
 * NOT called by run(): stopping a marquee somebody is watching is not a demo.
 *******************************************************************************/
EXAMPLE_HOOK void ipc_ui_ext_clear_all(void)
{
    s_ran_clear_all = true;
    /* Your peripheral reset goes here: stop timers, blank the panel, drop any
     * state the finished program left behind. */
}

/*******************************************************************************
 * The example: probe each hook and say who answered.
 *******************************************************************************/
static void hook_row(lv_obj_t *box, const char *name, bool ours, const char *note)
{
    lv_obj_t *lbl = lv_label_create(box);
    lv_label_set_text_fmt(lbl, "%-24s %s  %s", name,
                          ours ? "ours " : "other", note);
    lv_obj_set_style_text_color(lbl,
        lv_color_hex(ours ? 0x7BE38B : 0x9AD1FF), 0);
}

int example_ipc_core_override_hooks(lv_obj_t *parent)
{
    lv_obj_clean(parent);

    s_ran_lookup = false;
    s_ran_create = false;
    s_ran_set = false;
    s_ran_controls = false;
    s_ran_diag = false;
    s_ran_dispatch = false;

    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_size(box, lv_pct(96), 290);
    lv_obj_set_pos(box, 8, 8);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x141428), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(0x2A3550), 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_radius(box, 8, 0);
    lv_obj_set_style_pad_all(box, 10, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *title = lv_label_create(box);
    lv_label_set_text(title, "weak hook            whose definition answered");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);

    /* --- probe 1: sprite lookup ------------------------------------------ */
    const lv_image_dsc_t *dsc = game_sprite_lookup(BENTO_SPR_SNAKE_HEAD_R);
    hook_row(box, "game_sprite_lookup", s_ran_lookup,
             (dsc != NULL) ? "returned a descriptor" : "returned NULL");

    /* --- probe 2 and 3: create + set, drawing a real sprite -------------- */
    build_sprite();
    lv_obj_t *spr = game_sprite_create(parent, &s_sprite_dsc);
    if (spr != NULL) {
        lv_obj_set_pos(spr, 20, 310);
        game_sprite_set(spr, &s_sprite_dsc);   /* same frame: should no-op */
    }
    hook_row(box, "game_sprite_create", s_ran_create,
             (spr != NULL) ? "made an object (see below)" : "returned NULL");
    hook_row(box, "game_sprite_set", s_ran_set,
             (spr != NULL) ? "frame swap requested" : "not reached");

    /* --- probe 4: controls snapshot -------------------------------------- */
    ipc_controls_state_t st;
    memset(&st, 0, sizeof(st));
    const bool have_controls = cm55_controls_snapshot(&st);
    {
        char note[48];
        lv_snprintf(note, sizeof(note), "%s, caps_valid %u",
                    have_controls ? "true" : "false",
                    (unsigned)st.caps_valid);
        hook_row(box, "cm55_controls_snapshot", s_ran_controls, note);
    }

    /* --- probe 5: platform diag ------------------------------------------ */
    uint32_t words[8];
    memset(words, 0, sizeof(words));
    const uint16_t nw = ipc_ui_platform_diag(words, (uint16_t)(sizeof(words) / sizeof(words[0])));
    {
        char note[56];
        lv_snprintf(note, sizeof(note), "%u word(s), first 0x%08X",
                    (unsigned)nw, (unsigned)words[0]);
        hook_row(box, "ipc_ui_platform_diag", s_ran_diag, note);
    }

    /* --- probe 6: ext dispatch on a reserved-free opcode ------------------ */
    uint8_t payload[8];
    memset(payload, 0, sizeof(payload));
    const bool handled = ipc_ui_ext_dispatch(EXAMPLE_EXT_OPCODE, payload);
    hook_row(box, "ipc_ui_ext_dispatch", s_ran_dispatch,
             handled ? "claimed opcode 0x6C" : "declined opcode 0x6C");

    /* --- 7: defined, deliberately not called ----------------------------- */
    hook_row(box, "ipc_ui_ext_clear_all", false,
             "defined; not called (would reset a peripheral)");

    sdk_example_logf("weak-hook probe (ours = this file's definition won):");
    sdk_example_logf("  lookup %d  create %d  set %d",
                     (int)s_ran_lookup, (int)s_ran_create, (int)s_ran_set);
    sdk_example_logf("  controls %d  diag %d  dispatch %d",
                     (int)s_ran_controls, (int)s_ran_diag, (int)s_ran_dispatch);
    sdk_example_logf("  platform_diag wrote %u word(s)", (unsigned)nw);
    sdk_example_logf("build -DTESAIOT_EXAMPLE_HOOKS_STRONG to take ownership");

    if (spr == NULL) {
        /* Whichever definition answered, it refused to build an object. */
        sdk_example_logf("no sprite engine answered - ui.Sprite will return -2");
        return SDK_EX_UNAVAILABLE;
    }
    return SDK_EX_OK;
}
