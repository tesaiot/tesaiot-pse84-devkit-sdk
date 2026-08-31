/*******************************************************************************
 * File Name: page_edge_ai.h
 *
 * Description: Edge AI hub — one page for ALL compiled-in DEEPCRAFT models.
 *              Left: model list (tap to activate). Right: live class scores,
 *              inference latency and engine state.
 *
 *              ONE page for every model is a hard requirement, not a style
 *              choice: PM_MAX_PAGES is 20 and this project already registers
 *              18 pages, so per-model pages would silently fail to register
 *              (page_manager.c returns on overflow) and produce dead cards.
 *
 *              Compiled only when BENTO_HAS_EDGE_AI=1.
 *******************************************************************************/

#ifndef PAGE_EDGE_AI_H
#define PAGE_EDGE_AI_H

#include "bsp_feature_flags.h"

#if defined(BENTO_HAS_EDGE_AI) && (BENTO_HAS_EDGE_AI == 1)

#include "ipc_sensorhub.h"
#include "lvgl.h"

lv_obj_t *page_edge_ai_create(void);
void      page_edge_ai_render(sensorhub_snapshot_t *snap);
void      page_edge_ai_destroy(void);

#endif /* BENTO_HAS_EDGE_AI */
#endif /* PAGE_EDGE_AI_H */
