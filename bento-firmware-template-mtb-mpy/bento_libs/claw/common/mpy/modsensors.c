/*******************************************************************************
 * File Name: modsensors.c
 *
 * Description: MicroPython 'sensors' module for CM33_NS.
 *              Provides access to on-board I2C sensors:
 *              - BMI270 (6-axis IMU, accel + gyro) — both boards
 *              - DPS368 (barometric pressure + temperature) — AI Kit only
 *              - SHT40 (humidity + temperature) — AI Kit only
 *
 *              BSP-conditional compilation via bsp_feature_flags.h:
 *              - DPS368/SHT40 sub-modules in separate files (modsensors_dps368.c etc.)
 *              - Radar via IPC to CM55 (AI Kit only)
 *
 *              Also provides sensors.push() to send data to CM55 via IPC
 *              for the LVGL sensor dashboard.
 *
 *******************************************************************************/

#include "py/runtime.h"
#include "py/obj.h"
#include "py/objlist.h"

#include "bsp_feature_flags.h"

#include "sensor_i2c.h"
#include "sensor_bmi270.h"

#if BSP_HAS_DPS368
#include "sensor_dps368.h"
#endif

#if BSP_HAS_SHT40
#include "sensor_sht40.h"
#endif

#if BSP_HAS_BMM350
#include "sensor_bmm350.h"
#endif

#if BSP_HAS_CAPSENSE
#include "sensor_capsense.h"
#include "tacp.h"
#include "FreeRTOS.h"
#include "task.h"
#include "py/mphal.h"
#endif

#if BSP_HAS_POTENTIOMETER
#include "sensor_potentiometer.h"
#endif

#include "ipc_communication.h"
#include "sensor_auto_task.h"
#include "cy_scb_uart.h"
#include <string.h>

/* IPC message buffer for sensor data push (in shared memory) */
CY_SECTION_SHAREDMEM static ipc_msg_t sensor_ipc_msg;

/* IPC send retry config */
#define SENSOR_IPC_SEND_RETRIES    (20)
#define SENSOR_IPC_RETRY_DELAY_US  (100)

/* Sequence counter for IPC messages. Only the push paths stamp it, and those
 * are compiled out on Eva Kit, so the variable would be unused there and
 * -Werror=unused-variable stops the build. */
#ifndef USE_KIT_PSE84_EVAL_EPC2
static uint16_t sensor_seq = 0;
#endif

/* Check UART RX FIFO for Ctrl+C (0x03).
 * The PSoC Edge port uses CORE_FEATURES ROM level which disables
 * MICROPY_KBD_EXCEPTION, so mp_sched_keyboard_interrupt() is unavailable.
 * We check the UART hardware directly and return true if Ctrl+C found. */
static bool check_uart_ctrl_c(void) {
    while (Cy_SCB_UART_GetNumInRxFifo(CYBSP_DEBUG_UART_HW) > 0) {
        uint32_t c = Cy_SCB_UART_Get(CYBSP_DEBUG_UART_HW);
        if (c == 0x03) {
            return true;
        }
    }
    return false;
}

/* Flag: IPC pipe initialized on CM33_NS */
static bool sensor_ipc_initialized = false;

/* Lazy IPC pipe initialization (same pattern as modlcd.c) */
static void ensure_sensor_ipc(void) {
    if (!sensor_ipc_initialized) {
        cm33_ipc_communication_setup();
        Cy_SysLib_Delay(50); /* Let CM55 IPC settle */
        sensor_ipc_initialized = true;
    }
}

/* Send sensor data via IPC to CM55 */
static bool sensor_ipc_send(uint32_t cmd, const void *data, size_t len) {
    if (len > IPC_DATA_MAX_LEN) return false;
    ensure_sensor_ipc();

    memset(&sensor_ipc_msg, 0, sizeof(sensor_ipc_msg));
    sensor_ipc_msg.client_id = CM55_IPC_SENSOR_CLIENT_ID;
    sensor_ipc_msg.intr_mask = CY_IPC_CYPIPE_INTR_MASK_EP1;
    sensor_ipc_msg.cmd = cmd;
    sensor_ipc_msg.value = (uint32_t)len;
    memcpy(sensor_ipc_msg.data, data, len);

    int retries = SENSOR_IPC_SEND_RETRIES;
    cy_en_ipc_pipe_status_t status;
    do {
        status = Cy_IPC_Pipe_SendMessage(
            CM55_IPC_PIPE_EP_ADDR, CM33_IPC_PIPE_EP_ADDR,
            (void *)&sensor_ipc_msg, NULL);
        if (CY_IPC_PIPE_SUCCESS == status) return true;
        Cy_SysLib_DelayUs(SENSOR_IPC_RETRY_DELAY_US);
    } while (--retries > 0);

    return false;
}

/* ========================================================================== */
/* BMI270 submodule (both boards)                                             */
/* ========================================================================== */

#ifdef USE_KIT_PSE84_EVAL_EPC2
/* On Eva Kit every accessor below reads through CM55's snapshot instead of the
 * I2C bus. Guarding only sensors.init() and sensors.scan() was not enough: the
 * accessors reach sensor_i2c_read_reg() directly, and sensor_i2c_lock() returns
 * true when its mutex was never created, so an unguarded call put a second
 * master on a live SCB0 that the display owns. That is the wedge no reset
 * short of the debugger cleared.
 *
 * Redirecting rather than refusing keeps every lesson in sessions 5 to 8
 * running exactly as written -- sensors.bmi270.motion() still returns six
 * floats, it just gets them from the core that is allowed to ask. */
typedef struct { int16_t ax, ay, az, gx, gy, gz; uint16_t sequence; } eva_imu_t;
bool eva_snapshot_imu(eva_imu_t *out);
bool eva_snapshot_capsense(uint8_t *b0, uint8_t *b1, uint8_t *slider);
bool eva_snapshot_pot(uint16_t *raw, uint16_t *percent_x10);

static void eva_imu_or_raise(float *ax, float *ay, float *az,
                             float *gx, float *gy, float *gz) {
    eva_imu_t s;
    if (!eva_snapshot_imu(&s)) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT(
            "BMI270: CM55 did not answer. The display core owns this bus on "
            "Eva Kit -- values come from its snapshot, and it is not replying"));
    }
    if (ax) *ax = (float)s.ax / IPC_BMI270_ACCEL_LSB_PER_G * 9.80665f;
    if (ay) *ay = (float)s.ay / IPC_BMI270_ACCEL_LSB_PER_G * 9.80665f;
    if (az) *az = (float)s.az / IPC_BMI270_ACCEL_LSB_PER_G * 9.80665f;
    if (gx) *gx = (float)s.gx / IPC_BMI270_GYRO_LSB_PER_DPS;
    if (gy) *gy = (float)s.gy / IPC_BMI270_GYRO_LSB_PER_DPS;
    if (gz) *gz = (float)s.gz / IPC_BMI270_GYRO_LSB_PER_DPS;
}
#endif /* USE_KIT_PSE84_EVAL_EPC2 */

static mp_obj_t bmi270_acceleration(void) {
#ifdef USE_KIT_PSE84_EVAL_EPC2
    float sx, sy, sz;
    eva_imu_or_raise(&sx, &sy, &sz, NULL, NULL, NULL);
    mp_obj_t eva[3] = { mp_obj_new_float(sx), mp_obj_new_float(sy),
                        mp_obj_new_float(sz) };
    return mp_obj_new_tuple(3, eva);
#else
    float ax, ay, az;
    sensor_i2c_lock(100);
    bool ok = bmi270_read_accel(&ax, &ay, &az);
    sensor_i2c_unlock();
    if (!ok) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("BMI270 accel read failed"));
    }
    mp_obj_t items[3] = {
        mp_obj_new_float(ax), mp_obj_new_float(ay), mp_obj_new_float(az)
    };
    return mp_obj_new_tuple(3, items);
#endif
}
static MP_DEFINE_CONST_FUN_OBJ_0(bmi270_acceleration_obj, bmi270_acceleration);

static mp_obj_t bmi270_gyroscope(void) {
#ifdef USE_KIT_PSE84_EVAL_EPC2
    float sx, sy, sz;
    eva_imu_or_raise(NULL, NULL, NULL, &sx, &sy, &sz);
    mp_obj_t eva[3] = { mp_obj_new_float(sx), mp_obj_new_float(sy),
                        mp_obj_new_float(sz) };
    return mp_obj_new_tuple(3, eva);
#else
    float gx, gy, gz;
    sensor_i2c_lock(100);
    bool ok = bmi270_read_gyro(&gx, &gy, &gz);
    sensor_i2c_unlock();
    if (!ok) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("BMI270 gyro read failed"));
    }
    mp_obj_t items[3] = {
        mp_obj_new_float(gx), mp_obj_new_float(gy), mp_obj_new_float(gz)
    };
    return mp_obj_new_tuple(3, items);
#endif
}
static MP_DEFINE_CONST_FUN_OBJ_0(bmi270_gyroscope_obj, bmi270_gyroscope);

static mp_obj_t bmi270_temperature(void) {
#ifdef USE_KIT_PSE84_EVAL_EPC2
    /* CM55's snapshot carries no die temperature, and the IMU's own reading is
     * not a room thermometer anyway. Refuse plainly rather than invent a
     * number, and say where a real temperature would have to come from. */
    mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT(
        "bmi270.temperature(): not carried in the CM55 snapshot on Eva Kit, "
        "and this board has no SHT40"));
    return mp_const_none;
#else
    float temp;
    sensor_i2c_lock(100);
    bool ok = bmi270_read_temperature(&temp);
    sensor_i2c_unlock();
    if (!ok) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("BMI270 temp read failed"));
    }
    return mp_obj_new_float(temp);
#endif
}
static MP_DEFINE_CONST_FUN_OBJ_0(bmi270_temperature_obj, bmi270_temperature);

/* sensors.bmi270.motion() -> (ax, ay, az, gx, gy, gz)
 * All six axes under ONE bus lock — a coherent snapshot for fusion/teaching
 * (two separate calls can interleave with the auto task between them). */
static mp_obj_t bmi270_motion(void) {
    float ax, ay, az, gx, gy, gz;
#ifdef USE_KIT_PSE84_EVAL_EPC2
    /* One snapshot for all six axes, so they are coherent by construction --
     * the same property the bus lock gives the other boards. */
    eva_imu_or_raise(&ax, &ay, &az, &gx, &gy, &gz);
#else
    sensor_i2c_lock(100);
    bool ok = bmi270_read_accel(&ax, &ay, &az) &&
              bmi270_read_gyro(&gx, &gy, &gz);
    sensor_i2c_unlock();
    if (!ok) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("BMI270 motion read failed"));
    }
#endif
    mp_obj_t items[6] = {
        mp_obj_new_float(ax), mp_obj_new_float(ay), mp_obj_new_float(az),
        mp_obj_new_float(gx), mp_obj_new_float(gy), mp_obj_new_float(gz)
    };
    return mp_obj_new_tuple(6, items);
}
static MP_DEFINE_CONST_FUN_OBJ_0(bmi270_motion_obj, bmi270_motion);

/* sensors.bmi270.chip_id() -> int (0x24 on a healthy BMI270) */
static mp_obj_t bmi270_chip_id(void) {
#ifdef USE_KIT_PSE84_EVAL_EPC2
    /* Reading a register means driving the bus, which this core may not do.
     * The snapshot's sequence counter answers the question chip_id() is really
     * asked -- is the IMU alive -- so point at that instead of at a wedge. */
    mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT(
        "bmi270.chip_id(): needs the I2C bus, which the display core owns on "
        "Eva Kit. Use sensors.snapshot()['bmi270']['sequence'] to see it live"));
    return mp_const_none;
#else
    uint8_t id = 0;
    sensor_i2c_lock(100);
    bool ok = bmi270_read_chip_id(&id);
    sensor_i2c_unlock();
    if (!ok) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("BMI270 chip id read failed"));
    }
    return MP_OBJ_NEW_SMALL_INT(id);
#endif
}
static MP_DEFINE_CONST_FUN_OBJ_0(bmi270_chip_id_obj, bmi270_chip_id);

static const mp_rom_map_elem_t bmi270_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),      MP_ROM_QSTR(MP_QSTR_bmi270) },
    { MP_ROM_QSTR(MP_QSTR_acceleration),   MP_ROM_PTR(&bmi270_acceleration_obj) },
    { MP_ROM_QSTR(MP_QSTR_gyroscope),      MP_ROM_PTR(&bmi270_gyroscope_obj) },
    { MP_ROM_QSTR(MP_QSTR_temperature),    MP_ROM_PTR(&bmi270_temperature_obj) },
    { MP_ROM_QSTR(MP_QSTR_motion),         MP_ROM_PTR(&bmi270_motion_obj) },
    { MP_ROM_QSTR(MP_QSTR_chip_id),        MP_ROM_PTR(&bmi270_chip_id_obj) },
};
static MP_DEFINE_CONST_DICT(bmi270_module_globals, bmi270_module_globals_table);

const mp_obj_module_t mp_module_bmi270 = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&bmi270_module_globals,
};

/* ========================================================================== */
/* Radar (via IPC to CM55) — AI Kit only                                      */
/* ========================================================================== */

#if BSP_HAS_RADAR

/* IPC buffers for radar request/response (in shared memory) */
CY_SECTION_SHAREDMEM static ipc_msg_t radar_ipc_msg;
CY_SECTION_SHAREDMEM static ipc_response_t radar_ipc_response;

#define RADAR_IPC_SEND_RETRIES    (20)
#define RADAR_IPC_RETRY_DELAY_US  (100)
#define RADAR_IPC_RESPONSE_TIMEOUT_MS (500)

/* sensors.radar() — read radar presence + energy from CM55 via IPC */
static mp_obj_t sensors_radar(void) {
    ensure_sensor_ipc();

    /* Prepare response buffer */
    memset((void *)&radar_ipc_response, 0, sizeof(radar_ipc_response));
    radar_ipc_response.ready = 0;

    /* Send IPC request */
    memset(&radar_ipc_msg, 0, sizeof(radar_ipc_msg));
    radar_ipc_msg.client_id = CM55_IPC_SERVICE_CLIENT_ID;
    radar_ipc_msg.intr_mask = CY_IPC_CYPIPE_INTR_MASK_EP1;
    radar_ipc_msg.cmd = IPC_CMD_RADAR_STATUS;
    radar_ipc_msg.value = (uint32_t)&radar_ipc_response;

    int retries = RADAR_IPC_SEND_RETRIES;
    cy_en_ipc_pipe_status_t status;
    do {
        status = Cy_IPC_Pipe_SendMessage(
            CM55_IPC_PIPE_EP_ADDR, CM33_IPC_PIPE_EP_ADDR,
            (void *)&radar_ipc_msg, NULL);
        if (CY_IPC_PIPE_SUCCESS == status) break;
        Cy_SysLib_DelayUs(RADAR_IPC_RETRY_DELAY_US);
    } while (--retries > 0);

    if (retries <= 0) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("radar IPC send failed"));
    }

    /* Wait for CM55 response */
    uint32_t timeout = RADAR_IPC_RESPONSE_TIMEOUT_MS;
    while (!radar_ipc_response.ready && timeout > 0) {
        Cy_SysLib_Delay(1);
        timeout--;
    }

    if (!radar_ipc_response.ready) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("radar IPC timeout"));
    }

    /* Parse response */
    ipc_radar_status_t rd;
    memcpy(&rd, radar_ipc_response.data, sizeof(rd));

    mp_obj_dict_t *result = MP_OBJ_TO_PTR(mp_obj_new_dict(3));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(result),
                      MP_OBJ_NEW_QSTR(MP_QSTR_initialized),
                      mp_obj_new_bool(rd.initialized));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(result),
                      MP_OBJ_NEW_QSTR(MP_QSTR_presence),
                      mp_obj_new_bool(rd.presence));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(result),
                      MP_OBJ_NEW_QSTR(MP_QSTR_energy),
                      mp_obj_new_float((double)rd.energy));

    return MP_OBJ_FROM_PTR(result);
}
static MP_DEFINE_CONST_FUN_OBJ_0(sensors_radar_obj, sensors_radar);

/* Shared IPC round-trip for the radar_dsp commands (0xB1/0xB2). Reuses the
 * radar_ipc_msg/response sharedmem statics above — no new sharedmem (the Eva
 * shared region is full; these bindings are runtime-gated there anyway). */
static void radar_dsp_ipc_roundtrip(uint8_t cmd, uint32_t value)
{
    ensure_sensor_ipc();

    memset((void *)&radar_ipc_response, 0, sizeof(radar_ipc_response));
    radar_ipc_response.ready = 0;

    memset(&radar_ipc_msg, 0, sizeof(radar_ipc_msg));
    radar_ipc_msg.client_id = CM55_IPC_SERVICE_CLIENT_ID;
    radar_ipc_msg.intr_mask = CY_IPC_CYPIPE_INTR_MASK_EP1;
    radar_ipc_msg.cmd = cmd;
    /* Framework contract: msg.value = response buffer pointer (see
     * ipc_service_callback). Command arguments ride in msg.data[]. */
    radar_ipc_msg.value = (uint32_t)&radar_ipc_response;
    memcpy(radar_ipc_msg.data, &value, sizeof(value));

    int retries = RADAR_IPC_SEND_RETRIES;
    cy_en_ipc_pipe_status_t status;
    do {
        status = Cy_IPC_Pipe_SendMessage(
            CM55_IPC_PIPE_EP_ADDR, CM33_IPC_PIPE_EP_ADDR,
            (void *)&radar_ipc_msg, NULL);
        if (CY_IPC_PIPE_SUCCESS == status) break;
        Cy_SysLib_DelayUs(RADAR_IPC_RETRY_DELAY_US);
    } while (--retries > 0);

    if (retries <= 0) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("radar IPC send failed"));
    }

    uint32_t timeout = RADAR_IPC_RESPONSE_TIMEOUT_MS;
    while (!radar_ipc_response.ready && timeout > 0) {
        Cy_SysLib_Delay(1);
        timeout--;
    }
    if (!radar_ipc_response.ready) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("radar IPC timeout"));
    }
}

/* sensors.radar_range() -> {'distance_m','peak_db','resolution_m','target','seq'}
 * First-peak range from the CM55 radar_dsp chain (HPF->FFT->dB->anti-coupling).
 * distance_m == 0.0 means no target above threshold. */
static mp_obj_t sensors_radar_range(void)
{
    radar_dsp_ipc_roundtrip(IPC_CMD_RADAR_RANGE, 0);

    ipc_radar_range_t rr;
    memcpy(&rr, radar_ipc_response.data, sizeof(rr));

    if (!rr.initialized) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("radar dsp not running"));
    }

    mp_obj_dict_t *result = MP_OBJ_TO_PTR(mp_obj_new_dict(5));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(result),
                      MP_OBJ_NEW_QSTR(MP_QSTR_distance_m),
                      mp_obj_new_float((double)rr.distance_mm / 1000.0));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(result),
                      MP_OBJ_NEW_QSTR(MP_QSTR_peak_db),
                      mp_obj_new_float((double)rr.peak_db));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(result),
                      MP_OBJ_NEW_QSTR(MP_QSTR_resolution_m),
                      mp_obj_new_float((double)rr.resolution_mm / 1000.0));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(result),
                      MP_OBJ_NEW_QSTR(MP_QSTR_target),
                      mp_obj_new_bool(rr.target));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(result),
                      MP_OBJ_NEW_QSTR(MP_QSTR_seq),
                      mp_obj_new_int(rr.seq));
    return MP_OBJ_FROM_PTR(result);
}
static MP_DEFINE_CONST_FUN_OBJ_0(sensors_radar_range_obj, sensors_radar_range);

/* sensors.radar_config(threshold_db) — detection threshold in dB above the
 * clutter baseline (0.1..60.0). Special value 0 = re-capture the baseline
 * (clear the scene of moving targets first; takes ~160 ms). */
static mp_obj_t sensors_radar_config(mp_obj_t threshold_obj)
{
    float th = mp_obj_get_float_to_f(threshold_obj);
    if (th == 0.0f) {
        radar_dsp_ipc_roundtrip(IPC_CMD_RADAR_CONFIG, 0u);   /* recalibrate */
        return mp_const_none;
    }
    if (!(th >= 0.1f && th <= 60.0f)) {
        mp_raise_ValueError(MP_ERROR_TEXT("threshold 0.1..60.0 dB (0=recal)"));
    }
    radar_dsp_ipc_roundtrip(IPC_CMD_RADAR_CONFIG, (uint32_t)(th * 10.0f));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(sensors_radar_config_obj, sensors_radar_config);

#endif /* BSP_HAS_RADAR */

/* ========================================================================== */
/* Extern declarations for BSP-conditional sub-modules                        */
/* ========================================================================== */

#if BSP_HAS_DPS368
extern const mp_obj_module_t mp_module_dps368;
#endif

#if BSP_HAS_SHT40
extern const mp_obj_module_t mp_module_sht40;
#endif

#if BSP_HAS_BMM350
extern const mp_obj_module_t mp_module_bmm350;
#endif

#if BSP_HAS_CAPSENSE
extern const mp_obj_module_t mp_module_capsense;
#endif

#if BSP_HAS_POTENTIOMETER
extern const mp_obj_module_t mp_module_pot;
#endif

/* ========================================================================== */
/* Top-level sensors module                                                   */
/* ========================================================================== */

/* sensors.init() — initialize I2C bus and all sensors */

/*******************************************************************************
 * Eva Kit: CM33_NS must not touch SCB0
 *
 * On KIT_PSE84_EVAL_EPC2 the display's touch controller, BMI270 and the
 * CapSense co-processor all sit on SCB0, and CM55 owns that block — it drives
 * touch every 20 ms and polls the sensors every 200 ms. ipc_sensorhub.h says so
 * in as many words: "On Eva Kit, CM33_NS cannot access SCB0 I2C (display owns
 * it)."
 *
 * When MicroPython called sensor_i2c_init() anyway it became a SECOND master on
 * the same peripheral, with its own PDL context and its own NVIC line (IRQ 43
 * on CM33 against IRQ 22 on CM55 — one interrupt, two vectors, no arbitration).
 * CM33's master-only ISR was then entered for causes its context could not
 * service, drained nothing, and cleared only the level flags, which re-assert
 * immediately. The result was a permanent interrupt storm: UART silent, LCD
 * dark, Ctrl-C dead, and a plain reset did not clear it.
 *
 * Measured on hardware 2026-08-13, three times out of three. It is NOT
 * catchable with try/except — a hang is not an exception — so every shipped
 * example that wrapped sensors.init() in try/except wedged the board too.
 *
 * Refusing loudly is strictly better than hanging. The values are still
 * available: CM55 reads these sensors and answers IPC_CMD_SENSOR_SNAPSHOT.
 ******************************************************************************/
#ifdef USE_KIT_PSE84_EVAL_EPC2
#define EVA_SCB0_REFUSE(what)                                                  \
    mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT(                              \
        what " is unavailable on the Eva Kit: CM55 owns SCB0 (display touch +" \
        " sensors). Driving it from MicroPython hangs the board. Use"          \
        " sensors.snapshot() instead, which asks CM55 over IPC."))
#endif

static mp_obj_t sensors_init(void) {
#ifdef USE_KIT_PSE84_EVAL_EPC2
    EVA_SCB0_REFUSE("sensors.init()");
    return mp_const_false;   /* not reached */
#else
    if (!sensor_i2c_init()) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("Sensor I2C bus init failed"));
    }

    bool ok = true;
    if (!bmi270_init()) {
        mp_printf(&mp_plat_print, "sensors: BMI270 init failed\n");
        ok = false;
    }

#if BSP_HAS_DPS368
    if (!dps368_init()) {
        mp_printf(&mp_plat_print, "sensors: DPS368 init failed\n");
        ok = false;
    }
#endif

#if BSP_HAS_SHT40
    if (!sht40_init()) {
        mp_printf(&mp_plat_print, "sensors: SHT40 init failed\n");
        ok = false;
    }
#endif

#if BSP_HAS_BMM350
    if (!bmm350_init()) {
        mp_printf(&mp_plat_print, "sensors: BMM350 init failed\n");
        ok = false;
    }
#endif

#if BSP_HAS_CAPSENSE
    if (!capsense_init()) {
        mp_printf(&mp_plat_print, "sensors: CapSense init failed\n");
        ok = false;
    }
#endif

#if BSP_HAS_POTENTIOMETER
    if (!potentiometer_init()) {
        mp_printf(&mp_plat_print, "sensors: Potentiometer init failed\n");
        ok = false;
    }
#endif

    if (ok) {
        mp_printf(&mp_plat_print, "sensors: all sensors initialized OK\n");
    }
    return mp_obj_new_bool(ok);
#endif
}
static MP_DEFINE_CONST_FUN_OBJ_0(sensors_init_obj, sensors_init);


/*******************************************************************************
 * sensors.snapshot() -> dict            EVA KIT ONLY
 *
 * The readings MicroPython cannot take itself, fetched from the core that can.
 *
 * CM55 already polls BMI270, CapSense and the potentiometer every 200 ms and
 * feeds ipc_sensorhub (cm55_sensor_poll.c), and ipc_service.c already answers
 * IPC_CMD_SENSOR_SNAPSHOT. Every part of that path shipped long ago; the only
 * thing missing was a caller on this side.
 *
 * Freshness is bounded by CM55's 200 ms poll, so a value can be that old. That
 * is the cost of not fighting CM55 for SCB0, and it beats the alternative,
 * which is a board that hangs until someone power-cycles it.
 *
 * Reuses js_ipc_response instead of declaring another shared buffer: Eva's
 * m33_allocatable_shared region is 4 KB and already full. Safe because every
 * IPC call from MicroPython is issued from the MicroPython task, never
 * concurrently.
 ******************************************************************************/
#ifdef USE_KIT_PSE84_EVAL_EPC2

extern ipc_response_t js_ipc_response;   /* defined in modjoystick.c */

/* Set once CM55 has answered anything in this session — see the timeout below. */
static bool s_cm55_answered = false;

static bool sensors_fetch_snapshot(ipc_sensor_snapshot_t *ss) {
    /* Keep asking until the deadline, because early on it is the SEND that
     * fails, not the reply that is slow.
     *
     * Measured on an Eva Kit, 2026-08-13: after a hard reset CM55 does not
     * accept a message for the service client until about 13.3 s in, while
     * ui.screen() is answered at 2.25 s. Cy_IPC_Pipe_SendMessage returns an
     * error in roughly 25 ms during that window, so widening the reply timeout
     * changed nothing -- every sensor call in a script's opening lines still
     * failed instantly, which is exactly what the earlier fix got wrong.
     *
     * One retry loop around the whole exchange covers both failure shapes. The
     * first successful answer switches the deadline down to a second, so a
     * genuinely dead peer is still reported quickly instead of stalling a
     * lesson for a quarter of a minute. Why the sensor path lags the UI path by
     * ten seconds is still unexplained and worth chasing. */
    /* Counted with a PDL delay, deliberately, because MicroPython's own clock
     * cannot be trusted this early. A mp_hal_ticks_ms() deadline exited on its
     * first pass (2 ms instead of 16 s), and mp_hal_delay_ms() did the same --
     * both read modtime_ticks_us64(), so both fail together at boot. This is
     * the one wait in this file that must survive that, so it waits on the
     * FreeRTOS scheduler instead. Cy_SysLib_Delay was tried and also returned
     * instantly here -- every delay primitive that reads a hardware counter
     * fails at this point in boot, and vTaskDelay is the only one that does
     * not. It yields, which is what we want anyway. */
    int rounds = s_cm55_answered ? 20 : 320;   /* x50 ms = 1 s / 16 s */

    do {
        memset((void *)&js_ipc_response, 0, sizeof(js_ipc_response));
        js_ipc_response.ready = 0;

        memset(&sensor_ipc_msg, 0, sizeof(sensor_ipc_msg));
        sensor_ipc_msg.client_id = CM55_IPC_SERVICE_CLIENT_ID;
        sensor_ipc_msg.intr_mask = CY_IPC_CYPIPE_INTR_MASK_EP1;
        sensor_ipc_msg.cmd = IPC_CMD_SENSOR_SNAPSHOT;
        sensor_ipc_msg.value = (uint32_t)(uintptr_t)&js_ipc_response;

        cy_en_ipc_pipe_status_t st = Cy_IPC_Pipe_SendMessage(
                CM55_IPC_PIPE_EP_ADDR, CM33_IPC_PIPE_EP_ADDR,
                (void *)&sensor_ipc_msg, NULL);

        if (CY_IPC_PIPE_SUCCESS == st) {
            /* Yielding wait -- a busy loop here would make Ctrl-C and the IDE
             * Stop button dead for the whole window. */
            //! [mpy_tacp_poll_uart_freertos]
            /* ...context: inside the joystick IPC response wait ... */
            for (int w = 0; w < 50 && !js_ipc_response.ready; w++) {
                tacp_poll_uart();
                vTaskDelay(pdMS_TO_TICKS(1));
                //! [mpy_tacp_poll_uart_freertos]
            }
            if (js_ipc_response.ready &&
                js_ipc_response.cmd == IPC_CMD_SENSOR_SNAPSHOT) {
                /* Check whose answer this is before believing it. The buffer is
                 * shared with modjoystick and modbentoclaw and it is filled by
                 * CM55's queue worker, not by us: a request that timed out here
                 * is still queued there and can land while a later call waits.
                 * CM55 echoes the command, so cross-delivery is detectable --
                 * without this a joystick payload would decode as plausible
                 * acceleration and nothing would ever raise. */
                memcpy(ss, js_ipc_response.data, sizeof(*ss));
                /* A reply is not the same as a reading. For the first few
                 * seconds after a reset CM55 answers immediately with every
                 * has_* flag clear, because its 200 ms sensor poll has not run
                 * yet. Returning that produced a dict with no 'bmi270' key and
                 * a KeyError three lines into a student's script -- which is
                 * how this was finally caught, after four rebuilds spent
                 * chasing a timeout that was never the problem. Keep asking
                 * until something is actually in there. */
                if (ss->has_bmi270 || ss->has_capsense || ss->has_pot) {
                    s_cm55_answered = true;
                    return true;
                }
            }
        }

        /* Either the send was refused or the snapshot came back empty. Wait a
         * beat and ask again rather than reporting a dead peer at 2 ms. */
        tacp_poll_uart();
        vTaskDelay(pdMS_TO_TICKS(50));
    } while (--rounds > 0);

    return false;
}

/* Accessors on Eva Kit read through these instead of driving SCB0. Not static:
 * the CapSense and potentiometer sub-modules are separate translation units. */
bool eva_snapshot_imu(eva_imu_t *out) {
    ipc_sensor_snapshot_t ss;
    if (!sensors_fetch_snapshot(&ss) || !ss.has_bmi270) return false;
    out->ax = ss.bmi270.ax;  out->ay = ss.bmi270.ay;  out->az = ss.bmi270.az;
    out->gx = ss.bmi270.gx;  out->gy = ss.bmi270.gy;  out->gz = ss.bmi270.gz;
    out->sequence = ss.bmi270.sequence;
    return true;
}

bool eva_snapshot_capsense(uint8_t *b0, uint8_t *b1, uint8_t *slider) {
    ipc_sensor_snapshot_t ss;
    if (!sensors_fetch_snapshot(&ss) || !ss.has_capsense) return false;
    if (b0)     *b0     = ss.capsense.btn0_pressed;
    if (b1)     *b1     = ss.capsense.btn1_pressed;
    if (slider) *slider = ss.capsense.slider;
    return true;
}

bool eva_snapshot_pot(uint16_t *raw, uint16_t *percent_x10) {
    ipc_sensor_snapshot_t ss;
    if (!sensors_fetch_snapshot(&ss) || !ss.has_pot) return false;
    if (raw)         *raw         = ss.pot.raw;
    if (percent_x10) *percent_x10 = ss.pot.percent_x10;
    return true;
}

static void sensors_dict_f(mp_obj_t d, qstr k, float v) {
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(k), mp_obj_new_float(v));
}

static void sensors_dict_i(mp_obj_t d, qstr k, int v) {
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(k), mp_obj_new_int(v));
}

static mp_obj_t sensors_snapshot(void) {
    ipc_sensor_snapshot_t ss;
    if (!sensors_fetch_snapshot(&ss)) {
        mp_raise_msg(&mp_type_OSError,
            MP_ERROR_TEXT("sensors.snapshot(): no reply from the display core. "
                          "It answers sensor requests about 13 s after a reset "
                          "-- if this fired at boot, ask again in a moment"));
    }

    mp_obj_t out = mp_obj_new_dict(3);

    if (ss.has_bmi270) {
        mp_obj_t d = mp_obj_new_dict(7);
        /* CM55 sends raw int16. Scale here with the same constants the C side
         * uses so accel is m/s^2 and gyro deg/s — the units the course teaches.
         * On Eva, BENTO_BMI270_ACCEL_8G makes that 4096 LSB/g, not 16384. */
        sensors_dict_f(d, MP_QSTR_ax, ss.bmi270.ax / IPC_BMI270_ACCEL_LSB_PER_G * GRAVITY_ACCEL);
        sensors_dict_f(d, MP_QSTR_ay, ss.bmi270.ay / IPC_BMI270_ACCEL_LSB_PER_G * GRAVITY_ACCEL);
        sensors_dict_f(d, MP_QSTR_az, ss.bmi270.az / IPC_BMI270_ACCEL_LSB_PER_G * GRAVITY_ACCEL);
        sensors_dict_f(d, MP_QSTR_gx, ss.bmi270.gx / IPC_BMI270_GYRO_LSB_PER_DPS);
        sensors_dict_f(d, MP_QSTR_gy, ss.bmi270.gy / IPC_BMI270_GYRO_LSB_PER_DPS);
        sensors_dict_f(d, MP_QSTR_gz, ss.bmi270.gz / IPC_BMI270_GYRO_LSB_PER_DPS);
        sensors_dict_i(d, MP_QSTR_sequence, ss.bmi270.sequence);
        mp_obj_dict_store(out, MP_OBJ_NEW_QSTR(MP_QSTR_bmi270), d);
    }

    if (ss.has_capsense) {
        mp_obj_t d = mp_obj_new_dict(4);
        mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_btn0),
                          mp_obj_new_bool(ss.capsense.btn0_pressed));
        mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_btn1),
                          mp_obj_new_bool(ss.capsense.btn1_pressed));
        sensors_dict_i(d, MP_QSTR_slider, ss.capsense.slider);
        sensors_dict_i(d, MP_QSTR_sequence, ss.capsense.sequence);
        mp_obj_dict_store(out, MP_OBJ_NEW_QSTR(MP_QSTR_capsense), d);
    }

    if (ss.has_pot) {
        mp_obj_t d = mp_obj_new_dict(4);
        sensors_dict_i(d, MP_QSTR_raw, ss.pot.raw);
        sensors_dict_f(d, MP_QSTR_percent, ss.pot.percent_x10 / 10.0f);
        sensors_dict_f(d, MP_QSTR_voltage, ss.pot.raw / 65535.0f * 3.3f);
        sensors_dict_i(d, MP_QSTR_sequence, ss.pot.sequence);
        mp_obj_dict_store(out, MP_OBJ_NEW_QSTR(MP_QSTR_pot), d);
    }

    return out;
}
static MP_DEFINE_CONST_FUN_OBJ_0(sensors_snapshot_obj, sensors_snapshot);

#endif /* USE_KIT_PSE84_EVAL_EPC2 */

/* sensors.scan() — scan I2C bus for devices */
static mp_obj_t sensors_scan(void) {
#ifdef USE_KIT_PSE84_EVAL_EPC2
    /* A 112-address probe is the fastest way to corrupt the display bus. */
    EVA_SCB0_REFUSE("sensors.scan()");
    return mp_const_none;   /* not reached */
#else
    if (!sensor_i2c_is_init()) {
        if (!sensor_i2c_init()) {
            mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("I2C init failed"));
        }
    }

    uint8_t addrs[16];
    int count = sensor_i2c_scan(addrs, 16);

    mp_obj_list_t *list = MP_OBJ_TO_PTR(mp_obj_new_list(0, NULL));
    for (int i = 0; i < count; i++) {
        mp_obj_list_append(MP_OBJ_FROM_PTR(list), mp_obj_new_int(addrs[i]));
    }
    return MP_OBJ_FROM_PTR(list);
#endif
}
static MP_DEFINE_CONST_FUN_OBJ_0(sensors_scan_obj, sensors_scan);

/* sensors.read_all() — read all sensors, return dict */
static mp_obj_t sensors_read_all(void) {
#ifdef USE_KIT_PSE84_EVAL_EPC2
    /* The last door into SCB0 from this core. init() and scan() were guarded,
     * then the submodule accessors were redirected, and this one was still
     * sitting here taking the bus lock directly -- found by the emulator team
     * reading the registration table, not by anything that ran. On Eva that is
     * a second master on a bus the display owns, which is the wedge only a
     * debugger clears. snapshot() returns the same three sensors. */
    return sensors_snapshot();
#else
    mp_obj_dict_t *result = MP_OBJ_TO_PTR(mp_obj_new_dict(3));

    sensor_i2c_lock(200);

    /* BMI270 */
    {
        float ax, ay, az, gx, gy, gz;
        if (bmi270_read_accel(&ax, &ay, &az) && bmi270_read_gyro(&gx, &gy, &gz)) {
            mp_obj_dict_t *bmi = MP_OBJ_TO_PTR(mp_obj_new_dict(6));
            mp_obj_dict_store(MP_OBJ_FROM_PTR(bmi), MP_OBJ_NEW_QSTR(MP_QSTR_ax), mp_obj_new_float(ax));
            mp_obj_dict_store(MP_OBJ_FROM_PTR(bmi), MP_OBJ_NEW_QSTR(MP_QSTR_ay), mp_obj_new_float(ay));
            mp_obj_dict_store(MP_OBJ_FROM_PTR(bmi), MP_OBJ_NEW_QSTR(MP_QSTR_az), mp_obj_new_float(az));
            mp_obj_dict_store(MP_OBJ_FROM_PTR(bmi), MP_OBJ_NEW_QSTR(MP_QSTR_gx), mp_obj_new_float(gx));
            mp_obj_dict_store(MP_OBJ_FROM_PTR(bmi), MP_OBJ_NEW_QSTR(MP_QSTR_gy), mp_obj_new_float(gy));
            mp_obj_dict_store(MP_OBJ_FROM_PTR(bmi), MP_OBJ_NEW_QSTR(MP_QSTR_gz), mp_obj_new_float(gz));
            mp_obj_dict_store(MP_OBJ_FROM_PTR(result), MP_OBJ_NEW_QSTR(MP_QSTR_bmi270), MP_OBJ_FROM_PTR(bmi));
        }
    }

#if BSP_HAS_DPS368
    /* DPS368 */
    {
        float pressure, temperature;
        if (dps368_read_both(&pressure, &temperature)) {
            mp_obj_dict_t *dps = MP_OBJ_TO_PTR(mp_obj_new_dict(2));
            mp_obj_dict_store(MP_OBJ_FROM_PTR(dps), MP_OBJ_NEW_QSTR(MP_QSTR_pressure), mp_obj_new_float(pressure));
            mp_obj_dict_store(MP_OBJ_FROM_PTR(dps), MP_OBJ_NEW_QSTR(MP_QSTR_temperature), mp_obj_new_float(temperature));
            mp_obj_dict_store(MP_OBJ_FROM_PTR(result), MP_OBJ_NEW_QSTR(MP_QSTR_dps368), MP_OBJ_FROM_PTR(dps));
        }
    }
#endif

#if BSP_HAS_SHT40
    /* SHT40 */
    {
        float temperature, humidity;
        if (sht40_read_both(&temperature, &humidity)) {
            mp_obj_dict_t *sht = MP_OBJ_TO_PTR(mp_obj_new_dict(2));
            mp_obj_dict_store(MP_OBJ_FROM_PTR(sht), MP_OBJ_NEW_QSTR(MP_QSTR_temperature), mp_obj_new_float(temperature));
            mp_obj_dict_store(MP_OBJ_FROM_PTR(sht), MP_OBJ_NEW_QSTR(MP_QSTR_humidity), mp_obj_new_float(humidity));
            mp_obj_dict_store(MP_OBJ_FROM_PTR(result), MP_OBJ_NEW_QSTR(MP_QSTR_sht40), MP_OBJ_FROM_PTR(sht));
        }
    }
#endif

#if BSP_HAS_BMM350
    /* BMM350 */
    {
        float mx, my, mz, heading;
        if (bmm350_read_xyz(&mx, &my, &mz)) {
            mp_obj_dict_t *bmm = MP_OBJ_TO_PTR(mp_obj_new_dict(4));
            mp_obj_dict_store(MP_OBJ_FROM_PTR(bmm), MP_OBJ_NEW_QSTR(MP_QSTR_mx), mp_obj_new_float(mx));
            mp_obj_dict_store(MP_OBJ_FROM_PTR(bmm), MP_OBJ_NEW_QSTR(MP_QSTR_my), mp_obj_new_float(my));
            mp_obj_dict_store(MP_OBJ_FROM_PTR(bmm), MP_OBJ_NEW_QSTR(MP_QSTR_mz), mp_obj_new_float(mz));
            if (bmm350_read_heading(&heading)) {
                mp_obj_dict_store(MP_OBJ_FROM_PTR(bmm), MP_OBJ_NEW_QSTR(MP_QSTR_heading), mp_obj_new_float(heading));
            }
            mp_obj_dict_store(MP_OBJ_FROM_PTR(result), MP_OBJ_NEW_QSTR(MP_QSTR_bmm350), MP_OBJ_FROM_PTR(bmm));
        }
    }
#endif

#if BSP_HAS_CAPSENSE
    /* CapSense */
    {
        capsense_data_t cs;
        if (capsense_read(&cs)) {
            mp_obj_dict_t *cap = MP_OBJ_TO_PTR(mp_obj_new_dict(3));
            mp_obj_dict_store(MP_OBJ_FROM_PTR(cap), MP_OBJ_NEW_QSTR(MP_QSTR_btn0), mp_obj_new_bool(cs.btn0_pressed));
            mp_obj_dict_store(MP_OBJ_FROM_PTR(cap), MP_OBJ_NEW_QSTR(MP_QSTR_btn1), mp_obj_new_bool(cs.btn1_pressed));
            mp_obj_dict_store(MP_OBJ_FROM_PTR(cap), MP_OBJ_NEW_QSTR(MP_QSTR_slider), mp_obj_new_int(cs.slider));
            mp_obj_dict_store(MP_OBJ_FROM_PTR(result), MP_OBJ_NEW_QSTR(MP_QSTR_capsense), MP_OBJ_FROM_PTR(cap));
        }
    }
#endif

#if BSP_HAS_POTENTIOMETER
    /* Potentiometer */
    {
        float pct;
        if (potentiometer_read_percent(&pct)) {
            mp_obj_dict_store(MP_OBJ_FROM_PTR(result), MP_OBJ_NEW_QSTR(MP_QSTR_pot), mp_obj_new_float(pct));
        }
    }
#endif

    sensor_i2c_unlock();

    return MP_OBJ_FROM_PTR(result);
#endif /* USE_KIT_PSE84_EVAL_EPC2 */
}
static MP_DEFINE_CONST_FUN_OBJ_0(sensors_read_all_obj, sensors_read_all);

/* sensors.push() — read all sensors and send to CM55 via IPC */
static mp_obj_t sensors_push(void) {
#ifdef USE_KIT_PSE84_EVAL_EPC2
    /* Door five. init(), scan(), read_all() and every submodule accessor were
     * closed one at a time, each after the previous one produced a wedged
     * board, because each fix followed the path that had just failed instead of
     * reading the whole registration table. push() and live_push() were still
     * open. They exist to shove readings at CM55's own display, which on this
     * board already has them -- so on Eva there is nothing to push and nothing
     * to gain by driving a bus this core may not touch. */
    mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT(
        "sensors.push() is unavailable on the Eva Kit: the display core reads these "
        "sensors itself and already has the values"));
    return mp_const_none;
#else
    bool any_sent = false;

    /* Push BMI270 data */
    {
        float ax, ay, az, gx, gy, gz;
        sensor_i2c_lock(100);
        bool ok = bmi270_read_accel(&ax, &ay, &az) && bmi270_read_gyro(&gx, &gy, &gz);
        sensor_i2c_unlock();
        if (ok) {
            ipc_sensor_bmi270_t bmi_data;
            bmi_data.ax = (int16_t)(ax / GRAVITY_ACCEL * IPC_BMI270_ACCEL_LSB_PER_G);
            bmi_data.ay = (int16_t)(ay / GRAVITY_ACCEL * IPC_BMI270_ACCEL_LSB_PER_G);
            bmi_data.az = (int16_t)(az / GRAVITY_ACCEL * IPC_BMI270_ACCEL_LSB_PER_G);
            bmi_data.gx = (int16_t)(gx * IPC_BMI270_GYRO_LSB_PER_DPS);
            bmi_data.gy = (int16_t)(gy * IPC_BMI270_GYRO_LSB_PER_DPS);
            bmi_data.gz = (int16_t)(gz * IPC_BMI270_GYRO_LSB_PER_DPS);
            bmi_data.sequence = sensor_seq++;
            if (sensor_ipc_send(IPC_CMD_SENSOR_BMI270, &bmi_data, sizeof(bmi_data))) {
                any_sent = true;
            }
            Cy_SysLib_DelayUs(200); /* Brief gap between IPC messages */
        }
    }

#if BSP_HAS_DPS368
    /* Push DPS368 data */
    {
        float pressure, temperature;
        sensor_i2c_lock(100);
        bool ok = dps368_read_both(&pressure, &temperature);
        sensor_i2c_unlock();
        if (ok) {
            ipc_sensor_dps368_t dps_data;
            dps_data.pressure_x100 = (int32_t)(pressure * 100.0f);
            dps_data.temperature_x100 = (int16_t)(temperature * 100.0f);
            dps_data.sequence = sensor_seq++;
            if (sensor_ipc_send(IPC_CMD_SENSOR_DPS368, &dps_data, sizeof(dps_data))) {
                any_sent = true;
            }
            Cy_SysLib_DelayUs(200);
        }
    }
#endif

#if BSP_HAS_SHT40
    /* Push SHT40 data */
    {
        float temperature, humidity;
        sensor_i2c_lock(100);
        bool ok = sht40_read_both(&temperature, &humidity);
        sensor_i2c_unlock();
        if (ok) {
            ipc_sensor_sht40_t sht_data;
            sht_data.temperature_x100 = (int16_t)(temperature * 100.0f);
            sht_data.humidity_x100 = (uint16_t)(humidity * 100.0f);
            sht_data.sequence = sensor_seq++;
            if (sensor_ipc_send(IPC_CMD_SENSOR_SHT40, &sht_data, sizeof(sht_data))) {
                any_sent = true;
            }
        }
    }
#endif

#if BSP_HAS_BMM350
    /* Push BMM350 data */
    {
        float mx, my, mz, heading = 0.0f;
        sensor_i2c_lock(100);
        bool ok = bmm350_read_xyz(&mx, &my, &mz);
        bool hok = ok ? bmm350_read_heading(&heading) : false;
        sensor_i2c_unlock();
        if (ok) {
            ipc_sensor_bmm350_t bmm_data;
            bmm_data.mx_x100 = (int16_t)(mx * 100.0f);
            bmm_data.my_x100 = (int16_t)(my * 100.0f);
            bmm_data.mz_x100 = (int16_t)(mz * 100.0f);
            bmm_data.heading_x10 = hok ? (uint16_t)(heading * 10.0f) : 0;
            bmm_data.sequence = sensor_seq++;
            if (sensor_ipc_send(IPC_CMD_SENSOR_BMM350, &bmm_data, sizeof(bmm_data))) {
                any_sent = true;
            }
            Cy_SysLib_DelayUs(200);
        }
    }
#endif

#if BSP_HAS_CAPSENSE
    /* Push CapSense data */
    {
        capsense_data_t cs;
        sensor_i2c_lock(100);
        bool ok = capsense_read(&cs);
        sensor_i2c_unlock();
        if (ok) {
            ipc_sensor_capsense_t cs_data;
            cs_data.btn0_pressed = cs.btn0_pressed ? 1 : 0;
            cs_data.btn1_pressed = cs.btn1_pressed ? 1 : 0;
            cs_data.slider = cs.slider;
            cs_data.reserved = 0;
            cs_data.sequence = sensor_seq++;
            if (sensor_ipc_send(IPC_CMD_SENSOR_CAPSENSE, &cs_data, sizeof(cs_data))) {
                any_sent = true;
            }
            Cy_SysLib_DelayUs(200);
        }
    }
#endif

#if BSP_HAS_POTENTIOMETER
    /* Push Potentiometer data */
    {
        uint16_t raw;
        float pct;
        sensor_i2c_lock(100);
        bool ok = potentiometer_read_raw(&raw) && potentiometer_read_percent(&pct);
        sensor_i2c_unlock();
        if (ok) {
            ipc_sensor_pot_t pot_data;
            pot_data.raw = raw;
            pot_data.percent_x10 = (uint16_t)(pct * 10.0f);
            pot_data.sequence = sensor_seq++;
            if (sensor_ipc_send(IPC_CMD_SENSOR_POT, &pot_data, sizeof(pot_data))) {
                any_sent = true;
            }
        }
    }
#endif

    return mp_obj_new_bool(any_sent);
#endif /* USE_KIT_PSE84_EVAL_EPC2 */
}
static MP_DEFINE_CONST_FUN_OBJ_0(sensors_push_obj, sensors_push);

/* sensors.live_push(interval_ms=200) — continuously read and push to CM55.
 * Runs until Ctrl+C. Prints status every 20 pushes. */
static mp_obj_t sensors_live_push(size_t n_args, const mp_obj_t *args) {
#ifdef USE_KIT_PSE84_EVAL_EPC2
    /* Same door, other handle -- see sensors_push(). */
    (void)n_args; (void)args;
    mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT(
        "sensors.live_push() is unavailable on the Eva Kit: the display core "
        "reads these sensors itself and already has the values"));
    return mp_const_none;
#else
    int interval_ms = 200; /* default 5 Hz */
    if (n_args > 0) {
        interval_ms = mp_obj_get_int(args[0]);
        if (interval_ms < 50) interval_ms = 50;   /* min 50ms (20Hz) */
        if (interval_ms > 5000) interval_ms = 5000;
    }

    mp_printf(&mp_plat_print, "sensors: live push started (%d ms interval, Ctrl+C to stop)\n",
              interval_ms);

    uint32_t count = 0;
    for (;;) {
        /* Check UART for Ctrl+C */
        if (check_uart_ctrl_c()) {
            mp_printf(&mp_plat_print, "\nsensors: live push stopped (%lu pushes)\n",
                      (unsigned long)count);
            mp_raise_type(&mp_type_KeyboardInterrupt);
        }
        mp_handle_pending(true);

        /* Push BMI270 (fast — just I2C register reads) */
        {
            float ax, ay, az, gx, gy, gz;
            sensor_i2c_lock(100);
            bool ok = bmi270_read_accel(&ax, &ay, &az) && bmi270_read_gyro(&gx, &gy, &gz);
            sensor_i2c_unlock();
            if (ok) {
                ipc_sensor_bmi270_t bmi_data;
                bmi_data.ax = (int16_t)(ax / GRAVITY_ACCEL * IPC_BMI270_ACCEL_LSB_PER_G);
                bmi_data.ay = (int16_t)(ay / GRAVITY_ACCEL * IPC_BMI270_ACCEL_LSB_PER_G);
                bmi_data.az = (int16_t)(az / GRAVITY_ACCEL * IPC_BMI270_ACCEL_LSB_PER_G);
                bmi_data.gx = (int16_t)(gx * IPC_BMI270_GYRO_LSB_PER_DPS);
                bmi_data.gy = (int16_t)(gy * IPC_BMI270_GYRO_LSB_PER_DPS);
                bmi_data.gz = (int16_t)(gz * IPC_BMI270_GYRO_LSB_PER_DPS);
                bmi_data.sequence = sensor_seq++;
                sensor_ipc_send(IPC_CMD_SENSOR_BMI270, &bmi_data, sizeof(bmi_data));
                Cy_SysLib_DelayUs(200);
            }
        }

#if BSP_HAS_DPS368 || BSP_HAS_SHT40
        /* Push DPS368 + SHT40 every 5th cycle (slow sensors) */
        if ((count % 5) == 0) {
#if BSP_HAS_DPS368
            float pressure, temperature;
            sensor_i2c_lock(100);
            bool dps_ok = dps368_read_both(&pressure, &temperature);
            sensor_i2c_unlock();
            if (dps_ok) {
                ipc_sensor_dps368_t dps_data;
                dps_data.pressure_x100 = (int32_t)(pressure * 100.0f);
                dps_data.temperature_x100 = (int16_t)(temperature * 100.0f);
                dps_data.sequence = sensor_seq++;
                sensor_ipc_send(IPC_CMD_SENSOR_DPS368, &dps_data, sizeof(dps_data));
                Cy_SysLib_DelayUs(200);
            }
#endif

#if BSP_HAS_SHT40
            float sht_temp, humidity;
            sensor_i2c_lock(100);
            bool sht_ok = sht40_read_both(&sht_temp, &humidity);
            sensor_i2c_unlock();
            if (sht_ok) {
                ipc_sensor_sht40_t sht_data;
                sht_data.temperature_x100 = (int16_t)(sht_temp * 100.0f);
                sht_data.humidity_x100 = (uint16_t)(humidity * 100.0f);
                sht_data.sequence = sensor_seq++;
                sensor_ipc_send(IPC_CMD_SENSOR_SHT40, &sht_data, sizeof(sht_data));
            }
#endif
        }
#endif /* BSP_HAS_DPS368 || BSP_HAS_SHT40 */

        count++;

        /* Print status every 20 pushes */
        if ((count % 20) == 0) {
            mp_printf(&mp_plat_print, "[live] %lu pushes (seq=%u)\n",
                      (unsigned long)count, (unsigned)sensor_seq);
        }

        /* Delay in 10ms chunks, polling UART for Ctrl+C */
        for (int d = 0; d < interval_ms; d += 10) {
            if (check_uart_ctrl_c()) {
                mp_printf(&mp_plat_print, "\nsensors: live push stopped (%lu pushes)\n",
                          (unsigned long)count);
                mp_raise_type(&mp_type_KeyboardInterrupt);
            }
            Cy_SysLib_Delay(10);
        }
    }

    /* Never reached (Ctrl+C raises exception) */
    return mp_const_none;
#endif /* USE_KIT_PSE84_EVAL_EPC2 */
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(sensors_live_push_obj, 0, 1, sensors_live_push);

/* ========================================================================== */
/* Auto-sensor background task control                                        */
/* ========================================================================== */

/* sensors.auto() / sensors.auto(True/False) / sensors.auto('name', True/False) */
static mp_obj_t sensors_auto(size_t n_args, const mp_obj_t *args) {
#ifdef USE_KIT_PSE84_EVAL_EPC2
    /* Door six, and the worst of them.
     *
     * init(), scan(), read_all(), push() and live_push() were each closed in
     * turn, every one after it had already wedged a board. This one was found
     * by an auditor reading the whole registration table instead of following
     * the call that had just failed -- which is how it should have been done
     * five doors ago.
     *
     * It is worse than the others because it does not drive the bus itself: it
     * turns on a background task that does. sensor_auto_enable() ORs the mask
     * bit back on with no board check, and the Eva restriction is a one-time
     * assignment at task creation, so nothing re-applies it. The task then
     * reaches sensor_i2c_lock() and bmi270_read_accel() on its own schedule.
     * A wedge from init() dies with the call; a wedge from here outlives it,
     * and the student who typed one word gets a board that only a debugger
     * clears.
     *
     * CM55 already polls all three sensors every 200 ms and hands them over
     * through sensors.snapshot(), which is what auto-push existed to provide. */
    (void)n_args; (void)args;
    mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT(
        "sensors.auto() is unavailable on the Eva Kit: the display core owns "
        "the sensor bus and already polls it. Use sensors.snapshot()"));
    return mp_const_none;
#else
    if (n_args == 0) {
        /* sensors.auto() — start auto-push */
        sensor_auto_start();
        return mp_const_true;
    }
    if (n_args == 1) {
        /* sensors.auto(True/False) — start/stop */
        if (mp_obj_is_true(args[0])) {
            sensor_auto_start();
        } else {
            sensor_auto_stop();
        }
        return mp_const_none;
    }
    /* n_args == 2: sensors.auto('sensor_name', True/False) */
    const char *name = mp_obj_str_get_str(args[0]);
    bool enable = mp_obj_is_true(args[1]);
    uint32_t flag = 0;
    if (strcmp(name, "bmi270") == 0)        flag = SENSOR_AUTO_BMI270;
    else if (strcmp(name, "dps368") == 0)   flag = SENSOR_AUTO_DPS368;
    else if (strcmp(name, "sht40") == 0)    flag = SENSOR_AUTO_SHT40;
    else if (strcmp(name, "bmm350") == 0)   flag = SENSOR_AUTO_BMM350;
    else if (strcmp(name, "capsense") == 0) flag = SENSOR_AUTO_CAPSENSE;
    else if (strcmp(name, "pot") == 0)      flag = SENSOR_AUTO_POT;
    else mp_raise_ValueError(MP_ERROR_TEXT("unknown sensor name"));

    if (enable) sensor_auto_enable(flag);
    else        sensor_auto_disable(flag);
    return mp_const_none;
#endif /* USE_KIT_PSE84_EVAL_EPC2 */
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(sensors_auto_obj, 0, 2, sensors_auto);

/* sensors.auto_rate(ms) — set auto-push interval */
static mp_obj_t sensors_auto_rate(mp_obj_t ms_obj) {
    int ms = mp_obj_get_int(ms_obj);
    /* Matches the C API's range. 20 ms (50 Hz) is the rate DEEPCRAFT motion
     * models are trained at, so MicroPython must be able to ask for it too. */
    if (ms < 20) ms = 20;
    if (ms > 5000) ms = 5000;
    sensor_auto_set_rate((uint32_t)ms);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(sensors_auto_rate_obj, sensors_auto_rate);

/* sensors.auto_status() — return auto-push status dict */
static mp_obj_t sensors_auto_status(void) {
    mp_obj_dict_t *d = MP_OBJ_TO_PTR(mp_obj_new_dict(4));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        MP_OBJ_NEW_QSTR(MP_QSTR_running),
        mp_obj_new_bool(sensor_auto_is_running()));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        MP_OBJ_NEW_QSTR(MP_QSTR_rate_ms),
        mp_obj_new_int(sensor_auto_get_rate()));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        MP_OBJ_NEW_QSTR(MP_QSTR_push_count),
        mp_obj_new_int(sensor_auto_get_push_count()));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        MP_OBJ_NEW_QSTR(MP_QSTR_mask),
        mp_obj_new_int(sensor_auto_get_mask()));
    return MP_OBJ_FROM_PTR(d);
}
static MP_DEFINE_CONST_FUN_OBJ_0(sensors_auto_status_obj, sensors_auto_status);

/* ========================================================================== */
/* BMM350 Diagnostic (REPL-accessible debug)                                  */
/* ========================================================================== */

#if BSP_HAS_BMM350
static mp_obj_t sensors_bmm350_diag(void) {
    bmm350_diag_t diag;
    (void)bmm350_diagnose(&diag);

    /* Only 12 fields — keeps dict small to avoid REPL truncation */
    mp_obj_dict_t *d = MP_OBJ_TO_PTR(mp_obj_new_dict(12));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        MP_OBJ_NEW_QSTR(MP_QSTR_step),
        mp_obj_new_int(diag.step));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        mp_obj_new_str("init", 4),
        mp_obj_new_int(diag.init_st));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        mp_obj_new_str("att", 3),
        mp_obj_new_int(diag.attach_st));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        mp_obj_new_str("s_init", 6),
        mp_obj_new_int(diag.state_post_init));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        mp_obj_new_str("s_en", 4),
        mp_obj_new_int(diag.state_post_en));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        mp_obj_new_str("pres", 4),
        mp_obj_new_int(diag.present_st));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        mp_obj_new_str("forced", 6),
        mp_obj_new_bool(diag.forced_idle));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        mp_obj_new_str("wr_st", 5),
        mp_obj_new_int(diag.wr_st));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        mp_obj_new_str("wr_ev", 5),
        mp_obj_new_int(diag.wr_ev));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        mp_obj_new_str("rd_st", 5),
        mp_obj_new_int(diag.rd_st));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        mp_obj_new_str("rd_ev", 5),
        mp_obj_new_int(diag.rd_ev));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        mp_obj_new_str("id", 2),
        mp_obj_new_int(diag.chip_id));
    return MP_OBJ_FROM_PTR(d);
}
static MP_DEFINE_CONST_FUN_OBJ_0(sensors_bmm350_diag_obj, sensors_bmm350_diag);

static mp_obj_t sensors_bmm350_debug(void) {
    bmm350_debug_read();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(sensors_bmm350_debug_obj, sensors_bmm350_debug);
#endif

/* ========================================================================== */
/* Module Definition                                                          */
/* ========================================================================== */

static const mp_rom_map_elem_t sensors_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),    MP_ROM_QSTR(MP_QSTR_sensors) },
    { MP_ROM_QSTR(MP_QSTR_init),         MP_ROM_PTR(&sensors_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_scan),         MP_ROM_PTR(&sensors_scan_obj) },
#ifdef USE_KIT_PSE84_EVAL_EPC2
    /* Eva Kit only: CM33 cannot drive SCB0, so ask CM55 for its readings */
    { MP_ROM_QSTR(MP_QSTR_snapshot),     MP_ROM_PTR(&sensors_snapshot_obj) },
#endif
    { MP_ROM_QSTR(MP_QSTR_read_all),     MP_ROM_PTR(&sensors_read_all_obj) },
    { MP_ROM_QSTR(MP_QSTR_push),         MP_ROM_PTR(&sensors_push_obj) },
    { MP_ROM_QSTR(MP_QSTR_live_push),    MP_ROM_PTR(&sensors_live_push_obj) },
    { MP_ROM_QSTR(MP_QSTR_auto),         MP_ROM_PTR(&sensors_auto_obj) },
    { MP_ROM_QSTR(MP_QSTR_auto_rate),    MP_ROM_PTR(&sensors_auto_rate_obj) },
    { MP_ROM_QSTR(MP_QSTR_auto_status),  MP_ROM_PTR(&sensors_auto_status_obj) },
    /* Sensor sub-modules */
    { MP_ROM_QSTR(MP_QSTR_bmi270),       MP_ROM_PTR(&mp_module_bmi270) },
#if BSP_HAS_DPS368
    { MP_ROM_QSTR(MP_QSTR_dps368),       MP_ROM_PTR(&mp_module_dps368) },
#endif
#if BSP_HAS_SHT40
    { MP_ROM_QSTR(MP_QSTR_sht40),        MP_ROM_PTR(&mp_module_sht40) },
#endif
#if BSP_HAS_BMM350
    { MP_ROM_QSTR(MP_QSTR_bmm350),       MP_ROM_PTR(&mp_module_bmm350) },
    { MP_ROM_QSTR(MP_QSTR_bmm350_diag),  MP_ROM_PTR(&sensors_bmm350_diag_obj) },
    { MP_ROM_QSTR(MP_QSTR_bmm350_debug), MP_ROM_PTR(&sensors_bmm350_debug_obj) },
#endif
#if BSP_HAS_CAPSENSE
    { MP_ROM_QSTR(MP_QSTR_capsense),     MP_ROM_PTR(&mp_module_capsense) },
#endif
#if BSP_HAS_POTENTIOMETER
    { MP_ROM_QSTR(MP_QSTR_pot),          MP_ROM_PTR(&mp_module_pot) },
#endif
#if BSP_HAS_RADAR
    { MP_ROM_QSTR(MP_QSTR_radar),        MP_ROM_PTR(&sensors_radar_obj) },
    { MP_ROM_QSTR(MP_QSTR_radar_range),  MP_ROM_PTR(&sensors_radar_range_obj) },
    { MP_ROM_QSTR(MP_QSTR_radar_config), MP_ROM_PTR(&sensors_radar_config_obj) },
#endif
};
static MP_DEFINE_CONST_DICT(sensors_module_globals, sensors_module_globals_table);

const mp_obj_module_t mp_module_sensors = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&sensors_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_sensors, mp_module_sensors);
