/*******************************************************************************
 * dfr0522_ipc.c — CM55 feature handler that services the QWA309 DFR0522 RGB
 *                 matrix over IPC_CMD_UI_RGB_MATRIX.
 *
 * Exposes dfr0522_ext_dispatch() (a feature handler, NOT the weak hook itself).
 * The project's single strong ipc_ui_ext_dispatch() override lives in
 * ipc_ui_ext_chain.c, which chains to this handler and to the audio SFX handler.
 * This lets several project features share the one weak extension hook the
 * shared ipc_ui.c provides, without a link-time collision.
 *
 * Decodes the RGB sub-op payload and drives the DFR0522 on the display I2C bus.
 * It runs in GFX-task context (process_ui_command), which is the owner of that
 * SCB — the same context/bus cm55_sensor_poll uses for CapSense — so the
 * transaction is safe (no cross-task I2C contention, no IPC re-entrancy).
 *
 * When BSP_HAS_QWA309_BASEBOARD=0 this file compiles to nothing; ipc_ui_ext_chain.c
 * then links the weak no-op dfr0522_ext_dispatch() it declares locally.
 *******************************************************************************/

#include "bsp_feature_flags.h"

#if BSP_HAS_QWA309_BASEBOARD

#include "ipc_ui_protocol.h"

/* Anti-copy-paste guard: this file hard-codes canonical opcode-map v2 behavior. */
_Static_assert(IPC_UI_OPMAP_VERSION == 2, "wrong tree's ipc_ui_protocol.h (opcode map mismatch)");
#include "dfr0522_rgb.h"
#include "dfr0522_ipc.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/* Strong override of the shared ipc_ui.c clear-all hook: a new MicroPython
 * script is starting — stop the marquee and blank the matrix so the previous
 * student's output doesn't keep running under the next program. */
void ipc_ui_ext_clear_all(void)
{
    (void)dfr0522_clear();
}

bool dfr0522_ext_dispatch(uint32_t cmd, const uint8_t *data)
{
    if (cmd != IPC_CMD_UI_RGB_MATRIX || data == NULL) {
        return false;
    }

    const ipc_ui_rgb_matrix_t *req = (const ipc_ui_rgb_matrix_t *)data;

    switch (req->op) {
    case IPC_RGB_OP_CLEAR:
        (void)dfr0522_clear();
        return true;
    case IPC_RGB_OP_FILL:
        (void)dfr0522_fill(req->color);
        return true;
    case IPC_RGB_OP_PIXEL:
        (void)dfr0522_pixel(req->x, req->y, req->color);
        return true;
    case IPC_RGB_OP_BLIT: {
        const ipc_ui_rgb_blit_t *b = (const ipc_ui_rgb_blit_t *)data;
        (void)dfr0522_blit(b->frame);
        return true;
    }
    case IPC_RGB_OP_SCORE: {
        const ipc_ui_rgb_score_t *s = (const ipc_ui_rgb_score_t *)data;
        (void)dfr0522_score(s->value, s->color);
        return true;
    }
    case IPC_RGB_OP_BAR: {
        const ipc_ui_rgb_bar_t *b = (const ipc_ui_rgb_bar_t *)data;
        (void)dfr0522_bar(b->value, b->max, b->color);
        return true;
    }
    case IPC_RGB_OP_SCROLL: {
        const ipc_ui_rgb_scroll_t *s = (const ipc_ui_rgb_scroll_t *)data;
        (void)dfr0522_scroll(s->text, s->len, s->color,
                             (uint16_t)s->period_cs * 10U);
        return true;
    }
    default:
        return false;
    }
}

#endif /* BSP_HAS_QWA309_BASEBOARD */
