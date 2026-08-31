/*******************************************************************************
 * File Name: wm8960.c
 *
 * Description: WM8960 audio codec driver — minimal I2S DAC → headphone init.
 *              Uses sensor_i2c bus (SCB0) for register access.
 *
 *              Reference: WM8960 datasheet Rev 4.4, NXP i.MX RT WM8960 driver
 *
 *******************************************************************************/

#include "wm8960.h"
#include "sensor_i2c.h"
#include "cy_syslib.h"
#include <stdio.h>

/* ── I2C register write ────────────────────────────────────────────── */

bool wm8960_write_reg(uint8_t reg, uint16_t data) {
    /*
     * WM8960 I2C write format (non-standard):
     *   Byte 0: [R6 R5 R4 R3 R2 R1 R0 D8]  — 7-bit reg + MSB of data
     *   Byte 1: [D7 D6 D5 D4 D3 D2 D1 D0]  — lower 8 bits of data
     */
    uint8_t buf[2];
    buf[0] = (uint8_t)((reg << 1) | ((data >> 8) & 0x01));
    buf[1] = (uint8_t)(data & 0xFF);

    return sensor_i2c_write_raw(WM8960_I2C_ADDR, buf, 2);
}

/* ── Init sequence for DAC → Headphone playback ───────────────────── */

bool wm8960_init_playback(void) {
    bool ok = true;

    /* Ensure I2C bus is initialized */
    printf("WM8960: sensor_i2c_is_init=%d\n", sensor_i2c_is_init());
    if (!sensor_i2c_is_init()) {
        bool i2c_ok = sensor_i2c_init();
        printf("WM8960: sensor_i2c_init=%d\n", i2c_ok);
        if (!i2c_ok) {
            return false;
        }
    }

    /* Lock I2C bus for the entire init sequence */
    if (!sensor_i2c_lock(500)) {
        printf("WM8960: I2C lock failed\n");
        return false;
    }

    /* 1. Reset all registers to defaults */
    ok = wm8960_write_reg(WM8960_REG_RESET, 0x000);
    printf("WM8960: reset write=%d\n", ok);
    if (!ok) {
        printf("WM8960: device not responding at 0x%02X\n", WM8960_I2C_ADDR);
        sensor_i2c_unlock();
        return false;
    }
    Cy_SysLib_Delay(10);

    /*
     * 2. Power Management 1 (R25 = 0x19)
     *    [7:6] VMIDSEL = 01 (50kΩ divider — normal operation)
     *    [5]   VREF    = 1  (enable reference voltage)
     *    Value: 0b0_01_1_00000 = 0x060
     */
    ok &= wm8960_write_reg(WM8960_REG_POWER1, 0x060);
    Cy_SysLib_Delay(50);  /* Wait for VMID to stabilize */

    /*
     * 3. Power Management 2 (R26 = 0x1A)
     *    [8] DACL  = 1 (Left DAC)
     *    [7] DACR  = 1 (Right DAC)
     *    [6] LOUT1 = 1 (Left headphone output)
     *    [5] ROUT1 = 1 (Right headphone output)
     *    Value: 0x1E0
     */
    ok &= wm8960_write_reg(WM8960_REG_POWER2, 0x1E0);

    /*
     * 4. Power Management 3 (R47 = 0x2F)
     *    [3] LOMIX = 1 (Left output mixer)
     *    [2] ROMIX = 1 (Right output mixer)
     *    Value: 0x00C
     */
    ok &= wm8960_write_reg(WM8960_REG_POWER3, 0x00C);

    /*
     * 5. Audio Interface (R7 = 0x07)
     *    [6]   MS     = 0 (slave mode — PSoC provides clocks)
     *    [3:2] WL     = 00 (16-bit word length)
     *    [1:0] FORMAT = 10 (I2S format)
     *    Value: 0x002
     */
    ok &= wm8960_write_reg(WM8960_REG_IFACE1, 0x002);

    /*
     * 6. Clocking 1 (R4 = 0x04)
     *    [0]   CLKSEL    = 0 (SYSCLK derived from MCLK)
     *    [2:1] SYSCLKDIV = 00 (divide by 1)
     *    [5:3] DACDIV    = 000 (divide SYSCLK by 1 for DAC)
     *    Value: 0x000
     */
    ok &= wm8960_write_reg(WM8960_REG_CLOCK1, 0x000);

    /*
     * 7. ADC/DAC Control 1 (R5 = 0x05)
     *    [3] DACMU = 0 (DAC soft mute disabled = audio on)
     *    Value: 0x000
     */
    ok &= wm8960_write_reg(WM8960_REG_DACCTL1, 0x000);

    /*
     * 8. Left DAC Volume (R10 = 0x0A)
     *    [7:0] DACVOL = 0xFF (0 dB)
     *    [8]   DACVU  = 1   (volume update — apply to both channels)
     *    Value: 0x1FF
     */
    ok &= wm8960_write_reg(WM8960_REG_LDAC, 0x1FF);

    /*
     * 9. Right DAC Volume (R11 = 0x0B)
     *    Same as left: 0 dB + update
     */
    ok &= wm8960_write_reg(WM8960_REG_RDAC, 0x1FF);

    /*
     * 10. Left Output Mixer (R34 = 0x22)
     *     [8] LD2LO = 1 (Left DAC to Left Output Mixer)
     *     Value: 0x100
     */
    ok &= wm8960_write_reg(WM8960_REG_LOUTMIX, 0x100);

    /*
     * 11. Right Output Mixer (R37 = 0x25)
     *     [8] RD2RO = 1 (Right DAC to Right Output Mixer)
     *     Value: 0x100
     */
    ok &= wm8960_write_reg(WM8960_REG_ROUTMIX, 0x100);

    /*
     * 12. LOUT1 Volume (R2 = 0x02) — Headphone Left
     *     [6:0] LOUT1VOL = 0x79 (0 dB, range: 0x30=-73dB .. 0x7F=+6dB)
     *     [7]   LZCEN    = 1    (zero cross detect)
     *     [8]   OUT1VU   = 1    (volume update — apply to both)
     *     Value: 0x1F9  (0b1_1_1111001)
     */
    ok &= wm8960_write_reg(WM8960_REG_LOUT1, 0x1F9);

    /*
     * 13. ROUT1 Volume (R3 = 0x03) — Headphone Right
     *     Same as left: 0 dB + zero cross + update
     */
    ok &= wm8960_write_reg(WM8960_REG_ROUT1, 0x1F9);

    sensor_i2c_unlock();

    return ok;
}

/* ── Headphone volume control ─────────────────────────────────────── */

void wm8960_set_headphone_volume(uint8_t vol_l, uint8_t vol_r) {
    if (vol_l > 127) vol_l = 127;
    if (vol_r > 127) vol_r = 127;

    sensor_i2c_lock(100);

    /* LZCEN=1, OUT1VU=1, volume value */
    wm8960_write_reg(WM8960_REG_LOUT1,
                     0x180 | (uint16_t)vol_l);  /* bit8=OUT1VU, bit7=LZCEN */
    wm8960_write_reg(WM8960_REG_ROUT1,
                     0x180 | (uint16_t)vol_r);

    sensor_i2c_unlock();
}

/* ── DAC mute control ─────────────────────────────────────────────── */

void wm8960_set_mute(bool mute) {
    sensor_i2c_lock(100);
    /* R5 bit 3: DACMU (1=mute, 0=unmute) */
    wm8960_write_reg(WM8960_REG_DACCTL1, mute ? 0x008 : 0x000);
    sensor_i2c_unlock();
}

/* ── Power down ───────────────────────────────────────────────────── */

void wm8960_power_down(void) {
    sensor_i2c_lock(100);
    wm8960_write_reg(WM8960_REG_POWER1, 0x000);
    wm8960_write_reg(WM8960_REG_POWER2, 0x000);
    wm8960_write_reg(WM8960_REG_POWER3, 0x000);
    sensor_i2c_unlock();
}
