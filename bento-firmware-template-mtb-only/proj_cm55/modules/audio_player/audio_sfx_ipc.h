/*******************************************************************************
 * audio_sfx_ipc.h — feature handler for IPC_CMD_UI_SFX (ui.sfx / ui.tone).
 *
 * audio_sfx_ext_dispatch() decodes the SFX payload and drives bento_sfx on the
 * TLV320DAC3100 codec. It is chained from the project's single strong
 * ipc_ui_ext_dispatch() override (dfr0522_ipc.c) so several project features can
 * share the one weak extension hook without a link-time collision.
 *
 * Declared __attribute__((weak)) with a no-op fallback so the strong override
 * links even when BSP_HAS_AUDIO_CODEC=0 (audio_sfx_ipc.c is CY_IGNORE'd then).
 *******************************************************************************/

#ifndef AUDIO_SFX_IPC_H
#define AUDIO_SFX_IPC_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns true if cmd == IPC_CMD_UI_SFX and it was handled. */
bool audio_sfx_ext_dispatch(uint32_t cmd, const uint8_t *data);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_SFX_IPC_H */
