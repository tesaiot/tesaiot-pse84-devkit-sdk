/*******************************************************************************
 * File Name: sensor_bmi270.c
 *
 * Description: BMI270 6-axis IMU register-level driver.
 *              Accelerometer: +/-2g, 16384 LSB/g → m/s^2
 *              Gyroscope: +/-2000 dps, 16.4 LSB/dps → deg/s
 *
 *              Includes full 8KB config file upload (required by BMI270).
 *
 * Reference: BMI270 datasheet (Bosch Sensortec)
 *
 *******************************************************************************/

#include "sensor_bmi270.h"
#include "sensor_i2c.h"
#include "cy_syslib.h"
#include "bmi270_config_data.h"
#include "ipc_communication.h"

/* BMI270 Register Map */
#define BMI270_REG_CHIP_ID      (0x00)
#define BMI270_REG_ERR_REG      (0x02)
#define BMI270_REG_STATUS       (0x03)
#define BMI270_REG_ACC_X_LSB    (0x0C)
#define BMI270_REG_GYR_X_LSB    (0x12)
#define BMI270_REG_TEMP_LSB     (0x22)
#define BMI270_REG_INTERNAL_STATUS (0x21)
#define BMI270_REG_ACC_CONF     (0x40)
#define BMI270_REG_ACC_RANGE    (0x41)
#define BMI270_REG_GYR_CONF     (0x42)
#define BMI270_REG_GYR_RANGE    (0x43)
#define BMI270_REG_INIT_ADDR_0  (0x5B)
#define BMI270_REG_INIT_ADDR_1  (0x5C)
#define BMI270_REG_INIT_CTRL    (0x59)
#define BMI270_REG_INIT_DATA    (0x5E)
#define BMI270_REG_PWR_CONF     (0x7C)
#define BMI270_REG_PWR_CTRL     (0x7D)
#define BMI270_REG_CMD          (0x7E)

/* Expected chip ID */
#define BMI270_CHIP_ID_VALUE    (0x24)

/* Command values */
#define BMI270_CMD_SOFT_RESET   (0xB6)

/* Config upload chunk size (I2C max 32 bytes per write) */
#define BMI270_CONFIG_CHUNK_SIZE (32U)

/* Scaling factors */
/* Accel full-scale range. Default +/-2g; a board that runs the DEEPCRAFT motion
 * model defines BENTO_BMI270_ACCEL_8G to use +/-8g so vigorous gestures are not
 * clipped. The scale values come from ipc_communication.h so the driver and the
 * IPC wire format can never drift; only the range register is chosen here. */
#ifdef BENTO_BMI270_ACCEL_8G
#define BMI270_ACC_RANGE_REG    (0x02)     /* +/-8g */
#else
#define BMI270_ACC_RANGE_REG    (0x00)     /* +/-2g (default) */
#endif
#define BMI270_ACCEL_SCALE      IPC_BMI270_ACCEL_LSB_PER_G  /* LSB/g */
#define BMI270_GYRO_SCALE_2000  IPC_BMI270_GYRO_LSB_PER_DPS /* LSB/dps for +/-2000 dps range */
#define GRAVITY_MS2             GRAVITY_ACCEL               /* 9.80665f m/s^2 */

static bool bmi270_initialized = false;

bool bmi270_read_chip_id(uint8_t *chip_id) {
    return sensor_i2c_read_byte(BMI270_I2C_ADDR, BMI270_REG_CHIP_ID, chip_id);
}

/* Upload 8KB config file to BMI270 via I2C in 32-byte chunks.
 * Protocol: for each chunk, set INIT_ADDR_0/1 (word address), then write to INIT_DATA. */
static bool bmi270_upload_config(void) {
    for (uint16_t index = 0; index < BMI270_CONFIG_FILE_SIZE; index += BMI270_CONFIG_CHUNK_SIZE) {
        /* Set config write address (word address = byte_index / 2) */
        uint16_t word_addr = index / 2;
        uint8_t addr_pair[2] = {
            (uint8_t)(word_addr & 0x0F),
            (uint8_t)((word_addr >> 4) & 0xFF)
        };
        if (!sensor_i2c_write_reg(BMI270_I2C_ADDR, BMI270_REG_INIT_ADDR_0, addr_pair, 2)) {
            return false;
        }

        /* Write chunk to INIT_DATA */
        uint16_t remaining = BMI270_CONFIG_FILE_SIZE - index;
        uint16_t chunk = (remaining < BMI270_CONFIG_CHUNK_SIZE) ? remaining : BMI270_CONFIG_CHUNK_SIZE;
        if (!sensor_i2c_write_reg(BMI270_I2C_ADDR, BMI270_REG_INIT_DATA,
                                   &bmi270_config_file[index], chunk)) {
            return false;
        }
    }
    return true;
}

/* Number of full bring-up attempts before giving up. The config upload below is
 * ~8 KB of I2C traffic; a single transient hiccup anywhere in that sequence used
 * to fail the whole init permanently. */
#define BMI270_INIT_ATTEMPTS  (3u)

/* One complete BMI270 bring-up attempt. Split out of bmi270_init() so the WHOLE
 * sequence can be retried -- it starts with a soft reset, so a retry is safe and
 * always begins from a known state. */
static bool bmi270_init_attempt(void) {
    /* Soft reset */
    if (!sensor_i2c_write_byte(BMI270_I2C_ADDR, BMI270_REG_CMD, BMI270_CMD_SOFT_RESET)) {
        return false;
    }
    Cy_SysLib_Delay(2); /* Wait for reset */

    /* Verify chip ID */
    uint8_t chip_id = 0;
    if (!bmi270_read_chip_id(&chip_id)) {
        return false;
    }
    if (chip_id != BMI270_CHIP_ID_VALUE) {
        return false;
    }

    /* Disable advanced power save to allow configuration */
    if (!sensor_i2c_write_byte(BMI270_I2C_ADDR, BMI270_REG_PWR_CONF, 0x00)) {
        return false;
    }
    Cy_SysLib_DelayUs(450); /* Wait for power mode change */

    /* Prepare config load: set init_ctrl to 0 */
    if (!sensor_i2c_write_byte(BMI270_I2C_ADDR, BMI270_REG_INIT_CTRL, 0x00)) {
        return false;
    }

    /* Upload 8KB config file */
    if (!bmi270_upload_config()) {
        return false;
    }

    /* Signal config load complete */
    if (!sensor_i2c_write_byte(BMI270_I2C_ADDR, BMI270_REG_INIT_CTRL, 0x01)) {
        return false;
    }
    Cy_SysLib_Delay(20); /* Wait for config initialization */

    /* Check internal status — bit 0 should be 1 (init OK) */
    uint8_t internal_status = 0;
    if (!sensor_i2c_read_byte(BMI270_I2C_ADDR, BMI270_REG_INTERNAL_STATUS, &internal_status)) {
        return false;
    }
    if ((internal_status & 0x0F) != 0x01) {
        return false; /* Config initialization failed */
    }

    /* Configure accelerometer: ODR=100Hz, BWP=normal, filter=perf_opt */
    if (!sensor_i2c_write_byte(BMI270_I2C_ADDR, BMI270_REG_ACC_CONF, 0xA8)) {
        return false;
    }

    /* Accelerometer range: BMI270_ACC_RANGE_REG (0x00=+/-2g default, 0x02=+/-8g
     * when BENTO_BMI270_ACCEL_8G is set for the DEEPCRAFT motion model). */
    if (!sensor_i2c_write_byte(BMI270_I2C_ADDR, BMI270_REG_ACC_RANGE, BMI270_ACC_RANGE_REG)) {
        return false;
    }

    /* Configure gyroscope: ODR=100Hz, BWP=normal, filter=perf_opt */
    if (!sensor_i2c_write_byte(BMI270_I2C_ADDR, BMI270_REG_GYR_CONF, 0xA9)) {
        return false;
    }

    /* Gyroscope range: +/-2000 dps */
    if (!sensor_i2c_write_byte(BMI270_I2C_ADDR, BMI270_REG_GYR_RANGE, 0x00)) {
        return false;
    }

    /* Enable accelerometer and gyroscope */
    if (!sensor_i2c_write_byte(BMI270_I2C_ADDR, BMI270_REG_PWR_CTRL, 0x0E)) {
        return false;
    }
    Cy_SysLib_Delay(1);

    /* Enable performance mode (disable advanced power save) */
    if (!sensor_i2c_write_byte(BMI270_I2C_ADDR, BMI270_REG_PWR_CONF, 0x02)) {
        return false;
    }

    return true;
}

/* Bring up the BMI270, retrying the whole sequence on a transient failure.
 *
 * The config upload itself is UNCHANGED -- what is added is only a retry around
 * it. Unlike DPS368/SHT40, the BMI270 is not usable until an ~8 KB config blob has
 * been pushed over I2C, and every step above returns false on the first hiccup.
 * With a single attempt one transient failure left the part un-configured for the
 * whole boot: reads returned 0.0 even though the chip answered its ID correctly,
 * and sensor_auto_task cleared the BMI270 mask bit with no self-heal path (BMM350
 * re-inits itself after repeated failures; BMI270 had nothing). Observed on the
 * TESAIoT Dev Kit: the boot attempt failed while an immediate retry succeeded and
 * read correct gravity, so the failure is transient, not a dead part. */
bool bmi270_init(void) {
    if (bmi270_initialized) {
        return true;
    }

    if (!sensor_i2c_is_init()) {
        if (!sensor_i2c_init()) {
            return false;
        }
    }

    for (uint8_t attempt = 0; attempt < BMI270_INIT_ATTEMPTS; attempt++) {
        if (attempt > 0) {
            Cy_SysLib_Delay(5);   /* let the part settle before re-trying */
        }
        if (bmi270_init_attempt()) {
            bmi270_initialized = true;
            return true;
        }
    }
    return false;
}

bool bmi270_read_accel(float *ax, float *ay, float *az) {
    uint8_t data[6];
    if (!sensor_i2c_read_reg(BMI270_I2C_ADDR, BMI270_REG_ACC_X_LSB, data, 6)) {
        return false;
    }

    int16_t raw_x = (int16_t)((data[1] << 8) | data[0]);
    int16_t raw_y = (int16_t)((data[3] << 8) | data[2]);
    int16_t raw_z = (int16_t)((data[5] << 8) | data[4]);

    *ax = ((float)raw_x / BMI270_ACCEL_SCALE) * GRAVITY_MS2;
    *ay = ((float)raw_y / BMI270_ACCEL_SCALE) * GRAVITY_MS2;
    *az = ((float)raw_z / BMI270_ACCEL_SCALE) * GRAVITY_MS2;

    return true;
}

bool bmi270_read_gyro(float *gx, float *gy, float *gz) {
    uint8_t data[6];
    if (!sensor_i2c_read_reg(BMI270_I2C_ADDR, BMI270_REG_GYR_X_LSB, data, 6)) {
        return false;
    }

    int16_t raw_x = (int16_t)((data[1] << 8) | data[0]);
    int16_t raw_y = (int16_t)((data[3] << 8) | data[2]);
    int16_t raw_z = (int16_t)((data[5] << 8) | data[4]);

    *gx = (float)raw_x / BMI270_GYRO_SCALE_2000;
    *gy = (float)raw_y / BMI270_GYRO_SCALE_2000;
    *gz = (float)raw_z / BMI270_GYRO_SCALE_2000;

    return true;
}

bool bmi270_read_temperature(float *temp) {
    uint8_t data[2];
    if (!sensor_i2c_read_reg(BMI270_I2C_ADDR, BMI270_REG_TEMP_LSB, data, 2)) {
        return false;
    }

    int16_t raw_temp = (int16_t)((data[1] << 8) | data[0]);
    /* BMI270 temperature: 0x0000 = 23°C, resolution = 1/512 °C/LSB */
    *temp = 23.0f + ((float)raw_temp / 512.0f);
    return true;
}
