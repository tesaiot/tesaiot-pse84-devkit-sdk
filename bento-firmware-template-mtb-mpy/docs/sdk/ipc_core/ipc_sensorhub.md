# ipc_sensorhub.h

CM55 IPC receiver for sensor data from CM33_NS MicroPython. Stores latest sensor samples from all sensors. Thread-safe: ISR writes volatile data, getters read snapshots.

## Functions (exported by the archive)

### `ipc_sensorhub_ble_connected`

```c
bool ipc_sensorhub_ble_connected(void);
```

Returns true if CM33_NS reported BLE NUS host CONNECTED via IPC_CMD_BLE_STATE_PUSH. Only true while a host is connected; advertising/idle return false.

### `ipc_sensorhub_feed_bmi270`

```c
void ipc_sensorhub_feed_bmi270(const ipc_sensor_bmi270_t *data);
```

Local Feed API — inject sensor data directly from CM55 (Eva Kit). On Eva Kit, CM33_NS cannot access SCB0 I2C (display owns it). CM55 reads sensors locally and feeds data here instead of via IPC.

### `ipc_sensorhub_feed_bmm350`

```c
void ipc_sensorhub_feed_bmm350(const ipc_sensor_bmm350_t *data);
```

_No description in the header._

### `ipc_sensorhub_feed_capsense`

```c
void ipc_sensorhub_feed_capsense(const ipc_sensor_capsense_t *data);
```

_No description in the header._

### `ipc_sensorhub_feed_pot`

```c
void ipc_sensorhub_feed_pot(const ipc_sensor_pot_t *data);
```

_No description in the header._

### `ipc_sensorhub_get_time_str`

```c
bool ipc_sensorhub_get_time_str(char *buf, size_t buf_size);
```

Copy the latest time string from CM33_NS (e.g. "Mon 10 Mar 14:35"). Returns false if NTP not yet synced. buf must be >= 32 bytes.

### `ipc_sensorhub_init`

```c
bool ipc_sensorhub_init(void);
```

File Name: ipc_sensorhub.h Description: CM55 IPC receiver for sensor data from CM33_NS MicroPython. Stores latest sensor samples from all sensors. Thread-safe: ISR writes volatile data, getters read snapshots. / #ifndef IPC_SENSORHUB_H #define IPC_SENSORHUB_H #include "ipc_communication.h" #include <stdbool.h> #include <stdint.h> /* Snapshot of all sensor data with change-detection flags */ typedef struct { /* BMI270 IMU */ ipc_sensor_bmi270_t bmi270; bool has_bmi270; bool bmi270_changed; /* DPS368 Barometric Pressure (AI Kit) */ ipc_sensor_dps368_t dps368; bool has_dps368; bool dps368_changed; /* SHT40 Humidity + Temperature (AI Kit) */ ipc_sensor_sht40_t sht40; bool has_sht40; bool sht40_changed; /* BMM350 Magnetometer */ ipc_sensor_bmm350_t bmm350; bool has_bmm350; bool bmm350_changed; /* CapSense Touch (requires base board) */ ipc_sensor_capsense_t capsense; bool has_capsense; bool capsense_changed; /* Potentiometer (requires base board) */ ipc_sensor_pot_t pot; bool has_pot; bool pot_changed; /* GPIO LED State bitmask (both BSPs) */ uint8_t led_state_bitmask; bool has_led_state; bool led_state_changed; } sensorhub_snapshot_t; /** Initialize IPC sensorhub receiver. Registers IPC pipe callback on CM55_IPC_SENSOR_CLIENT_ID. Must be called AFTER cm55_ipc_communication_setup(). @return true on success.

### `ipc_sensorhub_ntp_synced`

```c
bool ipc_sensorhub_ntp_synced(void);
```

Returns true after CM33_NS sends IPC_CMD_NTP_SYNCED (RTC is valid).

### `ipc_sensorhub_ntp_synced`

```c
bool ipc_sensorhub_ntp_synced(void);
```

Returns true if CM33_NS has sent at least one IPC_CMD_TIME_PUSH (NTP synced)

### `ipc_sensorhub_snapshot`

```c
void ipc_sensorhub_snapshot(sensorhub_snapshot_t *snap);
```

Take a snapshot of the latest sensor data. Clears the "changed" flags after reading. Safe to call from any task context. @param snap  Output snapshot struct.

### `ipc_sensorhub_weather`

```c
bool ipc_sensorhub_weather(weather_ipc_t *out);
```

Latest weather pushed from CM33_NS. False until the first push lands.

### `ipc_sensorhub_wifi_connected`

```c
bool ipc_sensorhub_wifi_connected(void);
```

WiFi State + Time (pushed from CM33_NS via IPC, zero I2C involvement) / /** Returns true if CM33_NS reported WiFi connected via IPC_CMD_WIFI_STATE_PUSH

## Structs

### `sensorhub_snapshot_t`

```c
typedef struct {
    /* BMI270 IMU */
    ipc_sensor_bmi270_t bmi270;
    bool has_bmi270;
    bool bmi270_changed;

    /* DPS368 Barometric Pressure (AI Kit) */
    ipc_sensor_dps368_t dps368;
    bool has_dps368;
    bool dps368_changed;

    /* SHT40 Humidity + Temperature (AI Kit) */
    ipc_sensor_sht40_t sht40;
    bool has_sht40;
    bool sht40_changed;

    /* BMM350 Magnetometer */
    ipc_sensor_bmm350_t bmm350;
    bool has_bmm350;
    bool bmm350_changed;

    /* CapSense Touch (requires base board) */
    ipc_sensor_capsense_t capsense;
    bool has_capsense;
    bool capsense_changed;

    /* Potentiometer (requires base board) */
    ipc_sensor_pot_t pot;
    bool has_pot;
    bool pot_changed;

    /* GPIO LED State bitmask (both BSPs) */
    uint8_t led_state_bitmask;
    bool has_led_state;
    bool led_state_changed;} sensorhub_snapshot_t;
```

## Constants

| Name | Value |
|---|---|
| `IPC_SENSORHUB_H` | `#include` |
