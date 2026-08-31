/*******************************************************************************
 * File Name: page_playground.h
 *
 * Description: Playground page — merged UX/UI widget sandbox + Console/LCD.
 *              Provides scrollable container for MicroPython-created widgets
 *              and emergency control buttons (STOP/RESTART/RESET).
 *              Page-based replacement for tab_uxui + terminal tab.
 *
 *******************************************************************************/

#ifndef PAGE_PLAYGROUND_H
#define PAGE_PLAYGROUND_H

#include "ipc_sensorhub.h"
#include "lvgl.h"

lv_obj_t *page_playground_create(void);
void      page_playground_render(sensorhub_snapshot_t *snap);
void      page_playground_destroy(void);

/** Get the scrollable container for widget creation (IPC ui.* commands). */
lv_obj_t *page_playground_get_uxui_container(void);

/** Get the terminal container for LCD print/log (IPC lcd.* commands). */
lv_obj_t *page_playground_get_terminal_container(void);

#endif /* PAGE_PLAYGROUND_H */
