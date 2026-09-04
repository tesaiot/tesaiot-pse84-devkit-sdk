/* sdk-example: core=cm33 variant=both group=sensors
 * id:      cm33/sensors/03_read_magnetometer
 * title:   Read the BMM350 magnetometer and calibrate the compass
 * teaches: a compass is useless until hard-iron calibration converges - how to
 *          drive that, how to know when it has, and why heading_from_xy beats
 *          a second bus read
 * apis:    bmm350_read_chip_id, bmm350_read_xyz, bmm350_read_heading,
 *          bmm350_cal_update, bmm350_cal_get_offsets, bmm350_heading_from_xy,
 *          bmm350_cal_reset, sensor_i2c_lock, sensor_i2c_unlock,
 *          bmm350_reinit, bmm350_diagnose, bmm350_debug_read
 * entry:   example_sensors_read_magnetometer
 */
/*******************************************************************************
 * sensors/03_read_magnetometer — north, once the board has earned it.
 *
 * THIS PART IS NOT ON THE I2C BUS
 * -------------------------------
 * The BMM350 sits on the I3C controller, P3[0] = SCL and P3[1] = SDA, at
 * static address 0x15 in I3C legacy (MIXED_FAST) mode. It is a different
 * peripheral from the SCB0 sensor bus, which is why 01_i2c_bus_scan never
 * finds it and why a "missing" magnetometer is not an I2C problem.
 *
 * You still take sensor_i2c_lock() around it. That mutex is not protecting
 * SCB0 here — it is protecting the BMM350 from two readers. The auto task
 * takes it around its own bmm350_read_xyz(), so a caller that skips it is
 * interleaving I3C transactions with a task that assumed it was alone.
 *
 * HARD IRON, AND WHY A FRESH BOARD POINTS THE WRONG WAY
 * -----------------------------------------------------
 * Every magnetometer on a real PCB measures the Earth's field PLUS the
 * permanent field of the metal beside it — connectors, the shield can, a
 * speaker magnet. That offset is fixed in board coordinates, so it does not
 * average out: it moves the centre of the X/Y circle away from the origin and
 * the computed bearing is wrong by an amount that changes with heading.
 *
 * The driver corrects it by watching the extremes. Feed every raw sample to
 * bmm350_cal_update(mx, my); it tracks min and max on both axes, and once it
 * has at least 50 samples AND more than 15 uT of spread on BOTH axes it
 * recomputes the offset (and then every 10 samples after that). Until then
 * s_cal_valid is false, bmm350_cal_get_offsets() reports valid = false, and
 * headings are raw — biased, but not wildly wrong.
 *
 * "Spread on both axes" is the part people miss: it means the board must be
 * TURNED THROUGH A FULL CIRCLE, flat, while this runs. Waving it back and
 * forth along one axis gives spread on one axis only and the calibration
 * never validates, however long you wait.
 *
 * bmm350_cal_reset() throws it all away. Call it when the board's magnetic
 * surroundings change — a new enclosure, a magnet clipped on — and not
 * merely because the heading looks off.
 *
 * READ ONCE, NOT TWICE
 * --------------------
 * bmm350_read_heading() is read_xyz() plus heading_from_xy(). If you already
 * hold mx and my — and you do, because you need them for cal_update() — then
 * calling read_heading() as well costs a SECOND I3C transaction and returns a
 * bearing computed from a DIFFERENT instant. Use bmm350_heading_from_xy() on
 * the pair you have. This example shows both, side by side, so you can see
 * them disagree while the board is moving. That disagreement is the point.
 *
 * BOARDS WITHOUT ONE
 * ------------------
 * The Game Console build sets BSP_HAS_BMM350=0. The driver still links (the
 * Makefile compiles sensor_bmm350.c unconditionally), so nothing here fails
 * at build time — it fails at the I3C bring-up, which is a much worse way to
 * find out. The flag is checked first and the example says so and stops.
 *******************************************************************************/

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "bsp_feature_flags.h"
#include "sensor_bmm350.h"
#include "sensor_i2c.h"

#include "../sdk_examples_cm33.h"

#define MAG_LOCK_TIMEOUT_MS   (500u)

/* 50 samples is the driver's own minimum before it will even try to solve for
 * an offset, so anything less than that here could only ever report failure.
 * 120 at 50 ms is six seconds — about one comfortable turn of the wrist. */
#define MAG_SAMPLE_COUNT      (120)
#define MAG_SAMPLE_PERIOD_MS  (50u)

/* Print every tenth sample. All 120 lines would bury the result. */
#define MAG_PRINT_EVERY       (10)

static int32_t milli(float v)
{
    return (int32_t)(v * 1000.0f);
}

int example_sensors_read_magnetometer(void);

int example_sensors_read_magnetometer(void)
{
    printf("\r\n--- sensors/03_read_magnetometer ---\r\n");

    if (!BSP_HAS_BMM350) {
        printf("  This board has no BMM350 (BSP_HAS_BMM350=0).\r\n");
        printf("  Nothing is wired to the I3C controller — stopping here\r\n");
        printf("  rather than hanging in an I3C bring-up that cannot work.\r\n");
        return SDK_EX_UNAVAILABLE;
    }

    /* ---- Step 1: is it answering? ---------------------------------------- */
    /* bmm350_read_chip_id() calls bmm350_init() for you if the bus is not up
     * yet, so this doubles as the bring-up. It is the only probe in this file
     * that can take a hundred milliseconds. */
    uint8_t chip_id = 0u;
    bool ok;

    if (!sensor_i2c_lock(MAG_LOCK_TIMEOUT_MS)) {
        printf("  another task holds the sensor mutex — try again\r\n");
        return SDK_EX_BUSY;
    }
    ok = bmm350_read_chip_id(&chip_id);
    sensor_i2c_unlock();

    if (!ok) {
        printf("  bmm350_read_chip_id() failed.\r\n");
        printf("  The I3C controller did not come up or the part is silent.\r\n");
        printf("  bmm350_diagnose() in ref_sensors.c reports WHICH step failed.\r\n");
        return SDK_EX_UNAVAILABLE;
    }
    printf("  chip id at 0x%02X = 0x%02X (expected 0x%02X)\r\n",
           (unsigned)BMM350_I2C_ADDR, (unsigned)chip_id,
           (unsigned)BMM350_CHIP_ID_VALUE);
    if (chip_id != BMM350_CHIP_ID_VALUE) {
        printf("  wrong part at this address.\r\n");
        return SDK_EX_UNAVAILABLE;
    }

    /* ---- Step 2: start a clean calibration ------------------------------- */
    /* Reset so the run below is measuring THIS session and not whatever the
     * auto task accumulated since boot. In your own code you would normally
     * NOT do this — the longer the tracker runs, the better the offset. */
    bmm350_cal_reset();
    printf("  calibration reset — now TURN THE BOARD FLAT THROUGH A FULL\r\n");
    printf("  CIRCLE over the next %u seconds. Both X and Y need >15 uT of\r\n",
           (unsigned)((MAG_SAMPLE_COUNT * MAG_SAMPLE_PERIOD_MS) / 1000u));
    printf("  spread or the offset never validates.\r\n");

    /* ---- Step 3: sample, feed the tracker, watch it converge -------------- */
    int good = 0;
    int became_valid_at = -1;

    for (int i = 0; i < MAG_SAMPLE_COUNT; i++) {
        float mx = 0.0f, my = 0.0f, mz = 0.0f;
        float heading_same_sample = 0.0f;
        float heading_second_read = 0.0f;
        bool second_ok = false;

        if (!sensor_i2c_lock(MAG_LOCK_TIMEOUT_MS)) {
            vTaskDelay(pdMS_TO_TICKS(MAG_SAMPLE_PERIOD_MS));
            continue;
        }
        ok = bmm350_read_xyz(&mx, &my, &mz);
        if (ok && ((i % MAG_PRINT_EVERY) == 0)) {
            /* The deliberately wasteful call, made only on printed rows so the
             * comparison is visible without doubling the bus traffic. */
            second_ok = bmm350_read_heading(&heading_second_read);
        }
        sensor_i2c_unlock();

        if (!ok) {
            continue;
        }
        good++;

        /* Feed the tracker with the RAW pair. Do this every sample — the
         * offset solver is only as good as the extremes it has seen. */
        bmm350_cal_update(mx, my);

        /* Same sample, no extra transaction. This is the one to use. */
        heading_same_sample = bmm350_heading_from_xy(mx, my);

        float off_x = 0.0f, off_y = 0.0f;
        bool  cal_valid = false;
        bmm350_cal_get_offsets(&off_x, &off_y, &cal_valid);

        if (cal_valid && became_valid_at < 0) {
            became_valid_at = i;
            printf("  >> calibration became VALID at sample %d:"
                   " offset %ld / %ld muT\r\n",
                   i, (long)milli(off_x), (long)milli(off_y));
        }

        if ((i % MAG_PRINT_EVERY) == 0) {
            printf("    [%3d] m %6ld %6ld %6ld muT   hdg(this sample) %4ld mdeg",
                   i, (long)milli(mx), (long)milli(my), (long)milli(mz),
                   (long)milli(heading_same_sample));
            if (second_ok) {
                printf("   hdg(second read) %4ld mdeg",
                       (long)milli(heading_second_read));
            }
            printf("   cal %s\r\n", cal_valid ? "valid" : "collecting");
        }

        vTaskDelay(pdMS_TO_TICKS(MAG_SAMPLE_PERIOD_MS));
    }

    /* ---- Step 4: the verdict --------------------------------------------- */
    float off_x = 0.0f, off_y = 0.0f;
    bool  cal_valid = false;
    bmm350_cal_get_offsets(&off_x, &off_y, &cal_valid);

    printf("  %d/%d samples read\r\n", good, MAG_SAMPLE_COUNT);
    if (cal_valid) {
        printf("  hard-iron offset: X %ld muT   Y %ld muT (converged at sample %d)\r\n",
               (long)milli(off_x), (long)milli(off_y), became_valid_at);
        printf("  headings from here on are corrected.\r\n");
    } else {
        printf("  calibration did NOT converge. The board was not turned far\r\n");
        printf("  enough, or was turned about one axis only — the solver needs\r\n");
        printf("  >15 uT of spread on BOTH X and Y. Headings so far are raw.\r\n");
    }

    if (good == 0) {
        /* ---- Step 5: recovery, in escalation order ----------------------
         * Zero good samples is a bus problem, not a calibration problem.
         *
         * bmm350_reinit() is the first move: a full I3C + sensor re-init,
         * the same call the firmware's own sensor task uses after an error.
         *
         * If the chip is still silent, bmm350_diagnose() runs the bring-up
         * one step at a time and fills bmm350_diag_t with the return code of
         * each — .step names the first failing stage (1=init, 2=attach,
         * 3=read), so you learn WHERE it died, not just that it died.
         *
         * bmm350_debug_read() is the last resort: it dumps the raw register
         * window to the console for a bug report. It prints; it fixes
         * nothing. */
        if (!bmm350_reinit()) {
            bmm350_diag_t diag;
            (void)bmm350_diagnose(&diag);
            printf("  reinit failed; diagnose stopped at step %d (chip id 0x%02X, expect 0x33)\r\n",
                   diag.step, diag.chip_id);
            bmm350_debug_read();
        }
        return SDK_EX_NO_DATA;
    }
    return cal_valid ? SDK_EX_OK : SDK_EX_NO_DATA;
}
