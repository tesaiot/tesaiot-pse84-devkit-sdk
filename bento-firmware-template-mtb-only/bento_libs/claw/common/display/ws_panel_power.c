/*******************************************************************************
 * File: ws_panel_power.c   — see ws_panel_power.h for the rationale.
 *
 * Modeled on Linux drivers/regulator/rpi-panel-attiny-regulator.c. The Waveshare
 * 4.3" 0x45 MCU clones the RPi 7" ATTINY sequencer; register names below are the
 * TRUTHFUL Linux names (the vendor MTB header mis-names them).
 *
 * Phase map (Linux driver -> this clone):
 *   1 validate     read REG_ID; must be 0xC3/0xDE for reads to be trusted.
 *   2 power-down    PWM=0 then POWERON=0            (Linux disable() order).
 *   3 off-settle    wait off_settle_ms             (rail discharge + deaf window).
 *   4 power-up      POWERON=1                       (one clean 0->1 edge; rewriting
 *                                                    1 over 1 does NOT re-run the
 *                                                    sequencer on this MCU).
 *   5 power-good    Linux polls REG_PORTB bit0 (nPWRDWN) up to 1 s. This clone's
 *                   PORTB is STATIC, so it cannot be polled -> fixed on_settle_ms
 *                   fallback (the single, documented deviation).
 *   6 backlight     PWM=level x pwm_retries (blind, spread over the deaf window),
 *                   then PORTA orientation.
 *******************************************************************************/
#include "ws_panel_power.h"
#include "cy_pdl.h"
#include "FreeRTOS.h"
#include "task.h"

/* Truthful register names — Linux panel-raspberrypi-touchscreen.c. */
#define WS_REG_ID          (0x80u)  /* only register that returns real data       */
#define WS_REG_PORTA       (0x81u)  /* GPIO; BIT(2) = horizontal flip             */
#define WS_REG_PORTB       (0x82u)  /* bit0 = nPWRDWN power-good (static on clone) */
#define WS_REG_POWERON     (0x85u)  /* panel+bridge power; writes open deaf window */
#define WS_REG_PWM         (0x86u)  /* backlight brightness 0..255                 */

volatile uint8_t g_ws_regs[7] = {0xEEu,0xEEu,0xEEu,0xEEu,0xEEu,0xEEu,0xEEu};
volatile uint8_t g_ws_acks = 0u;
volatile uint8_t g_ws_naks = 0u;
volatile uint8_t g_ws_done = 0u;

/* Single-register write. NEVER calls Cy_SCB_I2C_Disable/Enable: it must not churn
 * the shared SCB context (touch + codec live on the same bus). Returns true only
 * on a real ACK + complete; leaves the master idle. */
static bool ws_write_reg(const ws_panel_cfg_t *c, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    cy_stc_scb_i2c_master_xfer_config_t xc = {
        .slaveAddress = c->i2c_addr, .buffer = buf,
        .bufferSize = sizeof(buf), .xferPending = false,
    };
    if (CY_SCB_I2C_SUCCESS != Cy_SCB_I2C_MasterWrite(c->scb, &xc, c->ctx)) {
        return false;
    }
    uint32_t st, guard = 10u;                 /* ~10 ms cap; 2 bytes @400k ~ 60 us */
    do {
        st = Cy_SCB_I2C_MasterGetStatus(c->scb, c->ctx);
        Cy_SysLib_Delay(1u);
    } while ((st & CY_SCB_I2C_MASTER_BUSY) && --guard);
    return (guard != 0u) && !(st & CY_SCB_I2C_MASTER_ERR);
}

/* Truthful read: TWO separate transactions with a STOP + pause between (the
 * ATTINY-class MCU cannot serve a repeated-start; Linux uses usleep_range 100-300).
 * Only REG_ID returns real data on this clone; everything else echoes 0xFF. */
static bool ws_read_reg(const ws_panel_cfg_t *c, uint8_t reg, uint8_t *val)
{
    cy_stc_scb_i2c_master_xfer_config_t xc = {
        .slaveAddress = c->i2c_addr, .buffer = &reg,
        .bufferSize = 1u, .xferPending = false,
    };
    uint32_t st, guard = 10u;
    if (CY_SCB_I2C_SUCCESS != Cy_SCB_I2C_MasterWrite(c->scb, &xc, c->ctx)) {
        return false;
    }
    do {
        st = Cy_SCB_I2C_MasterGetStatus(c->scb, c->ctx);
        Cy_SysLib_Delay(1u);
    } while ((st & CY_SCB_I2C_MASTER_BUSY) && --guard);
    if ((guard == 0u) || (st & CY_SCB_I2C_MASTER_ERR)) { return false; }
    Cy_SysLib_Delay(1u);                     /* >= 300 us addr -> data pause */
    xc.buffer = val; xc.bufferSize = 1u;
    if (CY_SCB_I2C_SUCCESS != Cy_SCB_I2C_MasterRead(c->scb, &xc, c->ctx)) {
        return false;
    }
    guard = 10u;
    do {
        st = Cy_SCB_I2C_MasterGetStatus(c->scb, c->ctx);
        Cy_SysLib_Delay(1u);
    } while ((st & CY_SCB_I2C_MASTER_BUSY) && --guard);
    return (guard != 0u) && !(st & CY_SCB_I2C_MASTER_ERR);
}

bool ws_panel_power_up(const ws_panel_cfg_t *cfg)
{
    if (cfg == NULL || cfg->scb == NULL || cfg->ctx == NULL) { return false; }

    /* Phase 1 — validate: dump 0x80..0x86. g_ws_regs[0]=REG_ID must be 0xC3/0xDE
     * for any read telemetry to be trusted (PORTA/POWERON/PWM echo a fake 0xFF). */
    for (uint8_t i = 0u; i < 7u; ++i) {
        uint8_t rv = 0xEEu;
        if (ws_read_reg(cfg, (uint8_t)(WS_REG_ID + i), &rv)) { g_ws_regs[i] = rv; }
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    /* Phase 2 — power-down (Linux disable() order: backlight first, then power). */
    (void)ws_write_reg(cfg, WS_REG_PWM, 0x00u);
    (void)ws_write_reg(cfg, WS_REG_POWERON, 0x00u);

    /* Phase 3 — off-settle: let the rails fully discharge and the power-down deaf
     * window close, so the 0->1 edge below is accepted rather than dropped. */
    vTaskDelay(pdMS_TO_TICKS(cfg->off_settle_ms));

    /* Phase 4 — power-up: ONE clean 0->1 edge. */
    (void)ws_write_reg(cfg, WS_REG_POWERON, 0x01u);

    /* Phase 5 — power-good: Linux polls REG_PORTB bit0 up to 1 s; this clone's
     * PORTB is static and unpollable, so wait the board's worst-case fixed time. */
    vTaskDelay(pdMS_TO_TICKS(cfg->on_settle_ms));

    /* Phase 6 — backlight: PWM blind-retried across the deaf window, then PORTA
     * orientation. Readback is a fake 0xFF on this clone, so it never gates. */
    uint8_t naks_here = 0u;
    for (uint8_t t = 0u; t < cfg->pwm_retries; ++t) {
        if (ws_write_reg(cfg, WS_REG_PWM, cfg->pwm_level)) { g_ws_acks++; }
        else { g_ws_naks++; naks_here++; }
        vTaskDelay(pdMS_TO_TICKS(cfg->pwm_gap_ms));
    }
    (void)ws_write_reg(cfg, WS_REG_PORTA, cfg->porta_orient);
    vTaskDelay(pdMS_TO_TICKS(5));

    g_ws_done = (naks_here == 0u) ? 1u : 2u;
    return (naks_here == 0u);
}
