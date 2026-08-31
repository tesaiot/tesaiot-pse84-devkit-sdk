/**
* \copyright
* MIT License
*
* Copyright (c) 2022 Infineon Technologies AG
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE
*
* \endcopyright
*
* \author Infineon Technologies AG
*
* \file pal_i2c.c
*
* \brief   This file implements the platform abstraction layer(pal) APIs for I2C.
*
* \ingroup  grPAL
*
* @{
*/

#include "include/pal/pal_i2c.h"
#include "pal_psoc_i2c_mapping.h"
#include "cy_pdl.h"
#include "cy_gpio.h"
#include "cy_sysclk.h"
#include "mtb_hal.h"
#include "cybsp.h"
#include "cycfg_peripheral_clocks.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#define PAL_I2C_MASTER_MAX_BITRATE  (400U)
#define PAL_I2C_MASTER_INTR_PRIO    (3U)

/* OPTIGA I2C bus selection.
 * Default: CYBSP_I2C_CONTROLLER (SCB0 = P8.0/P8.1, 1V8 on-SoM bus) — matches
 * boards where the Trust M wires to the sensor bus (AI Kit / Eva Kit pattern).
 * OPTIGA_I2C_ON_3V3_BUS: CYBSP_I2C_CAM_CONTROLLER (SCB5 = P17.0/P17.1, shared
 * 3V3 bus). Required on the TESAIoT Dev Kit (QWA309 base): all three mikroBUS
 * sockets, QWIIC, and the DSI touch route I2C to P17.0/P17.1. */
#ifdef OPTIGA_I2C_ON_3V3_BUS
#define OPTIGA_I2C_SCB_HW      CYBSP_I2C_CAM_CONTROLLER_HW
#define OPTIGA_I2C_SCB_config  CYBSP_I2C_CAM_CONTROLLER_config
#else
#define OPTIGA_I2C_SCB_HW      CYBSP_I2C_CONTROLLER_HW
#define OPTIGA_I2C_SCB_config  CYBSP_I2C_CONTROLLER_config
#endif
/// @cond hidden

_STATIC_H volatile uint32_t g_entry_count = 0;
_STATIC_H const pal_i2c_t * gp_pal_i2c_current_ctx;
_STATIC_H uint8_t g_pal_i2c_init_flag = 0;
_STATIC_H TaskHandle_t i2c_taskhandle = NULL;
_STATIC_H SemaphoreHandle_t xIicSemaphoreHandle;


static mtb_hal_i2c_t CYBSP_I2C_CONTROLLER_0_hal_obj __attribute__((unused));
cy_stc_scb_i2c_context_t CYBSP_I2C_CONTROLLER_context;

//lint --e{715} suppress "This is implemented for overall completion of API"
static pal_status_t pal_i2c_acquire(const void * p_i2c_context)
{

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if ( xSemaphoreTakeFromISR(xIicSemaphoreHandle, &xHigherPriorityTaskWoken) == pdTRUE )
        return PAL_STATUS_SUCCESS;
    else
        return PAL_STATUS_FAILURE;
}

// I2C release bus function
//lint --e{715} suppress the unused p_i2c_context variable lint, since this is kept for future enhancements
static void pal_i2c_release(const void* p_i2c_context)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    xSemaphoreGiveFromISR(xIicSemaphoreHandle, &xHigherPriorityTaskWoken);
}


static void i2c_task(void *pvParameters)
{
  upper_layer_callback_t upper_layer_handler;
  uint32_t event = 0;

  while(1)
  {
    xTaskNotifyWait(0, 0xffffffff, &event, portMAX_DELAY);

    upper_layer_handler = (upper_layer_callback_t)gp_pal_i2c_current_ctx->upper_layer_event_handler;

    if (0UL != (MTB_HAL_I2C_TARGET_ERR_EVENT & event))
    {
        /* In case of error abort transfer */
        upper_layer_handler(gp_pal_i2c_current_ctx->p_upper_layer_ctx, PAL_I2C_EVENT_ERROR);
    }
    /* Check write complete event */
    else if (0UL != (MTB_HAL_I2C_TARGET_WR_CMPLT_EVENT & event))
    {
        /* Perform the required functions */
        upper_layer_handler(gp_pal_i2c_current_ctx->p_upper_layer_ctx, PAL_I2C_EVENT_SUCCESS);
    }
    /* Check read complete event */
    else if (0UL != (MTB_HAL_I2C_TARGET_RD_CMPLT_EVENT & event))
    {
        /* Perform the required functions */
        upper_layer_handler(gp_pal_i2c_current_ctx->p_upper_layer_ctx, PAL_I2C_EVENT_SUCCESS);
    }

    pal_i2c_release(gp_pal_i2c_current_ctx->p_upper_layer_ctx);

  }
}

/* Defining master callback handler */
void i2c_master_event_handler(void *callback_arg, mtb_hal_i2c_event_t event)
{
    BaseType_t xHigherPriorityTaskWoken= pdFALSE;

    xTaskNotifyFromISR(i2c_taskhandle, event, eSetBits, &xHigherPriorityTaskWoken);

    portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
}

pal_status_t pal_i2c_init(const pal_i2c_t * p_i2c_context)
{
    cy_rslt_t cy_hal_status = CY_RSLT_SUCCESS;

    /* Use 100kHz to match BSP clock divider=31 (~3.125 MHz SCB clock).
     * 400kHz requires divider=9 (~10 MHz) which breaks CM55 display/touch. */
    mtb_hal_i2c_cfg_t i2c_master_config = {false,
    		0,
			100000,
			0,
			true
    };

    do
    {
        /* Phase 1: Create FreeRTOS task and semaphore (called with NULL context) */
        if (i2c_taskhandle == NULL)
        {
            xIicSemaphoreHandle = xSemaphoreCreateBinary();

            if (xTaskCreate(i2c_task, "i2c_task", configMINIMAL_STACK_SIZE * 2, NULL, configMAX_PRIORITIES - 1, &i2c_taskhandle) != pdPASS)
            {
                break;
            }

            pal_i2c_release((void * )p_i2c_context);
            return PAL_STATUS_SUCCESS;
        }

        if (g_pal_i2c_init_flag == 0)
        {
            if (p_i2c_context == NULL)
            {
                return PAL_STATUS_SUCCESS;
            }

            /* Phase 2: Co-exist with the CM55 bus users on the selected SCB.
             *
             * The BSP has already initialized the SCB for I2C at 100 kHz-
             * compatible clocking. On the shared bus CM55 polls touch (0x38);
             * OPTIGA Trust M supports 100 kHz (Standard Mode), so we adopt
             * the existing config without modification.
             *
             * We only need to: (1) initialize our local PDL context,
             * (2) set up HAL for CM33_NS side. No clock or pin changes. */

            /* Disable SCB briefly to reinit PDL context for CM33_NS */
            if (OPTIGA_I2C_SCB_HW->CTRL & SCB_CTRL_ENABLED_Msk) {
                OPTIGA_I2C_SCB_HW->CTRL &= ~SCB_CTRL_ENABLED_Msk;
            }
            cy_hal_status = Cy_SCB_I2C_Init(OPTIGA_I2C_SCB_HW,
                            &OPTIGA_I2C_SCB_config,
                            &CYBSP_I2C_CONTROLLER_context);
            if (CY_RSLT_SUCCESS != cy_hal_status) {
                return cy_hal_status;
            }
            Cy_SCB_I2C_Enable(OPTIGA_I2C_SCB_HW);

            cy_hal_status = mtb_hal_i2c_setup(((pal_psoc_i2c_t *)(p_i2c_context->p_i2c_hw_config))->i2c_controller_channel,
                        ((pal_psoc_i2c_t *)(p_i2c_context->p_i2c_hw_config))->dev_config,
						&CYBSP_I2C_CONTROLLER_context,
                        NULL);

            if (CY_RSLT_SUCCESS != cy_hal_status)
            {
                break;
            }

            cy_hal_status = mtb_hal_i2c_configure(((pal_psoc_i2c_t *)(p_i2c_context->p_i2c_hw_config))->i2c_controller_channel, &i2c_master_config);

            mtb_hal_i2c_register_callback(((pal_psoc_i2c_t *)(p_i2c_context->p_i2c_hw_config))->i2c_controller_channel,
                          (mtb_hal_i2c_event_callback_t) i2c_master_event_handler,
                          NULL);

            mtb_hal_i2c_enable_event(((pal_psoc_i2c_t *)(p_i2c_context->p_i2c_hw_config))->i2c_controller_channel,
                         (mtb_hal_i2c_event_t)(MTB_HAL_I2C_TARGET_WR_CMPLT_EVENT \
                         | MTB_HAL_I2C_TARGET_RD_CMPLT_EVENT  \
                         | MTB_HAL_I2C_TARGET_ERR_EVENT ),    \
                         true);
            g_pal_i2c_init_flag = 1;
        }
        else
        {
            cy_hal_status = (pal_status_t)PAL_STATUS_SUCCESS;
        }
    } while (FALSE);

    return (pal_status_t)cy_hal_status;
}

pal_status_t pal_i2c_deinit(const pal_i2c_t * p_i2c_context)
{
    if (i2c_taskhandle != NULL && (p_i2c_context == NULL))
    {
        vTaskDelete(i2c_taskhandle);
        i2c_taskhandle = NULL;
        vSemaphoreDelete(xIicSemaphoreHandle);
    }
    return (PAL_STATUS_SUCCESS);
}

pal_status_t pal_i2c_write(const pal_i2c_t * p_i2c_context, uint8_t * p_data, uint16_t length)
{
    pal_status_t status = PAL_STATUS_FAILURE;

    //Acquire the I2C bus before read/write
    if (PAL_STATUS_SUCCESS == pal_i2c_acquire(p_i2c_context))
    {
        gp_pal_i2c_current_ctx = p_i2c_context;

        status = mtb_hal_i2c_controller_write(((pal_psoc_i2c_t *)(p_i2c_context->p_i2c_hw_config))->i2c_controller_channel,
                p_i2c_context->slave_address,
                p_data,
                length,
                100,
                true);
        //Invoke the low level i2c master driver API to write to the bus
        if (CY_RSLT_SUCCESS != status)
        {
            //If I2C Master fails to invoke the write operation, invoke upper layer event handler with error.
            //lint --e{611} suppress "void* function pointer is type casted to upper_layer_callback_t type"
            ((upper_layer_callback_t)(p_i2c_context->upper_layer_event_handler))
                                                       (p_i2c_context->p_upper_layer_ctx , PAL_I2C_EVENT_ERROR);

            //Release I2C Bus
            pal_i2c_release((void * )p_i2c_context);
        }
        else
        {
        	((upper_layer_callback_t)(p_i2c_context->upper_layer_event_handler))
        	                                                       (p_i2c_context->p_upper_layer_ctx , PAL_I2C_EVENT_SUCCESS);
            status = PAL_STATUS_SUCCESS;

            pal_i2c_release((void * )p_i2c_context);
        }
    }
    else
    {
        status = PAL_STATUS_I2C_BUSY;
        //lint --e{611} suppress "void* function pointer is type casted to upper_layer_callback_t type"
        ((upper_layer_callback_t)(p_i2c_context->upper_layer_event_handler))
                                                        (p_i2c_context->p_upper_layer_ctx , PAL_I2C_EVENT_BUSY);
    }
    return (status);
}

pal_status_t pal_i2c_read(const pal_i2c_t * p_i2c_context, uint8_t * p_data, uint16_t length)
{
    pal_status_t status = PAL_STATUS_FAILURE;

    //Acquire the I2C bus before read/write
    if (PAL_STATUS_SUCCESS == pal_i2c_acquire(p_i2c_context))
    {
        gp_pal_i2c_current_ctx = p_i2c_context;

        //Invoke the low level i2c master driver API to read from the bus
        cy_rslt_t rd_status = mtb_hal_i2c_controller_read(((pal_psoc_i2c_t *)(p_i2c_context->p_i2c_hw_config))->i2c_controller_channel,
                                                                p_i2c_context->slave_address,
                                                                p_data,
                                                                length,
                                                                100,
                                                                true);
        if (CY_RSLT_SUCCESS != rd_status)
        {
            //If I2C Master fails to invoke the read operation, invoke upper layer event handler with error.
            //lint --e{611} suppress "void* function pointer is type casted to upper_layer_callback_t type"
            ((upper_layer_callback_t)(p_i2c_context->upper_layer_event_handler))
                                                       (p_i2c_context->p_upper_layer_ctx , PAL_I2C_EVENT_ERROR);

            //Release I2C Bus
            pal_i2c_release((void * )p_i2c_context);
        }
        else
        {
        	((upper_layer_callback_t)(p_i2c_context->upper_layer_event_handler))
        	                                                       (p_i2c_context->p_upper_layer_ctx , PAL_I2C_EVENT_SUCCESS);
            status = PAL_STATUS_SUCCESS;
            pal_i2c_release((void * )p_i2c_context);
        }
    }
    else
    {
        status = PAL_STATUS_I2C_BUSY;
        //lint --e{611} suppress "void* function pointer is type casted to upper_layer_callback_t type"
        ((upper_layer_callback_t)(p_i2c_context->upper_layer_event_handler))
                                                        (p_i2c_context->p_upper_layer_ctx , PAL_I2C_EVENT_BUSY);
    }
    return (status);
}

pal_status_t pal_i2c_set_bitrate(const pal_i2c_t * p_i2c_context, uint16_t bitrate)
{
	return PAL_STATUS_SUCCESS;
}

/**
* @}
*/
