/*******************************************************************************
 * File Name: page_animation.h
 *
 * Description: Lottie Animation demo page header.
 *              Showcases ThorVG-powered vector animations on PSoC Edge E84.
 *
 *******************************************************************************/

#ifndef PAGE_ANIMATION_H
#define PAGE_ANIMATION_H

#include "lvgl.h"
#include "ipc_sensorhub.h"

lv_obj_t *page_animation_create(void);
void      page_animation_render(sensorhub_snapshot_t *snap);
void      page_animation_destroy(void);

#endif /* PAGE_ANIMATION_H */
