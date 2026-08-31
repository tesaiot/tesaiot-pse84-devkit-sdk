/*******************************************************************************
 * File Name: page_motion.h
 *
 * Description: Motion page — BMI270 accel/gyro charts + BMM350 compass.
 *              Page-based replacement for tab_motion (create/render/destroy).
 *
 *******************************************************************************/

#ifndef PAGE_MOTION_H
#define PAGE_MOTION_H

#include "ipc_sensorhub.h"
#include "lvgl.h"

lv_obj_t *page_motion_create(void);
void      page_motion_render(sensorhub_snapshot_t *snap);
void      page_motion_destroy(void);

#endif /* PAGE_MOTION_H */
