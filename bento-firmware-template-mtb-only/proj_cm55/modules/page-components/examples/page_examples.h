/*******************************************************************************
 * File Name: page_examples.h
 *
 * Description: SDK Examples — one page listing every public API example in the
 *              SDK, with the runnable ones a tap away.
 *
 *              ONE page, not one page per example. page_id_t is ABI (the
 *              prebuilt archives in lib/ bake page ids as immediates) and 24 of
 *              the 25 slots are already spoken for, so a page per example is not
 *              available even in principle. Each example is a table row plus a
 *              function pointer instead — which also keeps this page out of the
 *              pm_register / s_card_defs dual-file sync trap, because the table
 *              is generated from the example files themselves.
 *
 *              Compiled only when ENABLE_PAGE_EXAMPLES=1. Default is 0.
 *
 *******************************************************************************/

#ifndef PAGE_EXAMPLES_H
#define PAGE_EXAMPLES_H

#include "lvgl.h"
#include "ipc_sensorhub.h"

lv_obj_t *page_examples_create(void);
void      page_examples_render(sensorhub_snapshot_t *snap);
void      page_examples_destroy(void);

#endif /* PAGE_EXAMPLES_H */
