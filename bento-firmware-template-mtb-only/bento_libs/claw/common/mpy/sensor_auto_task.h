/*******************************************************************************
 * File Name: sensor_auto_task.h
 *
 * Description: Auto-sensor background FreeRTOS task.
 *              Reads all sensors and pushes data via IPC to CM55 for LVGL
 *              dashboard display. Starts automatically on boot.
 *              MicroPython can pause/resume and control per-sensor enable.
 *
 *******************************************************************************/

#ifndef SENSOR_AUTO_TASK_H
#define SENSOR_AUTO_TASK_H

#include <stdbool.h>
#include <stdint.h>

/* Sensor enable bitmask flags */
#define SENSOR_AUTO_BMI270      (1 << 0)
#define SENSOR_AUTO_DPS368      (1 << 1)
#define SENSOR_AUTO_SHT40       (1 << 2)
#define SENSOR_AUTO_BMM350      (1 << 3)
#define SENSOR_AUTO_CAPSENSE    (1 << 4)
#define SENSOR_AUTO_POT         (1 << 5)
#define SENSOR_AUTO_ALL         (0x3F)

/* Cached BMI270 sensor data (updated every auto-push cycle) */
typedef struct {
    float ax, ay, az;       /* Accel in m/s² */
    float gx, gy, gz;       /* Gyro in deg/s */
    float temp;             /* Die temperature °C */
    bool  valid;            /* true after first successful read */
} sensor_auto_bmi270_cache_t;

/* Get latest cached BMI270 values (lock-free, updated every ~100ms) */
void sensor_auto_get_bmi270(sensor_auto_bmi270_cache_t *out);

/* Create the auto-sensor FreeRTOS task (call from main before scheduler) */
void sensor_auto_task_create(void);

/* Start/resume auto-push */
void sensor_auto_start(void);

/* Pause auto-push (task suspended, zero CPU) */
void sensor_auto_stop(void);

/* Check if auto-push is running */
bool sensor_auto_is_running(void);

/* Set push interval in milliseconds (min 50, max 5000) */
void sensor_auto_set_rate(uint32_t interval_ms);
uint32_t sensor_auto_get_rate(void);

/* Per-sensor enable/disable */
void sensor_auto_set_mask(uint32_t mask);
uint32_t sensor_auto_get_mask(void);
void sensor_auto_enable(uint32_t flag);
void sensor_auto_disable(uint32_t flag);

/* Total push cycles since boot */
uint32_t sensor_auto_get_push_count(void);

/* Check and consume delete-main.py request (set by IPC ISR, polled from task) */
bool sensor_auto_is_delete_pending(void);

/* Check and consume restart-script request (set by IPC ISR, polled from task) */
bool sensor_auto_is_restart_pending(void);

/*******************************************************************************
 * WiFi State + Time Push to CM55 (callable from any CM33_NS task/module)
 *
 * Sends non-blocking IPC push to CM55 topbar.
 * Call after wifi.connect()/disconnect() succeeds.
 ******************************************************************************/

/** Push WiFi connected/disconnected state to CM55 topbar */
void sensor_auto_push_wifi_state(bool connected);

/** Push BLE NUS host-link state to CM55 topbar.
 * Caller passes true only on the BLE_NUS_STATE_CONNECTED transition;
 * advertising/idle/error must pass false so the LCD icon stays hidden
 * unless a Bento Desktop Buddy session is actually live. */
void sensor_auto_push_ble_state(bool connected);

/** Try NTP sync + push time to CM55. Call after WiFi connect succeeds. */
void sensor_auto_ntp_and_push_time(void);

/** Lock protecting the boot WiFi credential globals
 * (g_boot_wifi_creds, g_boot_wifi_creds_count, g_boot_wifi_creds_dirty).
 * Recursive: the same task may take it more than once. Lazy-init on
 * the first call so pre-scheduler boot paths are safe.
 *
 * Take the lock around any sequence that reads or writes more than
 * one byte of any of those three globals. The struct is ~100 B; on
 * Cortex-M33 a torn-write window between the BLE worker, the WiFi
 * IPC handler, and the REPL flush task is real. */
void wifi_creds_lock(void);
void wifi_creds_unlock(void);

#endif /* SENSOR_AUTO_TASK_H */
