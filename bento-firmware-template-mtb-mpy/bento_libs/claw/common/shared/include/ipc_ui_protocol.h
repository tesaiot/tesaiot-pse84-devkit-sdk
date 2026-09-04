/*******************************************************************************
 * File Name: ipc_ui_protocol.h
 *
 * Description: IPC protocol definitions for the MicroPython UI module.
 *              Shared between CM33_NS (modui.c) and CM55 (ipc_ui.c).
 *
 *              CM33_NS sends UI commands via IPC Pipe to CM55, which
 *              creates/modifies/deletes LVGL widgets in the UX/UI tab.
 *              Bidirectional for CREATE, POLL_EVENTS, and GET_VALUE.
 *
 *******************************************************************************/

#ifndef IPC_UI_PROTOCOL_H
#define IPC_UI_PROTOCOL_H

#include <stdint.h>

/*******************************************************************************
 * UI IPC Command Codes (0x50-0x6A)
 *******************************************************************************/
#define IPC_CMD_UI_CREATE       (0x50)  /* Create widget (bidirectional) */
#define IPC_CMD_UI_DELETE       (0x51)  /* Delete widget by handle */
#define IPC_CMD_UI_SET_TEXT     (0x52)  /* Set text content */
#define IPC_CMD_UI_SET_VALUE    (0x53)  /* Set numeric value */
#define IPC_CMD_UI_SET_POSITION (0x54)  /* Set x,y position */
#define IPC_CMD_UI_SET_SIZE     (0x55)  /* Set width, height */
#define IPC_CMD_UI_SET_COLOR    (0x56)  /* Set primary color */
#define IPC_CMD_UI_SET_VISIBLE  (0x57)  /* Show/hide widget */
#define IPC_CMD_UI_POLL_EVENTS  (0x58)  /* Poll pending events (bidirectional) */
#define IPC_CMD_UI_CLEAR_ALL    (0x59)  /* Delete all widgets */
#define IPC_CMD_UI_SET_DOTMATRIX (0x5A) /* Set dot matrix bitmap data */
#define IPC_CMD_UI_GET_VALUE    (0x5B)  /* Read current value (bidirectional) */
#define IPC_CMD_UI_LIST         (0x5C)  /* List active widgets (bidirectional) */
#define IPC_CMD_UI_SET_IMAGE    (0x5D)  /* Set image pixel data (chunked RGB565) */
#define IPC_CMD_UI_SET_SCREEN   (0x5E)  /* Set screen dimensions + reset layout */
#define IPC_CMD_UI_IDE_STATUS   (0x5F)  /* Show/hide IDE connection status */
#define IPC_CMD_UI_CHART_ADD_SERIES (0x60) /* Add series to chart (bidirectional) */
#define IPC_CMD_UI_CHART_SET_NEXT   (0x61) /* Set next value for chart series */
#define IPC_CMD_UI_DEPLOY_SCREEN   (0x62) /* Show native deploy overlay (fire-and-forget) */
#define IPC_CMD_UI_RGB_MATRIX      (0x63) /* QWA309 DFR0522 RGB matrix op (fire-and-forget) */
#define IPC_CMD_UI_SFX             (0x64) /* Play SFX / tone via bento_sfx (fire-and-forget) */
#define IPC_CMD_UI_SPRITE_NEW      (0x65) /* Create image sprite by registry id (bidirectional, returns handle) */
#define IPC_CMD_UI_SPRITE_FRAME    (0x66) /* Swap sprite image to another registry id (fire-and-forget) */

#define IPC_CMD_UI_FIRST        (0x50)
#define IPC_CMD_UI_MOTOR           (0x67) /* Motor Shield v2 command (fire-and-forget) */
#define IPC_CMD_UI_ITEM_ADD        (0x68) /* Append one item to a collection widget
                                           * (fire-and-forget for data widgets;
                                           * bidirectional -> child handle for
                                           * Tabview / Tileview / Win)          */
#define IPC_CMD_UI_ITEM_CLEAR      (0x69) /* Empty a collection widget (fire-and-forget) */
#define IPC_CMD_UI_SET_PROP        (0x6A) /* Per-widget knob: [handle][prop][int32] */
#define IPC_CMD_UI_GET_DIAG        (0x6F) /* Read CM55 UI/display diagnostics
                                           * (top slot of the former 0x6C-0x6F
                                           * ext headroom — a proper switch case,
                                           * NOT ext-dispatched, because it needs
                                           * the bidirectional response path the
                                           * ext hook does not carry; 0x6C-0x6E
                                           * remain free for ext consumers).
                                           * (bidirectional). Response payload is
                                           * u32[]: [0..4] ipc_ui drop counters
                                           * (shed, full_low, oldest, purge_high,
                                           * full_high_fail), [5] queue high-water,
                                           * [6] fast_mode active 0/1, then the
                                           * platform block appended by the weak
                                           * ipc_ui_platform_diag() hook — on
                                           * BentoClaw boards that is
                                           * tesaiot_display_diag_t (9 x u32) plus
                                           * gfx idle percent. Added for Bento
                                           * Engine V&V (2026-08-20). */
#define IPC_CMD_UI_GET_TEXT        (0x6B) /* Read a widget's text back (bidirectional)
                                           * request  = [handle]
                                           * response = NUL-terminated UTF-8,
                                           *            data_len = strlen + 1.
                                           * The counterpart of SET_TEXT, and the
                                           * only way anything a user TYPES can
                                           * reach MicroPython: GET_VALUE answers
                                           * 0 for a Textarea because a textarea
                                           * has no numeric value. */
/* 0x6C..0x6F are reserved headroom for the next per-widget opcodes. The band
 * ends at 0x70 and the next allocated band starts at 0x80
 * (IPC_CMD_GPIO_LED_STATE, ipc_communication.h) — asserted in ipc_ui.c. */
#define IPC_CMD_UI_LAST         (0x70)   /* one past the UI band (ISR admits < LAST) */

/*
 * IPC_UI_OPMAP_VERSION is deliberately NOT bumped for 0x67, nor for 0x68-0x6B.
 *
 * The version guards the MEANING of opcodes, not how many exist -- it is what
 * catches the game tree's map v1, where 0x63 is SFX rather than RGB_MATRIX.
 * Appending an opcode above the existing set remaps nothing, so bumping would
 * force five unrelated _Static_assert sites to be touched for no behavioural
 * change. Bump it only when an existing opcode's meaning moves.
 */

/* Opcode-map version — the anti-copy-paste guard between library trees.
 * This tree (Claw) implements the CANONICAL map, version 2:
 *   0x63 RGB_MATRIX · 0x64 SFX · 0x65 SPRITE_NEW · 0x66 SPRITE_FRAME
 *   IPC_CMD_UI_LAST is EXCLUSIVE (one past the highest; ISR admits < LAST).
 * BENTO-TESAIoT-Game-libraries still ships map v1 (0x63 SFX, 0x64/0x65 sprites,
 * LAST INCLUSIVE — equal to the highest opcode). NEVER rsync this header or any
 * opcode-consuming file between the trees: every consumer that hard-codes
 * opcode behavior carries _Static_assert(IPC_UI_OPMAP_VERSION == 2) so a file
 * copied across trees fails at compile, not at runtime with sound driving the
 * RGB matrix. */
#define IPC_UI_OPMAP_VERSION    (2)

/*******************************************************************************
 * RGB matrix (DFR0522) sub-op payload — packed into ipc_msg_t.data[]
 * for IPC_CMD_UI_RGB_MATRIX. The CM55 GFX task owns the display I2C bus the
 * panel lives on, so the transaction runs there. See proj_cm55 dfr0522_rgb.
 *******************************************************************************/
#define IPC_RGB_OP_CLEAR   (0U)
#define IPC_RGB_OP_FILL    (1U)
#define IPC_RGB_OP_PIXEL   (2U)
#define IPC_RGB_OP_BLIT    (3U)  /* full 128-px frame, one message; CM55 shadow-diffs */
#define IPC_RGB_OP_SCORE   (4U)  /* u16 rendered C-side as 3x5 digits (right-aligned) */
#define IPC_RGB_OP_BAR     (5U)  /* bottom-row progress bar: value/max */
#define IPC_RGB_OP_SCROLL  (6U)  /* C-side marquee on a CM55 timer (survives a busy
                                  * Python loop, same philosophy as the SFX mixer) */

typedef struct __attribute__((packed)) {
    uint8_t op;      /* IPC_RGB_OP_* (CLEAR/FILL/PIXEL) */
    uint8_t color;   /* 0..7 */
    uint8_t x;       /* 0..15 (pixel op) */
    uint8_t y;       /* 0..7  (pixel op) */
} ipc_ui_rgb_matrix_t;

/* BLIT frame packing: 4 bits per pixel (color 0..7), row-major.
 * byte = frame[y*8 + (x>>1)]; even x = LOW nibble, odd x = HIGH nibble.
 * 16x8 px * 4 bpp = 64 bytes -> whole frame in ONE IPC message (68 B payload)
 * instead of 128 pixel messages. bentogame packs the same layout in Python. */
#define IPC_RGB_BLIT_FRAME_LEN (64U)

typedef struct __attribute__((packed)) {
    uint8_t op;                              /* IPC_RGB_OP_BLIT */
    uint8_t reserved[3];                     /* keep 4-byte header parity */
    uint8_t frame[IPC_RGB_BLIT_FRAME_LEN];   /* 4bpp packed, see above */
} ipc_ui_rgb_blit_t;

typedef struct __attribute__((packed)) {
    uint8_t  op;      /* IPC_RGB_OP_SCORE */
    uint8_t  color;   /* 1..7 */
    uint16_t value;   /* 0..9999, little-endian; 3x5 digits, right-aligned */
} ipc_ui_rgb_score_t;

typedef struct __attribute__((packed)) {
    uint8_t op;       /* IPC_RGB_OP_BAR */
    uint8_t color;    /* 1..7 */
    uint8_t value;    /* 0..max */
    uint8_t max;      /* >0; lit pixels = value*16/max on the bottom row */
} ipc_ui_rgb_bar_t;

#define IPC_RGB_SCROLL_TEXT_MAX (60U)

typedef struct __attribute__((packed)) {
    uint8_t op;        /* IPC_RGB_OP_SCROLL */
    uint8_t color;     /* 1..7 */
    uint8_t period_cs; /* marquee step period in 10 ms units (8 = 80 ms); 0 stops */
    uint8_t len;       /* text length (0 stops the marquee) */
    char    text[IPC_RGB_SCROLL_TEXT_MAX]; /* ASCII, len governs (no NUL on wire) */
} ipc_ui_rgb_scroll_t;

/*******************************************************************************
 * Sound — payload for IPC_CMD_UI_SFX (item->data). Lets MicroPython (CM33)
 * drive the C sound engine (bento_sfx, CM55) over IPC:
 *   [0]=op
 *   op BENTO_SFX_OP_EVENT: [1]=sfx id                       -> bento_sfx_event(id)
 *   op BENTO_SFX_OP_TONE : [1]=note [2]=wave [3]=velocity
 *                          [4..5]=duration ms (little-endian) -> bento_sfx_play_tone()
 *
 * The BENTO_SFX_* / BENTO_SFX_WAVE_* ids MUST stay in sync with sfx_id_t /
 * sfx_wave_t in bento_sfx.h. Handled on CM55 by the strong ipc_ui_ext_dispatch()
 * override in modules/audio_player (compiled only when BSP_HAS_AUDIO_CODEC=1);
 * the shared ipc_ui.c weak default is a no-op on non-audio boards.
 * Exposed to MicroPython as ui.SFX_* / ui.WAVE_* integer constants.
 *******************************************************************************/
#define BENTO_SFX_OP_EVENT   (0)
#define BENTO_SFX_OP_TONE    (1)

#define BENTO_SFX_UI_MOVE         (0)
#define BENTO_SFX_UI_SELECT       (1)
#define BENTO_SFX_UI_BACK         (2)
#define BENTO_SFX_UI_DENY         (3)
#define BENTO_SFX_UI_START        (4)
#define BENTO_SFX_SNAKE_EAT       (5)
#define BENTO_SFX_SNAKE_TURN      (6)
#define BENTO_SFX_SNAKE_DIE       (7)
#define BENTO_SFX_FLAPPY_FLAP     (8)
#define BENTO_SFX_FLAPPY_SCORE    (9)
#define BENTO_SFX_FLAPPY_DIE      (10)
#define BENTO_SFX_PONG_WALL       (11)
#define BENTO_SFX_PONG_PADDLE     (12)
#define BENTO_SFX_PONG_SCORE      (13)
#define BENTO_SFX_PONG_WIN        (14)
#define BENTO_SFX_PONG_LOSE       (15)
#define BENTO_SFX_SHOOT_FIRE      (16)
#define BENTO_SFX_SHOOT_HIT       (17)
#define BENTO_SFX_SHOOT_EXPLODE   (18)
#define BENTO_SFX_SHOOT_LOSE_LIFE (19)
#define BENTO_SFX_GAME_OVER       (20)

#define BENTO_SFX_WAVE_SINE       (0)
#define BENTO_SFX_WAVE_SQUARE     (1)
#define BENTO_SFX_WAVE_TRIANGLE   (2)
#define BENTO_SFX_WAVE_SAW        (3)

/*******************************************************************************
 * Sprite registry ids — payload for IPC_CMD_UI_SPRITE_NEW / _FRAME.
 * The integer is sent over IPC; CM55 resolves it to a const lv_image_dsc_t*
 * via game_sprite_lookup() (defined PER-PROJECT — the pixel descriptors are
 * project-local symbols, never named on CM33). MicroPython only names the
 * sprite by id. Order IS the contract: the per-project registry array is
 * indexed by this enum, so keep them in lock-step (a typo = link error there),
 * and NEVER renumber — the ids are course-visible via bentogame's name→id maps
 * and are numerically identical in BENTO-TESAIoT-Game-libraries.
 * Exposed to MicroPython as ui.SPR_* integer constants.
 *
 * SPRITE_NEW payload = ipc_ui_sprite_new_t (bidirectional -> [handle]).
 * SPRITE_FRAME payload = [handle][sprite_id] (2 bytes, fire-and-forget).
 *******************************************************************************/
#define BENTO_SPR_SNAKE_HEAD_R    (0)
#define BENTO_SPR_SNAKE_HEAD_D    (1)
#define BENTO_SPR_SNAKE_HEAD_L    (2)
#define BENTO_SPR_SNAKE_HEAD_U    (3)
#define BENTO_SPR_SNAKE_BODY      (4)
#define BENTO_SPR_SNAKE_FOOD      (5)
#define BENTO_SPR_FLAPPY_BIRD     (6)
#define BENTO_SPR_FLAPPY_PIPE_CAP (7)
#define BENTO_SPR_PONG_BALL       (8)
#define BENTO_SPR_PONG_PADDLE     (9)
#define BENTO_SPR_SHOOTER_SHIP    (10)
#define BENTO_SPR_SHOOTER_ENEMY   (11)
#define BENTO_SPR_SHOOTER_ENEMY2  (12)
#define BENTO_SPR_SHOOTER_ENEMY3  (13)
#define BENTO_SPR_SHOOTER_BOOM_P1 (14)
#define BENTO_SPR_SHOOTER_BOOM_P2 (15)
#define BENTO_SPR_SHOOTER_BOOM_C1 (16)
#define BENTO_SPR_SHOOTER_BOOM_C2 (17)
#define BENTO_SPR_SHOOTER_BOOM_F1 (18)
#define BENTO_SPR_SHOOTER_BOOM_F2 (19)
#define BENTO_SPR_COUNT           (20)

/*
 * Motor Shield v2 — payload for IPC_CMD_UI_MOTOR (item->data).
 *
 * The shield hangs off the header I2C (SCB5), which CM55 owns, while
 * MicroPython runs on CM33_NS. This is how `shield.motor_v2` reaches it.
 *
 * NOTE these are deliberately absent from ipc_ui_is_droppable(): a dropped
 * SET_POSITION is invisible, a dropped motor stop is a machine that keeps
 * running.
 */
#define IPC_UI_MOTOR_OP_STOP_ALL      (0x00)
#define IPC_UI_MOTOR_OP_DC_SPEED      (0x01)  /* index=1..4, arg=speed 0..255  */
#define IPC_UI_MOTOR_OP_DC_RUN        (0x02)  /* index=1..4, arg=run enum      */
#define IPC_UI_MOTOR_OP_ST_ATTACH     (0x03)  /* index=1..2, steps=revsteps    */
#define IPC_UI_MOTOR_OP_ST_DETACH     (0x04)  /* index=1..2                    */
#define IPC_UI_MOTOR_OP_ST_RPM        (0x05)  /* index=1..2, arg=style, steps=rpm */
#define IPC_UI_MOTOR_OP_ST_START      (0x06)  /* index=1..2, arg=style, dir, steps */
#define IPC_UI_MOTOR_OP_ST_STOP       (0x07)  /* index=1..2                    */
#define IPC_UI_MOTOR_OP_ST_RELEASE    (0x08)  /* index=1..2                    */

typedef struct __attribute__((packed)) {
    uint8_t  op;      /* IPC_UI_MOTOR_OP_*                                     */
    uint8_t  index;   /* DC terminal 1..4, or stepper port 1..2                */
    uint8_t  arg;     /* speed, run command or stepping style, per op          */
    uint8_t  dir;     /* run enum: forward / backward                          */
    uint32_t steps;   /* step count, rpm or steps-per-rev, per op              */
} ipc_ui_motor_t;

/* IPC_CMD_UI_SPRITE_NEW (0x65) payload — fits the 128-byte pipe trivially. */
typedef struct __attribute__((packed)) {
    uint8_t  sprite_id;   /* BENTO_SPR_* */
    int16_t  x;
    int16_t  y;
} ipc_ui_sprite_new_t;    /* bidirectional response -> data[0] = handle */

/*******************************************************************************
 * Widget Type Enum
 *******************************************************************************/
typedef enum {
    UI_WIDGET_BUTTON    = 1,
    UI_WIDGET_LABEL     = 2,
    UI_WIDGET_SLIDER    = 3,
    UI_WIDGET_SWITCH    = 4,
    UI_WIDGET_CHECKBOX  = 5,
    UI_WIDGET_ARC       = 6,
    UI_WIDGET_BAR       = 7,   /* Progress bar */
    UI_WIDGET_SPINNER   = 8,
    UI_WIDGET_DROPDOWN  = 9,
    UI_WIDGET_TEXTAREA  = 10,
    UI_WIDGET_SEG7      = 11,  /* 7-segment display (styled label) */
    UI_WIDGET_DOTMATRIX = 12,  /* Dot matrix (lv_canvas) */
    UI_WIDGET_CHART     = 13,
    UI_WIDGET_IMAGE     = 14,  /* Image (lv_canvas: built-in icon or dynamic RGB565) */
    UI_WIDGET_PANEL     = 15,  /* Dark rect container (dashboard card) */
    UI_WIDGET_COMPASS   = 16,  /* Heading compass (circle + needle + NESW) */
    UI_WIDGET_SPRITE    = 17,  /* Image sprite from a compiled C descriptor */
    UI_WIDGET_LED       = 18,  /* Annunciator lamp (lv_led) */
    UI_WIDGET_SCALE     = 19,  /* Ruler with ticks and numbered labels (lv_scale) */
    UI_WIDGET_SPINBOX   = 20,  /* Digit-stepped numeric entry (lv_spinbox) */

    /* Collection widgets — content arrives through IPC_CMD_UI_ITEM_ADD, one
     * item per message, because none of it fits the fixed CREATE payload. */
    UI_WIDGET_TABLE     = 21,  /* lv_table       — ITEM_ADD: a=row, b=col, text=cell */
    UI_WIDGET_LIST      = 22,  /* lv_list        — ITEM_ADD: a=icon id, text=label  */
    UI_WIDGET_ROLLER    = 23,  /* lv_roller      — ITEM_ADD appends an option       */
    UI_WIDGET_BTNMATRIX = 24,  /* lv_buttonmatrix— ITEM_ADD: b=ctrl, flags bit0=row */
    UI_WIDGET_MSGBOX    = 25,  /* lv_msgbox      — ITEM_ADD appends a footer button */
    UI_WIDGET_KEYBOARD  = 26,  /* lv_keyboard    — bound to a Textarea via SET_PROP */

    /* Container widgets — ITEM_ADD is BIDIRECTIONAL here and answers with the
     * handle of the child page, so other widgets can be created inside it by
     * passing that handle in ipc_ui_create_t.parent_plus1. */
    UI_WIDGET_TABVIEW   = 27,  /* lv_tabview     — ITEM_ADD adds a tab, returns page */
    UI_WIDGET_TILEVIEW  = 28,  /* lv_tileview    — ITEM_ADD: a=col, b=row, flags=dir */
    UI_WIDGET_WIN       = 29,  /* lv_win         — ITEM_ADD returns the content body */

    UI_WIDGET_LINE      = 30,  /* lv_line        — ITEM_ADD: text = "x y" (see below) */
    UI_WIDGET_IMG       = 31,  /* lv_image, the real one: rotation / scale / pivot.
                                * UI_WIDGET_IMAGE (14) stays an lv_canvas and is
                                * NOT this — ui.Image() keeps its old behaviour. */
    UI_WIDGET_CONTAINER = 32,  /* A child page handed back by a container's ITEM_ADD
                                * (tab page, tile, window body, menu section).
                                * Never created directly by CREATE. */

    UI_WIDGET_MENU      = 33,  /* lv_menu — page-stack navigator with its own
                                * back button and history. ITEM_ADD makes a page
                                * and returns it (see UI_WIDGET_MENU_PAGE).    */
    UI_WIDGET_MENU_PAGE = 34,  /* A page of an lv_menu. ITEM_ADD on it builds
                                * the rows: a=0 cont (a row, with `text` as its
                                * label), a=1 section, a=2 separator; each is
                                * returned as a handle. Never created by CREATE. */
    UI_WIDGET_SPANGROUP = 35,  /* lv_spangroup — one flowing paragraph of
                                * differently styled runs. A span is NOT an
                                * lv_obj_t and gets no handle: runs arrive
                                * through ITEM_ADD (a = font px, flags =
                                * UI_SPAN_*), coloured by the pen prop.        */
    UI_WIDGET_CALENDAR  = 36,  /* lv_calendar — month grid over a buttonmatrix.
                                * min=year, max=month, value=day.              */
} ui_widget_type_t;

/*
 * Adding a type here obliges BOTH implementations of ui_widget_mgr_create():
 * claw's and game's. The two Infineon Game kits deliberately compile claw's
 * ipc_ui.c against game's ui_widget_mgr.c (see their proj_cm55/Makefile), which
 * links only because the two expose an identical function set. A type handled
 * in one tree and not the other does not fail at build — it renders on some
 * boards and returns UI_STATUS_INVALID_TYPE on others, which is far harder to
 * find than a linker error.
 *
 * The same obligation covers the ITEM_ADD / ITEM_CLEAR / SET_PROP handlers:
 * ui_widget_mgr_item_add(), _item_clear() and _set_prop() must exist, with the
 * same behaviour for the same type, in both files.
 */

/*******************************************************************************
 * IPC Payload: UI_ITEM_ADD (0x68) / UI_ITEM_CLEAR (0x69)
 *
 * One item per message. `a`, `b` and `flags` mean whatever the target widget
 * needs them to mean; the table is in the enum above. `text` carries the label,
 * cell value or option — 124 bytes so the struct is exactly the 128-byte pipe
 * payload with nothing wasted and nothing packed two-to-a-field.
 *
 * Fire-and-forget for the data widgets: msg.value stays 0 and write_response()
 * no-ops on a zero pointer. The three container widgets send the SAME opcode
 * with a response pointer, and get the child's handle back in data[0].
 *
 * LINE is the one widget whose item does not fit a byte: a point is two screen
 * coordinates, and this panel is 800 px wide. Its point therefore travels in
 * `text` as decimal "x y" (e.g. "120 40"). That is a text payload, not a
 * bit-packed integer — the thing we are not doing here.
 *******************************************************************************/
#define UI_ITEM_TEXT_MAX        (124)

/* flags bit 0 — start a new row (Buttonmatrix); for Tileview `flags` is the
 * whole lv_dir_t of allowed swipe directions instead. */
#define UI_ITEM_FLAG_NEW_ROW    (0x01u)

typedef struct __attribute__((packed)) {
    uint8_t  handle;                  /* collection / container widget      */
    uint8_t  a;                       /* row, col, icon id — per widget     */
    uint8_t  b;                       /* col, ctrl bits    — per widget     */
    uint8_t  flags;                   /* UI_ITEM_FLAG_*, or lv_dir_t (tile) */
    char     text[UI_ITEM_TEXT_MAX];  /* null-terminated                    */
} ipc_ui_item_add_t;                  /* 128 bytes = IPC_DATA_MAX_LEN       */

/* Response byte for a container ITEM_ADD when no child was produced (a data
 * widget answered the same opcode). 0xFF is not a legal handle. */
#define UI_ITEM_NO_CHILD        (0xFFu)

/* ITEM_ADD `a` values for UI_WIDGET_LIST — an integer id, never a symbol
 * string: LV_SYMBOL_* is a CM55 concept and CM33 must not name it.
 * Exposed to MicroPython as ui.ICON_*. */
#define UI_LIST_ICON_NONE       (0)
#define UI_LIST_ICON_FILE       (1)
#define UI_LIST_ICON_DIR        (2)
#define UI_LIST_ICON_SETTINGS   (3)
#define UI_LIST_ICON_WIFI       (4)
#define UI_LIST_ICON_BATTERY    (5)
#define UI_LIST_ICON_BELL       (6)
#define UI_LIST_ICON_HOME       (7)
#define UI_LIST_ICON_OK         (8)
#define UI_LIST_ICON_CLOSE      (9)
#define UI_LIST_ICON_PLAY       (10)
#define UI_LIST_ICON_PAUSE      (11)
#define UI_LIST_ICON_EDIT       (12)
#define UI_LIST_ICON_TRASH      (13)
#define UI_LIST_ICON_POWER      (14)
#define UI_LIST_ICON_USB        (15)
#define UI_LIST_ICON_BLUETOOTH  (16)
#define UI_LIST_ICON_AUDIO      (17)
#define UI_LIST_ICON_IMAGE      (18)
#define UI_LIST_ICON_REFRESH    (19)
#define UI_LIST_ICON_WARNING    (20)

/*******************************************************************************
 * IPC Payload: UI_SET_PROP (0x6A)
 *
 * The general escape hatch for a per-widget knob the fixed CREATE struct has no
 * field for. Every future knob lands here rather than being packed into an
 * existing integer: that was tried for Scale and Spinbox, the packing was
 * miscalculated on its first test, and it was reverted the same afternoon.
 *
 * Two props below do carry a pair in one int32 (hi16/lo16). Both are a SINGLE
 * logical setting whose halves are meaningless apart — a spinbox's digit count
 * and decimal position are one format, a column and its width are one column.
 * That is the whole licence; a prop that would carry two unrelated numbers gets
 * two prop ids instead.
 *******************************************************************************/
#define UI_PROP_SCALE_TOTAL_TICKS   (1)  /* lv_scale_set_total_tick_count      */
#define UI_PROP_SCALE_MAJOR_EVERY   (2)  /* lv_scale_set_major_tick_every      */
#define UI_PROP_SCALE_MODE          (3)  /* LV_SCALE_MODE_* (ui.SCALE_*)       */
#define UI_PROP_SPINBOX_DIGITS      (4)  /* hi16 = digit count,
                                          * lo16 = digits BEFORE the point
                                          *        (LVGL's separator position):
                                          *        (4,2) shows 2375 as 23.75   */
#define UI_PROP_LED_BRIGHTNESS      (5)  /* 0..255 (lv_led_set_brightness)     */
#define UI_PROP_TEXTAREA_ONE_LINE   (6)  /* 0/1                                */
#define UI_PROP_TEXTAREA_PASSWORD   (7)  /* 0/1                                */
#define UI_PROP_ROLLER_VISIBLE_ROWS (8)  /* visible option rows                */
#define UI_PROP_TABLE_COL_WIDTH     (9)  /* hi16 = column, lo16 = width px     */
#define UI_PROP_KEYBOARD_TEXTAREA  (10)  /* value = the Textarea's HANDLE      */
#define UI_PROP_IMAGE_ROTATION     (11)  /* 0.1 deg units, 0..3600 (Img only)  */
#define UI_PROP_IMAGE_SCALE        (12)  /* 256 = 100 % (Img only)             */
#define UI_PROP_IMAGE_PIVOT        (13)  /* hi16 = x, lo16 = y (Img only)      */
#define UI_PROP_LINE_WIDTH         (14)  /* line thickness in px (Line only)   */
#define UI_PROP_TABVIEW_ACTIVE     (15)  /* select tab by index                */
#define UI_PROP_MSGBOX_CLOSE_BTN   (16)  /* non-zero adds the header close X   */

/*
 * Menu. UI_PROP_MENU_LOAD_PAGE is the handle-to-handle link — "tapping this
 * row opens that page" — and is the reason it is a prop rather than a new
 * opcode.
 *
 * lv_menu_set_load_page_event(menu, obj, page) needs THREE objects, and a
 * SET_PROP carries two (a handle and a value). The third is the menu, and a
 * dedicated opcode was the alternative. It is not needed: the menu is not
 * inferred at link time by walking the widget tree — which would be guesswork
 * about an unattached page — but recorded on CM55 when ITEM_ADD created the
 * page, the one moment its owner is known for certain. So the link is exactly
 * two handles on the wire (the row, and the page as the value), no opcode is
 * spent, and nothing is packed into a shared field.
 */
#define UI_PROP_MENU_LOAD_PAGE     (17)  /* on a ROW: value = page handle      */
#define UI_PROP_MENU_MAIN_PAGE     (18)  /* on the MENU: value = page handle   */
#define UI_PROP_MENU_SIDEBAR_PAGE  (19)  /* on the MENU: value = page handle   */
#define UI_PROP_MENU_ROOT_BACK     (20)  /* on the MENU: 0/1 back btn on root  */

#define UI_PROP_SPAN_COLOR         (21)  /* pen: colour of the spans added NEXT */
#define UI_PROP_SPAN_MODE          (22)  /* LV_SPAN_MODE_* (0 fixed,1 expand,2 break) */

#define UI_PROP_CALENDAR_SHOWN     (23)  /* value = YYYYMM, the month on screen */
#define UI_PROP_CALENDAR_HEADER    (24)  /* 2 = also add the year/month dropdown
                                          * header; the arrow header is always
                                          * created (a calendar you cannot page
                                          * is not much use).                   */

/*
 * UI_PROP_EVENT_MASK — which of the input events below this widget reports.
 *
 * value = OR of UI_EVENT_MASK(UI_EVENT_*). Default is 0 on every widget, and
 * that default is the whole reason this is a subscription rather than a
 * firehose. The three original events (CLICKED / VALUE_CHANGED / TOGGLED) are
 * NOT in this mask: they are wired per widget type at CREATE and keep firing
 * exactly as they always have, so no existing program changes behaviour.
 *
 * Delivering the new events unconditionally was tried on paper first and
 * rejected. Two course examples that already ship read only `handle` and never
 * look at `type` — examples/s05/08_six_filters_one_signal.py and
 * examples/s05/09_sensors_api_tour.py — so a button that suddenly answered
 * PRESSED + CLICKED + RELEASED would have stepped their state machines three
 * times per tap. An opt-in cannot do that to a program written before it
 * existed, and it also keeps the 16-entry ring (below) spent only on events
 * somebody asked for.
 */
#define UI_PROP_EVENT_MASK         (25)  /* value = OR of UI_EVENT_MASK(...)    */
#define UI_PROP_CHART_POINTS       (26)
#define UI_PROP_SCALE_NEEDLE_COLOR (27)  /* Scale: needle line color (0xRRGGBB).
                                          * Creates the needle lazily. */
#define UI_PROP_SCALE_NEEDLE       (28)  /* Scale: hi16 = needle length px
                                          * (0 = auto), lo16 = value (int16).
                                          * Uses lv_scale_set_line_needle_value
                                          * so the needle ROTATES in place —
                                          * the delete/recreate ui.Line pattern
                                          * repainted the whole gauge area and
                                          * flickered on the bench (2026-08-20). */  /* Chart: point count per series,
                                          * clamped 10..400 on CM55. Added for
                                          * the Part-3 oscilloscope ports
                                          * (Bento Engine R4, 2026-08-20): the
                                          * C course ran 200-point scopes and
                                          * the old fixed 50 was the limit. */

#define UI_PROP_DISABLED           (29)  /* value != 0 puts the widget into
                                          * LV_STATE_DISABLED; 0 clears it.
                                          *
                                          * This is NOT only a tint. LVGL 9.5
                                          * tests the same state before routing
                                          * a press, so a disabled widget also
                                          * emits NO events — lv_indev.c lines
                                          * 828, 983, 1339, 1391 and 1510 all
                                          * gate on
                                          *   !lv_obj_has_state(obj, LV_STATE_DISABLED)
                                          * Nothing reaches ui.poll() from it,
                                          * which is the whole point: a control
                                          * that looks dead must BE dead. */

/* ITEM_ADD `a` on a UI_WIDGET_MENU_PAGE (or on a section handed back by one) */
#define UI_MENU_ITEM_ROW       (0)   /* lv_menu_cont + a label from `text`     */
#define UI_MENU_ITEM_SECTION   (1)   /* lv_menu_section — a grouped block      */
#define UI_MENU_ITEM_SEPARATOR (2)   /* lv_menu_separator                      */

/* ITEM_ADD `flags` on a UI_WIDGET_SPANGROUP. `a` is the font size in px
 * (0 = inherit the group's font, which is what Thai text wants). */
#define UI_SPAN_UNDERLINE      (0x01u)
#define UI_SPAN_STRIKETHROUGH  (0x02u)

typedef struct __attribute__((packed)) {
    uint8_t  handle;
    uint8_t  prop_id;   /* UI_PROP_* */
    int32_t  value;
} ipc_ui_set_prop_t;

/* Halves of the hi16/lo16 props, so callers and CM55 agree in one place. */
#define UI_PROP_HI16(v)  ((int32_t)(((uint32_t)(v) >> 16) & 0xFFFFu))
#define UI_PROP_LO16(v)  ((int32_t)((uint32_t)(v) & 0xFFFFu))

/*******************************************************************************
 * Event Type Enum
 *
 * APPEND ONLY. 1, 2 and 3 are named in every example the course has ever
 * shipped; renumbering them would rewrite the meaning of programs already on
 * students' boards.
 *
 * 1-3 are wired per widget type at CREATE and always fire.
 * 4-15 are the input events, and a widget reports them only after
 * UI_PROP_EVENT_MASK asks it to (ui.Widget.listen() in MicroPython).
 *
 * WHAT IS *NOT* HERE, AND WHY. LVGL 9.5 defines about seventy LV_EVENT_* codes
 * (lv_event.h). Most describe the renderer talking to itself and cannot be
 * carried by an 8-byte event that has one int32 in it:
 *
 *   PRESSING              fires every frame a finger is down — one held button
 *                         would fill the 16-entry ring in a quarter of a second
 *                         and starve the RELEASED that ends the action.
 *                         LONG_PRESSED_REPEAT covers "keep going while held"
 *                         at a rate LVGL bounds for us.
 *   SCROLL, SCROLL_THROW_BEGIN
 *                         same problem, per scrolled pixel. SCROLL_BEGIN and
 *                         SCROLL_END are the two edges worth knowing about.
 *   SHORT_CLICKED         CLICKED already fires on every release that was not
 *                         a scroll, long press included. Two names, one gesture.
 *   SINGLE/DOUBLE/TRIPLE_CLICKED
 *                         LVGL 9.5 only. The two Infineon Game consoles are
 *                         pinned to 9.2 (their proj_cm55/deps/lvgl.mtb), and a
 *                         double tap is not an HMI idiom this course teaches:
 *                         NUREG-0700 wants one deliberate action, confirmed.
 *   STATE_CHANGED         9.5 only, and it fires for pressed / focused /
 *                         checked alike — a duplicate of events already here.
 *   KEY, ROTARY           there is no keypad or encoder input device on this
 *                         board; the only indev is the touch panel.
 *   LEAVE                 means "defocused but still selected", which only has
 *                         a meaning inside an lv_group. Nothing creates one
 *                         here, so it can never fire.
 *   HOVER_OVER, HOVER_LEAVE, INDEV_RESET
 *                         hover needs a pointer that exists while not touching.
 *   HIT_TEST, COVER_CHECK, REFR_EXT_DRAW_SIZE, GET_SELF_SIZE, DRAW_*
 *                         the handler is expected to WRITE an answer back into
 *                         the event parameter. A one-way ring cannot answer.
 *   INSERT                carries the char* being typed and exists so a handler
 *                         can veto or rewrite it. Both halves are impossible
 *                         across a core boundary.
 *   REFRESH               an instruction sent TO a widget, not news from one.
 *   CREATE                fires before the widget has a handle to report under.
 *   DELETE                already spoken for: it is how the handle table learns
 *                         an object died (widget_deleted_cb).
 *   CHILD_CHANGED / CHILD_CREATED / CHILD_DELETED / SIZE_CHANGED /
 *   STYLE_CHANGED / LAYOUT_CHANGED / SCREEN_*
 *                         structure and layout churn, most of it raised by our
 *                         own construction code while a script is still drawing.
 *   INVALIDATE_AREA .. VSYNC, TRANSLATION_LANGUAGE_CHANGED
 *                         display-driver traffic. Sent to the display, not to
 *                         a widget, so a widget handle would be a fiction.
 *******************************************************************************/
#define UI_EVENT_CLICKED        (1)
#define UI_EVENT_VALUE_CHANGED  (2)
#define UI_EVENT_TOGGLED        (3)

/* Input events — opt in per widget with UI_PROP_EVENT_MASK. */
#define UI_EVENT_PRESSED              (4)  /* finger down on the widget        */
#define UI_EVENT_RELEASED             (5)  /* finger up, wherever it ended     */
#define UI_EVENT_PRESS_LOST           (6)  /* still down, but slid off         */
#define UI_EVENT_LONG_PRESSED         (7)  /* held past long_press_time        */
#define UI_EVENT_LONG_PRESSED_REPEAT  (8)  /* still held; value = repeat count */
#define UI_EVENT_READY                (9)  /* a process finished — keyboard OK,
                                            * Enter in a one-line Textarea     */
#define UI_EVENT_CANCEL              (10)  /* a process was abandoned          */
#define UI_EVENT_FOCUSED             (11)  /* this widget became the one being
                                            * touched (no lv_group needed:
                                            * lv_indev's click-focus sends it) */
#define UI_EVENT_DEFOCUSED           (12)  /* another widget took the focus    */
#define UI_EVENT_SCROLL_BEGIN        (13)  /* content started moving           */
#define UI_EVENT_SCROLL_END          (14)  /* content stopped moving           */
#define UI_EVENT_GESTURE             (15)  /* swipe; value = UI_DIR_* below    */

#define UI_EVENT_FIRST_INPUT    (UI_EVENT_PRESSED)
#define UI_EVENT_LAST_INPUT     (UI_EVENT_GESTURE)

/* One bit per event id, so the mask is readable in a debugger and a new event
 * costs no translation table. Bits 1..3 are unused: those three are not
 * subscribable. */
#define UI_EVENT_MASK(ev)       (1UL << (ev))

/* Everything a widget may be asked for, and nothing else. A mask arriving with
 * bit 1, 2 or 3 set would be a script trying to unsubscribe from CLICKED, and
 * the answer to that is no — those three are the contract every existing
 * example was written against. CM55 ANDs with this before storing. */
#define UI_EVENT_MASK_SUBSCRIBABLE \
    (UI_EVENT_MASK(UI_EVENT_PRESSED)             | \
     UI_EVENT_MASK(UI_EVENT_RELEASED)            | \
     UI_EVENT_MASK(UI_EVENT_PRESS_LOST)          | \
     UI_EVENT_MASK(UI_EVENT_LONG_PRESSED)        | \
     UI_EVENT_MASK(UI_EVENT_LONG_PRESSED_REPEAT) | \
     UI_EVENT_MASK(UI_EVENT_READY)               | \
     UI_EVENT_MASK(UI_EVENT_CANCEL)              | \
     UI_EVENT_MASK(UI_EVENT_FOCUSED)             | \
     UI_EVENT_MASK(UI_EVENT_DEFOCUSED)           | \
     UI_EVENT_MASK(UI_EVENT_SCROLL_BEGIN)        | \
     UI_EVENT_MASK(UI_EVENT_SCROLL_END)          | \
     UI_EVENT_MASK(UI_EVENT_GESTURE))

/* Value carried by UI_EVENT_GESTURE. Numerically lv_dir_t (lv_area.h), which
 * is why they are powers of two — but CM33 must not name an LVGL symbol, so
 * they are restated here and exposed to MicroPython as ui.DIR_*. */
#define UI_DIR_NONE             (0x00)
#define UI_DIR_LEFT             (0x01)
#define UI_DIR_RIGHT            (0x02)
#define UI_DIR_TOP              (0x04)
#define UI_DIR_BOTTOM           (0x08)

/*******************************************************************************
 * Limits
 *******************************************************************************/
/* 64 handles, up from 32. A container widget spends handles on its own pages
 * (a three-tab Tabview is four entries before any content), so the old ceiling
 * was reachable by a screen a learner would call small. The UI_LIST response
 * still fits: 64 * 2 + 1 = 129 bytes of the 240-byte response.
 * NOTE: handles are a uint8_t on the wire; 0xFF is reserved (UI_ITEM_NO_CHILD). */
#define UI_MAX_WIDGETS          (64)
#define UI_EVENT_RING_SIZE      (16)
#define UI_MAX_EVENTS_PER_POLL  (8)
#define UI_CREATE_TEXT_MAX      (96)
#define UI_CHART_MAX_SERIES     (4)

/* Longest text IPC_CMD_UI_GET_TEXT can hand back, NOT counting the NUL.
 * 239 + 1 is the whole of ipc_response_t.data[] (IPC_RESPONSE_DATA_MAX = 240,
 * ipc_communication.h) — asserted in ipc_ui.c, which is the file that can see
 * both headers. A Textarea holds far more than this, so the answer is cut, and
 * cut on a UTF-8 boundary: Thai spends three bytes per character and half a
 * character draws as an empty box. The truncation is silent by design — a
 * status code saying "there was more" would still not fit the rest. */
#define UI_GET_TEXT_MAX         (239)

/*******************************************************************************
 * IPC Payload: UI_CREATE (0x50)
 * Packed into ipc_msg_t.data[] (max 128 bytes)
 * Total: 122 bytes
 *
 * parent_plus1 was APPENDED, never inserted: every field above it keeps the
 * offset it has always had, so a CM33 built before this change and a CM55 built
 * after it still agree about where `text` starts.
 *******************************************************************************/
typedef struct __attribute__((packed)) {
    uint8_t  widget_type;       /* ui_widget_type_t (1-32) */
    int16_t  x;                 /* X position (-1 = auto) */
    int16_t  y;                 /* Y position (-1 = auto) */
    int16_t  w;                 /* Width (-1 = auto) */
    int16_t  h;                 /* Height (-1 = auto) */
    uint32_t color;             /* 0xRRGGBB (0 = theme default) */
    int32_t  min_val;           /* Range minimum / cols (dotmatrix) */
    int32_t  max_val;           /* Range maximum / rows (dotmatrix) */
    int32_t  init_val;          /* Initial value */
    char     text[UI_CREATE_TEXT_MAX]; /* Label text (null-terminated) */
    /* Parent widget handle PLUS ONE, so that 0 — which is what the memset in
     * create_widget_common leaves behind, and what every caller built before
     * this field existed sends — means "the screen container", i.e. exactly
     * today's behaviour. 1..UI_MAX_WIDGETS select handle 0..63 as the parent. */
    uint8_t  parent_plus1;
} ipc_ui_create_t;

/*******************************************************************************
 * IPC Response: UI_POLL_EVENTS (0x58)
 * Packed into ipc_response_t.data[] (max 240 bytes)
 * Up to UI_MAX_EVENTS_PER_POLL events (8 * 8 = 64 bytes)
 *******************************************************************************/
typedef struct __attribute__((packed)) {
    uint8_t  handle_id;         /* Widget that generated event */
    uint8_t  event_type;        /* UI_EVENT_* */
    int32_t  value;             /* Event value */
    uint16_t reserved;
} ipc_ui_event_t;

/*******************************************************************************
 * IPC Response: UI_LIST (0x5C)
 * Packed into ipc_response_t.data[] (max 240 bytes)
 * data[0] = count, then count × ui_widget_info_t entries (2 bytes each)
 * Max 64 widgets × 2 + 1 = 129 bytes (response cap is 240)
 *******************************************************************************/
typedef struct __attribute__((packed)) {
    uint8_t  handle_id;         /* Widget handle (0..UI_MAX_WIDGETS-1) */
    uint8_t  widget_type;       /* ui_widget_type_t */
} ipc_ui_widget_info_t;

/*******************************************************************************
 * IPC Response Status Codes
 *******************************************************************************/
#define UI_STATUS_OK            (0)
#define UI_STATUS_TABLE_FULL    (1)
#define UI_STATUS_INVALID_TYPE  (2)
#define UI_STATUS_INVALID_HANDLE (3)
#define UI_STATUS_ERROR         (4)
#define UI_STATUS_ALLOC_FAILED  (5)

#endif /* IPC_UI_PROTOCOL_H */
