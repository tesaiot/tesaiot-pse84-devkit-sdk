# BENTO PSoC Edge E84 -- Sensor Driver C API Reference

**Library**: `libtesaiot_sensors.a`
**Source**: `BENTO-TESAIoT-libraries/common/mpy/sensor_*.c`
**Headers**: `BENTO-TESAIoT-libraries/common/mpy/sensor_*.h`
**MicroPython Binding**: `modsensors.c` (source distribution, CM33_NS)
**Target MCU**: Infineon PSoC Edge E84 (CM33_NS core)
**RTOS**: FreeRTOS 10.x

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [Sensor I2C Bus Driver](#2-sensor-i2c-bus-driver)
3. [BMI270 -- 6-Axis IMU (Accelerometer + Gyroscope)](#3-bmi270----6-axis-imu)
4. [DPS368 -- Barometric Pressure Sensor](#4-dps368----barometric-pressure-sensor)
5. [SHT40 -- Humidity and Temperature Sensor](#5-sht40----humidity-and-temperature-sensor)
6. [BMM350 -- 3-Axis Magnetometer](#6-bmm350----3-axis-magnetometer)
7. [CapSense -- Capacitive Touch (PSoC 4000T)](#7-capsense----capacitive-touch)
8. [Potentiometer -- ADC Input](#8-potentiometer----adc-input)
9. [Sensor Auto Task -- Background Polling](#9-sensor-auto-task----background-polling)
10. [Board Support Matrix](#10-board-support-matrix)
11. [Error Handling Conventions](#11-error-handling-conventions)
12. [Thread Safety Model](#12-thread-safety-model)

---

## 1. Architecture Overview

The sensor subsystem runs on the **CM33_NS** (non-secure) core of the PSoC Edge E84 SoC. All sensor drivers share a common I2C bus (SCB0) except the BMM350 magnetometer, which uses a dedicated I3C bus. Sensor data is transmitted to the CM55 core via IPC for LVGL dashboard rendering.

```
CM33_NS (FreeRTOS)                     CM55 (LVGL)
+-------------------+                  +-----------+
| sensor_auto_task  |---IPC Pipe--->   | Dashboard |
| modsensors.c (MP) |                  | Widgets   |
+--------+----------+                  +-----------+
         |
    +----+----+        +----------+
    | SCB0    |        | I3C      |
    | I2C Bus |        | Bus      |
    | 400kHz  |        | (SDR)    |
    +----+----+        +----+-----+
         |                  |
    +----+----+        +----+-----+
    | BMI270  |        | BMM350   |
    | DPS368  |        | 0x15     |
    | SHT40   |        +----------+
    | CapSense|
    +----+----+
         |
    P8[0]=SCL, P8[1]=SDA (1.8V)
```

**Key design constraints:**
- The I2C bus operates at 1.8V logic levels (internal sensor bus). This is separate from the user-accessible `machine.I2C` on SCB5 (P17[0]/P17[1], 3.3V).
- The BMM350 uses the I3C peripheral in PURE mode with SETAASA (assigning the BMM350 static I2C address 0x15 as its I3C dynamic address).
- All I2C sensor reads are mutex-protected for multi-task safety.
- The CapSense and Potentiometer are only present on the Eva Kit evaluation board.

---

## 2. Sensor I2C Bus Driver

**Header**: `sensor_i2c.h`
**Source**: `sensor_i2c.c`
**Hardware**: SCB0, P8[0]=SCL, P8[1]=SDA, 400kHz, 1.8V

The shared I2C master driver provides thread-safe bus access for all on-board sensors (BMI270, DPS368, SHT40, CapSense). Each sensor driver calls these functions internally; application code typically does not call them directly.

### 2.1 Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `SENSOR_I2C_TIMEOUT_US` | `50000` | Transfer timeout in microseconds (50 ms) |

### 2.2 Functions

---

#### `sensor_i2c_init`

```c
bool sensor_i2c_init(void);
```

Initialize SCB0 as I2C master at 400 kHz for the on-board sensor bus.

**Details:**
- Configures P8[0] (SCL) and P8[1] (SDA) with open-drain slow drive mode.
- Sets clock divider to 9 (100 MHz / 10 = 10 MHz SCB clock, yielding 400 kHz I2C).
- Enables interrupt-driven transfers (IRQ priority 7).
- Creates a FreeRTOS mutex for thread-safe bus access.
- Idempotent: subsequent calls return `true` immediately.

| Parameter | Type | Description |
|-----------|------|-------------|
| *(none)* | | |

| Return | Description |
|--------|-------------|
| `true` | Initialization succeeded (or already initialized) |
| `false` | SCB0 I2C init or clock configuration failed |

**Thread Safety:** Safe to call from any task. The mutex is created once.

**C Example:**
```c
if (!sensor_i2c_init()) {
    printf("Sensor I2C bus init failed\r\n");
}
```

---

#### `sensor_i2c_is_init`

```c
bool sensor_i2c_is_init(void);
```

Check whether the sensor I2C bus has been initialized.

| Return | Description |
|--------|-------------|
| `true` | Bus is initialized and ready |
| `false` | Bus has not been initialized |

**Thread Safety:** Safe. Reads a static `bool` flag.

---

#### `sensor_i2c_write_reg`

```c
bool sensor_i2c_write_reg(uint8_t addr, uint8_t reg, const uint8_t *data, uint16_t len);
```

Write one or more bytes to a sensor register. Constructs a single I2C transaction: `[START][addr+W][reg][data0..dataN][STOP]`.

| Parameter | Type | Description |
|-----------|------|-------------|
| `addr` | `uint8_t` | 7-bit I2C slave address |
| `reg` | `uint8_t` | Register address |
| `data` | `const uint8_t *` | Data bytes to write (may be `NULL` if `len == 0`) |
| `len` | `uint16_t` | Number of data bytes (max 32) |

| Return | Description |
|--------|-------------|
| `true` | Write completed successfully |
| `false` | Write failed (NACK, timeout, or `len > 32`) |

**Thread Safety:** NOT thread-safe. Caller must hold the I2C mutex (`sensor_i2c_lock`).

---

#### `sensor_i2c_read_reg`

```c
bool sensor_i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *data, uint16_t len);
```

Read one or more bytes from a sensor register. Uses repeated-start protocol: `[START][addr+W][reg][Sr][addr+R][data0..dataN][STOP]`.

| Parameter | Type | Description |
|-----------|------|-------------|
| `addr` | `uint8_t` | 7-bit I2C slave address |
| `reg` | `uint8_t` | Register address to read from |
| `data` | `uint8_t *` | Buffer to receive data |
| `len` | `uint16_t` | Number of bytes to read |

| Return | Description |
|--------|-------------|
| `true` | Read completed successfully |
| `false` | Read failed (NACK, timeout) |

**Thread Safety:** NOT thread-safe. Caller must hold the I2C mutex.

---

#### `sensor_i2c_write_byte`

```c
bool sensor_i2c_write_byte(uint8_t addr, uint8_t reg, uint8_t value);
```

Write a single byte to a register. Convenience wrapper around `sensor_i2c_write_reg`.

| Parameter | Type | Description |
|-----------|------|-------------|
| `addr` | `uint8_t` | 7-bit I2C slave address |
| `reg` | `uint8_t` | Register address |
| `value` | `uint8_t` | Byte value to write |

| Return | Description |
|--------|-------------|
| `true` | Write succeeded |
| `false` | Write failed |

---

#### `sensor_i2c_read_byte`

```c
bool sensor_i2c_read_byte(uint8_t addr, uint8_t reg, uint8_t *value);
```

Read a single byte from a register. Convenience wrapper around `sensor_i2c_read_reg`.

| Parameter | Type | Description |
|-----------|------|-------------|
| `addr` | `uint8_t` | 7-bit I2C slave address |
| `reg` | `uint8_t` | Register address |
| `value` | `uint8_t *` | Pointer to receive the read byte |

| Return | Description |
|--------|-------------|
| `true` | Read succeeded |
| `false` | Read failed |

---

#### `sensor_i2c_write_raw`

```c
bool sensor_i2c_write_raw(uint8_t addr, const uint8_t *data, uint16_t len);
```

Send raw bytes without a register address prefix. Used by sensors with command-response protocols (e.g., SHT40).

Transaction: `[START][addr+W][data0..dataN][STOP]`

| Parameter | Type | Description |
|-----------|------|-------------|
| `addr` | `uint8_t` | 7-bit I2C slave address |
| `data` | `const uint8_t *` | Command/data bytes to send |
| `len` | `uint16_t` | Number of bytes |

| Return | Description |
|--------|-------------|
| `true` | Write succeeded |
| `false` | Write failed |

---

#### `sensor_i2c_read_raw`

```c
bool sensor_i2c_read_raw(uint8_t addr, uint8_t *data, uint16_t len);
```

Read raw bytes without sending a register address first. Used for command-response protocols (SHT40) and direct-read protocols (CapSense).

Transaction: `[START][addr+R][data0..dataN][STOP]`

| Parameter | Type | Description |
|-----------|------|-------------|
| `addr` | `uint8_t` | 7-bit I2C slave address |
| `data` | `uint8_t *` | Buffer to receive data |
| `len` | `uint16_t` | Number of bytes to read |

| Return | Description |
|--------|-------------|
| `true` | Read succeeded |
| `false` | Read failed |

---

#### `sensor_i2c_scan`

```c
int sensor_i2c_scan(uint8_t *addrs, int max_addrs);
```

Scan the I2C bus for responding devices. Probes addresses 0x08 through 0x77 with single-byte reads. Inserts a 100 us delay between each probe to avoid bus contention.

| Parameter | Type | Description |
|-----------|------|-------------|
| `addrs` | `uint8_t *` | Array to store detected addresses |
| `max_addrs` | `int` | Maximum entries in `addrs` array |

| Return | Description |
|--------|-------------|
| `int` | Number of detected devices (0 if none) |

**Thread Safety:** NOT thread-safe. Caller should hold the I2C mutex during scan.

**C Example:**
```c
uint8_t addrs[16];
int count = sensor_i2c_scan(addrs, 16);
for (int i = 0; i < count; i++) {
    printf("Found device at 0x%02X\r\n", addrs[i]);
}
```

---

#### `sensor_i2c_lock`

```c
bool sensor_i2c_lock(uint32_t timeout_ms);
```

Acquire the I2C bus mutex for exclusive access. Must be paired with `sensor_i2c_unlock()`.

| Parameter | Type | Description |
|-----------|------|-------------|
| `timeout_ms` | `uint32_t` | Maximum wait time in milliseconds. 0 = try without blocking. |

| Return | Description |
|--------|-------------|
| `true` | Mutex acquired (or bus not yet initialized -- single-task safe) |
| `false` | Timeout expired without acquiring mutex |

**Thread Safety:** Safe. This is the synchronization primitive itself.

---

#### `sensor_i2c_unlock`

```c
void sensor_i2c_unlock(void);
```

Release the I2C bus mutex. Must be called after every successful `sensor_i2c_lock()`.

| Parameter | Type | Description |
|-----------|------|-------------|
| *(none)* | | |

**Thread Safety:** Safe. No-op if mutex does not exist.

---

## 3. BMI270 -- 6-Axis IMU

**Header**: `sensor_bmi270.h`
**Source**: `sensor_bmi270.c`
**Sensor**: Bosch BMI270 (accelerometer + gyroscope)
**Bus**: I2C (SCB0), address `0x68`
**Board Support**: AI Kit, Eva Kit, Game Console (`BSP_HAS_BMI270=1`)

### 3.1 Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `BMI270_I2C_ADDR` | `0x68` | I2C 7-bit address |
| `BMI270_CHIP_ID_VALUE` | `0x24` | Expected chip ID from register 0x00 |

### 3.2 Internal Configuration (set during `bmi270_init`)

| Parameter | Setting | Register |
|-----------|---------|----------|
| Accelerometer range | +/- 2 g | `ACC_RANGE = 0x00` |
| Accelerometer ODR | 100 Hz | `ACC_CONF = 0xA8` |
| Gyroscope range | +/- 2000 deg/s | `GYR_RANGE = 0x00` |
| Gyroscope ODR | 100 Hz | `GYR_CONF = 0xA9` |
| Power mode | Performance (adv. power save off) | `PWR_CONF = 0x02` |

### 3.3 Scaling Factors

| Factor | Value | Description |
|--------|-------|-------------|
| `BMI270_ACCEL_SCALE_2G` | `16384.0` | LSB per g at +/- 2g range |
| `BMI270_GYRO_SCALE_2000` | `16.4` | LSB per deg/s at +/- 2000 dps range |
| `GRAVITY_MS2` | `9.80665` | Standard gravity (m/s^2) |

### 3.4 Functions

---

#### `bmi270_init`

```c
bool bmi270_init(void);
```

Initialize the BMI270 IMU sensor. Performs soft reset, verifies chip ID (0x24), uploads the mandatory 8 KB configuration file in 32-byte I2C chunks, configures accelerometer and gyroscope, and enables performance mode.

**Initialization Sequence:**
1. Soft reset (CMD register = 0xB6, 2 ms delay)
2. Verify chip ID == 0x24
3. Disable advanced power save
4. Upload 8 KB config file via `INIT_ADDR_0/1` + `INIT_DATA` registers
5. Signal config complete (`INIT_CTRL = 0x01`)
6. Verify internal status (bit 0 == 1)
7. Configure accel: 100 Hz, normal BWP, +/- 2g
8. Configure gyro: 100 Hz, normal BWP, +/- 2000 dps
9. Enable accel + gyro (`PWR_CTRL = 0x0E`)
10. Enable performance mode

| Return | Description |
|--------|-------------|
| `true` | Sensor initialized and ready |
| `false` | I2C bus not available, chip ID mismatch, config upload failed, or internal status error |

**Thread Safety:** Acquires I2C bus internally (via `sensor_i2c_*` calls). Not re-entrant. Idempotent.

**MicroPython Equivalent:** `sensors.init()` (initializes all sensors)

**C Example:**
```c
if (!bmi270_init()) {
    printf("BMI270 init failed\r\n");
    return;
}
```

---

#### `bmi270_read_accel`

```c
bool bmi270_read_accel(float *ax, float *ay, float *az);
```

Read 3-axis accelerometer data. Reads 6 bytes from register 0x0C (ACC_X_LSB), parses as signed 16-bit little-endian, and converts to m/s^2.

**Conversion formula:** `accel_ms2 = (raw / 16384.0) * 9.80665`

| Parameter | Type | Description |
|-----------|------|-------------|
| `ax` | `float *` | X-axis acceleration in m/s^2 |
| `ay` | `float *` | Y-axis acceleration in m/s^2 |
| `az` | `float *` | Z-axis acceleration in m/s^2 |

| Return | Description |
|--------|-------------|
| `true` | Read succeeded |
| `false` | I2C read failed |

**Thread Safety:** NOT thread-safe. Caller must hold I2C mutex if used outside auto-task.

**MicroPython Equivalent:** `sensors.accel()` returns `(ax, ay, az)` tuple

**C Example:**
```c
float ax, ay, az;
if (bmi270_read_accel(&ax, &ay, &az)) {
    printf("Accel: X=%.2f Y=%.2f Z=%.2f m/s^2\r\n", ax, ay, az);
}
```

---

#### `bmi270_read_gyro`

```c
bool bmi270_read_gyro(float *gx, float *gy, float *gz);
```

Read 3-axis gyroscope data. Reads 6 bytes from register 0x12 (GYR_X_LSB), parses as signed 16-bit little-endian, and converts to deg/s.

**Conversion formula:** `gyro_dps = raw / 16.4`

| Parameter | Type | Description |
|-----------|------|-------------|
| `gx` | `float *` | X-axis angular velocity in deg/s |
| `gy` | `float *` | Y-axis angular velocity in deg/s |
| `gz` | `float *` | Z-axis angular velocity in deg/s |

| Return | Description |
|--------|-------------|
| `true` | Read succeeded |
| `false` | I2C read failed |

**Thread Safety:** NOT thread-safe. Caller must hold I2C mutex.

**MicroPython Equivalent:** `sensors.gyro()` returns `(gx, gy, gz)` tuple

---

#### `bmi270_read_temperature`

```c
bool bmi270_read_temperature(float *temp);
```

Read the BMI270 on-chip temperature sensor. Reads 2 bytes from register 0x22 (TEMP_LSB).

**Conversion formula:** `temp_c = 23.0 + (raw_int16 / 512.0)`

Note: This is the die temperature, not ambient temperature. Typical accuracy is +/- 1 degree C.

| Parameter | Type | Description |
|-----------|------|-------------|
| `temp` | `float *` | Temperature in degrees Celsius |

| Return | Description |
|--------|-------------|
| `true` | Read succeeded |
| `false` | I2C read failed |

**Thread Safety:** NOT thread-safe.

---

#### `bmi270_read_chip_id`

```c
bool bmi270_read_chip_id(uint8_t *chip_id);
```

Read the BMI270 chip ID register (0x00). Expected value: `0x24`.

| Parameter | Type | Description |
|-----------|------|-------------|
| `chip_id` | `uint8_t *` | Pointer to receive chip ID byte |

| Return | Description |
|--------|-------------|
| `true` | Read succeeded |
| `false` | I2C read failed |

---

## 4. DPS368 -- Barometric Pressure Sensor

**Header**: `sensor_dps368.h`
**Source**: `sensor_dps368.c`
**Sensor**: Infineon DPS368 (barometric pressure + temperature)
**Bus**: I2C (SCB0), address `0x77`
**Board Support**: AI Kit only (`BSP_HAS_DPS368=1`)

### 4.1 Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `DPS368_I2C_ADDR` | `0x77` | I2C 7-bit address |
| `DPS368_PRODUCT_ID` | `0x10` | Expected product revision ID (upper nibble of reg 0x0D) |

### 4.2 Internal Configuration (set during `dps368_init`)

| Parameter | Setting | Description |
|-----------|---------|-------------|
| Pressure oversampling | 8x (OSR=3) | 8x oversampling for noise reduction |
| Temperature oversampling | 8x (OSR=3) | 8x oversampling |
| Pressure rate | 1 Hz | Single-shot mode (command-triggered) |
| Temperature rate | 1 Hz | Single-shot mode |
| Temperature source | Auto (from COEF_SRCE bit 7) | ASIC or MEMS sensor |

### 4.3 Structs

#### `dps368_coef_t` (internal)

```c
typedef struct {
    int32_t c0, c1;                         /* Temperature coefficients */
    int32_t c00, c10, c01, c11, c20, c21, c30;  /* Pressure coefficients */
} dps368_coef_t;
```

Factory calibration coefficients read from registers 0x10-0x21 during initialization. Used for compensation calculation per the DPS368 datasheet.

### 4.4 Functions

---

#### `dps368_init`

```c
bool dps368_init(void);
```

Initialize the DPS368 barometric sensor. Performs soft reset, verifies product ID, reads 18 bytes of factory calibration coefficients, and configures 8x oversampling for both pressure and temperature.

**Initialization Sequence:**
1. Soft reset (register 0x0C = 0x89, 40 ms delay)
2. Verify product ID upper nibble == 0x10
3. Read calibration coefficients (18 bytes from 0x10)
4. Determine temperature coefficient source (register 0x28)
5. Configure pressure: 1 Hz rate, 8x oversampling
6. Configure temperature: 1 Hz rate, 8x oversampling, source from COEF_SRCE
7. CFG_REG = 0x00 (no bit-shift needed for 8x oversampling)

| Return | Description |
|--------|-------------|
| `true` | Sensor initialized and calibrated |
| `false` | I2C bus error, product ID mismatch, or coefficient read failed |

**Thread Safety:** Idempotent. Uses I2C bus without external locking.

**MicroPython Equivalent:** `sensors.init()`

---

#### `dps368_read_pressure`

```c
bool dps368_read_pressure(float *pressure);
```

Read compensated barometric pressure in hPa (mbar). Internally reads temperature first (required for pressure compensation), then triggers a single pressure measurement and applies the second-order polynomial compensation using factory coefficients.

**Compensation formula:**
```
Pcomp = c00 + Praw_sc * (c10 + Praw_sc * (c20 + Praw_sc * c30))
      + Traw_sc * (c01 + Praw_sc * (c11 + Praw_sc * c21))
```

Measurement time: ~40 ms total (temperature + pressure, each with 8x oversampling).

| Parameter | Type | Description |
|-----------|------|-------------|
| `pressure` | `float *` | Barometric pressure in hPa (mbar). Sea level ~1013.25 hPa. |

| Return | Description |
|--------|-------------|
| `true` | Read succeeded |
| `false` | Measurement timeout or I2C error |

**Thread Safety:** NOT thread-safe (uses blocking delay during measurement).

**MicroPython Equivalent:** `sensors.pressure()` returns float in hPa

**C Example:**
```c
float pressure;
if (dps368_read_pressure(&pressure)) {
    printf("Pressure: %.2f hPa\r\n", pressure);
}
```

---

#### `dps368_read_temperature`

```c
bool dps368_read_temperature(float *temperature);
```

Read compensated temperature from the DPS368 in degrees Celsius. Triggers a single temperature measurement with 8x oversampling. Also updates the internal `last_temp_scaled` cache used for pressure compensation.

**Compensation formula:** `Tcomp = c0 * 0.5 + c1 * Traw_sc`

Measurement time: ~20 ms (8x oversampling, polled at 5 ms intervals, 100 retries max).

| Parameter | Type | Description |
|-----------|------|-------------|
| `temperature` | `float *` | Temperature in degrees Celsius |

| Return | Description |
|--------|-------------|
| `true` | Read succeeded |
| `false` | Measurement timeout (TMP_RDY not set within 500 ms) or I2C error |

**Thread Safety:** NOT thread-safe.

---

#### `dps368_read_both`

```c
bool dps368_read_both(float *pressure, float *temperature);
```

Read both pressure and temperature in a single call. Reads temperature first (updates compensation cache), then reads pressure. More efficient than calling `dps368_read_pressure()` alone, which internally reads temperature anyway.

| Parameter | Type | Description |
|-----------|------|-------------|
| `pressure` | `float *` | Barometric pressure in hPa |
| `temperature` | `float *` | Temperature in degrees Celsius |

| Return | Description |
|--------|-------------|
| `true` | Both reads succeeded |
| `false` | Any measurement failed |

---

#### `dps368_read_product_id`

```c
bool dps368_read_product_id(uint8_t *product_id);
```

Read the DPS368 product and revision ID from register 0x0D.

| Parameter | Type | Description |
|-----------|------|-------------|
| `product_id` | `uint8_t *` | Raw product/revision byte. Upper nibble = product (0x1 for DPS368). |

| Return | Description |
|--------|-------------|
| `true` | Read succeeded |
| `false` | I2C read failed |

---

## 5. SHT40 -- Humidity and Temperature Sensor

**Header**: `sensor_sht40.h`
**Source**: `sensor_sht40.c`
**Sensor**: Sensirion SHT40 (relative humidity + temperature)
**Bus**: I2C (SCB0), address `0x44`
**Protocol**: Command-response (no register addressing)
**Board Support**: AI Kit only (`BSP_HAS_SHT40=1`)

### 5.1 Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `SHT40_I2C_ADDR` | `0x44` | I2C 7-bit address |

### 5.2 Internal Command Codes

| Command | Value | Description |
|---------|-------|-------------|
| `SHT40_CMD_MEASURE_HIGH` | `0xFD` | High-precision measurement (~8.3 ms) |
| `SHT40_CMD_MEASURE_MED` | `0xF6` | Medium-precision measurement |
| `SHT40_CMD_MEASURE_LOW` | `0xE0` | Low-precision measurement |
| `SHT40_CMD_READ_SERIAL` | `0x89` | Read serial number |
| `SHT40_CMD_SOFT_RESET` | `0x94` | Soft reset |

### 5.3 CRC Verification

All SHT40 responses include CRC-8 checksums (polynomial 0x31, init 0xFF). The driver verifies CRC for every read and returns `false` on CRC mismatch.

### 5.4 Functions

---

#### `sht40_init`

```c
bool sht40_init(void);
```

Initialize the SHT40 sensor with a soft reset. The SHT40 uses a command-response protocol (no register addressing), so initialization only sends the reset command (0x94) via `sensor_i2c_write_raw`.

| Return | Description |
|--------|-------------|
| `true` | Reset command sent successfully |
| `false` | I2C bus error |

**Thread Safety:** Idempotent.

---

#### `sht40_read_temperature`

```c
bool sht40_read_temperature(float *temperature);
```

Read temperature from the SHT40 in degrees Celsius. Internally performs a full high-precision measurement (reads both temperature and humidity from the sensor), but only returns the temperature value.

**Conversion formula:** `temp = -45.0 + 175.0 * raw / 65535.0`

Measurement time: ~10 ms.

| Parameter | Type | Description |
|-----------|------|-------------|
| `temperature` | `float *` | Temperature in degrees Celsius |

| Return | Description |
|--------|-------------|
| `true` | Read and CRC verification succeeded |
| `false` | I2C error or CRC mismatch |

**MicroPython Equivalent:** `sensors.temperature()` returns float

---

#### `sht40_read_humidity`

```c
bool sht40_read_humidity(float *humidity);
```

Read relative humidity from the SHT40 in percent RH. Performs a full measurement, discards temperature, and returns humidity clamped to [0.0, 100.0].

**Conversion formula:** `rh = -6.0 + 125.0 * raw / 65535.0` (clamped to 0-100)

| Parameter | Type | Description |
|-----------|------|-------------|
| `humidity` | `float *` | Relative humidity in %RH (0.0 - 100.0) |

| Return | Description |
|--------|-------------|
| `true` | Read and CRC verification succeeded |
| `false` | I2C error or CRC mismatch |

**MicroPython Equivalent:** `sensors.humidity()` returns float

---

#### `sht40_read_both`

```c
bool sht40_read_both(float *temperature, float *humidity);
```

Read both temperature and humidity in a single measurement cycle. This is the most efficient way to read both values, as it avoids a duplicate measurement.

| Parameter | Type | Description |
|-----------|------|-------------|
| `temperature` | `float *` | Temperature in degrees Celsius |
| `humidity` | `float *` | Relative humidity in %RH |

| Return | Description |
|--------|-------------|
| `true` | Both values read and CRC verified |
| `false` | I2C error or CRC mismatch |

**C Example:**
```c
float temp, hum;
if (sht40_read_both(&temp, &hum)) {
    printf("Temp: %.1f C, Humidity: %.1f %%\r\n", temp, hum);
}
```

---

#### `sht40_read_serial`

```c
bool sht40_read_serial(uint32_t *serial);
```

Read the SHT40 unique serial number (32-bit). Sends command 0x89, reads 6 bytes (2 data + CRC + 2 data + CRC), verifies both CRCs, and assembles the 32-bit serial.

| Parameter | Type | Description |
|-----------|------|-------------|
| `serial` | `uint32_t *` | 32-bit unique serial number |

| Return | Description |
|--------|-------------|
| `true` | Serial read and CRC verified |
| `false` | I2C error or CRC mismatch |

---

## 6. BMM350 -- 3-Axis Magnetometer

**Header**: `sensor_bmm350.h`
**Source**: `sensor_bmm350.c`
**Sensor**: Bosch BMM350 (3-axis geomagnetic sensor)
**Bus**: I3C (CYBSP_I3C_CONTROLLER), P3[0]=SCL, P3[1]=SDA, PURE mode with SETAASA
**Address**: `0x15` (I2C static, ADSEL=HIGH on KIT_PSE84_AI)
**Board Support**: AI Kit, Eva Kit (`BSP_HAS_BMM350=1`)
**Compile Guard**: `#if BSP_HAS_BMM350`

### 6.1 Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `BMM350_I2C_ADDR` | `0x15` | I2C static address (ADSEL=HIGH per KIT_PSE84_AI user guide) |
| `BMM350_CHIP_ID_VALUE` | `0x33` | Expected chip ID |
| `BMM350_DUMMY_BYTES` | `2` | BMM350 sends 2 dummy bytes before register data (SPI/I2C) |
| `BMM350_SENS_XY` | `14.55` | Approximate sensitivity for X/Y axes (LSB/uT) |
| `BMM350_SENS_Z` | `9.0` | Approximate sensitivity for Z axis (LSB/uT) |
| `BMM350_I3C_TIMEOUT_US` | `50000` | Transfer timeout (50 ms) |
| `BMM350_INIT_RETRIES` | `5` | Max retries for chip ID read after soft reset |
| `BMM350_CAL_MIN_SPREAD` | `15.0` | Min uT spread (per axis) before calibration is valid |
| `BMM350_CAL_MIN_SAMPLES` | `50` | Min samples before calibration activates (~2 sec at 25 Hz) |
| `BMM350_HEADING_AVG_N` | `10` | Moving average window for heading (0.4 sec at 25 Hz) |

### 6.2 Structs

#### `bmm350_diag_t`

Diagnostic result structure returned by `bmm350_diagnose()`.

```c
typedef struct {
    int      step;              /* 0=OK, 1=init fail, 2=attach fail, 3=read fail */
    uint32_t init_st;           /* Cy_I3C_Init return code */
    uint32_t attach_st;         /* I2C attach return code */
    uint32_t state_post_init;   /* context->state after Init (expect 0x10000000) */
    uint32_t state_post_en;     /* context->state after Enable + 100ms */
    uint32_t present_st;        /* I3C_CORE PRESENT_STATE hardware register */
    uint32_t wr_st;             /* ControllerWrite return code for chip ID */
    uint32_t wr_ev;             /* Write transfer events (or error) */
    uint32_t rd_st;             /* ControllerRead return code */
    uint32_t rd_ev;             /* Read transfer events (or error) */
    uint8_t  chip_id;           /* Chip ID value read (expect 0x33) */
    bool     forced_idle;       /* true if context->state was forced to IDLE */
} bmm350_diag_t;
```

| Field | Description |
|-------|-------------|
| `step` | 0 = all steps passed. 1 = I3C init failed. 2 = I2C device attach failed. 3 = chip ID read failed. |
| `init_st` | `Cy_I3C_Init()` return code |
| `attach_st` | `Cy_I3C_ControllerAttachI2CDevice()` return code |
| `state_post_init` | I3C context state after `Cy_I3C_Init()` (expected: `0x10000000` = `CY_I3C_IDLE`) |
| `state_post_en` | I3C context state after `Cy_I3C_Enable()` + 100 ms |
| `present_st` | Hardware `PRESENT_STATE` register snapshot |
| `wr_st` / `wr_ev` | Write transfer status and event flags |
| `rd_st` / `rd_ev` | Read transfer status and event flags |
| `chip_id` | Value read from BMM350 chip ID register |
| `forced_idle` | Whether the driver had to force the I3C context to IDLE state |

### 6.3 I3C Bus Architecture Notes

The BMM350 on the PSoC Edge E84 uses the I3C peripheral in **PURE mode** (not MIXED_FAST). After `Cy_I3C_Init()` and `Cy_I3C_Enable()`, the driver issues a **SETAASA** Common Command Code to assign the BMM350's static I2C address (0x15) as its I3C dynamic address. All subsequent reads and writes use I3C SDR (Standard Data Rate) protocol.

**Critical behavior after soft reset:** The BMM350 reverts from I3C SDR back to I2C mode after a soft reset command (0xB6). The driver must re-issue SETAASA after every soft reset to restore communication.

**Dummy bytes:** The BMM350 always sends 2 dummy bytes before actual register data (a Bosch protocol quirk shared across SPI and I2C/I3C). The driver reads `len + 2` bytes and discards the first 2.

### 6.4 Functions

---

#### `bmm350_init`

```c
bool bmm350_init(void);
```

Initialize the BMM350 magnetometer. This is a complex multi-step process involving I3C bus initialization, SETAASA, soft reset, OTP dump, magnetic reset, and normal mode entry.

**Initialization Sequence:**
1. Initialize I3C bus in PURE mode + SETAASA
2. Soft reset (CMD = 0xB6, 24 ms delay)
3. Re-SETAASA (BMM350 reverts to I2C after reset)
4. Verify chip ID == 0x33 (up to 5 retries)
5. Read all 32 OTP words (required before PMU commands)
6. Power off OTP (critical -- without this, PMU returns `cmd_is_illegal`)
7. Magnetic reset: Bit Reset (BR) + Flux Guide Reset (FGR)
8. Configure ODR=25 Hz, averaging=4x
9. Apply settings (UPD_OAE)
10. Enable X, Y, Z axes
11. Enter Normal measurement mode (38 ms delay)

| Return | Description |
|--------|-------------|
| `true` | BMM350 initialized and in Normal mode |
| `false` | I3C bus error, chip ID mismatch, OTP error, or PMU command failure |

**Thread Safety:** Not re-entrant. Idempotent. Pauses sensor auto-task during re-init.

**MicroPython Equivalent:** `sensors.init()`

---

#### `bmm350_reinit`

```c
bool bmm350_reinit(void);
```

Full I3C + BMM350 re-initialization for error recovery. Disables the I3C controller, clears all state, then calls `bmm350_init()`. Use this after communication failures or bus lock-ups.

| Return | Description |
|--------|-------------|
| `true` | Re-initialization succeeded |
| `false` | Re-initialization failed |

---

#### `bmm350_read_xyz`

```c
bool bmm350_read_xyz(float *mx, float *my, float *mz);
```

Read 3-axis magnetic field strength in micro-Tesla (uT). Reads 12 bytes from register 0x31 (X, Y, Z, Temp -- 3 bytes each, little-endian signed 24-bit). Temperature bytes are read but discarded.

**Conversion:** `field_uT = raw_int24 / sensitivity` where XY sensitivity = 14.55, Z sensitivity = 9.0.

Note: These are approximate conversions without OTP calibration data. For heading calculation via `atan2`, the approximation is sufficient.

| Parameter | Type | Description |
|-----------|------|-------------|
| `mx` | `float *` | X-axis magnetic field in uT |
| `my` | `float *` | Y-axis magnetic field in uT |
| `mz` | `float *` | Z-axis magnetic field in uT |

| Return | Description |
|--------|-------------|
| `true` | Read succeeded |
| `false` | I3C transfer failed or sensor not initialized |

**Thread Safety:** NOT thread-safe. Must not be called concurrently with auto-task.

**MicroPython Equivalent:** `sensors.mag()` returns `(mx, my, mz)` tuple

**C Example:**
```c
float mx, my, mz;
if (bmm350_read_xyz(&mx, &my, &mz)) {
    printf("Mag: X=%.1f Y=%.1f Z=%.1f uT\r\n", mx, my, mz);
}
```

---

#### `bmm350_read_heading`

```c
bool bmm350_read_heading(float *heading);
```

Read compass heading in degrees (0-360, 0=North, 90=East). Internally reads X/Y/Z magnetic field, feeds the hard iron calibration tracker, computes heading via `atan2`, and applies a circular moving average filter (10 samples).

| Parameter | Type | Description |
|-----------|------|-------------|
| `heading` | `float *` | Compass heading in degrees [0.0, 360.0) |

| Return | Description |
|--------|-------------|
| `true` | Read and heading calculation succeeded |
| `false` | Magnetic field read failed |

**MicroPython Equivalent:** `sensors.heading()` returns float

---

#### `bmm350_read_chip_id`

```c
bool bmm350_read_chip_id(uint8_t *chip_id);
```

Read the BMM350 chip ID (expected: 0x33). Auto-initializes the sensor if not already initialized.

| Parameter | Type | Description |
|-----------|------|-------------|
| `chip_id` | `uint8_t *` | Pointer to receive chip ID byte |

| Return | Description |
|--------|-------------|
| `true` | Read succeeded |
| `false` | Initialization or I3C read failed |

---

#### `bmm350_cal_update`

```c
void bmm350_cal_update(float mx, float my);
```

Feed raw magnetometer X/Y readings to the hard iron calibration tracker. Tracks min/max of both axes to compute the offset (center of the circle). The calibration improves as the user rotates the board through 360 degrees.

Calibration becomes **valid** when:
- Both X and Y axes have a spread > 15 uT (approximately half a rotation)
- At least 50 samples have been collected (~2 seconds at 25 Hz)

Offsets are recomputed every 10 samples once valid.

| Parameter | Type | Description |
|-----------|------|-------------|
| `mx` | `float` | Raw X-axis magnetic field in uT |
| `my` | `float` | Raw Y-axis magnetic field in uT |

---

#### `bmm350_cal_reset`

```c
void bmm350_cal_reset(void);
```

Reset all hard iron calibration state. Clears min/max tracking, offsets, sample count, and validity flag. Use this to start a fresh calibration cycle.

---

#### `bmm350_cal_get_offsets`

```c
void bmm350_cal_get_offsets(float *offset_x, float *offset_y, bool *valid);
```

Get the current hard iron calibration offsets.

| Parameter | Type | Description |
|-----------|------|-------------|
| `offset_x` | `float *` | X-axis offset in uT (or NULL to skip) |
| `offset_y` | `float *` | Y-axis offset in uT (or NULL to skip) |
| `valid` | `bool *` | Whether calibration is valid (or NULL to skip) |

---

#### `bmm350_heading_from_xy`

```c
float bmm350_heading_from_xy(float mx, float my);
```

Compute calibrated compass heading from raw magnetometer X/Y values. Applies hard iron compensation (if calibration is valid), computes `atan2(cx, cy)`, normalizes to [0, 360), and applies a 10-sample circular moving average.

**Board orientation:** +X = East, +Y = North (typical for KIT_PSE84_AI).

| Parameter | Type | Description |
|-----------|------|-------------|
| `mx` | `float` | Raw X-axis magnetic field in uT |
| `my` | `float` | Raw Y-axis magnetic field in uT |

| Return | Description |
|--------|-------------|
| `float` | Heading in degrees [0.0, 360.0) |

---

#### `bmm350_diagnose`

```c
int bmm350_diagnose(bmm350_diag_t *diag);
```

Full I3C diagnostic test with MIXED_FAST mode. Performs a complete hardware reset, re-initializes I3C in MIXED_FAST mode (unlike normal init which uses PURE mode), attaches BMM350 as an I2C device, and attempts a chip ID read. Captures detailed status at every step for debugging.

**Pauses the sensor auto-task** during the diagnostic and resumes it afterward.

| Parameter | Type | Description |
|-----------|------|-------------|
| `diag` | `bmm350_diag_t *` | Pointer to diagnostic result structure |

| Return | Description |
|--------|-------------|
| `0` | All steps passed, chip ID verified |
| `1` | I3C init failed |
| `2` | I2C device attach failed |
| `3` | Chip ID read failed or mismatch |

---

#### `bmm350_debug_read`

```c
void bmm350_debug_read(void);
```

Debug utility: force re-initialization, dump PMU status registers, ODR/AVG settings, and raw magnetic data in both Normal and Forced modes. Also performs raw I3C bus reads (without dummy byte skipping) for low-level protocol debugging.

Output is printed to the debug UART via `printf()`.

**Pauses the sensor auto-task** during debug and resumes afterward.

---

## 7. CapSense -- Capacitive Touch

**Header**: `sensor_capsense.h`
**Source**: `sensor_capsense.c`
**Hardware**: PSoC 4000T CapSense I2C slave
**Bus**: I2C (SCB0), address `0x08`
**Protocol**: Direct 3-byte read (no register addressing)
**Board Support**: Eva Kit only (`BSP_HAS_CAPSENSE=1`)

### 7.1 Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `CAPSENSE_I2C_ADDR` | `0x08` | I2C 7-bit address (PSoC 4000T slave) |
| `CAPSENSE_DATA_SIZE` | `3` | Bytes per read (btn0, btn1, slider) |

### 7.2 Structs

#### `capsense_data_t`

```c
typedef struct {
    bool btn0_pressed;      /* Button 0 state (true = pressed) */
    bool btn1_pressed;      /* Button 1 state (true = pressed) */
    uint8_t slider;         /* Slider position (0-100 %) */
} capsense_data_t;
```

### 7.3 Protocol Details

The PSoC 4000T CapSense slave uses a direct read protocol -- no register address is sent. A 3-byte I2C read returns:

| Byte | Content | Range |
|------|---------|-------|
| 0 | Button 0 code | Variable encoding (see below) |
| 1 | Button 1 code | Variable encoding |
| 2 | Slider position | 0-100 (percent) |

**Button code normalization:** The PSoC 4000T sends button state in various formats depending on firmware version. The driver normalizes all formats:
- ASCII digits `'0'`-`'9'` (0x30-0x39): subtract 0x30
- Decimal 30-32: subtract 30
- Direct 0-2: use as-is
- Any other non-zero value: treated as pressed (1)

**Baseline capture:** On first read after init, the driver captures the idle button codes as a baseline. Subsequent reads detect a press when the code differs from the baseline.

### 7.4 Functions

---

#### `capsense_init`

```c
bool capsense_init(void);
```

Initialize the CapSense driver. Ensures the I2C bus is initialized, reads a baseline (idle state) from the PSoC 4000T, and stores the idle button codes for press detection.

| Return | Description |
|--------|-------------|
| `true` | Initialization succeeded (baseline captured or defaulted) |
| `false` | I2C bus initialization failed |

Note: If the baseline read fails (PSoC 4000T not responding), initialization still succeeds with default baseline values (0, 0). This allows the system to boot without the CapSense base board.

---

#### `capsense_read`

```c
bool capsense_read(capsense_data_t *data);
```

Read all CapSense data (both buttons and slider) in a single 3-byte I2C transaction. Button states are determined by comparing against the baseline captured during init.

| Parameter | Type | Description |
|-----------|------|-------------|
| `data` | `capsense_data_t *` | Pointer to receive CapSense data |

| Return | Description |
|--------|-------------|
| `true` | Read succeeded |
| `false` | I2C read failed or sensor not initialized |

**MicroPython Equivalent:** `sensors.capsense()` returns `(btn0, btn1, slider)` tuple

**C Example:**
```c
capsense_data_t cs;
if (capsense_read(&cs)) {
    printf("Btn0=%d Btn1=%d Slider=%u%%\r\n",
           cs.btn0_pressed, cs.btn1_pressed, cs.slider);
}
```

---

#### `capsense_read_buttons`

```c
bool capsense_read_buttons(bool *btn0, bool *btn1);
```

Read button states only. Internally calls `capsense_read()` and extracts button fields.

| Parameter | Type | Description |
|-----------|------|-------------|
| `btn0` | `bool *` | Button 0 state (true = pressed) |
| `btn1` | `bool *` | Button 1 state (true = pressed) |

| Return | Description |
|--------|-------------|
| `true` | Read succeeded |
| `false` | Read failed |

---

#### `capsense_read_slider`

```c
bool capsense_read_slider(uint8_t *position);
```

Read slider position only. Internally calls `capsense_read()`.

| Parameter | Type | Description |
|-----------|------|-------------|
| `position` | `uint8_t *` | Slider position (0-100 percent) |

| Return | Description |
|--------|-------------|
| `true` | Read succeeded |
| `false` | Read failed |

---

## 8. Potentiometer -- ADC Input

**Header**: `sensor_potentiometer.h`
**Source**: `sensor_potentiometer.c`
**Hardware**: On-board potentiometer on Eva Kit evaluation board
**Interface**: SAR ADC, P15[1] (CYBSP_ADC_6_POT)
**Board Support**: Eva Kit only (`BSP_HAS_POTENTIOMETER=1`)

### 8.1 Internal Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `POTENTIOMETER_VDDA_V` | `3.3` | Reference voltage (VDDA) |

### 8.2 Notes on ADC Resolution

The PSoC Edge SAR ADC has native 12-bit resolution. The MTB HAL `mtb_hal_adc_read_u16()` function scales the result to 16-bit (0x0000 = 0V, 0xFFFF = VDDA). The effective resolution remains 12 bits (4096 discrete levels).

### 8.3 Functions

---

#### `potentiometer_init`

```c
bool potentiometer_init(void);
```

Initialize the SAR ADC for potentiometer reading. Uses the BSP-generated `CYBSP_SAR_ADC_hal_config` configuration. Enables the ADC and verifies that channel 0 is configured.

| Return | Description |
|--------|-------------|
| `true` | ADC initialized and channel 0 ready |
| `false` | ADC setup, enable, or channel configuration failed |

**Thread Safety:** Idempotent.

---

#### `potentiometer_read_raw`

```c
bool potentiometer_read_raw(uint16_t *value);
```

Read the raw 16-bit ADC value. 0x0000 corresponds to 0V, 0xFFFF corresponds to VDDA (3.3V).

| Parameter | Type | Description |
|-----------|------|-------------|
| `value` | `uint16_t *` | Raw 16-bit ADC value |

| Return | Description |
|--------|-------------|
| `true` | Read succeeded |
| `false` | ADC not initialized and auto-init failed |

**Thread Safety:** Safe (HAL ADC reads are atomic on PSoC).

**MicroPython Equivalent:** `sensors.potentiometer()` returns raw integer

---

#### `potentiometer_read_percent`

```c
bool potentiometer_read_percent(float *percent);
```

Read the potentiometer position as a percentage (0.0 - 100.0).

**Conversion:** `percent = (raw / 65535.0) * 100.0`

| Parameter | Type | Description |
|-----------|------|-------------|
| `percent` | `float *` | Position percentage (0.0 - 100.0) |

| Return | Description |
|--------|-------------|
| `true` | Read succeeded |
| `false` | ADC read failed |

---

#### `potentiometer_read_voltage`

```c
bool potentiometer_read_voltage(float *voltage);
```

Read the potentiometer voltage in volts (0.0 - 3.3V, assuming VDDA = 3.3V).

**Conversion:** `voltage = (raw / 65535.0) * 3.3`

| Parameter | Type | Description |
|-----------|------|-------------|
| `voltage` | `float *` | Voltage in volts (0.0 - 3.3) |

| Return | Description |
|--------|-------------|
| `true` | Read succeeded |
| `false` | ADC read failed |

---

## 9. Sensor Auto Task -- Background Polling

**Header**: `sensor_auto_task.h`
**Source**: `sensor_auto_task.c`
**RTOS**: FreeRTOS task (`SensorAuto`, priority 2, 4 KB stack)

The Sensor Auto Task is a background FreeRTOS task that periodically reads all enabled sensors and pushes the data via IPC to the CM55 core for LVGL dashboard display. It starts automatically at boot and can be paused/resumed by MicroPython.

### 9.1 Constants and Bitmask Flags

| Constant | Value | Description |
|----------|-------|-------------|
| `SENSOR_AUTO_BMI270` | `0x01` (bit 0) | BMI270 IMU enable flag |
| `SENSOR_AUTO_DPS368` | `0x02` (bit 1) | DPS368 barometer enable flag |
| `SENSOR_AUTO_SHT40` | `0x04` (bit 2) | SHT40 climate sensor enable flag |
| `SENSOR_AUTO_BMM350` | `0x08` (bit 3) | BMM350 magnetometer enable flag |
| `SENSOR_AUTO_CAPSENSE` | `0x10` (bit 4) | CapSense enable flag |
| `SENSOR_AUTO_POT` | `0x20` (bit 5) | Potentiometer enable flag |
| `SENSOR_AUTO_ALL` | `0x3F` | All sensors enabled |

### 9.2 Internal Configuration

| Parameter | Value | Description |
|-----------|-------|-------------|
| `AUTO_TASK_STACK_SIZE` | 4096 bytes | FreeRTOS task stack |
| `AUTO_TASK_PRIORITY` | 2 | Below MicroPython (3), below WiFi/TCPIP (4) |
| `AUTO_TASK_NAME` | `"SensorAuto"` | Task name for debugger |
| `AUTO_IPC_SEND_RETRIES` | 20 | Max IPC send retries |
| `AUTO_IPC_RETRY_DELAY_US` | 100 | Delay between IPC retries |
| `AUTO_IPC_GAP_US` | 200 | Gap between consecutive IPC messages |
| `AUTO_I2C_LOCK_TIMEOUT_MS` | 50 | I2C mutex wait timeout |
| Default interval | 100 ms | Default push rate (10 Hz) |

### 9.3 Functions

---

#### `sensor_auto_task_create`

```c
void sensor_auto_task_create(void);
```

Create the auto-sensor FreeRTOS task. Call from `main()` **before** the FreeRTOS scheduler starts. The task will initialize all sensors on first run and begin periodic push cycles.

**Thread Safety:** Must be called once from startup code only.

---

#### `sensor_auto_start`

```c
void sensor_auto_start(void);
```

Start or resume the auto-push cycle. If the task was previously stopped with `sensor_auto_stop()`, this resumes it.

**Thread Safety:** Safe to call from any task.

**MicroPython Equivalent:** `sensors.auto(True)`

---

#### `sensor_auto_stop`

```c
void sensor_auto_stop(void);
```

Pause the auto-push cycle. The FreeRTOS task is suspended, consuming zero CPU. The I2C bus is released for exclusive use by MicroPython or other tasks.

**Thread Safety:** Safe to call from any task.

**MicroPython Equivalent:** `sensors.auto(False)`

---

#### `sensor_auto_is_running`

```c
bool sensor_auto_is_running(void);
```

Check whether the auto-push is currently running (not paused).

| Return | Description |
|--------|-------------|
| `true` | Auto-push is active |
| `false` | Auto-push is stopped/paused |

---

#### `sensor_auto_set_rate`

```c
void sensor_auto_set_rate(uint32_t interval_ms);
```

Set the push interval in milliseconds. Clamped to the range [50, 5000].

| Parameter | Type | Description |
|-----------|------|-------------|
| `interval_ms` | `uint32_t` | Push interval (50-5000 ms) |

**MicroPython Equivalent:** `sensors.auto_rate(200)` (set to 200 ms = 5 Hz)

---

#### `sensor_auto_get_rate`

```c
uint32_t sensor_auto_get_rate(void);
```

Get the current push interval in milliseconds.

| Return | Description |
|--------|-------------|
| `uint32_t` | Current interval (50-5000 ms, default 100) |

---

#### `sensor_auto_set_mask`

```c
void sensor_auto_set_mask(uint32_t mask);
```

Set the enabled sensor bitmask. Only sensors in the mask will be polled during auto-push.

| Parameter | Type | Description |
|-----------|------|-------------|
| `mask` | `uint32_t` | Bitmask of `SENSOR_AUTO_*` flags |

**C Example:**
```c
/* Only poll BMI270 and DPS368 */
sensor_auto_set_mask(SENSOR_AUTO_BMI270 | SENSOR_AUTO_DPS368);
```

---

#### `sensor_auto_get_mask`

```c
uint32_t sensor_auto_get_mask(void);
```

Get the current enabled sensor bitmask.

| Return | Description |
|--------|-------------|
| `uint32_t` | Current bitmask of enabled sensors |

---

#### `sensor_auto_enable`

```c
void sensor_auto_enable(uint32_t flag);
```

Enable a specific sensor in the bitmask (OR operation).

| Parameter | Type | Description |
|-----------|------|-------------|
| `flag` | `uint32_t` | Single `SENSOR_AUTO_*` flag to enable |

---

#### `sensor_auto_disable`

```c
void sensor_auto_disable(uint32_t flag);
```

Disable a specific sensor in the bitmask (AND-NOT operation).

| Parameter | Type | Description |
|-----------|------|-------------|
| `flag` | `uint32_t` | Single `SENSOR_AUTO_*` flag to disable |

---

#### `sensor_auto_get_push_count`

```c
uint32_t sensor_auto_get_push_count(void);
```

Get the total number of push cycles completed since boot.

| Return | Description |
|--------|-------------|
| `uint32_t` | Cumulative push cycle count |

---

#### `sensor_auto_is_delete_pending`

```c
bool sensor_auto_is_delete_pending(void);
```

Check and consume a delete-main.py request. This flag is set by an IPC ISR callback when the CM55 UI requests deletion of `/main.py`. Polling this from task context (e.g., `tacp_poll_uart()`) triggers a soft reset.

| Return | Description |
|--------|-------------|
| `true` | Delete request was pending (flag is now consumed/cleared) |
| `false` | No pending request |

---

#### `sensor_auto_push_wifi_state`

```c
void sensor_auto_push_wifi_state(bool connected);
```

Push WiFi connected/disconnected state to the CM55 topbar via IPC. Call after `wifi.connect()` or `wifi.disconnect()` succeeds.

| Parameter | Type | Description |
|-----------|------|-------------|
| `connected` | `bool` | `true` = connected, `false` = disconnected |

**Thread Safety:** Safe to call from any CM33_NS task.

---

#### `sensor_auto_ntp_and_push_time`

```c
void sensor_auto_ntp_and_push_time(void);
```

Perform NTP time synchronization and push the current time to the CM55 topbar. Call after WiFi connection is established. Uses the system NTP sync module, then formats the time and sends it via IPC.

**Thread Safety:** Safe to call from any CM33_NS task. Internally accesses network stack.

---

## 10. Board Support Matrix

### Sensor Availability by Board

| Sensor | Driver | AI Kit | Eva Kit | Game Console |
|--------|--------|--------|---------|--------------|
| BMI270 (Accel+Gyro) | `sensor_bmi270.c` | Yes | Yes | Yes |
| DPS368 (Barometer) | `sensor_dps368.c` | Yes | -- | -- |
| SHT40 (Humidity/Temp) | `sensor_sht40.c` | Yes | -- | -- |
| BMM350 (Magnetometer) | `sensor_bmm350.c` | Yes | Yes | -- |
| CapSense (Touch) | `sensor_capsense.c` | -- | Yes | -- |
| Potentiometer (ADC) | `sensor_potentiometer.c` | -- | Yes | -- |

### BSP Feature Flags

Compile-time guards control which sensor drivers are included in each build:

| Flag | AI Kit | Eva Kit | Game Console |
|------|--------|---------|--------------|
| `BSP_HAS_BMI270` | 1 | 1 | 1 |
| `BSP_HAS_DPS368` | 1 | 0 | 0 |
| `BSP_HAS_SHT40` | 1 | 0 | 0 |
| `BSP_HAS_BMM350` | 1 | 1 | 0 |
| `BSP_HAS_CAPSENSE` | 0 | 1 | 0 |
| `BSP_HAS_POTENTIOMETER` | 0 | 1 | 0 |

### I2C Address Map (SCB0 bus, 0x08-0x77)

| Address | Sensor | Board |
|---------|--------|-------|
| `0x08` | PSoC 4000T CapSense slave | Eva Kit |
| `0x44` | SHT40 | AI Kit |
| `0x68` | BMI270 | AI Kit, Eva Kit, Game Console |
| `0x77` | DPS368 | AI Kit |

### I3C Bus (separate from I2C)

| Address | Sensor | Mode | Board |
|---------|--------|------|-------|
| `0x15` | BMM350 | PURE + SETAASA (I3C SDR) | AI Kit, Eva Kit |

---

## 11. Error Handling Conventions

All sensor functions follow a consistent error handling pattern:

1. **Return `bool`**: `true` = success, `false` = failure.
2. **No error codes**: Detailed error information is printed to the debug UART via `printf()` in diagnostic functions only. Normal read functions fail silently.
3. **Auto-initialization**: Most read functions call `<sensor>_init()` if the sensor is not yet initialized. This allows lazy initialization.
4. **Idempotent init**: Calling `<sensor>_init()` multiple times is safe -- subsequent calls return `true` immediately.
5. **Timeout protection**: All I2C/I3C transfers have a 50 ms timeout to prevent indefinite blocking.
6. **CRC verification**: SHT40 reads verify CRC-8 on every response. CRC failure returns `false`.

### Common Failure Causes

| Symptom | Likely Cause | Recovery |
|---------|-------------|----------|
| `sensor_i2c_init()` returns `false` | SCB0 clock or pin misconfiguration | Check BSP configurator output |
| `bmi270_init()` fails at config upload | I2C bus noise or timing | Retry; check 1.8V supply |
| `bmm350_init()` fails at chip ID | SETAASA did not assign dynamic address | Call `bmm350_reinit()` |
| `dps368_read_pressure()` times out | Sensor in wrong measurement mode | Call `dps368_init()` again |
| `sht40_read_*()` CRC failure | I2C bus noise | Retry the read |
| `capsense_read()` returns `false` | PSoC 4000T not present or I2C NACK | Check base board connection |

---

## 12. Thread Safety Model

### Bus Ownership

| Bus | Owner | Sharing Mechanism |
|-----|-------|-------------------|
| SCB0 I2C | Any CM33_NS task | FreeRTOS mutex (`sensor_i2c_lock/unlock`) |
| I3C (BMM350) | Sensor Auto Task exclusively | Auto-task pause (`sensor_auto_stop()`) before direct access |

### Task Priority Ordering

| Priority | Task | Description |
|----------|------|-------------|
| 6 | RTSP Server | Highest -- camera streaming |
| 5 | WHD Thread | WiFi host driver |
| 4 | RTP Streamer, TCPIP, WCM | Network stack |
| 3 | MicroPython | User scripts |
| 2 | Sensor Auto Task, WiFi IPC | Background polling |

### Safe Access Patterns

**Pattern 1: Direct sensor read from MicroPython task**
```c
sensor_auto_stop();                      /* Pause auto-task */
Cy_SysLib_Delay(200);                    /* Wait for task to suspend */
sensor_i2c_lock(50);                     /* Acquire I2C mutex */
bmi270_read_accel(&ax, &ay, &az);        /* Read sensor */
sensor_i2c_unlock();                     /* Release mutex */
sensor_auto_start();                     /* Resume auto-task */
```

**Pattern 2: BMM350 access (I3C bus)**
```c
bool was_running = sensor_auto_is_running();
if (was_running) {
    sensor_auto_stop();
    Cy_SysLib_Delay(200);                /* Wait for in-flight I3C transfer */
}
bmm350_read_xyz(&mx, &my, &mz);
if (was_running) {
    sensor_auto_start();
}
```

### Known Concurrency Hazards

1. **I3C bus contention**: The BMM350 I3C bus has no mutex. Direct BMM350 reads concurrent with auto-task will corrupt the I3C controller state and freeze ALL sensors. Always pause auto-task first.
2. **IPC pipe deadlock**: Never call `Cy_IPC_Pipe_SendMessage()` from within an IPC handler callback. Use a deferred flag + task-context send pattern.
3. **I2C mutex starvation**: If the auto-task runs at high frequency (50 ms) and MicroPython tries to read sensors, the 50 ms I2C lock timeout may expire. Lower the auto-task rate or pause it during direct reads.

---

## Appendix: File Inventory

| File | Description |
|------|-------------|
| `sensor_i2c.h / .c` | Shared I2C bus driver (SCB0, 400 kHz) |
| `sensor_bmi270.h / .c` | BMI270 6-axis IMU driver |
| `sensor_dps368.h / .c` | DPS368 barometric pressure sensor driver |
| `sensor_sht40.h / .c` | SHT40 humidity + temperature sensor driver |
| `sensor_bmm350.h / .c` | BMM350 3-axis magnetometer driver (I3C) |
| `sensor_capsense.h / .c` | CapSense touch driver (PSoC 4000T I2C slave) |
| `sensor_potentiometer.h / .c` | Potentiometer ADC driver |
| `sensor_auto_task.h / .c` | Background sensor polling FreeRTOS task |
| `bsp_feature_flags.h` | Board-specific `BSP_HAS_*` compile guards |
| `bmi270_config_data.h` | BMI270 8 KB configuration blob |
| `modsensors.c` | MicroPython `sensors` module C bindings |

---

*BENTO PSoC Edge E84 Firmware -- Sensor Driver API v1.0*
*Last updated: 2026-03-14*
