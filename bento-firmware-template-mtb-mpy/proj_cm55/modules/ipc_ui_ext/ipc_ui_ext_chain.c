/*******************************************************************************
 * ipc_ui_ext_chain.c — the project's single strong ipc_ui_ext_dispatch() that
 *                      fans a UI-band opcode out to each project feature handler.
 *
 * The shared ipc_ui.c provides a weak no-op ipc_ui_ext_dispatch(); a board
 * project may override it once. Because this AI-Kit BentoClaw project has more
 * than one feature riding the extension hook (DFR0522 RGB matrix + audio SFX),
 * a single strong override here chains to each feature handler in turn. Each
 * handler is declared __attribute__((weak)) with a no-op fallback, so this unit
 * links whether or not the matching feature is compiled in (its .c is CY_IGNORE'd
 * when the BSP flag is 0).
 *
 * Runs in GFX-task context (process_ui_command) — same as the handlers it calls.
 *******************************************************************************/

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/* Feature handlers. Weak no-op fallbacks below cover the flag-off builds where
 * the owning .c is excluded, so the chain always links. When a feature IS built,
 * its strong definition (dfr0522_ipc.c / audio_sfx_ipc.c) wins. */
__attribute__((weak)) bool dfr0522_ext_dispatch(uint32_t cmd, const uint8_t *data)
{
    (void)cmd; (void)data;
    return false;
}

__attribute__((weak)) bool motor_ext_dispatch(uint32_t cmd, const uint8_t *data)
{
    (void)cmd; (void)data;
    return false;
}

__attribute__((weak)) bool audio_sfx_ext_dispatch(uint32_t cmd, const uint8_t *data)
{
    (void)cmd; (void)data;
    return false;
}

bool ipc_ui_ext_dispatch(uint32_t cmd, const uint8_t *data)
{
    if (dfr0522_ext_dispatch(cmd, data)) {
        return true;
    }
    if (motor_ext_dispatch(cmd, data)) {
        return true;
    }
    if (audio_sfx_ext_dispatch(cmd, data)) {
        return true;
    }
    return false;
}
