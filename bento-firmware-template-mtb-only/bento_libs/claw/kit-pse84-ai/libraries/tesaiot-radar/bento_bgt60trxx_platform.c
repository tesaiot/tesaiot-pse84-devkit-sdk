/***********************************************************************************************//**
 * \file bento_bgt60trxx_platform.c
 *
 * \brief
 * MTB platform functions for the XENSIV(TM) BGT60TRxx radar sensor.
 *
 * This replaces xensiv_bgt60trxx_edge.c from mtb_shared, which is CY_IGNOREd by
 * every project that builds this library. It is a faithful copy of that file
 * except for one thing, in two places.
 *
 * WHY THIS FILE EXISTS
 * --------------------
 * Both SPI entry points in the vendor version end in an unbounded spin:
 *
 *     while (0UL != (CY_SCB_SPI_TRANSFER_ACTIVE &
 *                    Cy_SCB_SPI_GetTransferStatus(base, ctx)))
 *     {
 *     }
 *
 * Cy_SCB_SPI_Transfer() is the interrupt-driven API, so TRANSFER_ACTIVE is
 * cleared only by the SCB interrupt handler. Lose that interrupt once and the
 * caller spins there forever, holding its priority and never returning.
 *
 * Measured on the TESAIoT Dev Kit, 2026-08-09. The Radar page's Energy value
 * ran for a while and then froze, repeatably, needing a power cut -- a reset
 * and a reflash both left it dead. Instrumenting the radar task pinned it
 * exactly: loop counter frozen at 1667 (about eight seconds of running), phase
 * parked on the FIFO read, and the sensor's own stall watchdog reporting 0
 * attempts because it sits at the BOTTOM of the same loop that was stuck. Every
 * recovery path in this driver is downstream of the hang it needs to recover
 * from.
 *
 * A bounded spin turns an unrecoverable wedge into an error return. The caller
 * gets XENSIV_BGT60TRXX_STATUS_COM_ERROR, radar_process_data() returns, the
 * loop reaches its watchdog, and radar_recover() restarts the sequencer -- the
 * self-healing path that was already written and could never be reached.
 *
 * The transfer is aborted before returning, so the SCB is left in a state the
 * next call can use rather than one it would immediately fail on.
 *
 ***************************************************************************************************
 * \copyright
 * Copyright 2022 Infineon Technologies AG
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 **************************************************************************************************/

#ifdef COMPONENT_MTB_HAL

#include "xensiv_bgt60trxx_edge.h"
#include "xensiv_bgt60trxx_platform.h"

/*******************************************************************************
* Macros
*******************************************************************************/
#define XENSIV_BGT60TRXX_ERROR(x)           (((x) == XENSIV_BGT60TRXX_STATUS_OK) ? CY_RSLT_SUCCESS :\
                                             CY_RSLT_CREATE(CY_RSLT_TYPE_ERROR, CY_RSLT_MODULE_BOARD_HARDWARE_XENSIV_BGT60TRXX, (x)))

#define GPIO_LOW                           (0U)
#define GPIO_HIGH                          (1U)

/* How long to wait for a transfer the SCB interrupt is supposed to finish.
 *
 * A worst-case transfer here is a 128-sample FIFO read: 128 x 12 bits at the
 * configured SPI clock, on the order of 60 us. The spin body is three
 * instructions, so at this core clock a legitimate wait is a few thousand
 * iterations. Two million is therefore roughly two orders of magnitude beyond
 * anything real -- about fifteen milliseconds -- which is late enough that it
 * can never fire on a healthy transfer and early enough that the radar task is
 * back in its loop well within the one-second stall watchdog.
 *
 * Deliberately an iteration count and not a timer read: this is the hot path
 * for every register access on the sensor, and a wall-clock check per iteration
 * would cost more than the transfer it is guarding. Being approximate is fine.
 * The requirement is that it terminates, not that it terminates punctually. */
#define BENTO_SPI_SPIN_LIMIT               (2000000UL)

/* Bounded replacement for the vendor's `while (TRANSFER_ACTIVE) {}`.
 * Returns true if the transfer completed, false if it gave up (having aborted
 * the transfer so the SCB is usable again). */
static bool bento_spi_wait_done(CySCB_Type* base, cy_stc_scb_spi_context_t* ctx)
{
    uint32_t spins = 0UL;

    while (0UL != (CY_SCB_SPI_TRANSFER_ACTIVE & Cy_SCB_SPI_GetTransferStatus(base, ctx)))
    {
        if (++spins > BENTO_SPI_SPIN_LIMIT)
        {
            Cy_SCB_SPI_AbortTransfer(base, ctx);
            return false;
        }
    }

    return true;
}


/*******************************************************************************
 * Public interface implementation
 ********************************************************************************/
cy_rslt_t xensiv_bgt60trxx_mtb_init(xensiv_bgt60trxx_mtb_t* obj, const uint32_t* regs, size_t len)
{
    CY_ASSERT(obj != NULL);
    CY_ASSERT(regs != NULL);

    cy_rslt_t rslt = CY_RSLT_SUCCESS;

    xensiv_bgt60trxx_t* dev = &obj->dev;
    xensiv_bgt60trxx_mtb_iface_t* iface = &obj->iface;

    Cy_GPIO_Pin_FastInit(iface->sel_port, iface->sel_pin, CY_GPIO_DM_STRONG_IN_OFF, GPIO_HIGH,
                         HSIOM_SEL_GPIO);

    Cy_GPIO_Pin_FastInit(iface->rst_port, iface->rst_pin, CY_GPIO_DM_STRONG_IN_OFF, GPIO_HIGH,
                         HSIOM_SEL_GPIO);


    /* perform device hard reset before beginning init via SPI */
    xensiv_bgt60trxx_platform_rst_set(iface, true);
    xensiv_bgt60trxx_platform_spi_cs_set(iface, true);
    xensiv_bgt60trxx_platform_delay(1U);
    xensiv_bgt60trxx_platform_rst_set(iface, false);
    xensiv_bgt60trxx_platform_delay(1U);
    xensiv_bgt60trxx_platform_rst_set(iface, true);
    xensiv_bgt60trxx_platform_delay(1U);

    if (CY_RSLT_SUCCESS == rslt)
    {
        int32_t res = xensiv_bgt60trxx_init(dev, iface, false);
        rslt = XENSIV_BGT60TRXX_ERROR(res);
    }

    if (CY_RSLT_SUCCESS == rslt)
    {
        int32_t res = xensiv_bgt60trxx_config(dev, regs, len);
        rslt = XENSIV_BGT60TRXX_ERROR(res);
    }

    return rslt;
}


cy_rslt_t xensiv_bgt60trxx_mtb_interrupt_init(xensiv_bgt60trxx_mtb_t* obj, uint16_t fifo_limit)
{
    CY_ASSERT(obj != NULL);

    cy_rslt_t result = CY_RSLT_SUCCESS;
    xensiv_bgt60trxx_t* dev = &obj->dev;
    xensiv_bgt60trxx_mtb_iface_t* iface = &obj->iface;

    Cy_GPIO_ClearInterrupt(iface->irq_port, iface->irq_pin);
    Cy_GPIO_Pin_FastInit(iface->irq_port, iface->irq_pin, CY_GPIO_DM_PULLDOWN, GPIO_LOW,
                         HSIOM_SEL_GPIO);
    Cy_GPIO_SetInterruptEdge(iface->irq_port, iface->irq_pin, CY_GPIO_INTR_RISING);
    Cy_GPIO_SetInterruptMask(iface->irq_port, iface->irq_pin, 1u);

    int32_t res = xensiv_bgt60trxx_set_fifo_limit(dev, fifo_limit);
    result = XENSIV_BGT60TRXX_ERROR(res);

    return result;
}


void xensiv_bgt60trxx_mtb_free(xensiv_bgt60trxx_mtb_t* obj)
{
    CY_ASSERT(obj != NULL);

    xensiv_bgt60trxx_mtb_iface_t* iface = &obj->iface;

    Cy_GPIO_SetDrivemode(iface->sel_port, iface->sel_pin, CY_GPIO_DM_ANALOG);
    Cy_GPIO_SetDrivemode(iface->rst_port, iface->rst_pin, CY_GPIO_DM_ANALOG);
    Cy_GPIO_SetDrivemode(iface->irq_port, iface->irq_pin, CY_GPIO_DM_ANALOG);
    Cy_GPIO_SetInterruptEdge(iface->irq_port, iface->irq_pin, CY_GPIO_INTR_DISABLE);
    Cy_GPIO_SetInterruptMask(iface->irq_port, iface->irq_pin, 0u);
}


/*******************************************************************************
 * Platform functions implementation
 ********************************************************************************/
__STATIC_INLINE void spi_set_data_width(CySCB_Type* base, uint32_t data_width)
{
    CY_ASSERT(CY_SCB_SPI_IS_DATA_WIDTH_VALID(data_width));

    CY_REG32_CLR_SET(SCB_TX_CTRL(base),
                     SCB_TX_CTRL_DATA_WIDTH,
                     (uint32_t)data_width - 1U);
    CY_REG32_CLR_SET(SCB_RX_CTRL(base),
                     SCB_RX_CTRL_DATA_WIDTH,
                     (uint32_t)data_width - 1U);
}


int32_t xensiv_bgt60trxx_platform_spi_transfer(void* iface,
                                               uint8_t* tx_data,
                                               uint8_t* rx_data,
                                               uint32_t len)
{
    CY_ASSERT(iface != NULL);
    CY_ASSERT((tx_data != NULL) || (rx_data != NULL));

    const xensiv_bgt60trxx_mtb_iface_t* mtb_iface = iface;

    spi_set_data_width(mtb_iface->scb_inst, 8U);
    Cy_SCB_SetByteMode(mtb_iface->scb_inst, true);
    cy_en_scb_spi_status_t status = Cy_SCB_SPI_Transfer(mtb_iface->scb_inst, tx_data, rx_data, len,
                                                        mtb_iface->spi);
    if (CY_SCB_SPI_SUCCESS == status)
    {
        if (!bento_spi_wait_done(mtb_iface->scb_inst, mtb_iface->spi))
        {
            return XENSIV_BGT60TRXX_STATUS_COM_ERROR;
        }
    }

    return ((CY_SCB_SPI_SUCCESS == status) ?
            XENSIV_BGT60TRXX_STATUS_OK :
            XENSIV_BGT60TRXX_STATUS_COM_ERROR);
}


int32_t xensiv_bgt60trxx_platform_spi_fifo_read(void* iface,
                                                uint16_t* rx_data,
                                                uint32_t len)
{
    CY_ASSERT(iface != NULL);
    CY_ASSERT(rx_data != NULL);

    const xensiv_bgt60trxx_mtb_iface_t* mtb_iface = iface;

    spi_set_data_width(mtb_iface->scb_inst, 12U);
    Cy_SCB_SetByteMode(mtb_iface->scb_inst, false);
    cy_en_scb_spi_status_t status = Cy_SCB_SPI_Transfer(mtb_iface->scb_inst, NULL, rx_data, len,
                                                        mtb_iface->spi);
    if (CY_SCB_SPI_SUCCESS == status)
    {
        if (!bento_spi_wait_done(mtb_iface->scb_inst, mtb_iface->spi))
        {
            return XENSIV_BGT60TRXX_STATUS_COM_ERROR;
        }
    }

    return ((CY_SCB_SPI_SUCCESS == status) ?
            XENSIV_BGT60TRXX_STATUS_OK :
            XENSIV_BGT60TRXX_STATUS_COM_ERROR);
}


void xensiv_bgt60trxx_platform_rst_set(const void* iface, bool val)
{
    CY_ASSERT(iface != NULL);

    const xensiv_bgt60trxx_mtb_iface_t* mtb_iface = iface;

    Cy_GPIO_Write(mtb_iface->rst_port, mtb_iface->rst_pin, val);
}


void xensiv_bgt60trxx_platform_spi_cs_set(const void* iface, bool val)
{
    CY_ASSERT(iface != NULL);

    const xensiv_bgt60trxx_mtb_iface_t* mtb_iface = iface;

    Cy_GPIO_Write(mtb_iface->sel_port, mtb_iface->sel_pin, val);
}


void xensiv_bgt60trxx_platform_delay(uint32_t ms)
{
    (void)Cy_SysLib_Delay(ms);
}


uint32_t xensiv_bgt60trxx_platform_word_reverse(uint32_t x)
{
    return __REV(x);
}


void xensiv_bgt60trxx_platform_assert(bool expr)
{
    CY_ASSERT(expr);
    (void)expr; /* make release build */
}


#endif // ifdef COMPONENT_MTB_HAL
