/*******************************************************************************
 * File Name: page_gpio_rgb.h
 *
 * Description: GPIO & RGB Matrix page — QWA309 base-board I/O hub.
 *              TESAIoT Dev Kit only (BSP_HAS_QWA309_BASEBOARD).
 *
 *              Merges the former Potentiometers and Touch & RGB pages into a
 *              single view: 4 potentiometers (VR1-4, mV and raw),
 *              CapSense-4000T touch (BTN0/BTN1 + slider, SW1-SW4 when its
 *              firmware speaks protocol 0x0D/0x0E), the two base-board
 *              push-buttons on P17.5/P17.7 and a DFR0522 16x8 RGB-matrix
 *              preview + color swatches. Swatches and the push-buttons
 *              drive the physical matrix.
 *
 *******************************************************************************/

#ifndef PAGE_GPIO_RGB_H
#define PAGE_GPIO_RGB_H

#include "bsp_feature_flags.h"

#if BSP_HAS_QWA309_BASEBOARD

#include "ipc_sensorhub.h"
#include "lvgl.h"

lv_obj_t *page_gpio_rgb_create(void);
void      page_gpio_rgb_render(sensorhub_snapshot_t *snap);
void      page_gpio_rgb_destroy(void);

#endif /* BSP_HAS_QWA309_BASEBOARD */
#endif /* PAGE_GPIO_RGB_H */
