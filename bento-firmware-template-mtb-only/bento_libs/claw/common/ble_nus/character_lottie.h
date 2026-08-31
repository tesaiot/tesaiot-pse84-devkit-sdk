/*******************************************************************************
 * File Name: character_lottie.h
 *
 * Description: CM55-side runtime renderer for Bento Desktop Buddy character
 *              packs. Each pack lives under /buddy/<name>/ in LittleFS and
 *              contains a manifest.json plus one Lottie .json per state (per
 *              REFERENCE.md §Folder Push schema — Bento forked this protocol
 *              under its own branding; see Bento_Buddy/SPEC.md §3.1.1 Not
 *              Anthropic-compatible).
 *
 *              On state change the caller invokes character_lottie_set_state;
 *              this module reads the matching .json from flash and pushes it
 *              into an lv_lottie widget via lv_lottie_set_src_data.
 *
 *              Default pack is "tesaiot-bento" (see characters/tesaiot-bento/).
 *              A different pack can be selected with character_lottie_set_pack.
 *
 ******************************************************************************/

#ifndef CHARACTER_LOTTIE_H
#define CHARACTER_LOTTIE_H

#include <stdbool.h>
#include "lvgl.h"
#include "page_bento_buddy.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Attach a Lottie widget as a child of the given parent (typically the Bento
 * Buddy page's content container). Returns the widget, or NULL on allocation
 * failure. Safe to call only from the GFX task. */
lv_obj_t *character_lottie_create(lv_obj_t *parent);

/* Select a pack by folder name. Defaults to "tesaiot-bento" at boot.
 * If the pack directory or manifest does not exist, the previous
 * pack (or a transparent placeholder) keeps rendering. */
void character_lottie_set_pack(const char *pack_name);

/* Swap the currently-rendered animation to match the new buddy state.
 * No-op if the Lottie widget hasn't been created yet. */
void character_lottie_set_state(bento_buddy_state_t state);

/* Teardown — call from page_bento_buddy_destroy. */
void character_lottie_destroy(void);

/* Convenience wrapper for the bento.character.set SPEC verb. Looks up name
 * in the same allow-list as nus_commands.c and calls
 * character_lottie_set_state; returns 0 on success, -1 on unknown name.
 * duration_ms is advisory — the CM55 page_bento_buddy_render loop reverts
 * to the BLE-derived state after the window elapses.
 * TODO: when the real Lottie engine learns per-animation playback lengths,
 * gate the revert on the animation's natural end instead of wall-clock
 * duration. */
int character_lottie_play(const char *name, uint32_t duration_ms);

#ifdef __cplusplus
}
#endif

#endif /* CHARACTER_LOTTIE_H */
