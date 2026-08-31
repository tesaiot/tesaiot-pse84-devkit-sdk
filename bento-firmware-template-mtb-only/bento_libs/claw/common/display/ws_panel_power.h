/*******************************************************************************
 * File: ws_panel_power.h
 *
 * Deterministic cold-boot power-up for the Waveshare 4.3" DSI panel whose
 * on-board 0x45 I2C MCU CLONES the Raspberry Pi 7" touchscreen Atmel (ATTINY88)
 * sequencer. Register map + ID bytes (0xC3/0xDE) are byte-identical to the Linux
 * driver drivers/regulator/rpi-panel-attiny-regulator.c, which is the
 * authoritative reference this implementation is modeled on.
 *
 * WHY THIS EXISTS: the vendor MTB init is open-loop with fixed ~100 ms gaps, so
 * on cold rails the backlight writes land inside the MCU's documented post-
 * POWERON deaf window (ACKed but DROPPED) -> dark panel while init still reports
 * SUCCESS. Warm resets always light (pre-charged rails, latched registers). This
 * function drives ONE deterministic warm-reset-equivalent power cycle with board-
 * tunable timing so every boot takes the same, reliably-lit path.
 *
 * SHARED across every project that drives this panel (Game / MicroPython / ...),
 * so there is ONE tuned implementation and no per-project divergence. Board- and
 * bus-specific bindings (which SCB, which timing) are passed in via ws_panel_cfg_t.
 *******************************************************************************/
#ifndef WS_PANEL_POWER_H
#define WS_PANEL_POWER_H

#include <stdint.h>
#include <stdbool.h>
#include "cy_scb_i2c.h"

/* Per-board / per-project binding + tuning. */
typedef struct {
    CySCB_Type               *scb;      /* the SCB the 0x45 MCU is on (e.g. SCB5) */
    cy_stc_scb_i2c_context_t *ctx;      /* its initialized, already-enabled I2C ctx */
    uint8_t  i2c_addr;                  /* 7-bit slave address (0x45)              */

    uint16_t off_settle_ms;             /* wait after POWERON=0: full rail discharge
                                         * + power-down deaf window, so the 0->1
                                         * edge below is actually accepted.        */
    uint16_t on_settle_ms;              /* wait after POWERON=1: power-up sequencer
                                         * + deaf window. This is the fixed-time
                                         * FALLBACK for Linux's power-good poll,
                                         * which this clone cannot serve (PORTB is
                                         * static). Use the board's worst case.    */
    uint8_t  pwm_retries;               /* blind PWM writes spread across the deaf
                                         * window (rpi-panel-attiny-regulator.c
                                         * pattern) so one lands after it closes.  */
    uint16_t pwm_gap_ms;                /* gap between PWM retries                  */
    uint8_t  pwm_level;                 /* backlight brightness 0..255 (0xFF = max) */
    uint8_t  porta_orient;              /* PORTA GPIO orientation bits (0x04=hflip) */
} ws_panel_cfg_t;

/* Telemetry for headless triage (read from the CM33 REPL / openocd, since CM55
 * has no printf here). g_ws_regs[0] (REG_ID) MUST read 0xC3/0xDE for the reads to
 * be trusted. g_ws_naks should be 0; g_ws_acks counts PWM writes that ACKed. */
extern volatile uint8_t g_ws_regs[7];   /* boot dump of 0x80..0x86 (0xEE=read failed) */
extern volatile uint8_t g_ws_acks;      /* PWM writes that genuinely ACKed */
extern volatile uint8_t g_ws_naks;      /* PWM writes that NAK/errored (want 0) */
extern volatile uint8_t g_ws_done;      /* 0=not run, 1=ran (all ACKed), 2=NAKs seen */

/* Run ONE deterministic power cycle (PWM off -> POWERON off -> settle -> clean
 * 0->1 edge -> settle -> backlight) with the given timing. Returns true if every
 * write ACKed. NOTE: this clone cannot report actual panel-lit state, so "true"
 * means best-effort success, not a confirmed-lit panel — reliability comes from
 * the timing margins in cfg, not from readback. */
bool ws_panel_power_up(const ws_panel_cfg_t *cfg);

#endif /* WS_PANEL_POWER_H */
