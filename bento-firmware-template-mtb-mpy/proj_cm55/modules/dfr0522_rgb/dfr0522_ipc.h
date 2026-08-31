/*******************************************************************************
 * dfr0522_ipc.h — feature handler for IPC_CMD_UI_RGB_MATRIX (rgbmatrix module).
 *
 * dfr0522_ext_dispatch() decodes the RGB sub-op payload and drives the DFR0522
 * on the display I2C bus. It is chained from the project's single strong
 * ipc_ui_ext_dispatch() override (ipc_ui_ext_chain.c) so several project
 * features can share the one weak extension hook without a link-time collision.
 *
 * Declared __attribute__((weak)) in the chain unit with a no-op fallback so the
 * override links even when BSP_HAS_QWA309_BASEBOARD=0 (dfr0522_ipc.c is
 * CY_IGNORE'd then).
 *******************************************************************************/

#ifndef DFR0522_IPC_H
#define DFR0522_IPC_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns true if cmd == IPC_CMD_UI_RGB_MATRIX and it was handled. */
bool dfr0522_ext_dispatch(uint32_t cmd, const uint8_t *data);

#ifdef __cplusplus
}
#endif

#endif /* DFR0522_IPC_H */
