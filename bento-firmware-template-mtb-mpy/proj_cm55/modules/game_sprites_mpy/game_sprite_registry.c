/*******************************************************************************
 * File Name: game_sprite_registry.c
 *
 * Description: Per-project sprite registry for spike-b — maps BENTO_SPR_* ids
 *              (sent from MicroPython over IPC) to the compiled lv_image_dsc_t
 *              descriptors of the on-board C games. The descriptors are
 *              project-local symbols (LV_IMAGE_DECLARE), so this table MUST be
 *              compiled per project: a renamed/removed sprite becomes a LINK
 *              error here, not silent drift.
 *
 *******************************************************************************/

#include "lvgl.h"
#include "ipc_ui_protocol.h"
/* prototype declared locally — this copy lives in the MPY Dev Kit
 * (game_sprites_mpy), rsynced from the Game project 2026-08-20 */
const lv_image_dsc_t *game_sprite_lookup(int sprite_id);
#include "game_sprites/snake_sprites.h"
#include "game_sprites/flappy_sprites.h"
#include "game_sprites/pong_sprites.h"
#include "game_sprites/shooter_sprites.h"

static const lv_image_dsc_t *const s_sprite_registry[] = {
    [BENTO_SPR_SNAKE_HEAD_R]    = &snake_head_r,
    [BENTO_SPR_SNAKE_HEAD_D]    = &snake_head_d,
    [BENTO_SPR_SNAKE_HEAD_L]    = &snake_head_l,
    [BENTO_SPR_SNAKE_HEAD_U]    = &snake_head_u,
    [BENTO_SPR_SNAKE_BODY]      = &snake_body,
    [BENTO_SPR_SNAKE_FOOD]      = &snake_food,
    [BENTO_SPR_FLAPPY_BIRD]     = &flappy_bird,
    [BENTO_SPR_FLAPPY_PIPE_CAP] = &flappy_pipe_cap,
    [BENTO_SPR_PONG_BALL]       = &pong_ball,
    [BENTO_SPR_PONG_PADDLE]     = &pong_paddle,
    [BENTO_SPR_SHOOTER_SHIP]    = &shooter_ship,
    [BENTO_SPR_SHOOTER_ENEMY]   = &shooter_enemy,
    [BENTO_SPR_SHOOTER_ENEMY2]  = &shooter_enemy2,
    [BENTO_SPR_SHOOTER_ENEMY3]  = &shooter_enemy3,
    [BENTO_SPR_SHOOTER_BOOM_P1] = &shooter_boom_p1,
    [BENTO_SPR_SHOOTER_BOOM_P2] = &shooter_boom_p2,
    [BENTO_SPR_SHOOTER_BOOM_C1] = &shooter_boom_c1,
    [BENTO_SPR_SHOOTER_BOOM_C2] = &shooter_boom_c2,
    [BENTO_SPR_SHOOTER_BOOM_F1] = &shooter_boom_f1,
    [BENTO_SPR_SHOOTER_BOOM_F2] = &shooter_boom_f2,
};

/* Drift guard: array size must equal the enum count. (Descriptor addresses
 * are not constant-expressions, so the per-symbol guard is the &name reference
 * above, which fails at LINK if a sprite is renamed/removed.) */
_Static_assert(sizeof(s_sprite_registry) / sizeof(s_sprite_registry[0])
               == BENTO_SPR_COUNT, "sprite registry size drift vs BENTO_SPR_COUNT");

const lv_image_dsc_t *game_sprite_lookup(int sprite_id)
{
    if (sprite_id < 0 || sprite_id >= BENTO_SPR_COUNT) {
        return NULL;
    }
    return s_sprite_registry[sprite_id];
}
