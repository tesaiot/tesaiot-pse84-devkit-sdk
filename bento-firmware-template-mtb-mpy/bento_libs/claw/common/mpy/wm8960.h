/*******************************************************************************
 * File Name: wm8960.h
 *
 * Description: WM8960 audio codec driver for Eva Kit (KIT_PSE84_EVAL_EPC2).
 *              Uses sensor_i2c bus (SCB0, P8.0/P8.1) for register access.
 *              Provides minimal init for I2S DAC → headphone playback.
 *
 *              WM8960 I2C protocol: 7-bit register address + 9-bit data
 *              packed into 2 bytes:
 *                Byte 0: [R6:R0][D8]  (reg<<1 | data>>8)
 *                Byte 1: [D7:D0]      (data & 0xFF)
 *
 *******************************************************************************/

#ifndef WM8960_H
#define WM8960_H

#include <stdint.h>
#include <stdbool.h>

/* WM8960 I2C slave address (7-bit) */
#define WM8960_I2C_ADDR         0x1A

/* ── Register addresses (7-bit) ────────────────────────────────────── */

#define WM8960_REG_LINVOL       0x00    /* Left Input Volume */
#define WM8960_REG_RINVOL       0x01    /* Right Input Volume */
#define WM8960_REG_LOUT1        0x02    /* LOUT1 (headphone L) Volume */
#define WM8960_REG_ROUT1        0x03    /* ROUT1 (headphone R) Volume */
#define WM8960_REG_CLOCK1       0x04    /* Clocking (1) */
#define WM8960_REG_DACCTL1      0x05    /* ADC/DAC Control (1) */
#define WM8960_REG_DACCTL2      0x06    /* ADC/DAC Control (2) */
#define WM8960_REG_IFACE1       0x07    /* Audio Interface (1) */
#define WM8960_REG_CLOCK2       0x08    /* Clocking (2) */
#define WM8960_REG_IFACE2       0x09    /* Audio Interface (2) */
#define WM8960_REG_LDAC         0x0A    /* Left DAC Volume */
#define WM8960_REG_RDAC         0x0B    /* Right DAC Volume */
#define WM8960_REG_RESET        0x0F    /* Reset (write any value) */
#define WM8960_REG_3D           0x10    /* 3D Control */
#define WM8960_REG_ALC1         0x11    /* ALC1 */
#define WM8960_REG_ALC2         0x12    /* ALC2 */
#define WM8960_REG_ALC3         0x13    /* ALC3 */
#define WM8960_REG_NOISEG       0x14    /* Noise Gate */
#define WM8960_REG_LADC         0x15    /* Left ADC Volume */
#define WM8960_REG_RADC         0x16    /* Right ADC Volume */
#define WM8960_REG_ADDCTL1      0x17    /* Additional Control (1) */
#define WM8960_REG_ADDCTL2      0x18    /* Additional Control (2) */
#define WM8960_REG_POWER1       0x19    /* Power Management (1) */
#define WM8960_REG_POWER2       0x1A    /* Power Management (2) */
#define WM8960_REG_ADDCTL3      0x1B    /* Additional Control (3) */
#define WM8960_REG_APOP1        0x1C    /* Anti-Pop 1 */
#define WM8960_REG_APOP2        0x1D    /* Anti-Pop 2 */
#define WM8960_REG_LINPATH      0x20    /* Left Signal Path */
#define WM8960_REG_RINPATH      0x21    /* Right Signal Path */
#define WM8960_REG_LOUTMIX      0x22    /* Left Out Mix */
#define WM8960_REG_ROUTMIX      0x25    /* Right Out Mix */
#define WM8960_REG_MONOMIX1     0x27    /* Mono Out Mix (1) */
#define WM8960_REG_MONOMIX2     0x28    /* Mono Out Mix (2) */
#define WM8960_REG_LOUT2        0x29    /* LOUT2 (speaker L) Volume */
#define WM8960_REG_ROUT2        0x2A    /* ROUT2 (speaker R) Volume */
#define WM8960_REG_MONO         0x2B    /* Mono Out Volume */
#define WM8960_REG_POWER3       0x2F    /* Power Management (3) */
#define WM8960_REG_CLASSD1      0x31    /* Class D Control (1) */
#define WM8960_REG_CLASSD3      0x33    /* Class D Control (3) */
#define WM8960_REG_PLL1         0x34    /* PLL N */
#define WM8960_REG_PLL2         0x35    /* PLL K 1 */
#define WM8960_REG_PLL3         0x36    /* PLL K 2 */
#define WM8960_REG_PLL4         0x37    /* PLL K 3 */

/* ── Public API ────────────────────────────────────────────────────── */

/**
 * Write a single register to WM8960.
 * @param reg   7-bit register address (0x00..0x37)
 * @param data  9-bit register value (0x000..0x1FF)
 * @return true on success
 */
bool wm8960_write_reg(uint8_t reg, uint16_t data);

/**
 * Initialize WM8960 for I2S DAC → headphone playback.
 * Configures: power, clocking, I2S interface, DAC, output mixers, headphone.
 * Requires sensor_i2c_init() to have been called first.
 * @return true on success (all register writes ACK'd)
 */
bool wm8960_init_playback(void);

/**
 * Set headphone volume (both channels).
 * @param vol_l  Left volume (0..127, 121=0dB, 127=+6dB)
 * @param vol_r  Right volume (0..127)
 */
void wm8960_set_headphone_volume(uint8_t vol_l, uint8_t vol_r);

/**
 * Mute/unmute DAC output.
 * @param mute  true=mute, false=unmute
 */
void wm8960_set_mute(bool mute);

/**
 * Power down WM8960 (low-power state).
 */
void wm8960_power_down(void);

#endif /* WM8960_H */
