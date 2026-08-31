/*******************************************************************************
 * File Name: page_bentoclaw.h
 *
 * Description: BentoClaw Agent Status page — shows connection status,
 *              last command, last result, and agent statistics.
 *              Data updated via IPC push from CM33_NS BentoClaw module.
 *
 *******************************************************************************/

#ifndef PAGE_BENTOCLAW_H
#define PAGE_BENTOCLAW_H

#include "lvgl.h"
#include "ipc_sensorhub.h"

/** Register IPC callback for BentoClaw commands from CM33_NS.
 *  Must be called after cm55_ipc_communication_setup(). */
bool ipc_bentoclaw_init(void);

/** Create the BentoClaw status screen. */
lv_obj_t *page_bentoclaw_create(void);

/** Update display with latest status (ignores snap — uses IPC cache). */
void page_bentoclaw_render(sensorhub_snapshot_t *snap);

/** Cleanup on page leave. */
void page_bentoclaw_destroy(void);

/** IPC callback: receive UI updates from CM33_NS BentoClaw module.
 *  Called from IPC ISR context — must NOT call LVGL directly.
 *  Sets deferred flags for render_cb to pick up. */
void page_bentoclaw_ipc_update(uint8_t update_type, const char *text, size_t len);

#endif /* PAGE_BENTOCLAW_H */
