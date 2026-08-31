/*******************************************************************************
 * File Name        : tesaiot_display.h
 *
 * Description      : TESAIoT display controller: GFXSS, vg_lite, LVGL init,
 *                    display/indev ports, and graphics task (CM55).
 *
 * Author           : Asst.Prof.Santi Nuratch, Ph.D
 *                    Thailand Embedded Systems Association (TESA)
 * Modifications    : (c) 2026 TESAIoT
 *
 *******************************************************************************/

#ifndef TESAIOT_DISPLAY_H
#define TESAIOT_DISPLAY_H

#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "display_i2c_config.h"
#include "lv_port_disp.h"
#include "lvgl.h"
#include "mtb_disp_dsi_waveshare_4p3.h"
#include "vg_lite.h"
#include "vg_lite_platform.h"

#define GPU_INT_PRIORITY (3U)
#define DC_INT_PRIORITY (3U)
#define I2C_CONTROLLER_IRQ_PRIORITY (2UL)

#define APP_BUFFER_COUNT (2U)
#define DEFAULT_GPU_CMD_BUFFER_SIZE ((64U) * (1024U))
#define GPU_TESSELLATION_BUFFER_SIZE ((MY_DISP_VER_RES) * 128U)
#define VGLITE_HEAP_SIZE                                                       \
  (((DEFAULT_GPU_CMD_BUFFER_SIZE) * (APP_BUFFER_COUNT)) +                      \
   ((GPU_TESSELLATION_BUFFER_SIZE) * (APP_BUFFER_COUNT)))

#define GPU_MEM_BASE (0x0U)
#define VG_PARAMS_POS (0UL)

#define GFX_TASK_NAME ("TESAIoT Gfx Task")
#define GFX_TASK_STACK_SIZE (configMINIMAL_STACK_SIZE * 32)
#define GFX_TASK_PRIORITY (configMAX_PRIORITIES - 1)

typedef struct {
  uint32_t last_dc_irq_status;
  uint32_t dc_disp0_count;
  uint32_t dc_underflow_count;
  uint32_t dc_bus_error_count;
  uint32_t gpu_recovery_count;
  uint32_t flush_start_count;
  uint32_t flush_ready_count;
  uint32_t flush_timeout_count;
  uint32_t gfx_task_stack_min_words;
} tesaiot_display_diag_t;

extern TaskHandle_t rtos_cm55_gfx_task_handle;
extern volatile tesaiot_display_diag_t g_tesaiot_display_diag;

BaseType_t tesaiot_display_init(void);
void tesaiot_display_task(void *arg);

static inline void tesaiot_display_tick(void) { lv_tick_inc(1); }

#endif /* TESAIOT_DISPLAY_H */
