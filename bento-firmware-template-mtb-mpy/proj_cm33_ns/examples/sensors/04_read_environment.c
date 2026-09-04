/* sdk-example: core=cm33 variant=both group=sensors
 * id:      cm33/sensors/04_read_environment
 * title:   Read pressure, air temperature and humidity (AI Kit)
 * teaches: read_both() is not a convenience wrapper - it halves the bus
 *          traffic; and the two parts report two different temperatures
 * apis:    dps368_read_product_id, dps368_read_both, dps368_read_pressure,
 *          dps368_read_temperature, sht40_read_serial, sht40_read_both,
 *          sht40_read_temperature, sht40_read_humidity, sensor_i2c_lock,
 *          sensor_i2c_unlock
 * entry:   example_sensors_read_environment
 */
/*******************************************************************************
 * sensors/04_read_environment — the weather, as this board sees it.
 *
 * WHICH BOARDS HAVE THESE
 * -----------------------
 * The AI Kit and the TESAIoT Dev Kit carry both parts: a DPS368 barometric
 * pressure sensor at 0x77 and an SHT40 humidity + temperature sensor at 0x44,
 * both on the SCB0 sensor bus. The Eva Kit carries NEITHER
 * (BSP_HAS_DPS368=0, BSP_HAS_SHT40=0) and the Game Console builds do not
 * either. Both flags are checked below and the example says which half it can
 * do rather than reporting zeros.
 *
 * READ_BOTH() IS NOT SUGAR
 * ------------------------
 * Look at what dps368_read_pressure() actually does: the DPS368's pressure
 * compensation polynomial needs a temperature term, so read_pressure()
 * performs a TEMPERATURE conversion first and then a pressure conversion. If
 * you call read_temperature() and then read_pressure(), you have paid for
 * three conversions to get two numbers, and the temperature you print is not
 * the one that compensated the pressure you print.
 *
 * dps368_read_both() does one temperature conversion, keeps it, and applies it
 * to the pressure. Two numbers, two conversions, one consistent instant. The
 * SHT40 is the same story for a different reason — one 0xFD command returns
 * temperature AND humidity in the same six-byte response, so read_both() is
 * one transaction where the two single readers are two.
 *
 * Use the single readers when you genuinely want one value. Use read_both()
 * the moment you want two. This example shows all four so the cost is visible.
 *
 * TWO PARTS, TWO TEMPERATURES, NEITHER OF THEM WRONG
 * --------------------------------------------------
 * You will see the DPS368 read a degree or two above the SHT40, and both of
 * them above the room. The DPS368 reports its own silicon; it is there to
 * compensate the pressure reading and its absolute accuracy is not the point.
 * The SHT40 sits in the air path and is the one to show a user. The BMI270's
 * die temperature (02_read_imu.c) is higher again. If you need air
 * temperature, use the SHT40 and say so in your UI.
 *
 * THE ID PROBES ARE FREE AND WORTH IT
 * -----------------------------------
 * dps368_read_product_id() is one byte from register 0x0D and sht40_read_
 * serial() is a command-response pair. Neither starts a conversion. They
 * separate "the part is not there" from "the part is there and the reading is
 * bad", which is the distinction every field bug report needs and almost none
 * of them contain. Note the DPS368 check masks the low nibble — that is a
 * revision field, so 0x10 and 0x1x are both a DPS368.
 *
 * Values are printed in milli-units: this core's printf has no float support.
 *******************************************************************************/

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "bsp_feature_flags.h"
#include "sensor_dps368.h"
#include "sensor_sht40.h"
#include "sensor_i2c.h"

#include "../sdk_examples_cm33.h"

#define ENV_LOCK_TIMEOUT_MS   (500u)
#define ENV_SAMPLE_COUNT      (5)
#define ENV_SAMPLE_PERIOD_MS  (1000u)

#ifdef USE_KIT_PSE84_EVAL_EPC2
#define SENSOR_BUS_OWNED_BY_CM55   (1)
#else
#define SENSOR_BUS_OWNED_BY_CM55   (0)
#endif

static int32_t milli(float v)
{
    return (int32_t)(v * 1000.0f);
}

int example_sensors_read_environment(void);

int example_sensors_read_environment(void)
{
    printf("\r\n--- sensors/04_read_environment ---\r\n");

    if (SENSOR_BUS_OWNED_BY_CM55) {
        printf("  REFUSED: Eva Kit — CM55 owns SCB0 (and has neither part).\r\n");
        return SDK_EX_UNAVAILABLE;
    }

    printf("  BSP_HAS_DPS368=%d  BSP_HAS_SHT40=%d\r\n",
           (int)BSP_HAS_DPS368, (int)BSP_HAS_SHT40);

    if (!BSP_HAS_DPS368 && !BSP_HAS_SHT40) {
        printf("  Neither part is fitted on this board. This example is for\r\n");
        printf("  the AI Kit and the TESAIoT Dev Kit.\r\n");
        return SDK_EX_UNAVAILABLE;
    }

    /* ---- Step 1: identity, before any conversion -------------------------- */
    bool have_baro = false;
    bool have_hum  = false;

    if (!sensor_i2c_lock(ENV_LOCK_TIMEOUT_MS)) {
        printf("  another owner holds the sensor bus — try again\r\n");
        return SDK_EX_BUSY;
    }

    if (BSP_HAS_DPS368) {
        uint8_t prod_id = 0u;
        if (dps368_read_product_id(&prod_id)) {
            /* The low nibble is a revision code; the driver masks it and so
             * should you. 0x10 and 0x1x are both a DPS368. */
            printf("  DPS368 at 0x%02X: product id 0x%02X (0x1x expected)\r\n",
                   (unsigned)DPS368_I2C_ADDR, (unsigned)prod_id);
            if ((prod_id & 0xF0u) == 0x10u) {
                /* init reads the nine calibration coefficients out of the part
                 * and sets 8x oversampling. Idempotent and latched. */
                have_baro = dps368_init();
                printf("  dps368_init() = %s\r\n", have_baro ? "ok" : "FAILED");
            }
        } else {
            printf("  DPS368 did not ACK at 0x%02X\r\n",
                   (unsigned)DPS368_I2C_ADDR);
        }
    }

    if (BSP_HAS_SHT40) {
        /* Soft reset, then the serial. Doing init first matters: a part that
         * was left mid-measurement answers a serial request with measurement
         * bytes. */
        const bool sht_init_ok = sht40_init();
        uint32_t serial = 0u;
        if (sht_init_ok && sht40_read_serial(&serial)) {
            printf("  SHT40 at 0x%02X: serial 0x%08lX\r\n",
                   (unsigned)SHT40_I2C_ADDR, (unsigned long)serial);
            have_hum = true;
        } else {
            printf("  SHT40 did not answer at 0x%02X (init %s)\r\n",
                   (unsigned)SHT40_I2C_ADDR, sht_init_ok ? "ok" : "failed");
        }
    }

    sensor_i2c_unlock();

    if (!have_baro && !have_hum) {
        printf("  Both flags are set but neither part answered. Run\r\n");
        printf("  01_i2c_bus_scan — it says whether 0x44 and 0x77 are there.\r\n");
        return SDK_EX_UNAVAILABLE;
    }

    /* ---- Step 2: the reading you would actually log ----------------------- */
    printf("  %d readings, %u ms apart (milli-units)\r\n",
           ENV_SAMPLE_COUNT, (unsigned)ENV_SAMPLE_PERIOD_MS);

    int good = 0;
    for (int i = 0; i < ENV_SAMPLE_COUNT; i++) {
        float hpa = 0.0f, baro_c = 0.0f;
        float air_c = 0.0f, rh = 0.0f;
        bool baro_ok = false, hum_ok = false;

        if (!sensor_i2c_lock(ENV_LOCK_TIMEOUT_MS)) {
            vTaskDelay(pdMS_TO_TICKS(ENV_SAMPLE_PERIOD_MS));
            continue;
        }
        if (have_baro) {
            /* ONE call. See the header block for why this is not the same as
             * read_temperature() followed by read_pressure(). */
            baro_ok = dps368_read_both(&hpa, &baro_c);
        }
        if (have_hum) {
            hum_ok = sht40_read_both(&air_c, &rh);
        }
        sensor_i2c_unlock();

        if (baro_ok || hum_ok) {
            good++;
        }
        printf("    [%d]", i);
        if (baro_ok) {
            printf("  %ld mhPa  die %ld mC", (long)milli(hpa), (long)milli(baro_c));
        } else if (have_baro) {
            printf("  baro read failed");
        }
        if (hum_ok) {
            printf("   air %ld mC  %ld m%%RH", (long)milli(air_c), (long)milli(rh));
        } else if (have_hum) {
            printf("   humidity read failed");
        }
        printf("\r\n");

        vTaskDelay(pdMS_TO_TICKS(ENV_SAMPLE_PERIOD_MS));
    }

    /* ---- Step 3: the single readers, and what they cost ------------------- */
    printf("  the single-value readers, for when you only want one:\r\n");
    if (!sensor_i2c_lock(ENV_LOCK_TIMEOUT_MS)) {
        printf("    bus busy — skipped\r\n");
        return (good > 0) ? SDK_EX_OK : SDK_EX_NO_DATA;
    }
    if (have_baro) {
        float only_c = 0.0f, only_hpa = 0.0f;
        if (dps368_read_temperature(&only_c)) {
            printf("    dps368_read_temperature() = %ld mC"
                   "  (1 conversion)\r\n", (long)milli(only_c));
        }
        if (dps368_read_pressure(&only_hpa)) {
            printf("    dps368_read_pressure()    = %ld mhPa"
                   "  (2 conversions — it must do temperature first)\r\n",
                   (long)milli(only_hpa));
        }
    }
    if (have_hum) {
        float only_c = 0.0f, only_rh = 0.0f;
        if (sht40_read_temperature(&only_c)) {
            printf("    sht40_read_temperature()  = %ld mC"
                   "   (1 command+response)\r\n", (long)milli(only_c));
        }
        if (sht40_read_humidity(&only_rh)) {
            printf("    sht40_read_humidity()     = %ld m%%RH"
                   " (a SECOND command+response for data the first one"
                   " already returned)\r\n", (long)milli(only_rh));
        }
    }
    sensor_i2c_unlock();

    printf("  air temperature comes from the SHT40. The DPS368 number is its\r\n");
    printf("  own silicon and exists to compensate the pressure.\r\n");

    return (good > 0) ? SDK_EX_OK : SDK_EX_NO_DATA;
}
