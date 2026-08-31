/*******************************************************************************
 * File Name: sensor_i2c.c
 *
 * Description: Shared I2C bus driver for on-board sensors (1.8V bus).
 *              Uses SCB0 on P8[0]=SCL, P8[1]=SDA.
 *              BSP configurator provides CYBSP_I2C_CONTROLLER_* macros.
 *
 *******************************************************************************/

#include "sensor_i2c.h"
#include "cybsp.h"
#include "cy_scb_i2c.h"
#include "cy_sysint.h"
#include "cy_sysclk.h"
#include "cycfg_peripherals.h"
#include "cycfg_peripheral_clocks.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include <string.h>

/* I2C context for SCB0 */
static cy_stc_scb_i2c_context_t sensor_i2c_ctx;
static bool sensor_i2c_initialized = false;

/* FreeRTOS mutex for thread-safe I2C bus access */
static SemaphoreHandle_t s_i2c_mutex = NULL;

/* Transfer timeout in microseconds */
#define SENSOR_I2C_TIMEOUT_US   (50000U)

/* ISR for SCB0 I2C */
static void sensor_i2c_isr(void) {
    Cy_SCB_I2C_MasterInterrupt(CYBSP_I2C_CONTROLLER_HW, &sensor_i2c_ctx);
}

/* Wait for I2C master transfer to complete */
static bool sensor_i2c_wait_complete(void) {
    uint32_t timeout = SENSOR_I2C_TIMEOUT_US;
    while (0UL != (CY_SCB_I2C_MASTER_BUSY &
                   Cy_SCB_I2C_MasterGetStatus(CYBSP_I2C_CONTROLLER_HW, &sensor_i2c_ctx))) {
        Cy_SysLib_DelayUs(1);
        if (--timeout == 0) {
            return false;
        }
    }
    uint32_t status = Cy_SCB_I2C_MasterGetStatus(CYBSP_I2C_CONTROLLER_HW, &sensor_i2c_ctx);
    return !(status & CY_SCB_I2C_MASTER_ERR);
}

/* I2C bus recovery — belt and braces, ahead of the SCB bring-up.
 *
 * A slave interrupted mid-byte keeps driving SDA low waiting for the clocks
 * that finish its byte, which holds the bus for everyone. The standard remedy
 * is to take the pins back as GPIO, clock SCL nine times so the slave can
 * finish and release SDA, then frame a STOP.
 *
 * Note this was NOT what fixed the 2026-08-04 Dev Kit failure — that was a
 * driver/hardware desync, see sensor_i2c_resync_if_stuck(). This runs anyway
 * because a genuinely held bus is a real failure mode this driver had no
 * answer for, and clocking an already-idle bus is harmless.
 */
static void sensor_i2c_bus_recover(void) {
    /* The block must be off before the pins can be driven by hand. */
    CYBSP_I2C_CONTROLLER_HW->CTRL &= ~SCB_CTRL_ENABLED_Msk;

    Cy_GPIO_SetHSIOM(GPIO_PRT8, 0U, HSIOM_SEL_GPIO);
    Cy_GPIO_SetHSIOM(GPIO_PRT8, 1U, HSIOM_SEL_GPIO);
    Cy_GPIO_SetDrivemode(GPIO_PRT8, 0U, CY_GPIO_DM_OD_DRIVESLOW);
    Cy_GPIO_SetDrivemode(GPIO_PRT8, 1U, CY_GPIO_DM_OD_DRIVESLOW);

    Cy_GPIO_Set(GPIO_PRT8, 1U);            /* release SDA (open-drain high) */
    Cy_GPIO_Set(GPIO_PRT8, 0U);            /* SCL idle high                 */
    Cy_SysLib_DelayUs(10);

    for (int i = 0; i < 9; i++) {          /* nine clocks frees a held SDA  */
        Cy_GPIO_Clr(GPIO_PRT8, 0U);
        Cy_SysLib_DelayUs(5);
        Cy_GPIO_Set(GPIO_PRT8, 0U);
        Cy_SysLib_DelayUs(5);
    }

    Cy_GPIO_Clr(GPIO_PRT8, 1U);            /* SDA low  while SCL high ...   */
    Cy_SysLib_DelayUs(5);
    Cy_GPIO_Set(GPIO_PRT8, 1U);            /* ... then high = STOP          */
    Cy_SysLib_DelayUs(10);
}

bool sensor_i2c_init(void) {
    if (sensor_i2c_initialized) {
        return true;
    }

    /* Free any slave still holding the bus from a previous, interrupted
     * transfer — see sensor_i2c_bus_recover(). Must precede the SCB pin
     * assignment below, which hands the pins to the peripheral. */
    sensor_i2c_bus_recover();

    /* Explicitly configure I2C pins (BSP may or may not init for CM33_NS) */
    Cy_GPIO_SetHSIOM(GPIO_PRT8, 0U, P8_0_SCB0_I2C_SCL);
    Cy_GPIO_SetHSIOM(GPIO_PRT8, 1U, P8_1_SCB0_I2C_SDA);
    Cy_GPIO_SetDrivemode(GPIO_PRT8, 0U, CY_GPIO_DM_OD_DRIVESLOW);
    Cy_GPIO_SetDrivemode(GPIO_PRT8, 1U, CY_GPIO_DM_OD_DRIVESLOW);

    /* SCB0 (this 1.8V bus) is SHARED on CM33_NS: OPTIGA Trust M also masters it,
     * and its PAL brings SCB0 up at 100 kHz. Cy_SCB_I2C_Init requires the block
     * DISABLED; calling it on an already-enabled SCB0 half-writes the peripheral
     * config on a live block (flips the data rate, reassigns the peri-clock, re-
     * points the ISR) and wedges the OPEN-DRAIN bus for EVERY device on it — the
     * DPS368/SHT40/BMI270 all go unreadable while the I3C BMM350 is untouched,
     * and because this re-runs each boot a power cycle does not recover it. Every
     * other SCB0 initializer on this platform disables before re-init (see
     * COMPONENT_OPTIGA_CYHAL/pal_i2c.c); this one must too. Disabling an already-
     * disabled block is a harmless no-op, so this is safe on every kit/boot path. */
    if (0U != (CYBSP_I2C_CONTROLLER_HW->CTRL & SCB_CTRL_ENABLED_Msk)) {
        CYBSP_I2C_CONTROLLER_HW->CTRL &= ~SCB_CTRL_ENABLED_Msk;
    }

    /* Initialize SCB0 I2C using BSP-provided config */
    cy_en_scb_i2c_status_t result = Cy_SCB_I2C_Init(
        CYBSP_I2C_CONTROLLER_HW, &CYBSP_I2C_CONTROLLER_config, &sensor_i2c_ctx);
    if (result != CY_SCB_I2C_SUCCESS) {
        return false;
    }

    /* Configure SCB0 clock using BSP PeriPclk API (group-aware) */
    Cy_SysClk_PeriPclkAssignDivider(PCLK_SCB0_CLOCK_SCB_EN,
                                     CYBSP_I2C_CONTROLLER_CLK_DIV_HW,
                                     CYBSP_I2C_CONTROLLER_CLK_DIV_NUM);
    Cy_SysClk_PeriPclkSetDivider(
        (en_clk_dst_t)CYBSP_I2C_CONTROLLER_CLK_DIV_GRP_NUM,
        CYBSP_I2C_CONTROLLER_CLK_DIV_HW,
        CYBSP_I2C_CONTROLLER_CLK_DIV_NUM, 9U);
    Cy_SysClk_PeriPclkEnableDivider(
        (en_clk_dst_t)CYBSP_I2C_CONTROLLER_CLK_DIV_GRP_NUM,
        CYBSP_I2C_CONTROLLER_CLK_DIV_HW,
        CYBSP_I2C_CONTROLLER_CLK_DIV_NUM);

    /* Get clock frequency and set I2C data rate */
    uint32_t clk_freq = Cy_SysClk_PeriPclkGetFrequency(
        PCLK_SCB0_CLOCK_SCB_EN, CYBSP_I2C_CONTROLLER_CLK_DIV_HW,
        CYBSP_I2C_CONTROLLER_CLK_DIV_NUM);
    if (clk_freq == 0) {
        clk_freq = 10000000U;  /* Fallback: 100MHz / 10 = 10MHz */
    }
    Cy_SCB_I2C_SetDataRate(CYBSP_I2C_CONTROLLER_HW, 400000U, clk_freq);

    /* Configure and enable interrupt */
    const cy_stc_sysint_t i2c_intr_config = {
        .intrSrc = CYBSP_I2C_CONTROLLER_IRQ,
        .intrPriority = 7UL,
    };
    Cy_SysInt_Init(&i2c_intr_config, &sensor_i2c_isr);
    NVIC_EnableIRQ(CYBSP_I2C_CONTROLLER_IRQ);

    /* Enable I2C */
    Cy_SCB_I2C_Enable(CYBSP_I2C_CONTROLLER_HW);


    /* Create I2C mutex (once) for multi-task thread safety */
    if (s_i2c_mutex == NULL) {
        s_i2c_mutex = xSemaphoreCreateMutex();
    }

    sensor_i2c_initialized = true;
    return true;
}

bool sensor_i2c_lock(uint32_t timeout_ms) {
    if (s_i2c_mutex == NULL) {
        return true; /* Not yet initialized — single-task mode */
    }
    return xSemaphoreTake(s_i2c_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void sensor_i2c_unlock(void) {
    if (s_i2c_mutex != NULL) {
        xSemaphoreGive(s_i2c_mutex);
    }
}

bool sensor_i2c_is_init(void) {
    return sensor_i2c_initialized;
}

/* Recover the master from a context/hardware desync.
 *
 * Observed on the TESAIoT Dev Kit 2026-08-04: MasterGetStatus() reports
 * MASTER_BUSY | MASTER_WR_IN_FIFO while INTR_M_MASK is 0 — the driver context
 * believes a transfer is in flight, but no interrupt source is unmasked, so
 * nothing will ever complete it. CTRL showed the block enabled, VTOR pointed at
 * the RAM table and the NVIC line was enabled with nothing pending, so this is
 * not a wiring or vector problem: the state machine and the hardware simply
 * disagree. Every subsequent transfer is then refused with MASTER_NOT_READY
 * before it reaches the bus, which is why a 112-address scan returned in 11 ms
 * and why a power cycle never helped — sensor_i2c_init() is latched behind
 * sensor_i2c_initialized and never runs a second time.
 *
 * Cy_SCB_I2C_Init is the only thing that rebuilds the context, so clear the
 * latch and run the full bring-up again.
 */
static void sensor_i2c_resync_if_stuck(void) {
    if (!sensor_i2c_initialized) {
        return;
    }
    uint32_t st = Cy_SCB_I2C_MasterGetStatus(CYBSP_I2C_CONTROLLER_HW,
                                             &sensor_i2c_ctx);
    if ((0U != (st & CY_SCB_I2C_MASTER_BUSY)) &&
        (0U == CYBSP_I2C_CONTROLLER_HW->INTR_M_MASK)) {
        Cy_SCB_I2C_Disable(CYBSP_I2C_CONTROLLER_HW, &sensor_i2c_ctx);
        sensor_i2c_initialized = false;
        (void)sensor_i2c_init();
    }
}

bool sensor_i2c_write_reg(uint8_t addr, uint8_t reg, const uint8_t *data, uint16_t len) {
    sensor_i2c_resync_if_stuck();
    /* Build write buffer: [reg_addr, data...] */
    uint8_t buf[33]; /* max 32 bytes data + 1 reg byte */
    if (len > 32) return false;
    buf[0] = reg;
    if (len > 0 && data != NULL) {
        memcpy(&buf[1], data, len);
    }

    cy_stc_scb_i2c_master_xfer_config_t xfer = {
        .slaveAddress = addr,
        .buffer = buf,
        .bufferSize = len + 1,
        .xferPending = false, /* Generate STOP */
    };

    cy_en_scb_i2c_status_t status = Cy_SCB_I2C_MasterWrite(
        CYBSP_I2C_CONTROLLER_HW, &xfer, &sensor_i2c_ctx);
    if (status != CY_SCB_I2C_SUCCESS) {
        return false;
    }
    return sensor_i2c_wait_complete();
}

bool sensor_i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *data, uint16_t len) {
    sensor_i2c_resync_if_stuck();
    /* Phase 1: Write register address (no STOP — repeated start) */
    cy_stc_scb_i2c_master_xfer_config_t write_xfer = {
        .slaveAddress = addr,
        .buffer = &reg,
        .bufferSize = 1,
        .xferPending = true, /* No STOP, repeated START follows */
    };

    cy_en_scb_i2c_status_t status = Cy_SCB_I2C_MasterWrite(
        CYBSP_I2C_CONTROLLER_HW, &write_xfer, &sensor_i2c_ctx);
    if (status != CY_SCB_I2C_SUCCESS) {
        return false;
    }
    if (!sensor_i2c_wait_complete()) {
        return false;
    }

    /* Phase 2: Read data bytes */
    cy_stc_scb_i2c_master_xfer_config_t read_xfer = {
        .slaveAddress = addr,
        .buffer = data,
        .bufferSize = len,
        .xferPending = false, /* Generate STOP */
    };

    status = Cy_SCB_I2C_MasterRead(
        CYBSP_I2C_CONTROLLER_HW, &read_xfer, &sensor_i2c_ctx);
    if (status != CY_SCB_I2C_SUCCESS) {
        return false;
    }
    return sensor_i2c_wait_complete();
}

bool sensor_i2c_write_byte(uint8_t addr, uint8_t reg, uint8_t value) {
    return sensor_i2c_write_reg(addr, reg, &value, 1);
}

bool sensor_i2c_read_byte(uint8_t addr, uint8_t reg, uint8_t *value) {
    return sensor_i2c_read_reg(addr, reg, value, 1);
}

bool sensor_i2c_write_raw(uint8_t addr, const uint8_t *data, uint16_t len) {
    sensor_i2c_resync_if_stuck();
    cy_stc_scb_i2c_master_xfer_config_t xfer = {
        .slaveAddress = addr,
        .buffer = (uint8_t *)data,
        .bufferSize = len,
        .xferPending = false,
    };

    cy_en_scb_i2c_status_t status = Cy_SCB_I2C_MasterWrite(
        CYBSP_I2C_CONTROLLER_HW, &xfer, &sensor_i2c_ctx);
    if (status != CY_SCB_I2C_SUCCESS) {
        return false;
    }
    return sensor_i2c_wait_complete();
}

bool sensor_i2c_read_raw(uint8_t addr, uint8_t *data, uint16_t len) {
    sensor_i2c_resync_if_stuck();
    cy_stc_scb_i2c_master_xfer_config_t xfer = {
        .slaveAddress = addr,
        .buffer = data,
        .bufferSize = len,
        .xferPending = false,
    };

    cy_en_scb_i2c_status_t status = Cy_SCB_I2C_MasterRead(
        CYBSP_I2C_CONTROLLER_HW, &xfer, &sensor_i2c_ctx);
    if (status != CY_SCB_I2C_SUCCESS) {
        return false;
    }
    return sensor_i2c_wait_complete();
}

int sensor_i2c_scan(uint8_t *addrs, int max_addrs) {
    int count = 0;
    uint8_t dummy;

    sensor_i2c_resync_if_stuck();

    for (uint8_t addr = 0x08; addr < 0x78 && count < max_addrs; addr++) {
        cy_stc_scb_i2c_master_xfer_config_t xfer = {
            .slaveAddress = addr,
            .buffer = &dummy,
            .bufferSize = 1,
            .xferPending = false,
        };

        cy_en_scb_i2c_status_t status = Cy_SCB_I2C_MasterRead(
            CYBSP_I2C_CONTROLLER_HW, &xfer, &sensor_i2c_ctx);
        if (status == CY_SCB_I2C_SUCCESS) {
            if (sensor_i2c_wait_complete()) {
                addrs[count++] = addr;
            }
        }
        /* Small delay between scan probes */
        Cy_SysLib_DelayUs(100);
    }
    return count;
}
