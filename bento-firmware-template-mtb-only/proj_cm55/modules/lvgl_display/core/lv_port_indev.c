/*******************************************************************************
 * Upstream provenance and licence — LVGL
 *
 * This file is derived from LVGL v9.5.0, upstream file:
 *     examples/porting/lv_port_indev_template.c
 *     https://github.com/lvgl/lvgl/tree/v9.5.0
 *
 * LVGL is distributed under the MIT licence, which requires its copyright
 * notice and permission notice to be retained in all copies and substantial
 * portions of the software. That notice is reproduced below in full.
 *
 * The Infineon notice that follows this block covers Infineon's modifications
 * to this file. Both notices stand together: neither supersedes the other, and
 * the Infineon terms do not narrow the MIT grant over the LVGL-authored code.
 *
 * ---------------------------------------------------------------------------
 * MIT licence
 * Copyright (c) 2025 LVGL Kft
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 * ---------------------------------------------------------------------------
 *
 * The quotation marks above are ASCII; the upstream file uses typographic
 * quotes. The byte-exact upstream text is shipped as ../LICENSE-LVGL.txt
 ******************************************************************************/
/*******************************************************************************
 * File Name        : lv_port_indev.c
 *
 * Description      : This file provides implementation of low level input
 * device driver for LVGL.
 *
 * Related Document : See README.md
 *
 ********************************************************************************
 * (c) 2025, Infineon Technologies AG, or an affiliate of Infineon
 * Technologies AG. All rights reserved.
 * This software, associated documentation and materials ("Software") is
 * owned by Infineon Technologies AG or one of its affiliates ("Infineon")
 * and is protected by and subject to worldwide patent protection, worldwide
 * copyright laws, and international treaty provisions. Therefore, you may use
 * this Software only as provided in the license agreement accompanying the
 * software package from which you obtained this Software. If no license
 * agreement applies, then any use, reproduction, modification, translation, or
 * compilation of this Software is prohibited without the express written
 * permission of Infineon.
 *
 * Disclaimer: UNLESS OTHERWISE EXPRESSLY AGREED WITH INFINEON, THIS SOFTWARE
 * IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING, BUT NOT LIMITED TO, ALL WARRANTIES OF NON-INFRINGEMENT OF
 * THIRD-PARTY RIGHTS AND IMPLIED WARRANTIES SUCH AS WARRANTIES OF FITNESS FOR A
 * SPECIFIC USE/PURPOSE OR MERCHANTABILITY.
 * Infineon reserves the right to make changes to the Software without notice.
 * You are responsible for properly designing, programming, and testing the
 * functionality and safety of your intended application of the Software, as
 * well as complying with any legal requirements related to its use. Infineon
 * does not guarantee that the Software will be free from intrusion, data theft
 * or loss, or other breaches ("Security Breaches"), and Infineon shall have
 * no liability arising out of any Security Breaches. Unless otherwise
 * explicitly approved by Infineon, the Software may not be used in any
 * application where a failure of the Product or any consequences of the use
 * thereof can reasonably be expected to result in personal injury.
 *******************************************************************************/

/*******************************************************************************
 * Header Files
 *******************************************************************************/
#include "lv_port_indev.h"
#include "cy_utils.h"
#include "lv_indev_private.h"
#include "lv_port_disp.h"
#if (CM55_TOUCH_INPUT_ENABLE == 0U)
#include "cm55_ipc_app.h"
#endif
#if defined(MTB_CTP_GT911)
#include "mtb_ctp_gt911.h"
#elif defined(MTB_CTP_ILI2511)
#include "mtb_ctp_ili2511.h"
#elif defined(MTB_CTP_FT5406)
#include "mtb_ctp_ft5406.h"
#endif
#include "cybsp.h"
#include "display_i2c_config.h"

/*******************************************************************************
 * Macros
 *******************************************************************************/
#if defined(MTB_CTP_ILI2511)
#define CTP_RESET_PORT GPIO_PRT17
#define CTP_RESET_PIN (3U)
#define CTP_IRQ_PORT GPIO_PRT17
#define CTP_IRQ_PIN (2U)
#endif

#define INDEV_READ_PERIOD_MS                                                   \
  20U /* 20ms touch polling from SensorHub IPC */

/*
 * 0 -> touch comes from SensorHub IPC (CM33 owns touch I2C)
 * 1 -> touch is read directly by CM55 touch middleware
 */
#ifndef CM55_TOUCH_INPUT_ENABLE
#define CM55_TOUCH_INPUT_ENABLE 0U
#endif

/*******************************************************************************
 * Global Variables
 *******************************************************************************/
lv_indev_t *indev_touchpad;

#if defined(MTB_CTP_ILI2511)
/* ILI2511 touch controller configuration */
mtb_ctp_ili2511_config_t ctp_ili2511_cfg = {
    .scb_inst = DISPLAY_I2C_CONTROLLER_HW,
    .i2c_context = &disp_touch_i2c_controller_context,
    .rst_port = CTP_RESET_PORT,
    .rst_pin = CTP_RESET_PIN,
    .irq_port = CTP_IRQ_PORT,
    .irq_pin = CTP_IRQ_PIN,
    .irq_num = ioss_interrupts_gpio_17_IRQn,
    .touch_event = false,
};
#endif /* MTB_CTP_ILI2511 */

#if defined(MTB_CTP_FT5406)
mtb_ctp_ft5406_config_t ctp_ft5406_cfg = {
    .i2c_base = DISPLAY_I2C_CONTROLLER_HW,
    .i2c_context = &disp_touch_i2c_controller_context,
};
#endif /* MTB_CTP_FT5406 */

/*******************************************************************************
 * Function Name: touchpad_init
 ********************************************************************************
 * Summary:
 *  Initialization function for touchpad supported by LVGL.
 *
 * Parameters:
 *  void
 *
 * Return:
 *  void
 *
 *******************************************************************************/
#if (CM55_TOUCH_INPUT_ENABLE != 0U)
static void touchpad_init(void) {
  cy_rslt_t result = CY_RSLT_SUCCESS;

#if defined(MTB_CTP_GT911)
  result = mtb_gt911_init(DISPLAY_I2C_CONTROLLER_HW,
                          &disp_touch_i2c_controller_context);

#elif defined(MTB_CTP_ILI2511)
  result = mtb_ctp_ili2511_init(&ctp_ili2511_cfg);
#elif defined(MTB_CTP_FT5406)
  result = (cy_rslt_t)mtb_ctp_ft5406_init(&ctp_ft5406_cfg);
#endif

  if (CY_RSLT_SUCCESS != result) {
    CY_ASSERT(0);
  }
}

/*******************************************************************************
 * Function Name: touchpad_read
 ********************************************************************************
 * Summary:
 *  Touchpad read function called by the LVGL library.
 *  Here you will find example implementation of input devices supported by
 *  LVGL:
 *   - Touchpad
 *   - Mouse (with cursor support)
 *   - Keypad (supports GUI usage only with key)
 *   - Encoder (supports GUI usage only with: left, right, push)
 *   - Button (external buttons to press points on the screen)
 *
 *   The `..._read()` function are only examples.
 *   You should shape them according to your hardware.
 *
 *
 * Parameters:
 *  *indev_drv: Pointer to the input driver structure to be registered by LVGL.
 *  *data: Pointer to the data buffer holding touch coordinates.
 *
 * Return:
 *  void
 *
 *******************************************************************************/
LV_ATTRIBUTE_FAST_MEM void touchpad_read(lv_indev_t *indev_drv,
                                         lv_indev_data_t *data) {
  static int touch_x = 0;
  static int touch_y = 0;
  cy_rslt_t result = CY_RSLT_SUCCESS;

  data->state = LV_INDEV_STATE_REL;

#if defined(MTB_CTP_GT911)
  result = mtb_gt911_get_single_touch(DISPLAY_I2C_CONTROLLER_HW,
                                      &disp_touch_i2c_controller_context,
                                      &touch_x, &touch_y);

  if (CY_RSLT_SUCCESS == result) {
    data->state = LV_INDEV_STATE_PR;
  }
#elif defined(MTB_CTP_ILI2511)
  result = mtb_ctp_ili2511_get_single_touch(&touch_x, &touch_y);

  if (CY_RSLT_SUCCESS == result) {
    data->state = LV_INDEV_STATE_PR;
  }
#elif defined(MTB_CTP_FT5406)
  mtb_ctp_touch_event_t touch_event;
  result = (cy_rslt_t)mtb_ctp_ft5406_get_single_touch(&touch_event, &touch_x,
                                                      &touch_y);

  if ((CY_RSLT_SUCCESS == result) && ((MTB_CTP_TOUCH_DOWN == touch_event) ||
                                      (MTB_CTP_TOUCH_CONTACT == touch_event))) {
    data->state = LV_INDEV_STATE_PR;
  }

#endif

#if defined(MTB_CTP_FT5406)
  /* Set the last pressed coordinates */
  data->point.x = ACTUAL_DISP_HOR_RES - touch_x;
  data->point.y = ACTUAL_DISP_VER_RES - touch_y;
#elif defined(MTB_CTP_ILI2511) || defined(MTB_CTP_GT911)
  /* Set the last pressed coordinates */
  data->point.x = touch_x;
  data->point.y = touch_y;
#endif
}
#endif /* CM55_TOUCH_INPUT_ENABLE != 0U */

#if (CM55_TOUCH_INPUT_ENABLE == 0U)
LV_ATTRIBUTE_FAST_MEM static void touchpad_read_ipc(lv_indev_t *indev_drv,
                                                    lv_indev_data_t *data) {
  static int touch_x = 0;
  static int touch_y = 0;
  static uint16_t last_seq = 0U;
  static uint32_t last_update_ms = 0U;
  static lv_indev_state_t state = LV_INDEV_STATE_REL;
  ipc_sensorhub_sample_t sample;

  (void)indev_drv;

  if (cm55_get_sensorhub_latest_touch(&sample)) {
    if (sample.sequence != last_seq) {
      touch_x = (int)sample.data.touch.x;
      touch_y = (int)sample.data.touch.y;
      state = (0U != sample.data.touch.pressed) ? LV_INDEV_STATE_PR
                                                 : LV_INDEV_STATE_REL;
      last_seq = sample.sequence;
      last_update_ms = lv_tick_get();
    }
  }

  if ((LV_INDEV_STATE_PR == state) && (lv_tick_elaps(last_update_ms) > 400U)) {
    state = LV_INDEV_STATE_REL;
  }

  data->state = state;
  data->point.x = touch_x;
  data->point.y = touch_y;
}
#endif

/*******************************************************************************
 * Function Name: lv_port_indev_init
 ********************************************************************************
 * Summary:
 *  Initialization function for input devices supported by LittelvGL.
 *
 * Parameters:
 *  void
 *
 * Return:
 *  void
 *
 *******************************************************************************/
void lv_port_indev_init(void) {
#if (CM55_TOUCH_INPUT_ENABLE == 0U)
  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touchpad_read_ipc);
  lv_timer_pause(indev->read_timer);
  lv_timer_reset(indev->read_timer);
  lv_timer_set_period(indev->read_timer, INDEV_READ_PERIOD_MS);
  lv_timer_resume(indev->read_timer);
  return;
#else
  /* Initialize your touchpad if you have. */
  touchpad_init();

  /* Register a touchpad input device */
  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touchpad_read);
  lv_timer_pause(indev->read_timer);
  lv_timer_reset(indev->read_timer);
  lv_timer_set_period(indev->read_timer, INDEV_READ_PERIOD_MS);
  lv_timer_resume(indev->read_timer);
#endif
}

/* [] END OF FILE */
