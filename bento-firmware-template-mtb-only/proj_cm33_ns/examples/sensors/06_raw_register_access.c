/* sdk-example: core=cm33 variant=both group=sensors
 * id:      cm33/sensors/06_raw_register_access
 * title:   Talk to any device on the sensor bus directly
 * teaches: the escape hatch - register reads, a safe read-modify-write, and
 *          the command-response form for parts that have no registers at all
 * apis:    sensor_i2c_read_byte, sensor_i2c_write_byte, sensor_i2c_read_reg,
 *          sensor_i2c_write_reg, sensor_i2c_write_raw, sensor_i2c_read_raw,
 *          sensor_i2c_is_init, sensor_i2c_lock, sensor_i2c_unlock
 * entry:   example_sensors_raw_register_access
 */
/*******************************************************************************
 * sensors/06_raw_register_access — when the driver does not expose what you
 * need, and when there is no driver at all.
 *
 * THE TWO SHAPES OF I2C DEVICE
 * ----------------------------
 * Almost every part on this bus is a REGISTER FILE: you write one address
 * byte, then read or write data at that address. sensor_i2c_read_reg() does
 * that as a write of the register byte with NO stop (xferPending) followed by
 * a repeated START and the read — which is what the parts expect and what you
 * would get wrong writing it yourself.
 *
 *     sensor_i2c_read_byte / write_byte   one byte at one register
 *     sensor_i2c_read_reg  / write_reg    N bytes from/at one register
 *
 * A few parts are not register files at all. The SHT40 is one: you send a
 * COMMAND byte, wait for the conversion, and read a fixed-length response.
 * There is no register address anywhere in that exchange, so read_reg() would
 * send a byte the part interprets as a command and everything after that is
 * nonsense. That is what the raw pair is for:
 *
 *     sensor_i2c_write_raw / read_raw     bytes on the wire, nothing added
 *
 * WHAT WRITING BEHIND A DRIVER'S BACK COSTS
 * -----------------------------------------
 * The BMI270 driver converts raw counts to m/s^2 with a scale factor chosen at
 * COMPILE TIME from the range it configured at init (+/-2 g normally, +/-8 g
 * when BENTO_BMI270_ACCEL_8G is set). Write a different value to ACC_RANGE
 * (0x41) from here and the part changes range while the driver does not: every
 * subsequent bmi270_read_accel() is then wrong by a factor of four, silently,
 * with no failed call anywhere. The same trap exists for GYR_RANGE (0x43).
 *
 * So: read whatever you like. Write only registers no driver depends on, and
 * know which those are before you do it.
 *
 * The write demonstrated below is a read-modify-write of PWR_CTRL (0x7D) that
 * puts back the byte it just read — a true no-op, chosen so this example can
 * show you the write path without changing the state of your board. Note what
 * is NOT touched: 0x7E is CMD, where 0xB6 is a soft reset that would throw
 * away the 8 KB config image init uploaded, and 0x7C is PWR_CONF, where the
 * wrong value parks the part in a low-power mode that answers reads with
 * stale data.
 *
 * THE LOCK STILL APPLIES
 * ----------------------
 * None of these functions take the mutex. All of them are transfers. Hold
 * sensor_i2c_lock() across a whole exchange — and for a command-response part
 * that means across the command, the wait AND the response, or another task
 * can read your conversion out from under you.
 *
 * SIZE LIMIT
 * ----------
 * sensor_i2c_write_reg() builds [reg, data...] in a 33-byte stack buffer and
 * returns false for len > 32. Reads have no such limit.
 *******************************************************************************/

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "bsp_feature_flags.h"
#include "sensor_i2c.h"

#include "../sdk_examples_cm33.h"

#define RAW_LOCK_TIMEOUT_MS   (500u)

/* Addresses and registers used below. Deliberately spelled out here rather
 * than pulled from the driver headers: this file is about talking to a device
 * the SDK may know nothing about, and that is what your own will look like. */
#define ADDR_BMI270           (0x68u)
#define BMI270_REG_CHIP_ID    (0x00u)   /* reads 0x24                        */
#define BMI270_REG_ACC_X_LSB  (0x0Cu)   /* 6 bytes: X,Y,Z int16 little-endian */
#define BMI270_REG_PWR_CTRL   (0x7Du)   /* aux/gyr/acc/temp enables          */

#define ADDR_SHT40            (0x44u)
#define SHT40_CMD_MEAS_HIGH   (0xFDu)   /* high-precision T + RH             */
#define SHT40_MEAS_DELAY_MS   (10u)     /* datasheet max for the 0xFD command */

#ifdef USE_KIT_PSE84_EVAL_EPC2
#define SENSOR_BUS_OWNED_BY_CM55   (1)
#else
#define SENSOR_BUS_OWNED_BY_CM55   (0)
#endif

int example_sensors_raw_register_access(void);

int example_sensors_raw_register_access(void)
{
    printf("\r\n--- sensors/06_raw_register_access ---\r\n");

    if (SENSOR_BUS_OWNED_BY_CM55) {
        printf("  REFUSED: Eva Kit — CM55 owns SCB0.\r\n");
        return SDK_EX_UNAVAILABLE;
    }

    if (!sensor_i2c_is_init()) {
        printf("  the bus is not up. Run 02_read_imu (or anything that calls a\r\n");
        printf("  driver init) first — raw access does not bootstrap SCB0.\r\n");
        return SDK_EX_UNAVAILABLE;
    }

    int done = 0;

    /* ================================================================
     * 1. One byte from one register — the identity probe
     * ================================================================ */
    if (BSP_HAS_BMI270) {
        uint8_t chip_id = 0u;
        bool ok;

        if (!sensor_i2c_lock(RAW_LOCK_TIMEOUT_MS)) {
            printf("  bus busy\r\n");
            return SDK_EX_BUSY;
        }
        ok = sensor_i2c_read_byte(ADDR_BMI270, BMI270_REG_CHIP_ID, &chip_id);
        sensor_i2c_unlock();

        printf("  read_byte(0x%02X, 0x%02X) = %s, value 0x%02X\r\n",
               (unsigned)ADDR_BMI270, (unsigned)BMI270_REG_CHIP_ID,
               ok ? "ok" : "FAILED", (unsigned)chip_id);

        if (ok && chip_id == 0x24u) {
            done++;

            /* ============================================================
             * 2. A burst read — six bytes, one transaction
             * ============================================================
             * One repeated-START exchange, not three. The part auto-
             * increments its address pointer, so this is both faster and
             * ATOMIC in a way three separate reads are not: X, Y and Z come
             * from the same conversion. */
            uint8_t acc[6] = {0u};
            if (!sensor_i2c_lock(RAW_LOCK_TIMEOUT_MS)) {
                printf("  bus busy\r\n");
                return SDK_EX_BUSY;
            }
            ok = sensor_i2c_read_reg(ADDR_BMI270, BMI270_REG_ACC_X_LSB, acc, 6u);
            sensor_i2c_unlock();

            if (ok) {
                /* Little-endian int16 per axis. These are RAW COUNTS: the
                 * driver divides by the LSB/g of the range it configured and
                 * multiplies by g. Do not do that arithmetic here unless you
                 * also read ACC_RANGE — the range is not implied by the
                 * counts. */
                const int16_t rx = (int16_t)((uint16_t)acc[0] | ((uint16_t)acc[1] << 8));
                const int16_t ry = (int16_t)((uint16_t)acc[2] | ((uint16_t)acc[3] << 8));
                const int16_t rz = (int16_t)((uint16_t)acc[4] | ((uint16_t)acc[5] << 8));
                printf("  read_reg(0x%02X, 6) raw counts: X %6d  Y %6d  Z %6d\r\n",
                       (unsigned)BMI270_REG_ACC_X_LSB, (int)rx, (int)ry, (int)rz);
                done++;
            } else {
                printf("  read_reg burst FAILED\r\n");
            }

            /* ============================================================
             * 3. Read-modify-write, with the modification set to nothing
             * ============================================================
             * The safe shape for any register write: read it, change only the
             * bits you own, write it back, read it again to confirm. Here the
             * change is empty on purpose — see the header block. */
            uint8_t pwr_before = 0u, pwr_after = 0u;
            bool w_ok = false, v_ok = false;

            if (!sensor_i2c_lock(RAW_LOCK_TIMEOUT_MS)) {
                printf("  bus busy\r\n");
                return SDK_EX_BUSY;
            }
            if (sensor_i2c_read_byte(ADDR_BMI270, BMI270_REG_PWR_CTRL, &pwr_before)) {
                /* The single-byte form... */
                w_ok = sensor_i2c_write_byte(ADDR_BMI270, BMI270_REG_PWR_CTRL,
                                             pwr_before);
                /* ...and the general form, which is what you need for any
                 * multi-byte register. Same byte again: still a no-op. */
                if (w_ok) {
                    w_ok = sensor_i2c_write_reg(ADDR_BMI270, BMI270_REG_PWR_CTRL,
                                                &pwr_before, 1u);
                }
                v_ok = sensor_i2c_read_byte(ADDR_BMI270, BMI270_REG_PWR_CTRL,
                                            &pwr_after);
            }
            sensor_i2c_unlock();

            printf("  PWR_CTRL 0x%02X -> wrote the same byte back -> 0x%02X  (%s)\r\n",
                   (unsigned)pwr_before, (unsigned)pwr_after,
                   (w_ok && v_ok && pwr_before == pwr_after)
                       ? "verified, board unchanged" : "MISMATCH — investigate");
            if (w_ok && v_ok) {
                done++;
            }
        }
    } else {
        printf("  BSP_HAS_BMI270=0 — skipping the register-file demonstrations\r\n");
    }

    /* ================================================================
     * 4. Command-response — a part with no registers at all
     * ================================================================ */
    if (BSP_HAS_SHT40) {
        uint8_t cmd = SHT40_CMD_MEAS_HIGH;
        uint8_t resp[6] = {0u};
        bool ok = false;

        /* The lock spans command, wait and response. Releasing it during the
         * conversion would let another task issue its own command to this
         * part and collect ours. */
        if (!sensor_i2c_lock(RAW_LOCK_TIMEOUT_MS)) {
            printf("  bus busy\r\n");
            return SDK_EX_BUSY;
        }
        if (sensor_i2c_write_raw(ADDR_SHT40, &cmd, 1u)) {
            /* The part NACKs until the conversion finishes; the datasheet
             * allows up to 10 ms for the high-precision command. */
            vTaskDelay(pdMS_TO_TICKS(SHT40_MEAS_DELAY_MS));
            ok = sensor_i2c_read_raw(ADDR_SHT40, resp, 6u);
        }
        sensor_i2c_unlock();

        if (ok) {
            printf("  SHT40 0x%02X -> %02X %02X %02X %02X %02X %02X\r\n",
                   (unsigned)SHT40_CMD_MEAS_HIGH,
                   (unsigned)resp[0], (unsigned)resp[1], (unsigned)resp[2],
                   (unsigned)resp[3], (unsigned)resp[4], (unsigned)resp[5]);
            printf("    layout: T_MSB T_LSB T_CRC  RH_MSB RH_LSB RH_CRC\r\n");
            printf("    bytes 2 and 5 are CRC-8 checks. This example does NOT\r\n");
            printf("    verify them — sht40_read_both() does, which is one\r\n");
            printf("    reason to prefer the driver when there is one.\r\n");

            const uint32_t t_raw  = ((uint32_t)resp[0] << 8) | resp[1];
            const uint32_t rh_raw = ((uint32_t)resp[3] << 8) | resp[4];

            /* Datasheet conversions, in integer milli-units so this core's
             * float-free printf can show them. 64-bit intermediates because
             * 175000 * 65535 does not fit in 32 bits. */
            const int32_t t_mc  = (int32_t)(((uint64_t)175000u * t_raw) / 65535u)
                                  - 45000;
            const int32_t rh_pm = (int32_t)(((uint64_t)125000u * rh_raw) / 65535u)
                                  - 6000;
            printf("    decoded: %ld mC, %ld m%%RH (RH is not clamped to 0..100 here)\r\n",
                   (long)t_mc, (long)rh_pm);
            done++;
        } else {
            printf("  SHT40 command-response FAILED at 0x%02X\r\n",
                   (unsigned)ADDR_SHT40);
        }
    } else {
        printf("  BSP_HAS_SHT40=0 — skipping the command-response demonstration\r\n");
    }

    printf("  %d of the four exchanges completed\r\n", done);
    return (done > 0) ? SDK_EX_OK : SDK_EX_NO_DATA;
}
