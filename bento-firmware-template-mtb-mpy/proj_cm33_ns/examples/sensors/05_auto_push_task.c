/* sdk-example: core=cm33 variant=both group=sensors
 * id:      cm33/sensors/05_auto_push_task
 * title:   Drive the background sensor-push task
 * teaches: retune the publisher that feeds the display and MicroPython -
 *          mask, rate, the 50 ms cliff, the free cache - and put it back
 * apis:    sensor_auto_is_running, sensor_auto_get_mask, sensor_auto_set_mask,
 *          sensor_auto_enable, sensor_auto_disable, sensor_auto_get_rate,
 *          sensor_auto_set_rate, sensor_auto_get_push_count,
 *          sensor_auto_stop, sensor_auto_start, sensor_auto_get_bmi270
 * entry:   example_sensors_auto_push_task
 */
/*******************************************************************************
 * sensors/05_auto_push_task — the thing that was already reading your sensors.
 *
 * WHAT IT IS
 * ----------
 * A FreeRTOS task created in main() before the scheduler starts. Every cycle
 * it takes the sensor mutex, reads each ENABLED sensor, and pushes the result
 * to CM55 over IPC. That is where the Dashboard's numbers come from, where the
 * Smart Watch face's numbers come from, and what MicroPython's `sensors`
 * module reads back. It is running right now on your board.
 *
 * Which means: before you write a polling loop of your own, ask whether you
 * want to retune THIS one. Two readers on one bus at two rates is a bug
 * waiting for a deadline; one publisher and many consumers is not.
 *
 * THE MASK IS AN OUTPUT AS WELL AS AN INPUT
 * -----------------------------------------
 * sensor_auto_get_mask() does not simply echo what you set. At start-up the
 * task initialises each sensor and CLEARS that sensor's bit if init failed. So
 * a missing bit means one of two very different things — "you disabled it" or
 * "it is not working" — and the API cannot tell you which. Read the mask
 * BEFORE you touch it, as this example does, and you at least know which of
 * the two you are looking at.
 *
 * THE 50 ms CLIFF
 * ---------------
 * sensor_auto_set_rate() clamps to 20..5000 ms. (The header comment above the
 * prototype says "min 50" — the code says 20. The code is what runs.)
 *
 * But below 50 ms the loop changes SHAPE: it enters IMU-fast mode and reads
 * ONLY the accelerometer. The magnetometer, barometer, humidity, CapSense and
 * potentiometer are skipped, because each costs milliseconds of bus time and
 * together they cannot fit in a 20 ms budget. This exists because the CM55
 * Edge AI motion model was trained at 50 Hz and needs the accelerometer at
 * that rate.
 *
 * So "set_rate(20) to get everything faster" quietly stops publishing five of
 * your six sensors. Nothing returns an error. If you want everything, stay at
 * 50 ms or slower.
 *
 * READ THE CACHE, NOT THE BUS
 * ---------------------------
 * sensor_auto_get_bmi270() copies out the values the task read on its last
 * cycle. No mutex, no I2C, no waiting, and `valid` is false only until the
 * first successful read after boot. If a 100 ms-old accelerometer sample is
 * good enough — for a UI, a tilt check, a wake gesture — this is strictly
 * better than reading the part yourself: it costs nothing and it cannot
 * contend with the task that owns the bus.
 *
 * STOPPING IT IS NOT FREE
 * -----------------------
 * sensor_auto_stop() calls vTaskSuspend() on the task from YOUR context,
 * immediately, wherever it happens to be — including between its own
 * sensor_i2c_lock() and sensor_i2c_unlock(). The mutex then stays taken until
 * something resumes the task. Any code that stops the task and then wants the
 * bus must use a lock TIMEOUT and treat a timeout as "resume the task", never
 * as "the bus is broken". 01_i2c_bus_scan.c does exactly that.
 *
 * THIS EXAMPLE RESTORES WHAT IT CHANGED
 * -------------------------------------
 * Mask, rate and running state are captured on entry and put back on exit,
 * including on the early-return paths. An example that leaves the dashboard
 * at 5 Hz with the barometer switched off has broken the board for whoever
 * runs the next one.
 *******************************************************************************/

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "sensor_auto_task.h"

#include "../sdk_examples_cm33.h"

/* Long enough that the cycle count moves by a readable amount at any rate the
 * task supports, short enough not to sit on the console. */
#define AUTO_OBSERVE_MS   (1000u)

/* Deliberately below SENSOR_AUTO_FAST_THRESHOLD_MS (50) so the IMU-fast
 * behaviour described above is visible rather than merely documented. */
#define AUTO_FAST_RATE_MS (25u)

static void print_mask(const char *what, uint32_t mask)
{
    printf("  %s = 0x%02X  [%s%s%s%s%s%s]\r\n", what, (unsigned)mask,
           (mask & SENSOR_AUTO_BMI270)   ? "bmi270 "   : "",
           (mask & SENSOR_AUTO_DPS368)   ? "dps368 "   : "",
           (mask & SENSOR_AUTO_SHT40)    ? "sht40 "    : "",
           (mask & SENSOR_AUTO_BMM350)   ? "bmm350 "   : "",
           (mask & SENSOR_AUTO_CAPSENSE) ? "capsense " : "",
           (mask & SENSOR_AUTO_POT)      ? "pot "      : "");
}

/* Cycles the task completed over AUTO_OBSERVE_MS. The counter is free-running
 * since boot and is never reset, so a rate is always a difference. */
static uint32_t observe_cycles(void)
{
    const uint32_t before = sensor_auto_get_push_count();
    vTaskDelay(pdMS_TO_TICKS(AUTO_OBSERVE_MS));
    return sensor_auto_get_push_count() - before;
}

int example_sensors_auto_push_task(void);

int example_sensors_auto_push_task(void)
{
    printf("\r\n--- sensors/05_auto_push_task ---\r\n");

    /* ---- Capture everything we are about to change ----------------------- */
    const bool     was_running = sensor_auto_is_running();
    const uint32_t orig_mask   = sensor_auto_get_mask();
    const uint32_t orig_rate   = sensor_auto_get_rate();

    printf("  running: %s\r\n", was_running ? "yes" : "no (paused, or never created)");
    printf("  rate   : %lu ms\r\n", (unsigned long)orig_rate);
    print_mask("mask   ", orig_mask);
    printf("  pushes since boot: %lu\r\n",
           (unsigned long)sensor_auto_get_push_count());
    printf("  (a bit missing from the mask may mean that sensor's init FAILED,\r\n");
    printf("   not that someone disabled it — the task clears its own bits)\r\n");

    if (!was_running) {
        printf("  The task is not publishing. Something paused it, or this\r\n");
        printf("  build never called sensor_auto_task_create(). Not starting\r\n");
        printf("  it here: that is a decision for whoever stopped it.\r\n");
        return SDK_EX_NO_DATA;
    }

    /* ---- Baseline: is it keeping up with its own setting? ---------------- */
    const uint32_t base_cycles = observe_cycles();
    printf("  measured %lu cycles in %u ms (expected about %lu at %lu ms)\r\n",
           (unsigned long)base_cycles, (unsigned)AUTO_OBSERVE_MS,
           (unsigned long)(orig_rate ? (AUTO_OBSERVE_MS / orig_rate) : 0u),
           (unsigned long)orig_rate);
    printf("  fewer than expected means the reads themselves are costing more\r\n");
    printf("  than the interval — the loop delays AFTER the reads, not between\r\n");

    /* ---- The cache: the same numbers, for free --------------------------- */
    sensor_auto_bmi270_cache_t cache;
    sensor_auto_get_bmi270(&cache);
    if (cache.valid) {
        printf("  cached IMU (no bus access): a %ld %ld %ld mm/s^2"
               "  g %ld %ld %ld mdps  die %ld mC\r\n",
               (long)(cache.ax * 1000.0f), (long)(cache.ay * 1000.0f),
               (long)(cache.az * 1000.0f),
               (long)(cache.gx * 1000.0f), (long)(cache.gy * 1000.0f),
               (long)(cache.gz * 1000.0f), (long)(cache.temp * 1000.0f));
    } else {
        printf("  cached IMU not valid yet — no successful read since boot\r\n");
    }

    /* ---- Retune: IMU only, fast ------------------------------------------ */
    printf("  --- switching to BMI270-only at %u ms ---\r\n",
           (unsigned)AUTO_FAST_RATE_MS);

    /* set_mask REPLACES the mask (and masks off anything outside 0x3F). */
    sensor_auto_set_mask(SENSOR_AUTO_BMI270);
    sensor_auto_set_rate(AUTO_FAST_RATE_MS);

    print_mask("mask now", sensor_auto_get_mask());
    printf("  rate now = %lu ms\r\n", (unsigned long)sensor_auto_get_rate());
    printf("  below 50 ms the loop reads the ACCELEROMETER ONLY, whatever the\r\n");
    printf("  mask says. Nothing reports this — that is why it is here.\r\n");

    const uint32_t fast_cycles = observe_cycles();
    printf("  measured %lu cycles in %u ms (expected about %lu)\r\n",
           (unsigned long)fast_cycles, (unsigned)AUTO_OBSERVE_MS,
           (unsigned long)(AUTO_OBSERVE_MS / AUTO_FAST_RATE_MS));

    /* ---- enable / disable: one bit at a time ----------------------------- */
    /* enable ORs a bit in; disable ANDs it out. Use these when you want to
     * change one sensor without knowing or caring about the others — which is
     * almost always the right thing in code that did not set the mask. */
    sensor_auto_enable(SENSOR_AUTO_BMM350);
    print_mask("after enable(BMM350) ", sensor_auto_get_mask());
    sensor_auto_disable(SENSOR_AUTO_BMM350);
    print_mask("after disable(BMM350)", sensor_auto_get_mask());

    /* ---- stop / start: the counter proves it -------------------------- */
    printf("  --- pausing the task ---\r\n");
    sensor_auto_stop();
    const uint32_t at_stop = sensor_auto_get_push_count();
    vTaskDelay(pdMS_TO_TICKS(AUTO_OBSERVE_MS));
    const uint32_t after_stop = sensor_auto_get_push_count();
    printf("  is_running() = %s, counter %lu -> %lu over %u ms\r\n",
           sensor_auto_is_running() ? "true" : "false",
           (unsigned long)at_stop, (unsigned long)after_stop,
           (unsigned)AUTO_OBSERVE_MS);
    printf("  a suspended task burns zero CPU — and may still hold the bus\r\n");
    printf("  mutex if it was suspended mid-read. See the header block.\r\n");

    /* ---- Restore, in the reverse order of the changes -------------------- */
    sensor_auto_set_rate(orig_rate);
    sensor_auto_set_mask(orig_mask);
    sensor_auto_start();

    printf("  --- restored ---\r\n");
    printf("  rate %lu ms, running %s\r\n",
           (unsigned long)sensor_auto_get_rate(),
           sensor_auto_is_running() ? "yes" : "no");
    print_mask("mask   ", sensor_auto_get_mask());

    if (sensor_auto_get_mask() != orig_mask ||
        sensor_auto_get_rate() != orig_rate ||
        !sensor_auto_is_running()) {
        printf("  RESTORE INCOMPLETE — the board is not as this example found it\r\n");
        return SDK_EX_REFUSED;
    }
    return (base_cycles > 0u) ? SDK_EX_OK : SDK_EX_NO_DATA;
}
