/*******************************************************************************
 * File Name: page_edge_ai.h
 *
 * Description: Edge AI hub — one page for ALL compiled-in DEEPCRAFT models.
 *              Left: model list (tap to activate). Right: live class scores,
 *              inference latency and engine state.
 *
 *              ONE page for every model is a hard requirement, not a style
 *              choice: page_id_t names 24 pages (PAGE_ID_COUNT) and
 *              PM_MAX_PAGES is 24U (page_manager.h), so the table is already
 *              full. A per-model page would need a new explicit enum value —
 *              the ids are ABI and never re-numbered — and a larger
 *              PM_MAX_PAGES; the _Static_assert in page_manager.h fails the
 *              build otherwise, rather than shipping a dead card.
 *
 *              Compiled only when BENTO_HAS_EDGE_AI=1.
 *              //! [doc-drift-fix] — docs/template_local_deltas.list
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
