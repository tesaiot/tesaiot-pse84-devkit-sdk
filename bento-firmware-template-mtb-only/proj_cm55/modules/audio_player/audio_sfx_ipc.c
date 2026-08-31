/*******************************************************************************
 * audio_sfx_ipc.c — CM55 handler that services MicroPython ui.sfx()/ui.tone()
 *                   (IPC_CMD_UI_SFX) by driving the C sound engine (bento_sfx)
 *                   on the TLV320DAC3100 codec (U28) out the J8 speaker.
 *
 * This exposes audio_sfx_ext_dispatch() — a *feature handler*, NOT the
 * ipc_ui_ext_dispatch() weak hook itself. The project's single strong
 * ipc_ui_ext_dispatch() override (dfr0522_ipc.c) chains to this handler for any
 * opcode it does not own, so multiple project features can share the one
 * extension hook without colliding at link time.
 *
 * When BSP_HAS_AUDIO_CODEC=0 this whole file is CY_IGNORE'd; dfr0522_ipc.c then
 * links against the weak no-op audio_sfx_ext_dispatch() it declares locally.
 *
 * Runs in GFX-task context (process_ui_command) — the same non-ISR context page
 * / game code uses to call bento_sfx_* — so the trigger is lock-free and safe.
 *
 * The BENTO_SFX_* / BENTO_SFX_WAVE_* wire ids (ipc_ui_protocol.h, sent from
 * CM33) are asserted here to match sfx_id_t / sfx_wave_t (bento_sfx.h) so a
 * future reorder of either list is caught at compile time, not at runtime.
 *******************************************************************************/

#include "bsp_feature_flags.h"

#if BSP_HAS_AUDIO_CODEC

#include "ipc_ui_protocol.h"

/* Anti-copy-paste guard: this file hard-codes canonical opcode-map v2 behavior. */
_Static_assert(IPC_UI_OPMAP_VERSION == 2, "wrong tree's ipc_ui_protocol.h (opcode map mismatch)");
#include "audio_sfx_ipc.h"
#include "bento_sfx.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/* Drift guard: keep the CM33 wire ids in lock-step with the CM55 enums. */
_Static_assert((int)BENTO_SFX_UI_MOVE         == (int)SFX_UI_MOVE,         "sfx id drift: UI_MOVE");
_Static_assert((int)BENTO_SFX_GAME_OVER       == (int)SFX_GAME_OVER,       "sfx id drift: GAME_OVER");
_Static_assert((int)BENTO_SFX_SHOOT_LOSE_LIFE == (int)SFX_SHOOT_LOSE_LIFE, "sfx id drift: SHOOT_LOSE_LIFE");
_Static_assert((int)BENTO_SFX_WAVE_SINE       == (int)SFX_WAVE_SINE,       "sfx wave drift: SINE");
_Static_assert((int)BENTO_SFX_WAVE_SAW        == (int)SFX_WAVE_SAW,        "sfx wave drift: SAW");

bool audio_sfx_ext_dispatch(uint32_t cmd, const uint8_t *data)
{
    if (cmd != IPC_CMD_UI_SFX || data == NULL) {
        return false;
    }

    const uint8_t *d = data;
    if (d[0] == BENTO_SFX_OP_EVENT) {
        bento_sfx_event((sfx_id_t)d[1]);
        return true;
    } else if (d[0] == BENTO_SFX_OP_TONE) {
        uint16_t dur = (uint16_t)d[4] | ((uint16_t)d[5] << 8);
        bento_sfx_play_tone(d[1], (sfx_wave_t)d[2], d[3], dur);
        return true;
    }
    return false;
}

#endif /* BSP_HAS_AUDIO_CODEC */
