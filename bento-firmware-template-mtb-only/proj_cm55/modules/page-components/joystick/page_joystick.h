/*******************************************************************************
 * File Name: page_joystick.h
 *
 * Description: Joystick page — F310 controller image + button overlay + table.
 *              Page-based replacement for tab_joystick (create/render/destroy).
 *
 *******************************************************************************/

#ifndef PAGE_JOYSTICK_H
#define PAGE_JOYSTICK_H

#include "ipc_sensorhub.h"
#include "lvgl.h"

lv_obj_t *page_joystick_create(void);
void      page_joystick_render(sensorhub_snapshot_t *snap);
void      page_joystick_destroy(void);

#endif /* PAGE_JOYSTICK_H */
