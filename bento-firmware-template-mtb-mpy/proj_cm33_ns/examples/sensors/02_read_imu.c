/* sdk-example: core=cm33 variant=both group=sensors
 * id:      cm33/sensors/02_read_imu
 * title:   Read the BMI270 accelerometer and gyroscope
 * teaches: prove the wire before you trust the driver, the real init order,
 *          and one lock per SAMPLE rather than one per register
 * apis:    bmi270_read_chip_id, bmi270_read_accel, bmi270_read_gyro,
 *          bmi270_read_temperature, sensor_i2c_lock, sensor_i2c_unlock,
 *          sensor_auto_is_running
 * entry:   example_sensors_read_imu
 */
/*******************************************************************************
 * sensors/02_read_imu — the first sensor almost everybody reads.
 *
 * WHAT THE DRIVER IS DOING FOR YOU
 * --------------------------------
 * bmi270_init() is not a register poke. The BMI270 arrives with no feature
 * firmware, so init uploads an 8 KB configuration image in 32-byte I2C chunks,
 * waits for the part to report INTERNAL_STATUS, then enables the accelerometer
 * and gyroscope. It costs on the order of a hundred milliseconds and it is
 * retried up to three times before it gives up.
 *
 * Two consequences worth knowing:
 *
 *   * It is IDEMPOTENT and latched. Once it has succeeded, calling it again
 *     returns true without touching the part. So on a board where the auto
 *     task has already run, the call below is free.
 *
 *   * It BOOTSTRAPS THE BUS. If sensor_i2c_is_init() is false it calls
 *     sensor_i2c_init() itself. You do not have to — but you do have to hold
 *     the lock while it runs, because everything it does is a transfer.
 *
 * PROVE THE WIRE FIRST
 * --------------------
 * bmi270_init() returning false tells you nothing about WHY. A one-byte read
 * of register 0x00 does: 0x24 means the part is there and answering; 0xFF or
 * a failed read means the bus or the part is not; anything else means you are
 * talking to a different chip at 0x68. That one probe turns "init failed" into
 * a diagnosis, so this example does it before init, not after.
 *
 * ONE LOCK PER SAMPLE, NOT PER REGISTER
 * -------------------------------------
 * The accelerometer, gyroscope and die temperature are three separate reads.
 * Locking and unlocking around each one lets another task interleave between
 * them, so the three halves of a "sample" can come from different instants —
 * and a gyro reading that does not belong to its accelerometer reading is
 * worse than no reading, because fusion code cannot tell.
 *
 * Take the lock once, do the three reads, release. Never hold it across the
 * delay: this task is priority 1 and would starve everything else on the bus.
 *
 * WHAT THE UNITS ARE
 * ------------------
 *   accel  m/s^2, already scaled. Full scale is +/-2 g normally, and +/-8 g on
 *          a board built with BENTO_BMI270_ACCEL_8G (which the DEEPCRAFT
 *          motion model needs so vigorous gestures are not clipped). The
 *          driver hides the difference; a flat board still reads ~9.81 on Z.
 *   gyro   deg/s at +/-2000 dps full scale.
 *   temp   degrees C of the DIE, not of the room. It sits a few degrees above
 *          ambient because the part is powered. Use the SHT40 for air
 *          temperature — see 04_read_environment.c.
 *
 * Values are printed in MILLI-units. This core's printf is built without float
 * support; %f would print nothing useful, and pretending otherwise in an
 * example is how a developer loses an afternoon.
 *******************************************************************************/

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "sensor_bmi270.h"
#include "sensor_i2c.h"
#include "sensor_auto_task.h"

#include "../sdk_examples_cm33.h"

#define IMU_LOCK_TIMEOUT_MS   (500u)
#define IMU_SAMPLE_COUNT      (10)
#define IMU_SAMPLE_PERIOD_MS  (50u)

#ifdef USE_KIT_PSE84_EVAL_EPC2
#define SENSOR_BUS_OWNED_BY_CM55   (1)
#else
#define SENSOR_BUS_OWNED_BY_CM55   (0)
#endif

/* Float -> integer milli-units, rounded toward zero. Everything printed here
 * goes through this, so a sign error can only be made once. */
static int32_t milli(float v)
{
    return (int32_t)(v * 1000.0f);
}

int example_sensors_read_imu(void);

int example_sensors_read_imu(void)
{
    printf("\r\n--- sensors/02_read_imu ---\r\n");

    if (SENSOR_BUS_OWNED_BY_CM55) {
        printf("  REFUSED: Eva Kit — CM55 owns SCB0 and reads the BMI270.\r\n");
        printf("  Ask it over IPC (IPC_CMD_SENSOR_SNAPSHOT) instead.\r\n");
        return SDK_EX_UNAVAILABLE;
    }

    /* The auto task reads this same part every 100 ms by default. We are not
     * stopping it — the mutex is enough for three short reads — but say so,
     * because a developer watching the dashboard should know why the numbers
     * there keep moving while this runs. */
    printf("  auto task publishing: %s\r\n",
           sensor_auto_is_running() ? "yes (sharing the bus with it)" : "no");

    /* ---- Step 1: prove the wire ------------------------------------------ */
    uint8_t chip_id = 0u;
    bool id_ok = false;

    if (!sensor_i2c_lock(IMU_LOCK_TIMEOUT_MS)) {
        printf("  bus busy — sensor_i2c_lock(%u ms) timed out\r\n",
               (unsigned)IMU_LOCK_TIMEOUT_MS);
        return SDK_EX_BUSY;
    }
    id_ok = bmi270_read_chip_id(&chip_id);
    sensor_i2c_unlock();

    if (!id_ok) {
        printf("  bmi270_read_chip_id() failed — no ACK at 0x%02X.\r\n",
               (unsigned)BMI270_I2C_ADDR);
        printf("  The part is absent, unpowered, or the bus is held low.\r\n");
        printf("  Run 01_i2c_bus_scan first: it says whether ANYTHING answers.\r\n");
        return SDK_EX_UNAVAILABLE;
    }
    printf("  chip id at 0x%02X = 0x%02X (expected 0x24)\r\n",
           (unsigned)BMI270_I2C_ADDR, (unsigned)chip_id);
    if (chip_id != 0x24u) {
        printf("  that is not a BMI270. Something else answers at this address.\r\n");
        return SDK_EX_UNAVAILABLE;
    }

    /* ---- Step 2: init ----------------------------------------------------- */
    if (!sensor_i2c_lock(IMU_LOCK_TIMEOUT_MS)) {
        printf("  bus busy before init\r\n");
        return SDK_EX_BUSY;
    }
    /* Every byte of the 8 KB config upload happens inside this call, so the
     * lock must be held across the whole of it. It returns fast when another
     * owner has already initialised the part. */
    const bool init_ok = bmi270_init();
    sensor_i2c_unlock();

    if (!init_ok) {
        printf("  bmi270_init() FAILED after 3 attempts.\r\n");
        printf("  The part answers but would not take its config image —\r\n");
        printf("  usually a marginal 1.8 V rail or a bus fighting a second master.\r\n");
        return SDK_EX_REFUSED;
    }
    printf("  bmi270_init() ok\r\n");

    /* ---- Step 3: sample --------------------------------------------------- */
    printf("  %d samples, %u ms apart (milli-units; this printf has no float)\r\n",
           IMU_SAMPLE_COUNT, (unsigned)IMU_SAMPLE_PERIOD_MS);

    int good = 0;
    for (int i = 0; i < IMU_SAMPLE_COUNT; i++) {
        float ax = 0.0f, ay = 0.0f, az = 0.0f;
        float gx = 0.0f, gy = 0.0f, gz = 0.0f;
        float die_c = 0.0f;
        bool ok;

        /* ONE lock for the whole sample — see the header block. */
        if (!sensor_i2c_lock(IMU_LOCK_TIMEOUT_MS)) {
            printf("    [%2d] bus busy, sample skipped\r\n", i);
            vTaskDelay(pdMS_TO_TICKS(IMU_SAMPLE_PERIOD_MS));
            continue;
        }
        ok = bmi270_read_accel(&ax, &ay, &az);
        if (ok) {
            ok = bmi270_read_gyro(&gx, &gy, &gz);
        }
        if (ok) {
            /* Not fatal if this one fails — the motion data is still good. */
            (void)bmi270_read_temperature(&die_c);
        }
        sensor_i2c_unlock();

        if (!ok) {
            printf("    [%2d] read failed mid-sample\r\n", i);
        } else {
            good++;
            printf("    [%2d] a %6ld %6ld %6ld mm/s^2   g %6ld %6ld %6ld mdps"
                   "   die %ld mC\r\n",
                   i,
                   (long)milli(ax), (long)milli(ay), (long)milli(az),
                   (long)milli(gx), (long)milli(gy), (long)milli(gz),
                   (long)milli(die_c));
        }

        /* Outside the lock, always. */
        vTaskDelay(pdMS_TO_TICKS(IMU_SAMPLE_PERIOD_MS));
    }

    printf("  %d/%d samples read\r\n", good, IMU_SAMPLE_COUNT);
    printf("  sanity: lying flat, one axis should read about 9810 mm/s^2\r\n");
    printf("          and the gyro should be within a few hundred mdps of 0\r\n");

    if (good == 0) {
        return SDK_EX_NO_DATA;
    }
    return SDK_EX_OK;
}
