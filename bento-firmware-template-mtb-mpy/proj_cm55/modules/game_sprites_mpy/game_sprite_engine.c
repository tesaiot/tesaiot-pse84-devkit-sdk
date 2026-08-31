/*******************************************************************************
 * File Name: game_sprite_engine.c  (MPY Dev Kit)
 *
 * Description: Strong implementations of the three weak sprite hooks in the
 *              shared claw ipc_ui/ui_widget_mgr, so ui.Sprite and bentogame
 *              render REAL game sprites on the MicroPython Dev Kit firmware
 *              (previously Game-Console-only; requested by the professor
 *              2026-08-20 — full MPY game support incl. bentogame engine).
 *              Sprite pixel assets + registry are rsynced from
 *              TESAIoT_KIT_PSE84_AI-Game-BentoClaw (single source of truth
 *              for the artwork; re-rsync when the game project's art changes).
 ******************************************************************************/

#include "lvgl.h"

lv_obj_t *game_sprite_create(lv_obj_t *parent, const lv_image_dsc_t *dsc)
{
    lv_obj_t *img = lv_image_create(parent);
    lv_image_set_src(img, dsc);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
    return img;
}

void game_sprite_set(lv_obj_t *sprite, const lv_image_dsc_t *dsc)
{
    if (sprite == NULL) {
        return;
    }
    /* Skip the redraw when the source already matches — directional frames
     * only actually change on a turn, not every tick. */
    if (lv_image_get_src(sprite) == dsc) {
        return;
    }
    lv_image_set_src(sprite, dsc);
}
