/*******************************************************************************
 * File Name: sensor_bmm350.c
 *
 * Description: BMM350 3-axis magnetometer driver for PSoC Edge E84.
 *              Uses I3C PDL API in MIXED_FAST mode (I2C legacy) at address 0x15.
 *              I3C bus on P3[0]=SCL, P3[1]=SDA (CYBSP_I3C_CONTROLLER).
 *              Interrupt-driven transfers with busy-wait completion (50ms timeout).
 *
 *              Only compiled when BSP_HAS_BMM350=1.
 *
 * Reference: BMM350 datasheet (Bosch Sensortec), BMM350_SensorAPI v1.10.0
 *
 *******************************************************************************/

#include "sensor_bmm350.h"
#include "sensor_auto_task.h"
#include "cy_pdl.h"
#include "cy_sysint.h"
#include "cybsp.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

/*******************************************************************************
 * BMM350 Register Definitions (from Bosch SensorAPI bmm350_defs.h)
 *******************************************************************************/
#define BMM350_REG_CHIP_ID              (0x00)
#define BMM350_REG_REV_ID               (0x01)
#define BMM350_REG_ERR_REG              (0x02)
#define BMM350_REG_PAD_CTRL             (0x03)
#define BMM350_REG_PMU_CMD_AGGR_SET     (0x04)
#define BMM350_REG_PMU_CMD_AXIS_EN      (0x05)
#define BMM350_REG_PMU_CMD              (0x06)
#define BMM350_REG_PMU_CMD_STATUS_0     (0x07)
#define BMM350_REG_PMU_CMD_STATUS_1     (0x08)
#define BMM350_REG_INT_CTRL             (0x2E)
#define BMM350_REG_INT_STATUS           (0x30)
#define BMM350_REG_MAG_X_XLSB          (0x31)
#define BMM350_REG_CMD                  (0x7E)

/* Data readout: X(3) + Y(3) + Z(3) + T(3) = 12 bytes starting from 0x31 */
#define BMM350_MAG_DATA_LEN             (12)

/* PMU commands (from Bosch SensorAPI bmm350_defs.h) */
#define BMM350_PMU_CMD_SUSPEND          (0x00)
#define BMM350_PMU_CMD_NORMAL           (0x01)
#define BMM350_PMU_CMD_UPD_OAE          (0x02)
#define BMM350_PMU_CMD_FORCED           (0x03)
#define BMM350_PMU_CMD_FORCED_FAST      (0x04)
#define BMM350_PMU_CMD_FGR              (0x05)  /* Flux Guide Reset */
#define BMM350_PMU_CMD_FGR_FAST         (0x06)
#define BMM350_PMU_CMD_BR               (0x07)  /* Bit Reset */
#define BMM350_PMU_CMD_BR_FAST          (0x08)

/* Delays (microseconds, from Bosch SensorAPI) */
#define BMM350_SUSPEND_TO_NM_DELAY_MS   (38)
#define BMM350_FGR_DELAY_MS             (18)
#define BMM350_BR_DELAY_MS              (14)
#define BMM350_UPD_OAE_DELAY_MS         (1)

/* ODR settings (bits 3:0 of PMU_CMD_AGGR_SET) */
#define BMM350_ODR_400HZ                (0x02)
#define BMM350_ODR_200HZ                (0x03)
#define BMM350_ODR_100HZ                (0x04)
#define BMM350_ODR_50HZ                 (0x05)
#define BMM350_ODR_25HZ                 (0x06)
#define BMM350_ODR_12_5HZ               (0x07)

/* Averaging (bits 5:4 of PMU_CMD_AGGR_SET) */
#define BMM350_AVG_NO_AVG               (0x00)
#define BMM350_AVG_2                    (0x01)
#define BMM350_AVG_4                    (0x02)
#define BMM350_AVG_8                    (0x03)

/* Axis enable (PMU_CMD_AXIS_EN) */
#define BMM350_EN_X                     (0x01)
#define BMM350_EN_Y                     (0x02)
#define BMM350_EN_Z                     (0x04)
#define BMM350_EN_XYZ                   (0x07)

/* PMU status bits */
#define BMM350_PMU_STATUS_NM            (0x01)

/* Soft reset command */
#define BMM350_CMD_SOFTRESET            (0xB6)

/* Sensitivity coefficients (from Bosch SensorAPI, approximate without OTP) */
#define BMM350_SENS_XY                  (14.55f)
#define BMM350_SENS_Z                   (9.0f)

/* I3C transfer timeout (microseconds) — 50ms for slow I2C-over-I3C */
#define BMM350_I3C_TIMEOUT_US           (50000)

/* BMM350 sends 2 dummy bytes before actual register data (both SPI & I2C).
 * Must read len+2 bytes and skip first 2. Per Bosch SensorAPI bmm350.c. */
#define BMM350_DUMMY_BYTES              (2)

/* Maximum retries for chip ID read after soft reset */
#define BMM350_INIT_RETRIES             (5)

/* OTP register addresses and constants */
#define BMM350_REG_OTP_CMD              (0x50)
#define BMM350_REG_OTP_DATA_MSB         (0x52)
#define BMM350_REG_OTP_DATA_LSB         (0x53)
#define BMM350_REG_OTP_STATUS           (0x55)
#define BMM350_OTP_CMD_DIR_READ         (0x20)
#define BMM350_OTP_CMD_PWR_OFF_OTP      (0x80)  /* Power off OTP (REQUIRED before PMU!) */
#define BMM350_OTP_WORD_ADDR_MSK        (0x1F)
#define BMM350_OTP_STATUS_CMD_DONE      (0x01)
#define BMM350_OTP_STATUS_ERROR_MSK     (0xE0)
#define BMM350_OTP_DATA_LENGTH          (32)

/*******************************************************************************
 * I3C State (static — exclusive to BMM350 on this bus)
 *******************************************************************************/
static cy_stc_i3c_context_t bmm_i3c_ctx;
static volatile bool bmm_xfer_done;
static volatile uint32_t bmm_xfer_events;
static bool bmm_i3c_ready = false;
static bool bmm_initialized = false;

/*******************************************************************************
 * Hard Iron Calibration State
 *
 * Tracks min/max of mx, my over time to compute offset (center of circle).
 * The offset is subtracted before atan2 to compensate for PCB magnetic bias.
 * Auto-updates as more data is collected; improves as the user rotates the board.
 *******************************************************************************/
static float s_cal_mx_min =  1e9f;
static float s_cal_mx_max = -1e9f;
static float s_cal_my_min =  1e9f;
static float s_cal_my_max = -1e9f;
static float s_cal_offset_x = 0.0f;
static float s_cal_offset_y = 0.0f;
static uint32_t s_cal_sample_count = 0;
static bool s_cal_valid = false;  /* true after min/max spread > threshold */

/* Minimum spread (uT) before calibration is considered valid.
 * Earth's horizontal field ~20-25 uT at mid-latitudes → full 360° rotation
 * gives ~40-50 uT spread. Require 15 uT = ~half rotation on BOTH axes
 * to prevent premature calibration from partial arcs (which causes 180° jumps). */
#define BMM350_CAL_MIN_SPREAD  (15.0f)

/* Minimum samples before calibration can activate (~2 sec at 25 Hz) */
#define BMM350_CAL_MIN_SAMPLES (50)

/* Moving average filter for heading stability.
 * 10 samples at 25 Hz = 0.4s window — smooth enough to suppress jitter
 * while still responsive to deliberate rotation. */
#define BMM350_HEADING_AVG_N   10
static float s_heading_buf[BMM350_HEADING_AVG_N];
static uint32_t s_heading_idx = 0;
static uint32_t s_heading_count = 0;

/*******************************************************************************
 * I3C Interrupt Handler + Event Callback
 *******************************************************************************/
static void bmm350_i3c_isr(void)
{
    Cy_I3C_Interrupt(CYBSP_I3C_CONTROLLER_HW, &bmm_i3c_ctx);
}

static void bmm350_i3c_event_cb(uint32_t event)
{
    bmm_xfer_events = event;
    bmm_xfer_done = true;
}

/*******************************************************************************
 * I3C Transfer Helpers (synchronous via busy-wait with timeout)
 *******************************************************************************/

/* Wait for transfer completion */
static bool bmm350_wait_xfer(void)
{
    for (uint32_t i = 0; i < BMM350_I3C_TIMEOUT_US; i++) {
        if (bmm_xfer_done) {
            /* Check for completion event (write or read) */
            if (bmm_xfer_events & (CY_I3C_CONTROLLER_WR_CMPLT_EVENT |
                                    CY_I3C_CONTROLLER_RD_CMPLT_EVENT)) {
                return true;
            }
            /* Error event occurred */
            return false;
        }
        Cy_SysLib_DelayUs(1);
    }
    return false; /* Timeout */
}

/* Initialize I3C controller in PURE mode with SETAASA for BMM350.
 *
 * Root cause analysis (2026-03-13):
 * - BSP uses CY_I3C_BUS_PURE mode — this is CORRECT for PSoC Edge.
 * - MIXED_FAST mode reads return 0x00 on both CM55 and CM33 (BSP inits
 *   I3C in PURE mode during cybsp_init; re-init to MIXED_FAST fails).
 * - Solution: use PURE mode + SETAASA to assign BMM350's static address
 *   (0x15) as its I3C dynamic address. All subsequent R/W use I3C SDR.
 * - After soft reset, BMM350 reverts to I2C mode → must re-do SETAASA.
 */
static bool bmm350_i3c_init(void)
{
    /* Full hardware reset if retrying */
    if (bmm_i3c_ready) {
#ifdef BOOT_VERBOSE
        printf("[BMM350] I3C re-init: Disable + reset\r\n");
#endif
        NVIC_DisableIRQ(CYBSP_I3C_CONTROLLER_IRQ);
        Cy_I3C_Disable(CYBSP_I3C_CONTROLLER_HW, &bmm_i3c_ctx);
        bmm_i3c_ready = false;
        Cy_SysLib_Delay(50);
    }
    memset(&bmm_i3c_ctx, 0, sizeof(bmm_i3c_ctx));

    /* a. Enable I3C peripheral group (clock gating) */
    Cy_SysClk_PeriGroupSlaveInit(CY_MMIO_I3C_PERI_NR,
                                  CY_MMIO_I3C_GROUP_NR,
                                  CY_MMIO_I3C_SLAVE_NR,
                                  CY_MMIO_I3C_CLK_HF_NR);

    /* b. Assign clock divider to I3C peripheral */
    Cy_SysClk_PeriphAssignDivider(PCLK_I3C_CLOCK_I3C_EN,
                                   CY_SYSCLK_DIV_8_BIT, 0U);

    /* c. Configure I3C clock divider (8-bit #0, value=7 → 12.5 MHz) */
    Cy_SysClk_PeriPclkDisableDivider(
        (en_clk_dst_t)CYBSP_I3C_CONTROLLER_CLK_DIV_GRP_NUM,
        CY_SYSCLK_DIV_8_BIT, 0U);
    Cy_SysClk_PeriPclkSetDivider(
        (en_clk_dst_t)CYBSP_I3C_CONTROLLER_CLK_DIV_GRP_NUM,
        CY_SYSCLK_DIV_8_BIT, 0U, 7U);
    Cy_SysClk_PeriPclkEnableDivider(
        (en_clk_dst_t)CYBSP_I3C_CONTROLLER_CLK_DIV_GRP_NUM,
        CY_SYSCLK_DIV_8_BIT, 0U);

    /* d. Configure I3C pins: P3[0]=SCL, P3[1]=SDA */
    Cy_GPIO_Pin_Init(CYBSP_I3C_SCL_PORT, CYBSP_I3C_SCL_PIN,
                     &CYBSP_I3C_SCL_config);
    Cy_GPIO_Pin_Init(CYBSP_I3C_SDA_PORT, CYBSP_I3C_SDA_PIN,
                     &CYBSP_I3C_SDA_config);
    Cy_SysLib_DelayUs(100);

    /* 1. Init I3C in PURE mode (use BSP config as-is) */
    cy_en_i3c_status_t st = Cy_I3C_Init(CYBSP_I3C_CONTROLLER_HW,
                                         &CYBSP_I3C_CONTROLLER_config,
                                         &bmm_i3c_ctx);
    if (st != CY_I3C_SUCCESS) {
        printf("[BMM350] I3C Init failed: 0x%08X\r\n", (unsigned int)st);
        return false;
    }
#ifdef BOOT_VERBOSE
    printf("[BMM350] I3C Init OK (PURE)\r\n");
#endif

    /* 2. Enable I3C controller (NO IRQ yet — avoid ISR storm from SETAASA) */
    Cy_I3C_Enable(CYBSP_I3C_CONTROLLER_HW, &bmm_i3c_ctx);
    Cy_SysLib_Delay(50);

    /* 3. SETAASA: assign BMM350 static address (0x15) as dynamic address.
     *    This is how PURE I3C talks to an I2C legacy device like BMM350. */
    cy_stc_i3c_device_t bmm_dev;
    memset(&bmm_dev, 0, sizeof(bmm_dev));
    bmm_dev.staticAddress  = BMM350_I2C_ADDR;
    bmm_dev.dynamicAddress = 0;
    st = Cy_I3C_SetAASA(CYBSP_I3C_CONTROLLER_HW, &bmm_dev, 1, &bmm_i3c_ctx);
    if (st != CY_I3C_SUCCESS) {
        printf("[BMM350] SETAASA failed: 0x%08X\r\n", (unsigned int)st);
        return false;
    }
#ifdef BOOT_VERBOSE
    printf("[BMM350] SETAASA OK, dynAddr=0x%02X\r\n", bmm_dev.dynamicAddress);
#endif

    /* Wait for bus to become IDLE after SETAASA CCC.
     * PS bits [17:16] = CM_TFR_STS: 0 = idle */
    for (uint32_t w = 0; w < 500; w++) {
        uint32_t ps = CYBSP_I3C_CONTROLLER_HW->PRESENT_STATE;
        if ((ps & 0x00030000UL) == 0) {
            break;
        }
        Cy_SysLib_DelayUs(100);
    }

    /* 4. Register ISR after SETAASA completes */
    Cy_I3C_RegisterEventCallback(CYBSP_I3C_CONTROLLER_HW,
                                  bmm350_i3c_event_cb, &bmm_i3c_ctx);
    cy_stc_sysint_t i3c_irq_cfg = {
        .intrSrc = CYBSP_I3C_CONTROLLER_IRQ,
        .intrPriority = 3
    };
    Cy_SysInt_Init(&i3c_irq_cfg, bmm350_i3c_isr);
    NVIC_ClearPendingIRQ(CYBSP_I3C_CONTROLLER_IRQ);
    NVIC_EnableIRQ(CYBSP_I3C_CONTROLLER_IRQ);
    bmm_xfer_done = false;
    bmm_xfer_events = 0;

    bmm_i3c_ready = true;
    return true;
}

/* Write register(s): [reg_addr, data0, data1, ...] */
static bool bmm350_write_reg(uint8_t reg, const uint8_t *data, uint8_t len)
{
    uint8_t buf[16];
    if (len > 14) return false;

    buf[0] = reg;
    if (data != NULL && len > 0) {
        memcpy(&buf[1], data, len);
    }

    cy_stc_i3c_controller_xfer_config_t cfg = {
        .targetAddress = BMM350_I2C_ADDR,
        .buffer = buf,
        .bufferSize = (uint32_t)(1 + len),
        .toc = true
    };

    bmm_xfer_done = false;
    bmm_xfer_events = 0;
    cy_en_i3c_status_t st = Cy_I3C_ControllerWrite(
        CYBSP_I3C_CONTROLLER_HW, &cfg, &bmm_i3c_ctx);
    if (st != CY_I3C_SUCCESS) {
        return false;
    }
    if (!bmm350_wait_xfer()) return false;
    Cy_SysLib_DelayUs(500);  /* Bus settling time (I3C SDR needs margin) */
    return true;
}

/* Write single byte to register */
static bool bmm350_write_reg8(uint8_t reg, uint8_t val)
{
    return bmm350_write_reg(reg, &val, 1);
}

/* Read register(s): write reg_addr, then read (len + BMM350_DUMMY_BYTES) bytes,
 * skip first 2 dummy bytes, copy actual data to caller's buffer.
 * Per Bosch SensorAPI: BMM350 always sends 2 dummy bytes before register data. */
static bool bmm350_read_reg(uint8_t reg, uint8_t *data, uint8_t len)
{
    uint8_t temp_buf[32]; /* Max read: 14 data + 2 dummy = 16 */
    uint32_t total_len = (uint32_t)len + BMM350_DUMMY_BYTES;
    if (total_len > sizeof(temp_buf)) return false;

    memset(temp_buf, 0xAA, total_len);

    /* Step 1: Write register address */
    cy_stc_i3c_controller_xfer_config_t w_cfg = {
        .targetAddress = BMM350_I2C_ADDR,
        .buffer = &reg,
        .bufferSize = 1,
        .toc = true
    };

    bmm_xfer_done = false;
    bmm_xfer_events = 0;
    cy_en_i3c_status_t st = Cy_I3C_ControllerWrite(
        CYBSP_I3C_CONTROLLER_HW, &w_cfg, &bmm_i3c_ctx);
    if (st != CY_I3C_SUCCESS) return false;
    if (!bmm350_wait_xfer()) return false;
    if (bmm_xfer_events & 0x04) return false; /* NAK */

    /* Inter-transfer gap: I3C SDR needs settling time between write and read */
    Cy_SysLib_DelayUs(500);

    /* Step 2: Read (len + 2 dummy) bytes */
    cy_stc_i3c_controller_xfer_config_t r_cfg = {
        .targetAddress = BMM350_I2C_ADDR,
        .buffer = temp_buf,
        .bufferSize = total_len,
        .toc = true
    };

    bmm_xfer_done = false;
    bmm_xfer_events = 0;
    st = Cy_I3C_ControllerRead(
        CYBSP_I3C_CONTROLLER_HW, &r_cfg, &bmm_i3c_ctx);
    if (st != CY_I3C_SUCCESS) return false;
    if (!bmm350_wait_xfer()) return false;

    /* Skip 2 dummy bytes, copy actual data */
    memcpy(data, &temp_buf[BMM350_DUMMY_BYTES], len);
    return true;
}

/*******************************************************************************
 * Public API
 *******************************************************************************/

bool bmm350_init(void)
{
    if (bmm_initialized) {
        return true;
    }

    /* 1. Initialize I3C bus */
#ifdef BOOT_VERBOSE
    printf("[BMM350] init: step 1 — I3C bus init\r\n");
#endif
    if (!bmm350_i3c_init()) {
        printf("[BMM350] FAIL: I3C bus init failed\r\n");
        return false;
    }
#ifdef BOOT_VERBOSE
    printf("[BMM350] init: I3C bus OK\r\n");
#endif

    /* 2. Soft reset — triggers OTP re-boot.
     *    Per Bosch SensorAPI: bmm350_soft_reset() / default_power_mode()
     *    IMPORTANT: Soft reset reverts BMM350 from I3C SDR back to I2C mode.
     *    Must re-do SETAASA after reset to restore I3C communication. */
    (void)bmm350_write_reg8(BMM350_REG_CMD, BMM350_CMD_SOFTRESET);
    Cy_SysLib_Delay(24); /* BMM350_SOFT_RESET_DELAY = 24ms */

    /* Re-SETAASA: BMM350 reverted to I2C mode after soft reset */
    {
        cy_stc_i3c_device_t bmm_dev;
        memset(&bmm_dev, 0, sizeof(bmm_dev));
        bmm_dev.staticAddress  = BMM350_I2C_ADDR;
        bmm_dev.dynamicAddress = 0;
        cy_en_i3c_status_t st2 = Cy_I3C_SetAASA(CYBSP_I3C_CONTROLLER_HW,
                                                   &bmm_dev, 1, &bmm_i3c_ctx);
        if (st2 != CY_I3C_SUCCESS) {
            printf("[BMM350] FAIL: Re-SETAASA after reset: 0x%08X\r\n",
                   (unsigned int)st2);
            return false;
        }
        /* Wait for bus idle after SETAASA */
        for (uint32_t w = 0; w < 500; w++) {
            uint32_t ps = CYBSP_I3C_CONTROLLER_HW->PRESENT_STATE;
            if ((ps & 0x00030000UL) == 0) break;
            Cy_SysLib_DelayUs(100);
        }
#ifdef BOOT_VERBOSE
        printf("[BMM350] init: Re-SETAASA OK after soft reset\r\n");
#endif
    }

    /* 3. Verify chip ID */
    uint8_t chip_id = 0;
    bool id_ok = false;
    for (int retry = 0; retry < BMM350_INIT_RETRIES; retry++) {
        if (bmm350_read_reg(BMM350_REG_CHIP_ID, &chip_id, 1) &&
            chip_id == BMM350_CHIP_ID_VALUE) {
            id_ok = true;
            break;
        }
#ifdef BOOT_VERBOSE
        printf("[BMM350] init: chip ID retry %d — got 0x%02X\r\n", retry, chip_id);
#endif
        Cy_SysLib_Delay(10);
    }
    if (!id_ok) {
        printf("[BMM350] FAIL: chip ID=0x%02X (expected 0x%02X)\r\n",
               chip_id, BMM350_CHIP_ID_VALUE);
        return false;
    }
#ifdef BOOT_VERBOSE
    printf("[BMM350] init: chip ID OK (0x%02X)\r\n", chip_id);
#endif

    /* 4. Check OTP boot error */
    uint8_t err_reg = 0;
    (void)bmm350_read_reg(BMM350_REG_ERR_REG, &err_reg, 1);

    /* 5. OTP dump — read all 32 OTP words (REQUIRED before PMU commands).
     *    Per Bosch SensorAPI: bmm350_otp_dump_after_boot() */
    {
        uint16_t otp_data[BMM350_OTP_DATA_LENGTH];
        bool otp_ok = true;
        for (int i = 0; i < BMM350_OTP_DATA_LENGTH && otp_ok; i++) {
            uint8_t otp_cmd = BMM350_OTP_CMD_DIR_READ |
                              ((uint8_t)i & BMM350_OTP_WORD_ADDR_MSK);
            if (!bmm350_write_reg8(BMM350_REG_OTP_CMD, otp_cmd)) {
                otp_ok = false;
                break;
            }
            uint8_t otp_status = 0;
            int poll = 0;
            do {
                Cy_SysLib_DelayUs(300);
                (void)bmm350_read_reg(BMM350_REG_OTP_STATUS, &otp_status, 1);
                if (otp_status & BMM350_OTP_STATUS_ERROR_MSK) {
                    otp_ok = false;
                    break;
                }
            } while (!(otp_status & BMM350_OTP_STATUS_CMD_DONE) && ++poll < 20);
            if (!otp_ok) break;

            uint8_t msb = 0, lsb = 0;
            (void)bmm350_read_reg(BMM350_REG_OTP_DATA_MSB, &msb, 1);
            (void)bmm350_read_reg(BMM350_REG_OTP_DATA_LSB, &lsb, 1);
            otp_data[i] = ((uint16_t)msb << 8) | lsb;
        }
        (void)otp_data; /* OTP data used for host-side compensation (future) */
        if (!otp_ok) return false;
    }

    /* 6. Power off OTP (CRITICAL — Bosch API does this BEFORE any PMU command).
     *    Without this, PMU commands return cmd_is_illegal! */
    if (!bmm350_write_reg8(BMM350_REG_OTP_CMD, BMM350_OTP_CMD_PWR_OFF_OTP)) {
        return false;
    }
    Cy_SysLib_DelayUs(500); /* Let OTP power-down settle */

    /* 7. Magnetic reset from SUSPEND (BEFORE config/NM).
     *    Per Bosch SensorAPI: bmm350_magnetic_reset_and_wait()
     *    Sensor is already in SUSPEND after soft reset. */

    /* 7a. Bit Reset (BR) */
    (void)bmm350_write_reg8(BMM350_REG_PMU_CMD, BMM350_PMU_CMD_BR);
    Cy_SysLib_Delay(BMM350_BR_DELAY_MS);

    /* 7b. Flux Guide Reset (FGR) */
    (void)bmm350_write_reg8(BMM350_REG_PMU_CMD, BMM350_PMU_CMD_FGR);
    Cy_SysLib_Delay(BMM350_FGR_DELAY_MS);

    /* 8. Configure ODR (25Hz) + averaging (4x).
     *    Per Bosch SensorAPI: bmm350_set_odr_performance() */
    uint8_t aggr = (uint8_t)((BMM350_AVG_4 << 4) | BMM350_ODR_25HZ);
    (void)bmm350_write_reg8(BMM350_REG_PMU_CMD_AGGR_SET, aggr);

    /* 9. Apply settings: UPD_OAE */
    (void)bmm350_write_reg8(BMM350_REG_PMU_CMD, BMM350_PMU_CMD_UPD_OAE);
    Cy_SysLib_Delay(BMM350_UPD_OAE_DELAY_MS);

    /* 10. Enable X, Y, Z axes */
    (void)bmm350_write_reg8(BMM350_REG_PMU_CMD_AXIS_EN, BMM350_EN_XYZ);

    /* 11. Enter Normal measurement mode */
    (void)bmm350_write_reg8(BMM350_REG_PMU_CMD, BMM350_PMU_CMD_NORMAL);
    Cy_SysLib_Delay(BMM350_SUSPEND_TO_NM_DELAY_MS);

    bmm_initialized = true;
    return true;
}

bool bmm350_reinit(void)
{
    /* Full I3C + BMM350 re-initialization for error recovery */
    NVIC_DisableIRQ(CYBSP_I3C_CONTROLLER_IRQ);
    Cy_I3C_Disable(CYBSP_I3C_CONTROLLER_HW, &bmm_i3c_ctx);
    bmm_i3c_ready = false;
    bmm_initialized = false;
    Cy_SysLib_Delay(50);
    return bmm350_init();
}

bool bmm350_read_chip_id(uint8_t *chip_id)
{
    if (!bmm_initialized && !bmm350_init()) {
        return false;
    }
    return bmm350_read_reg(BMM350_REG_CHIP_ID, chip_id, 1);
}

/* Debug: force re-init + read raw register values + test forced mode */
void bmm350_debug_read(void)
{
    /* CRITICAL: Pause auto-task to avoid I3C bus contention.
     * Without this, auto_push_bmm350() may be mid-transfer when we re-init,
     * corrupting the I3C controller state and freezing ALL sensors. */
    bool was_running = sensor_auto_is_running();
    if (was_running) {
        sensor_auto_stop();
        Cy_SysLib_Delay(200); /* Wait for auto-task to suspend */
    }

    /* Force re-init to see step-by-step PMU status debug output */
    if (bmm_initialized) {
#ifdef BOOT_VERBOSE
        printf("BMM350: forcing re-init (bmm_initialized=false)\r\n");
#endif
        bmm_initialized = false;
        /* Keep bmm_i3c_ready=true so we don't re-init the I3C bus */
    }
    if (!bmm350_init()) {
#ifdef BOOT_VERBOSE
        printf("BMM350: init FAILED!\r\n");
#endif
        goto done;
    }

    /* 1. Read PMU status registers */
    uint8_t pmu_st0 = 0xEE, pmu_st1 = 0xEE;
    bmm350_read_reg(BMM350_REG_PMU_CMD_STATUS_0, &pmu_st0, 1);
    bmm350_read_reg(BMM350_REG_PMU_CMD_STATUS_1, &pmu_st1, 1);
    printf("PMU_STATUS_0=0x%02X  PMU_STATUS_1=0x%02X\r\n", pmu_st0, pmu_st1);

    /* 2. Read-back ODR/AVG and axis enable */
    uint8_t aggr = 0xEE, axis = 0xEE;
    bmm350_read_reg(BMM350_REG_PMU_CMD_AGGR_SET, &aggr, 1);
    bmm350_read_reg(BMM350_REG_PMU_CMD_AXIS_EN, &axis, 1);
    printf("AGGR_SET=0x%02X  AXIS_EN=0x%02X\r\n", aggr, axis);

    /* 3. Read raw mag data in Normal mode */
    uint8_t data[BMM350_MAG_DATA_LEN];
    memset(data, 0xAA, sizeof(data));
    bool ok = bmm350_read_reg(BMM350_REG_MAG_X_XLSB, data, BMM350_MAG_DATA_LEN);
    printf("Normal: ok=%d  raw=", ok);
    for (int i = 0; i < BMM350_MAG_DATA_LEN; i++) {
        printf("%02X ", data[i]);
    }
    printf("\r\n");

    /* 4. Try FORCED mode: trigger single measurement */
    bmm350_write_reg8(BMM350_REG_PMU_CMD, BMM350_PMU_CMD_FORCED);
    Cy_SysLib_Delay(50); /* Wait for measurement */

    memset(data, 0xAA, sizeof(data));
    ok = bmm350_read_reg(BMM350_REG_MAG_X_XLSB, data, BMM350_MAG_DATA_LEN);
    printf("Forced: ok=%d  raw=", ok);
    for (int i = 0; i < BMM350_MAG_DATA_LEN; i++) {
        printf("%02X ", data[i]);
    }
    printf("\r\n");

    /* 5. RAW I3C read: show ALL bytes including "dummy" bytes.
     *    Read 16 bytes from reg 0x31 (no dummy skip) to see what's actually on bus */
    {
        uint8_t raw[16];
        memset(raw, 0xBB, sizeof(raw));

        /* Write register address */
        uint8_t reg_addr = BMM350_REG_MAG_X_XLSB;
        cy_stc_i3c_controller_xfer_config_t w_cfg = {
            .targetAddress = BMM350_I2C_ADDR,
            .buffer = &reg_addr,
            .bufferSize = 1,
            .toc = true
        };
        bmm_xfer_done = false;
        bmm_xfer_events = 0;
        Cy_I3C_ControllerWrite(CYBSP_I3C_CONTROLLER_HW, &w_cfg, &bmm_i3c_ctx);
        bmm350_wait_xfer();

        /* Read 16 raw bytes (NO dummy skip) */
        cy_stc_i3c_controller_xfer_config_t r_cfg = {
            .targetAddress = BMM350_I2C_ADDR,
            .buffer = raw,
            .bufferSize = 16,
            .toc = true
        };
        bmm_xfer_done = false;
        bmm_xfer_events = 0;
        Cy_I3C_ControllerRead(CYBSP_I3C_CONTROLLER_HW, &r_cfg, &bmm_i3c_ctx);
        bool rok = bmm350_wait_xfer();

        printf("RAW bus (16B from 0x31): ok=%d  ", rok);
        for (int i = 0; i < 16; i++) {
            printf("%02X ", raw[i]);
        }
        printf("\r\n");
    }

    /* 6. RAW read from chip_id reg (to compare dummy byte pattern) */
    {
        uint8_t raw[6];
        memset(raw, 0xBB, sizeof(raw));

        uint8_t reg_addr = BMM350_REG_CHIP_ID;
        cy_stc_i3c_controller_xfer_config_t w_cfg = {
            .targetAddress = BMM350_I2C_ADDR,
            .buffer = &reg_addr,
            .bufferSize = 1,
            .toc = true
        };
        bmm_xfer_done = false;
        bmm_xfer_events = 0;
        Cy_I3C_ControllerWrite(CYBSP_I3C_CONTROLLER_HW, &w_cfg, &bmm_i3c_ctx);
        bmm350_wait_xfer();

        cy_stc_i3c_controller_xfer_config_t r_cfg = {
            .targetAddress = BMM350_I2C_ADDR,
            .buffer = raw,
            .bufferSize = 6,
            .toc = true
        };
        bmm_xfer_done = false;
        bmm_xfer_events = 0;
        Cy_I3C_ControllerRead(CYBSP_I3C_CONTROLLER_HW, &r_cfg, &bmm_i3c_ctx);
        bool rok = bmm350_wait_xfer();

        printf("RAW bus (6B from 0x00): ok=%d  ", rok);
        for (int i = 0; i < 6; i++) {
            printf("%02X ", raw[i]);
        }
        printf("\r\n");
    }

    /* Restore Normal mode */
    bmm350_write_reg8(BMM350_REG_PMU_CMD, BMM350_PMU_CMD_NORMAL);
    Cy_SysLib_Delay(BMM350_SUSPEND_TO_NM_DELAY_MS);

done:
    /* Resume auto-task if it was running before debug */
    if (was_running) {
        sensor_auto_start();
    }
}

bool bmm350_read_xyz(float *mx, float *my, float *mz)
{
    if (!bmm_initialized && !bmm350_init()) {
        return false;
    }

    /* Read 12 bytes: X(3) + Y(3) + Z(3) + Temp(3) */
    uint8_t data[BMM350_MAG_DATA_LEN];
    if (!bmm350_read_reg(BMM350_REG_MAG_X_XLSB, data, BMM350_MAG_DATA_LEN)) {
        return false;
    }

    /* Unpack 24-bit signed values (little-endian, MSB is sign-extended).
     * Data format from Bosch SensorAPI bmm350.c:
     *   raw = (int8_t)data[2] << 16 | data[1] << 8 | data[0]
     */
    int32_t raw_x = ((int32_t)((int8_t)data[2]) << 16) |
                    ((int32_t)data[1] << 8) | data[0];
    int32_t raw_y = ((int32_t)((int8_t)data[5]) << 16) |
                    ((int32_t)data[4] << 8) | data[3];
    int32_t raw_z = ((int32_t)((int8_t)data[8]) << 16) |
                    ((int32_t)data[7] << 8) | data[6];

    /* Convert to micro-Tesla (approximate, without OTP calibration).
     * Coefficients from Bosch SensorAPI: X/Y = 14.55, Z = 9.0
     * For heading calculation (atan2), approximate values are sufficient. */
    *mx = (float)raw_x / BMM350_SENS_XY;
    *my = (float)raw_y / BMM350_SENS_XY;
    *mz = (float)raw_z / BMM350_SENS_Z;

    return true;
}

/*******************************************************************************
 * bmm350_cal_update — Feed raw mx, my to update hard iron calibration
 *******************************************************************************/
void bmm350_cal_update(float mx, float my)
{
    if (mx < s_cal_mx_min) s_cal_mx_min = mx;
    if (mx > s_cal_mx_max) s_cal_mx_max = mx;
    if (my < s_cal_my_min) s_cal_my_min = my;
    if (my > s_cal_my_max) s_cal_my_max = my;
    s_cal_sample_count++;

    /* Recompute offset every 10 samples, but only after enough data collected.
     * Require BOTH axes (&&) to have sufficient spread — prevents premature
     * calibration from small arcs that produce wildly wrong offsets. */
    if (s_cal_sample_count >= BMM350_CAL_MIN_SAMPLES &&
        (s_cal_sample_count % 10) == 0) {
        float spread_x = s_cal_mx_max - s_cal_mx_min;
        float spread_y = s_cal_my_max - s_cal_my_min;
        if (spread_x > BMM350_CAL_MIN_SPREAD &&
            spread_y > BMM350_CAL_MIN_SPREAD) {
            s_cal_offset_x = (s_cal_mx_max + s_cal_mx_min) / 2.0f;
            s_cal_offset_y = (s_cal_my_max + s_cal_my_min) / 2.0f;
            s_cal_valid = true;
        }
    }
}

/*******************************************************************************
 * bmm350_cal_reset — Reset calibration (e.g. for fresh calibration cycle)
 *******************************************************************************/
void bmm350_cal_reset(void)
{
    s_cal_mx_min =  1e9f;
    s_cal_mx_max = -1e9f;
    s_cal_my_min =  1e9f;
    s_cal_my_max = -1e9f;
    s_cal_offset_x = 0.0f;
    s_cal_offset_y = 0.0f;
    s_cal_sample_count = 0;
    s_cal_valid = false;
}

/*******************************************************************************
 * bmm350_cal_get_offsets — Get current calibration offsets
 *******************************************************************************/
void bmm350_cal_get_offsets(float *offset_x, float *offset_y, bool *valid)
{
    if (offset_x) *offset_x = s_cal_offset_x;
    if (offset_y) *offset_y = s_cal_offset_y;
    if (valid)    *valid    = s_cal_valid;
}

/*******************************************************************************
 * bmm350_heading_from_xy — Compute calibrated heading from mx, my
 *******************************************************************************/
float bmm350_heading_from_xy(float mx, float my)
{
    /* Apply hard iron offset compensation ONLY when calibration is valid.
     * Before calibration, use raw values — heading will have some bias but
     * won't suffer from the catastrophic 180° jumps caused by bad offsets. */
    float cx = s_cal_valid ? (mx - s_cal_offset_x) : mx;
    float cy = s_cal_valid ? (my - s_cal_offset_y) : my;

    /* Compass heading: atan2(east, north) gives 0=N, 90=E, 180=S, 270=W.
     * BMM350 on this board: +X = East, +Y = North (typical orientation).
     * So heading = atan2(cx, cy) directly gives compass bearing. */
    float heading_rad = atan2f(cx, cy);
    float heading_deg = heading_rad * (180.0f / 3.14159265f);

    /* Robust normalize to [0, 360) */
    heading_deg = fmodf(heading_deg, 360.0f);
    if (heading_deg < 0.0f) heading_deg += 360.0f;

    /* Moving average filter to suppress noise-induced jumps.
     * Use circular mean (sin/cos averaging) to handle 0/360 wrap. */
    float rad = heading_deg * (3.14159265f / 180.0f);
    s_heading_buf[s_heading_idx] = rad;
    s_heading_idx = (s_heading_idx + 1) % BMM350_HEADING_AVG_N;
    if (s_heading_count < BMM350_HEADING_AVG_N) s_heading_count++;

    float sum_sin = 0.0f, sum_cos = 0.0f;
    for (uint32_t i = 0; i < s_heading_count; i++) {
        sum_sin += sinf(s_heading_buf[i]);
        sum_cos += cosf(s_heading_buf[i]);
    }
    float avg_rad = atan2f(sum_sin, sum_cos);
    float avg_deg = avg_rad * (180.0f / 3.14159265f);
    avg_deg = fmodf(avg_deg, 360.0f);
    if (avg_deg < 0.0f) avg_deg += 360.0f;

    return avg_deg;
}

bool bmm350_read_heading(float *heading)
{
    float mx, my, mz;
    if (!bmm350_read_xyz(&mx, &my, &mz)) {
        return false;
    }

    /* Feed calibration tracker */
    bmm350_cal_update(mx, my);

    *heading = bmm350_heading_from_xy(mx, my);

    /* Normalize to [0, 360) — already done inside heading_from_xy,
     * but ensure no edge case from float rounding */
    if (*heading < 0.0f) {
        *heading += 360.0f;
    }
    if (*heading >= 360.0f) {
        *heading -= 360.0f;
    }

    return true;
}

/*******************************************************************************
 * bmm350_diagnose — Full I3C reset + MIXED_FAST mode diagnostic
 *
 * Key changes from previous version:
 * 1. Attach I2C device BEFORE Cy_I3C_Enable() (populate DAT first)
 * 2. No probe writes in polling loop (they corrupted context->state)
 * 3. Record context->state at each step for debugging
 * 4. Force context->state = IDLE + Resume before chip ID read
 * 5. Read PRESENT_STATE hardware register for diagnostics
 *******************************************************************************/
int bmm350_diagnose(bmm350_diag_t *diag)
{
    memset(diag, 0, sizeof(*diag));
    diag->chip_id = 0xFF;

    /* Pause auto_task to avoid I3C bus contention */
    bool was_running = sensor_auto_is_running();
    if (was_running) {
        sensor_auto_stop();
        Cy_SysLib_Delay(200);
    }

    int result;

    /* ---- Full hardware reset of I3C controller ---- */
    NVIC_DisableIRQ(CYBSP_I3C_CONTROLLER_IRQ);
    Cy_I3C_Disable(CYBSP_I3C_CONTROLLER_HW, &bmm_i3c_ctx);
    bmm_i3c_ready = false;
    bmm_initialized = false;
    Cy_SysLib_Delay(50);

    /* Clock + pin setup (idempotent) */
    Cy_SysClk_PeriGroupSlaveInit(CY_MMIO_I3C_PERI_NR,
                                  CY_MMIO_I3C_GROUP_NR,
                                  CY_MMIO_I3C_SLAVE_NR,
                                  CY_MMIO_I3C_CLK_HF_NR);
    Cy_SysClk_PeriphAssignDivider(PCLK_I3C_CLOCK_I3C_EN,
                                   CY_SYSCLK_DIV_8_BIT, 0U);
    Cy_SysClk_PeriPclkDisableDivider(
        (en_clk_dst_t)CYBSP_I3C_CONTROLLER_CLK_DIV_GRP_NUM,
        CY_SYSCLK_DIV_8_BIT, 0U);
    Cy_SysClk_PeriPclkSetDivider(
        (en_clk_dst_t)CYBSP_I3C_CONTROLLER_CLK_DIV_GRP_NUM,
        CY_SYSCLK_DIV_8_BIT, 0U, 7U);
    Cy_SysClk_PeriPclkEnableDivider(
        (en_clk_dst_t)CYBSP_I3C_CONTROLLER_CLK_DIV_GRP_NUM,
        CY_SYSCLK_DIV_8_BIT, 0U);
    Cy_GPIO_Pin_Init(CYBSP_I3C_SCL_PORT, CYBSP_I3C_SCL_PIN,
                     &CYBSP_I3C_SCL_config);
    Cy_GPIO_Pin_Init(CYBSP_I3C_SDA_PORT, CYBSP_I3C_SDA_PIN,
                     &CYBSP_I3C_SDA_config);
    Cy_SysLib_Delay(10);

    /* MIXED_FAST mode config */
    cy_stc_i3c_config_t mixed_cfg = CYBSP_I3C_CONTROLLER_config;
    mixed_cfg.i3cBusMode = CY_I3C_BUS_MIXED_FAST;
    mixed_cfg.i2cFMPlusLowCnt = 7U;
    mixed_cfg.i2cFMPlusHighCnt = 4U;
    mixed_cfg.i2cFMLowCnt = 17U;
    mixed_cfg.i2cFMHighCnt = 8U;

    /* Step 1: Init */
    memset(&bmm_i3c_ctx, 0, sizeof(bmm_i3c_ctx));
    cy_en_i3c_status_t st = Cy_I3C_Init(
        CYBSP_I3C_CONTROLLER_HW, &mixed_cfg, &bmm_i3c_ctx);
    diag->init_st = (uint32_t)st;
    diag->state_post_init = bmm_i3c_ctx.state; /* Expect CY_I3C_IDLE=0x10000000 */

    if (st != CY_I3C_SUCCESS) {
        diag->step = 1;
        result = 1;
        goto done;
    }

    /* Step 2: Attach I2C device BEFORE Enable (populate DAT first) */
    cy_stc_i2c_device_t i2c_dev;
    memset(&i2c_dev, 0, sizeof(i2c_dev));
    i2c_dev.staticAddress = BMM350_I2C_ADDR;
    i2c_dev.lvr = 0x00; /* I2C FM with 50ns spike filter */
    st = Cy_I3C_ControllerAttachI2CDevice(
        CYBSP_I3C_CONTROLLER_HW, &i2c_dev, &bmm_i3c_ctx);
    diag->attach_st = (uint32_t)st;
    if (st != CY_I3C_SUCCESS) {
        diag->step = 2;
        result = 2;
        goto done;
    }

    /* Step 3: Register callback + IRQ + Enable */
    Cy_I3C_RegisterEventCallback(CYBSP_I3C_CONTROLLER_HW,
                                  bmm350_i3c_event_cb, &bmm_i3c_ctx);
    cy_stc_sysint_t i3c_irq_cfg = {
        .intrSrc = CYBSP_I3C_CONTROLLER_IRQ,
        .intrPriority = 3
    };
    Cy_SysInt_Init(&i3c_irq_cfg, bmm350_i3c_isr);
    NVIC_EnableIRQ(CYBSP_I3C_CONTROLLER_IRQ);

    Cy_I3C_Enable(CYBSP_I3C_CONTROLLER_HW, &bmm_i3c_ctx);
    Cy_SysLib_Delay(100); /* Wait for hardware stabilization */

    /* Record state + hardware register after Enable */
    diag->state_post_en = bmm_i3c_ctx.state;
    diag->present_st = CYBSP_I3C_CONTROLLER_HW->PRESENT_STATE;

    /* Force context->state = IDLE if it drifted (debug workaround) */
    if (0UL == (0x10000000UL & bmm_i3c_ctx.state)) {
        bmm_i3c_ctx.state = 0x10000000UL; /* CY_I3C_IDLE */
        diag->forced_idle = true;
    }

    /* Resume controller in case it's in halt state */
    Cy_I3C_Resume(CYBSP_I3C_CONTROLLER_HW, &bmm_i3c_ctx);
    Cy_SysLib_Delay(10);

    /* Flush ALL queues + FIFOs to clear stale responses from bus init */
    CYBSP_I3C_CONTROLLER_HW->RESET_CTRL =
        I3C_CORE_RESET_CTRL_CMD_QUEUE_RST_Msk |
        I3C_CORE_RESET_CTRL_RESP_QUEUE_RST_Msk |
        I3C_CORE_RESET_CTRL_TX_FIFO_RST_Msk |
        I3C_CORE_RESET_CTRL_RX_FIFO_RST_Msk;
    {
        uint32_t flush_timeout = 10000;
        while (CYBSP_I3C_CONTROLLER_HW->RESET_CTRL != 0U && flush_timeout > 0) {
            flush_timeout--;
        }
    }
    Cy_I3C_ClearInterrupt(CYBSP_I3C_CONTROLLER_HW, 0xFFFFFFFFUL);
    Cy_SysLib_Delay(5);

    bmm_i3c_ready = true;

    /* Step 4: Try chip ID read (toc=true: STOP+START for I2C) */
    {
        uint8_t reg_addr = BMM350_REG_CHIP_ID;
        cy_stc_i3c_controller_xfer_config_t w_cfg = {
            .targetAddress = BMM350_I2C_ADDR,
            .buffer = &reg_addr,
            .bufferSize = 1,
            .toc = true /* STOP after write, then separate START for read */
        };
        bmm_xfer_done = false;
        bmm_xfer_events = 0;
        st = Cy_I3C_ControllerWrite(
            CYBSP_I3C_CONTROLLER_HW, &w_cfg, &bmm_i3c_ctx);
        diag->wr_st = (uint32_t)st;

        if (st == CY_I3C_SUCCESS) {
            bool wok = bmm350_wait_xfer();
            diag->wr_ev = bmm_xfer_events;
            if (wok) {
                /* Read chip ID byte */
                cy_stc_i3c_controller_xfer_config_t r_cfg = {
                    .targetAddress = BMM350_I2C_ADDR,
                    .buffer = &diag->chip_id,
                    .bufferSize = 1,
                    .toc = true
                };
                bmm_xfer_done = false;
                bmm_xfer_events = 0;
                st = Cy_I3C_ControllerRead(
                    CYBSP_I3C_CONTROLLER_HW, &r_cfg, &bmm_i3c_ctx);
                diag->rd_st = (uint32_t)st;
                if (st == CY_I3C_SUCCESS) {
                    bool rok = bmm350_wait_xfer();
                    diag->rd_ev = bmm_xfer_events;
                    if (rok && diag->chip_id == BMM350_CHIP_ID_VALUE) {
                        diag->step = 0;
                        result = 0;
                        goto done;
                    }
                }
            }
        }
    }

    diag->step = 3;
    result = 3;

done:
    if (was_running) {
        sensor_auto_start();
    }
    return result;
}
