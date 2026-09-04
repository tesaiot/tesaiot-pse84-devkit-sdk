/* sdk-example: core=cm33 variant=both group=sensors
 * id:      cm33/sensors/01_i2c_bus_scan
 * title:   Scan the sensor I2C bus
 * teaches: the bus has one mutex and several owners - take it, scan, give it
 *          back, and quiesce the auto task first so you are not fighting it
 * apis:    sensor_i2c_is_init, sensor_i2c_lock, sensor_i2c_scan,
 *          sensor_i2c_unlock, sensor_auto_is_running, sensor_auto_stop,
 *          sensor_auto_start
 * entry:   example_sensors_i2c_bus_scan
 */
/*******************************************************************************
 * sensors/01_i2c_bus_scan — who is on the wire, and who else is talking.
 *
 * WHICH BUS THIS IS
 * -----------------
 * SCB0, P8[0] = SCL, P8[1] = SDA, 1.8 V, 400 kHz. That is the ON-BOARD sensor
 * bus and nothing else. MicroPython's machine.I2C is a DIFFERENT peripheral —
 * SCB5 on P17[0]/P17[1] at 3.3 V, for your own breakout boards — and a device
 * on one will never appear in a scan of the other.
 *
 * THE LOCK IS THE POINT OF THIS FILE
 * ----------------------------------
 * Read sensor_i2c.c and you will find that sensor_i2c_read_reg(),
 * write_reg(), read_raw(), write_raw() and scan() take NO mutex. Not one of
 * them. sensor_i2c_lock() / sensor_i2c_unlock() exist as a SEPARATE, CALLER-
 * DRIVEN pair, and every shipped caller — the auto task's per-sensor helpers,
 * MicroPython's `sensors` module — wraps its transfers in them by hand.
 *
 * So if you call a transfer without the lock, nothing warns you. It works, it
 * keeps working, and then one day a repeated START from this task lands
 * between the address phase and the data phase of the auto task's read and
 * both come back wrong. Take the lock.
 *
 * A scan is the worst offender: 112 addresses, 100 us of settle between
 * probes, so roughly 30-60 ms with the bus held. That is why this example
 * stops the auto task first rather than merely locking against it.
 *
 * TWO THINGS THE LOCK API WILL NOT TELL YOU
 * -----------------------------------------
 *  1. sensor_i2c_lock() returns TRUE IMMEDIATELY if the mutex does not exist
 *     yet — it is created inside sensor_i2c_init(). A true return is proof
 *     that you may proceed, not proof that there is a bus. Check
 *     sensor_i2c_is_init() for that, as this example does.
 *
 *  2. sensor_auto_stop() calls vTaskSuspend() on the auto task from YOUR
 *     context, immediately, wherever that task happens to be. If it was
 *     between sensor_i2c_lock() and sensor_i2c_unlock() inside one of its
 *     reads, the mutex stays taken until the task is resumed. That is not a
 *     bug you can code around — it is why the lock below has a real timeout
 *     and why a timeout here restarts the auto task instead of concluding the
 *     bus is dead.
 *
 * WHAT WILL NOT SHOW UP
 * ---------------------
 * The BMM350 magnetometer. It is on the I3C controller (P3[0]/P3[1]), a
 * different peripheral entirely, and sensor_i2c_scan() cannot see it. If you
 * are hunting for 0x15 and not finding it, that is why — see 03_read_magneto-
 * meter.c, which reads it fine.
 *
 * EVA KIT: THIS EXAMPLE REFUSES
 * -----------------------------
 * On KIT_PSE84_EVAL_EPC2 the display touch controller, the BMI270 and the
 * CapSense co-processor all sit on SCB0 and CM55 owns that block. CM33_NS
 * bringing up its own master on the same peripheral gives one SCB two PDL
 * contexts and two NVIC lines with no arbitration; the measured result
 * (2026-08-13, three times out of three) is a permanent interrupt storm —
 * UART silent, LCD dark, Ctrl-C dead, and a plain reset does not clear it.
 * A hang is not an exception, so try/except does not save you either. The
 * values are still available on that board: CM55 reads these sensors and
 * answers IPC_CMD_SENSOR_SNAPSHOT.
 *******************************************************************************/

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "sensor_i2c.h"
#include "sensor_auto_task.h"

#include "../sdk_examples_cm33.h"

/* Long enough to outlast one auto-task sensor read (its own lock timeout plus
 * a transfer), short enough that a wedged bus reports instead of hanging. */
#define SCAN_LOCK_TIMEOUT_MS   (1000u)

/* 0x08..0x77 is what sensor_i2c_scan() walks; 16 is far more than this board
 * can populate, and the call clamps to whatever you pass. */
#define SCAN_MAX_ADDRS         (16)

/* Compile-time truth about who owns SCB0, expressed as a value rather than as
 * #ifdef around the body, so BOTH paths are compiled on every board. */
#ifdef USE_KIT_PSE84_EVAL_EPC2
#define SENSOR_BUS_OWNED_BY_CM55   (1)
#else
#define SENSOR_BUS_OWNED_BY_CM55   (0)
#endif

/* Everything the AI Kit / Dev Kit can answer at on this bus. An address not
 * in this table is not an error — it is a board you have added something to. */
static const char *bus_device_name(uint8_t addr)
{
    switch (addr) {
    case 0x08: return "CapSense PSoC 4000T (base board)";
    case 0x18: return "TLV320DAC3100 audio codec";
    case 0x44: return "SHT40 humidity + temperature";
    case 0x68: return "BMI270 6-axis IMU";
    case 0x77: return "DPS368 pressure + temperature";
    default:   return "unknown — yours?";
    }
}

int example_sensors_i2c_bus_scan(void);

int example_sensors_i2c_bus_scan(void)
{
    printf("\r\n--- sensors/01_i2c_bus_scan ---\r\n");

    if (SENSOR_BUS_OWNED_BY_CM55) {
        printf("  REFUSED: this board is an Eva Kit and CM55 owns SCB0.\r\n");
        printf("  Driving it from CM33_NS wedges the board and no reset\r\n");
        printf("  clears it. Ask CM55 instead (IPC_CMD_SENSOR_SNAPSHOT).\r\n");
        return SDK_EX_UNAVAILABLE;
    }

    printf("  bus: SCB0, P8[0]=SCL P8[1]=SDA, 1.8 V, 400 kHz\r\n");
    printf("  sensor_i2c_is_init() = %s\r\n",
           sensor_i2c_is_init() ? "true" : "false (nothing has opened it yet)");

    /* Step 1 — get the other owner off the wire.
     *
     * The auto task reads the IMU, the barometer and the humidity sensor on a
     * 100 ms cycle by default. Locking against it would work, but it would
     * also mean this scan waits for, and then blocks, a task the dashboard is
     * watching. Pausing it is cheaper and the intent is legible. */
    const bool auto_was_running = sensor_auto_is_running();
    if (auto_was_running) {
        printf("  auto task is publishing — pausing it for the scan\r\n");
        sensor_auto_stop();
    } else {
        printf("  auto task is not publishing — nothing to pause\r\n");
    }

    /* Step 2 — take the bus. Never scan without this. */
    if (!sensor_i2c_lock(SCAN_LOCK_TIMEOUT_MS)) {
        /* Someone still holds it after a full second. The likeliest cause is
         * the hazard in the header block: the auto task was suspended between
         * its own lock and unlock. Resuming it lets it finish and release. */
        printf("  sensor_i2c_lock(%u ms) TIMED OUT — someone holds the bus\r\n",
               (unsigned)SCAN_LOCK_TIMEOUT_MS);
        if (auto_was_running) {
            printf("  resuming the auto task so it can release the mutex\r\n");
            sensor_auto_start();
        }
        return SDK_EX_BUSY;
    }

    /* Step 3 — bring the bus up if nobody has. sensor_i2c_init() is idempotent
     * and latched: a second call after the first success returns true without
     * touching the peripheral. It is called here, inside the lock, because it
     * is where the mutex itself is created. */
    bool bus_ready = sensor_i2c_is_init();
    if (!bus_ready) {
        bus_ready = sensor_i2c_init();
        printf("  sensor_i2c_init() = %s\r\n", bus_ready ? "true" : "FALSE");
    }

    int found = -1;
    uint8_t addrs[SCAN_MAX_ADDRS];

    if (bus_ready) {
        /* Step 4 — the scan itself. Returns how many addresses ACKed, and
         * writes them in ascending order into your buffer. */
        found = sensor_i2c_scan(addrs, SCAN_MAX_ADDRS);
    }

    /* Step 5 — give it back. There is no path out of here that skips this. */
    sensor_i2c_unlock();

    /* Step 6 — put the auto task back exactly as we found it. An example that
     * leaves the dashboard frozen has broken the board for the next person. */
    if (auto_was_running) {
        sensor_auto_start();
        printf("  auto task resumed\r\n");
    }

    if (!bus_ready) {
        printf("  bus did not come up — check the SCB0 clock and pin config\r\n");
        return SDK_EX_UNAVAILABLE;
    }

    printf("  %d device(s) responded:\r\n", found);
    for (int i = 0; i < found; i++) {
        printf("    0x%02X  %s\r\n", (unsigned)addrs[i], bus_device_name(addrs[i]));
    }
    if (found == 0) {
        printf("    none. SDA stuck low, or nothing is powered.\r\n");
    }
    printf("  (BMM350 lives on I3C P3[0]/P3[1] and never appears here)\r\n");

    return (found > 0) ? SDK_EX_OK : SDK_EX_NO_DATA;
}
