/*******************************************************************************
 * File Name: page_hsm.h
 *
 * Description: HSM — OPTIGA Trust M security dashboard page.
 *              Shows chip identity, certificates, key slots, health status,
 *              and security counters in card-based layout.
 *
 *              When ENABLE_OPTIGA=0 (default), displays informational
 *              placeholder content. When ENABLE_OPTIGA=1, reads real
 *              data from OPTIGA Trust M via optiga_manager API.
 *
 *******************************************************************************/

#ifndef PAGE_HSM_H
#define PAGE_HSM_H

#include "ipc_sensorhub.h"
#include "lvgl.h"

lv_obj_t *page_hsm_create(void);
void      page_hsm_render(sensorhub_snapshot_t *snap);
void      page_hsm_destroy(void);

#endif /* PAGE_HSM_H */
