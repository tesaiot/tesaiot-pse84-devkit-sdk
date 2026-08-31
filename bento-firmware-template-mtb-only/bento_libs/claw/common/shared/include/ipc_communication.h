/*******************************************************************************
 * File Name        : ipc_communication.h
 *
 * Description      : Headers and structures for IPC Pipes between CM33 and CM55.
 *                    Stripped-down version for lcd.print() IPC.
 *                    Based on TESA sensorhub ipc_communication.h
 *
 * Author           : Asst.Prof.Santi Nuratch, Ph.D
 *                    Thailand Embedded Systems Association (TESA)
 *
 *******************************************************************************/

#ifndef SOURCE_IPC_COMMUNICATION_H
#define SOURCE_IPC_COMMUNICATION_H

/*******************************************************************************
 * Header Files
 *******************************************************************************/
#include "cy_ipc_pipe.h"
#include "cy_pdl.h"
#include "cybsp.h"
#include <stdbool.h>
#include <stdint.h>

/*******************************************************************************
 * Macros
 *******************************************************************************/
#define CY_IPC_MAX_ENDPOINTS (5UL)
#define CY_IPC_CYPIPE_CLIENT_CNT (10UL)

#define CY_IPC_CHAN_CYPIPE_EP1 (4UL)
#define CY_IPC_INTR_CYPIPE_EP1 (4UL)
#define CY_IPC_CHAN_CYPIPE_EP2 (15UL)
#define CY_IPC_INTR_CYPIPE_EP2 (5UL)

/* IPC Pipe Endpoint-1 config (CM33) */
#define CY_IPC_CYPIPE_CHAN_MASK_EP1 CY_IPC_CH_MASK(CY_IPC_CHAN_CYPIPE_EP1)
#define CY_IPC_CYPIPE_INTR_MASK_EP1 CY_IPC_INTR_MASK(CY_IPC_INTR_CYPIPE_EP1)
#define CY_IPC_INTR_CYPIPE_PRIOR_EP1 (1UL)
#define CY_IPC_INTR_CYPIPE_MUX_EP1 (CY_IPC0_INTR_MUX(CY_IPC_INTR_CYPIPE_EP1))
#define CM33_IPC_PIPE_EP_ADDR (1UL)
#define CM33_IPC_PIPE_CLIENT_ID (3UL)

/* IPC Pipe Endpoint-2 config (CM55) */
#define CY_IPC_CYPIPE_CHAN_MASK_EP2 CY_IPC_CH_MASK(CY_IPC_CHAN_CYPIPE_EP2)
#define CY_IPC_CYPIPE_INTR_MASK_EP2 CY_IPC_INTR_MASK(CY_IPC_INTR_CYPIPE_EP2)
#define CY_IPC_INTR_CYPIPE_PRIOR_EP2 (1UL)
#define CY_IPC_INTR_CYPIPE_MUX_EP2 (CY_IPC0_INTR_MUX(CY_IPC_INTR_CYPIPE_EP2))
#define CM55_IPC_PIPE_EP_ADDR (2UL)
#define CM55_IPC_PIPE_CLIENT_ID (5UL)

/* Sensor data IPC client (separate from LCD client for independent callbacks) */
#define CM55_IPC_SENSOR_CLIENT_ID (6UL)
/* Sensor auto-control IPC client on CM33 (CM55 -> CM33) */
#define CM33_IPC_SENSOR_CTRL_CLIENT_ID (9UL)

/* Combined Interrupt Mask */
#define CY_IPC_CYPIPE_INTR_MASK (CY_IPC_CYPIPE_CHAN_MASK_EP1 | CY_IPC_CYPIPE_CHAN_MASK_EP2)

/* LCD terminal commands (CM33 → CM55) */
#define IPC_CMD_LCD_PRINT       (0xE0)
#define IPC_CMD_LCD_CLEAR       (0xE1)
#define IPC_CMD_LCD_THEME       (0xE2)

/* Sensor data commands (CM33 → CM55) */
#define IPC_CMD_SENSOR_BMI270   (0x91)
#define IPC_CMD_SENSOR_DPS368   (0x92)
#define IPC_CMD_SENSOR_SHT40    (0x93)
#define IPC_CMD_SENSOR_BMM350   (0x94)
#define IPC_CMD_SENSOR_CAPSENSE (0x95)
#define IPC_CMD_SENSOR_POT      (0x96)
/* Sensor auto-control command (CM55 -> CM33), data[0]: 0=stop, 1=start */
#define IPC_CMD_SENSOR_AUTO_CTRL (0x97)
/* Sensor snapshot request (CM33 → CM55): returns latest sensorhub data.
 * On Eva Kit, CM55 owns SCB0 I2C for BMI270/CapSense — CM33_NS must
 * request data via IPC instead of direct I2C reads. */
#define IPC_CMD_SENSOR_SNAPSHOT  (0x9B)
/* System recovery command (CM55 -> CM33): request one-shot safe boot */
#define IPC_CMD_SYSTEM_SAFE_REBOOT (0x98)
/* Delete /main.py command (CM55 -> CM33): remove autorun script + reboot */
#define IPC_CMD_DELETE_MAIN_PY     (0x99)
/* Restart script command (CM55 -> CM33): soft-reset to re-run main.py */
#define IPC_CMD_RESTART_SCRIPT     (0x9A)

/* WiFi IPC commands (CM33 → CM55, future) */
#define IPC_CMD_WIFI_SCAN       (0xD0)
#define IPC_CMD_WIFI_CONNECT    (0xD1)
#define IPC_CMD_WIFI_DISCONNECT (0xD2)
#define IPC_CMD_WIFI_STATUS     (0xD3)
#define IPC_CMD_WIFI_IP         (0xD4)
#define IPC_CMD_WIFI_SOFTAP     (0xD5)

/* Touch I2C bus control (CM33 → CM55, for OPTIGA/CAPSENSE bus sharing) */
#define IPC_CMD_TOUCH_PAUSE     (0xD6)  /* Pause touch I2C polling */
#define IPC_CMD_TOUCH_RESUME    (0xD7)  /* Resume touch + reinit controller */
/* WiFi state push (CM33 → CM55, non-blocking notification) */
#define IPC_CMD_WIFI_STATE_PUSH (0xD8)  /* data[0]: 0=disconnected, 1=connected */
/* Formatted time push (CM33 → CM55, after NTP sync) */
#define IPC_CMD_TIME_PUSH       (0xD9)  /* data[]: null-terminated time string */
/* BLE NUS state push (CM33 → CM55, only set true on host CONNECTED). */
#define IPC_CMD_BLE_STATE_PUSH  (0xDA)  /* data[0]: 0=disconnected/idle/adv, 1=connected */
/* Weather for the Smart Watch face, fetched on CM33_NS from two keyless
 * services. data[] carries a packed weather_ipc_t; CM55 never parses JSON. */
#define IPC_CMD_WEATHER_PUSH    (0xDB)

/* Forecast depth. Four hours because four panes fit the round face without a
 * horizontal scroll; six days because the week strip shows today plus five. */
#define WX_HOURS  (4)
#define WX_DAYS   (6)

typedef struct __attribute__((packed)) {
    char    city[24];
    char    description[20];
    int16_t temp_c;
    int16_t temp_max_c;
    int16_t temp_min_c;
    int16_t humidity;
    int16_t wind_kmh;
    int16_t code;
    int16_t rain_pct;

    /* Screen 2, the forecast page. Held as small integers rather than strings:
     * the whole reading has to fit one IPC message (IPC_DATA_MAX_LEN, 128), and
     * CM55 can format an hour or a weekday far more cheaply than CM33 can send
     * the text for it. */
    uint8_t hour_start;      /* local hour of hour_temp[0], 0-23            */
    int8_t  hour_temp[WX_HOURS];
    uint8_t hour_code[WX_HOURS];
    uint8_t day_wday;        /* weekday of day_max[0] (today), 0=Sun..6=Sat */
    int8_t  day_max[WX_DAYS];
    int8_t  day_min[WX_DAYS];
    uint8_t day_code[WX_DAYS];
} weather_ipc_t;

/* MQTT IPC commands (CM33 → CM55) */
#define IPC_CMD_MQTT_CONNECT    (0xF0)
#define IPC_CMD_MQTT_DISCONNECT (0xF1)
#define IPC_CMD_MQTT_PUBLISH    (0xF2)
#define IPC_CMD_MQTT_SUBSCRIBE  (0xF3)
#define IPC_CMD_MQTT_POLL       (0xF6)

/* TESAIoT Connectivity IPC commands (CM55 ↔ CM33_NS) */
#define IPC_CMD_TESAIOT_STATUS_PUSH  (0xF4)  /* CM33→CM55: status update (deferred flag) */
#define IPC_CMD_TESAIOT_MODE_SWITCH  (0xF5)  /* CM55→CM33: switch connectivity mode */
/* 0xF6 RESERVED — IPC_CMD_MQTT_POLL (above) */
#define IPC_CMD_TESAIOT_CONFIG_SET   (0xF7)  /* CM55→CM33: set single key=value */
#define IPC_CMD_TESAIOT_CONFIG_GET   (0xF8)  /* CM55→CM33: request full config */
#define IPC_CMD_TESAIOT_CONNECT      (0xF9)  /* CM55→CM33: connect MQTT/HTTPS */
#define IPC_CMD_TESAIOT_DISCONNECT   (0xFA)  /* CM55→CM33: disconnect MQTT/HTTPS */

/* TESAIoT connectivity status + config shared memory definitions */
#include "ipc_tesaiot_defs.h"

/* UI IPC commands (CM33 → CM55, MicroPython ui module → LVGL widgets) */
#include "ipc_ui_protocol.h"

/* GPIO IPC command (CM33 → CM55, LED state bitmask for LVGL UI) */
#define IPC_CMD_GPIO_LED_STATE  (0x80)

/* LED toggle command (CM55 → CM33, data[0] = LED index 0-2) */
#define IPC_CMD_LED_TOGGLE      (0x81)

/* CM33 callback client ID for receiving LED toggle from CM55 */
#define CM33_IPC_LED_CLIENT_ID  (4UL)

/* Joystick IPC commands (CM33 → CM55 request, CM55 → CM33 response) */
#define IPC_CMD_JOYSTICK_STATE  (0xC0)  /* Read joystick state */
#define IPC_CMD_JOYSTICK_INIT   (0xC1)  /* Initialize USB Host HID */

/* Smart Card (CCID) IPC commands (CM33 → CM55 request, CM55 → CM33 response) */
#define IPC_CMD_SMARTCARD_INIT  (0xC2)  /* Initialize USB Host CCID */
#define IPC_CMD_SMARTCARD_STATE (0xC3)  /* Read smartcard_state_t snapshot */
#define IPC_CMD_SMARTCARD_READ  (0xC4)  /* Trigger Thai ID card read */
#define IPC_CMD_SMARTCARD_APDU  (0xC5)  /* Raw APDU transceive */

/* QWA309 controls snapshot (CM33 → CM55 request, CM55 → CM33 response).
 * Returns the CM55-cached CapSense-4000T + pot state in one round trip.
 * CM33 must NEVER do direct I2C to the 4000T on QWA309 boards — it lives on
 * the CM55-owned display/touch bus (two-master hazard). Served from the
 * cm55_sensor_poll cache (weak default on boards without it). */
#define IPC_CMD_CONTROLS_STATE  (0xC6)  /* Read ipc_controls_state_t snapshot */

typedef struct __attribute__((packed)) {
    uint8_t  caps_valid;    /* 1 = CapSense data live */
    uint8_t  btn0;          /* pressed = 1 */
    uint8_t  btn1;          /* pressed = 1 */
    uint8_t  slider;        /* 0..100 */
    uint8_t  pot_valid;     /* 1 = pots live */
    uint8_t  reserved;
    uint16_t pot_raw[4];    /* VR1-4 raw 0..4095 */
} ipc_controls_state_t;

/* Filler for IPC_CMD_CONTROLS_STATE. Weak no-controls default lives in the
 * shared ipc_service.c; QWA309 board projects provide the strong version in
 * their cm55_sensor_poll module. Prototype lives HERE so both definitions
 * are checked against one signature. Returns false when unavailable. */
#include <stdbool.h>
bool cm55_controls_snapshot(ipc_controls_state_t *out);

/* Radar IPC commands (CM33 → CM55 request, CM55 → CM33 response) */
#define IPC_CMD_RADAR_STATUS    (0xB0)
#define IPC_CMD_RADAR_RANGE     (0xB1)  /* range profile: ipc_radar_range_t   */
#define IPC_CMD_RADAR_CONFIG    (0xB2)  /* msg.value = threshold dB * 10      */
/* 0xB3-0xB4 reserved for radar phase 2 (Doppler/gesture)                     */

/* HSM page IPC commands (CM55 → CM33): OPTIGA Trust M operations */
#define IPC_CMD_HSM_REQUEST     (0xB5)  /* Read chip data (UID, LCS, certs, counters) */
#define IPC_CMD_HSM_BENCHMARK   (0xB6)  /* Run crypto benchmarks (ECC, SHA, RNG) */
#define IPC_CMD_HSM_READ_CERT   (0xB7)  /* Read + parse cert DER (slot in resp->cmd) */
#define IPC_CMD_HSM_PIN_CHECK   (0xB8)  /* Check if PIN exists in DATA_3 */
#define IPC_CMD_HSM_PIN_SET     (0xB9)  /* Write SHA-256(digits) to DATA_3 */
#define IPC_CMD_HSM_PIN_VERIFY  (0xBA)  /* Verify PIN against stored hash */
#define IPC_CMD_HSM_HEALTH      (0xBB)  /* Run 8 self-tests */
#define IPC_CMD_HSM_PIN_RESET   (0xBC)  /* Erase PIN from DATA_3 (requires old PIN verify) */
#define IPC_CMD_HSM_SLOT_INFO   (0xBD)  /* Read one OID's metadata; optional plain-write probe */
#define IPC_CMD_HSM_PROVISION   (0xBE)  /* Start / poll CSR enrolment or Protected Update */
/* 0xBF is the last free HSM opcode. 0xC0 is IPC_CMD_JOYSTICK_STATE. */

/*******************************************************************************
 * IPC_CMD_HSM_SLOT_INFO — what an object's metadata says about how it may be
 * written. Request: msg->data[0..1] = OID, big-endian. msg->data[2] non-zero * msg->data[2] non-zero also asks for the object's write state.
 *
 * It is DERIVED FROM METADATA. Nothing is written to the object.
 *
 * An earlier version of this contract said a one-byte ordinary write "is
 * attempted" and called it safe because "an unlocked slot is written with the
 * byte it already holds". That was wrong on both counts: the byte came from a
 * read whose failure is indistinguishable from an empty object, so a failed
 * read wrote a literal 0x30 over the first byte of the device certificate —
 * which is 0xC0, the TLS identity tag — and a one-byte WRITE_ONLY at offset 0
 * plausibly truncates the object's used length to one. The write was removed.
 *
 * Change access, tag D0, already carries the answer: `21 <hi> <lo>` is
 * Int(OID), a manifest signed by that object and nothing else; 0xFF is NEV;
 * `E1 FC <lcs>` permits an ordinary write while LcsO is below <lcs>. Decode
 * those against LcsO from tag C0 and the object has told you, without being
 * touched. HSM_SLOT_PROBE_RC_OFF is therefore always zero: there is no chip
 * return code to report, because no command was sent.
 */
#define HSM_SLOT_OID_OFF        0   /*  2 bytes: OID, big-endian            */
#define HSM_SLOT_VALID_OFF      2   /*  1 byte:  1 = metadata was readable  */
#define HSM_SLOT_LCS_OFF        3   /*  1 byte:  tag C0                     */
#define HSM_SLOT_TYPE_OFF       4   /*  1 byte:  tag E8                     */
#define HSM_SLOT_USED_OFF       5   /*  2 bytes: tag C5, big-endian         */
#define HSM_SLOT_MAX_OFF        7   /*  2 bytes: tag C4, big-endian         */
#define HSM_SLOT_VERSION_OFF    9   /*  4 bytes: tag C1, big-endian; 0 = absent */
#define HSM_SLOT_ANCHOR_OFF    13   /*  2 bytes: Int(OID) from tag D0; 0 = plain write allowed */
#define HSM_SLOT_PROBE_OFF     15   /*  1 byte:  0 not run, 1 write accepted, 2 refused */
#define HSM_SLOT_PROBE_RC_OFF  16   /*  2 bytes: the chip's return code from the probe */
#define HSM_SLOT_RAW_LEN_OFF   18   /*  1 byte:  raw metadata length that follows */
#define HSM_SLOT_RAW_OFF       19   /*  up to 44 bytes: the metadata TLV verbatim */
#define HSM_SLOT_TOTAL_LEN     (HSM_SLOT_RAW_OFF + 44)

#define HSM_PROBE_NOT_RUN       0
#define HSM_PROBE_ACCEPTED      1
#define HSM_PROBE_REFUSED       2

/*******************************************************************************
 * IPC_CMD_HSM_PROVISION — start or poll a long operation.
 *
 * Asynchronous, unlike every other HSM command. CSR enrolment and Protected
 * Update take seconds and involve a network round trip, and hsm_ipc_send()
 * busy-waits on the GFX task — blocking for that long freezes the screen and
 * also stops ui_busy_modal_service() from ever drawing the overlay that would
 * explain the freeze. So the start call returns at once and CM55 polls.
 *
 * Request: msg->data[0] = op. msg->data[1..2] = target OID, [3..4] = anchor OID,
 *          msg->data[5..8] = run id, big endian.
 *
 * The run id is what makes an asynchronous screen safe to believe.
 *
 * CM55 picks a new one for every start and sends it with every poll; CM33
 * stores it on a start and echoes it in every response. A reply carrying any
 * other id belongs to a run the screen is no longer watching, and is discarded.
 *
 * Without it, three separate failures all looked like success: a reply that
 * timed out on CM55 and landed after the next request was accepted as the
 * answer to that request; a poll issued before a start had been dispatched read
 * the previous run's state; and because CM33's state never returns to IDLE, the
 * second run of a boot whose start was dropped reported the FIRST run's DONE
 * and its pair result — six green ticks and "key possession proved" for a run
 * in which no key was generated and nothing was published. Every one of those
 * is a confident false verdict on the one screen built to be trustworthy.
 *******************************************************************************/
#define HSM_PROV_OP_POLL        0   /* return current state, start nothing     */
#define HSM_PROV_OP_CSR         1   /* CSR enrolment: keygen, CSR, publish     */
#define HSM_PROV_OP_PU          2   /* Protected Update carrying a fresh CSR   */
#define HSM_PROV_OP_FETCH_CSR   3   /* read the last CSR back, msg->value = offset */
#define HSM_PROV_OP_UNLOCK      4   /* clear the signed-manifest requirement       */

#define HSM_PROV_STATE_IDLE     0
#define HSM_PROV_STATE_BUSY     1
#define HSM_PROV_STATE_DONE     2
#define HSM_PROV_STATE_FAILED   3

/* Steps are reported so the screen can show a chain rather than a spinner. */
#define HSM_PROV_STEP_NONE      0
#define HSM_PROV_STEP_KEYGEN    1   /* key pair generated inside the key slot  */
#define HSM_PROV_STEP_CSR       2   /* CSR built and self-signed               */
#define HSM_PROV_STEP_PUBLISH   3   /* request published to the platform       */
#define HSM_PROV_STEP_WAIT      4   /* waiting for the platform                */
#define HSM_PROV_STEP_INSTALL   5   /* certificate written to the target       */
#define HSM_PROV_STEP_VERIFY    6   /* verify_pair run                         */

/* Events the ingest reports to a watching UI. TESAIOT_PU_CHIP_VERIFIED_MANIFEST
 * is the moment the secure element itself checked the platform's signature
 * against its trust anchor — the one step in the flow that a compromised host
 * cannot fake, and the reason any of this is worth more than a plain write. */
#define TESAIOT_PU_CHIP_VERIFIED_MANIFEST  1

#define HSM_PROV_STATE_OFF      0   /*  1 byte                                  */
#define HSM_PROV_STEP_OFF       1   /*  1 byte                                  */
#define HSM_PROV_PAIR_OFF       2   /*  1 byte: 0 no, 1 yes, 0xFF not run       */
#define HSM_PROV_CSRSIG_OFF     3   /*  1 byte: CSR self-signature checked here */
#define HSM_PROV_CSRLEN_OFF     4   /*  2 bytes: CSR PEM length                 */
#define HSM_PROV_CORR_OFF       6   /* 40 bytes: correlation id, NUL-padded     */
#define HSM_PROV_MSG_OFF       46   /* 96 bytes: a sentence for the screen      */
#define HSM_PROV_RUN_OFF      142   /*  4 bytes: the run id this answer is for  */

/* resp->status on a start: whether the device took the request.
 *
 * The start used to be fire-and-forget. A start the device could not serve —
 * it is already running one — was answered with the state of the run ALREADY IN
 * PROGRESS, which the screen then adopted as its own. Rejecting explicitly is
 * what lets the screen say "busy" instead of showing someone else's verdict. */
#define HSM_PROV_ACCEPTED       0
#define HSM_PROV_REJECTED_BUSY  1
#define HSM_PROV_RUNOP_OFF    146   /*  1 byte: which operation that run is     */
#define HSM_PROV_TOTAL_LEN    (HSM_PROV_RUNOP_OFF + 1)

#define HSM_PROV_MSG_MAX        96
#define HSM_PROV_CORR_MAX       40
#define HSM_PROV_CSR_CHUNK     200  /* bytes per HSM_PROV_OP_FETCH_CSR reply    */

/* CM33 callback client ID for receiving HSM requests from CM55 */
#define CM33_IPC_HSM_CLIENT_ID  (2UL)

/* TESAIoT credential IPC commands (CM33 → CM55) */
#define IPC_CMD_TESAIOT_INIT       (0xA0)
#define IPC_CMD_TESAIOT_DEVICE_ID  (0xA1)
#define IPC_CMD_TESAIOT_LICENSE    (0xA2)
#define IPC_CMD_TESAIOT_CRED_READ  (0xA3)
#define IPC_CMD_TESAIOT_CRED_WRITE (0xA4)
#define IPC_CMD_TESAIOT_RANDOM     (0xA5)
#define IPC_CMD_TESAIOT_CRED_ERASE (0xA6)
#define IPC_CMD_TESAIOT_HASH       (0xA7)
#define IPC_CMD_TESAIOT_HMAC       (0xA8)
#define IPC_CMD_TESAIOT_AES_KEYGEN (0xA9)
#define IPC_CMD_TESAIOT_AES_ENC    (0xAA)
#define IPC_CMD_TESAIOT_AES_DEC    (0xAB)
#define IPC_CMD_TESAIOT_SIGN       (0xAC)
#define IPC_CMD_TESAIOT_COUNTER_RD (0xAD)
#define IPC_CMD_TESAIOT_COUNTER_INC (0xAE)
#define IPC_CMD_TESAIOT_HEALTH     (0xAF)
#define IPC_CMD_TESAIOT_LAST       (0xAF)

/* IPC Service client ID (WiFi + MQTT + TESAIoT bidirectional) */
#define CM55_IPC_SERVICE_CLIENT_ID (7UL)

/* IPC UI client ID (MicroPython ui module → LVGL widgets) */
#define CM55_IPC_UI_CLIENT_ID      (8UL)

/* Gravity constant for acceleration conversion */
#define GRAVITY_ACCEL           (9.80665f)

#define IPC_DATA_MAX_LEN (128UL)

/* Response data max length */
#define IPC_RESPONSE_DATA_MAX (240UL)

/*******************************************************************************
 * IPC Message Structure
 *******************************************************************************/
typedef struct
{
  uint16_t client_id;          /* Bits 0-7: Client ID */
  uint16_t intr_mask;          /* Bits 16-31: Release Mask (MANDATORY for Pipe Driver) */
  uint32_t cmd;                /* Command code */
  uint32_t value;              /* Command argument or flags */
  char data[IPC_DATA_MAX_LEN]; /* Payload buffer */
} ipc_msg_t;

/*******************************************************************************
 * Sensor Data Structures (packed into ipc_msg_t.data[])
 *******************************************************************************/

/* BMI270 IMU wire scale (shared by every encoder on CM33 and decoder on CM55).
 * Build the encoder and every decoder from ONE constant so the two never drift.
 *
 * Default is +/-2g (16384 LSB/g) which every board has always used. A board that
 * runs the DEEPCRAFT motion model defines BENTO_BMI270_ACCEL_8G to switch the
 * whole chain to +/-8g (4096 LSB/g): a vigorous "shaking" gesture easily exceeds
 * 2g and a 2g range clips the peaks so the model no longer recognises it. At
 * +/-8g the int16 wire value still spans full scale (8 g * 4096 = 32768 ~= INT16
 * max). The flag MUST be defined for both cores (proj_cm33_ns encoder + proj_cm55
 * decoders) or the accel reads 4x wrong. Gyro stays +/-2000 dps = 16.4 LSB/dps
 * (a hand gesture never approaches 2000 dps, so it never clips). */
#ifdef BENTO_BMI270_ACCEL_8G
#define IPC_BMI270_ACCEL_LSB_PER_G   (4096.0f)   /* +/-8g range */
#else
#define IPC_BMI270_ACCEL_LSB_PER_G   (16384.0f)  /* +/-2g range (default) */
#endif
#define IPC_BMI270_GYRO_LSB_PER_DPS  (16.4f)

/* BMI270 IMU: accel + gyro raw data */
typedef struct __attribute__((packed)) {
    int16_t ax, ay, az;     /* Accel raw (divide by IPC_BMI270_ACCEL_LSB_PER_G for g) */
    int16_t gx, gy, gz;     /* Gyro raw (divide by IPC_BMI270_GYRO_LSB_PER_DPS for dps) */
    uint16_t sequence;
} ipc_sensor_bmi270_t;

/* DPS368: pressure + temperature */
typedef struct __attribute__((packed)) {
    int32_t pressure_x100;      /* hPa * 100 */
    int16_t temperature_x100;   /* Celsius * 100 */
    uint16_t sequence;
} ipc_sensor_dps368_t;

/* SHT40: humidity + temperature */
typedef struct __attribute__((packed)) {
    int16_t temperature_x100;   /* Celsius * 100 */
    uint16_t humidity_x100;     /* %RH * 100 */
    uint16_t sequence;
} ipc_sensor_sht40_t;

/* BMM350: magnetometer X/Y/Z in micro-Tesla * 100 */
typedef struct __attribute__((packed)) {
    int16_t mx_x100;           /* X uT * 100 */
    int16_t my_x100;           /* Y uT * 100 */
    int16_t mz_x100;           /* Z uT * 100 */
    uint16_t heading_x10;      /* Compass heading * 10 (0-3600) */
    uint16_t sequence;
} ipc_sensor_bmm350_t;

/* CapSense: touch buttons + slider */
typedef struct __attribute__((packed)) {
    uint8_t btn0_pressed;      /* 0 or 1 */
    uint8_t btn1_pressed;      /* 0 or 1 */
    uint8_t slider;            /* 0-100 (%) */
    uint8_t reserved;
    uint16_t sequence;
} ipc_sensor_capsense_t;

/* Potentiometer: ADC value */
typedef struct __attribute__((packed)) {
    uint16_t raw;              /* 0-65535 (16-bit scaled) */
    uint16_t percent_x10;     /* 0-1000 (0.0-100.0%) */
    uint16_t sequence;
} ipc_sensor_pot_t;

/* Sensor snapshot (packed into ipc_response_t.data[])
 * Returned by IPC_CMD_SENSOR_SNAPSHOT — CM55 sensorhub latest values. */
typedef struct __attribute__((packed)) {
    ipc_sensor_bmi270_t   bmi270;
    uint8_t               has_bmi270;
    ipc_sensor_capsense_t capsense;
    uint8_t               has_capsense;
    ipc_sensor_pot_t      pot;
    uint8_t               has_pot;
} ipc_sensor_snapshot_t;

/* Joystick state (packed into ipc_response_t.data[]) */
typedef struct __attribute__((packed)) {
    uint8_t connected;         /* 0=disconnected, 1=connected */
    uint8_t sequence;          /* Increments on each new report */
    uint8_t left_x, left_y;   /* 0x00-0xFF, center ~0x80/0x7F */
    uint8_t right_x, right_y;
    uint8_t buttons1;          /* hat[0:3] + X[4] A[5] B[6] Y[7] */
    uint8_t buttons2;          /* LB[0] RB[1] LT[2] RT[3] Back[4] Start[5] L3[6] R3[7] */
    uint16_t vid, pid;         /* USB Vendor/Product ID */
    /* Debug fields */
    uint8_t usb_init_done;     /* 1 = USBH_Init completed OK */
    uint8_t init_stage;        /* USB_STAGE_* (0-7) */
    uint16_t add_event_cnt;    /* Device add events from USBH */
    uint16_t remove_event_cnt; /* Device remove events */
    uint16_t report_cnt;       /* HID reports received */
    uint32_t isr_count;        /* USB interrupt count */
    uint32_t port_power_cnt;   /* Port power events */
    uint8_t  usbh_running;     /* USBH_IsRunning() */
    uint8_t  num_devices;      /* USBH_GetNumDevicesConnected() */
    uint8_t  root_conns;       /* USBH_GetNumRootPortConnections() */
    uint8_t  usb_class;        /* USB interface class (0x03=HID, 0xFF=vendor) */
    uint16_t usb_vid;          /* VID from USB level */
    uint16_t usb_pid;          /* PID from USB level */
    uint8_t  source;           /* IPC_JOY_SRC_* : which controller/decoder is active
                                  — for UI/REPL name. Must match JOY_SRC_* in
                                  usb_hid_joystick.h. */
} ipc_joystick_state_t;

/* ipc_joystick_state_t.source values (controller identity, mirror of JOY_SRC_*) */
#define IPC_JOY_SRC_NONE   0
#define IPC_JOY_SRC_F310   1   /* Logitech F310 (USB HID DirectInput) */
#define IPC_JOY_SRC_XBOX   2   /* Xbox Series (USB GIP) */
#define IPC_JOY_SRC_NJ43   3   /* NUBWO NJ43 (USB HID, DragonRise layout) */

/* Smart Card state (packed into ipc_response_t.data[])
 * NOTE: Full thai_id_data_t won't fit in IPC_RESPONSE_DATA_MAX (240 bytes).
 * For IPC_CMD_SMARTCARD_STATE we return this compact status.
 * For IPC_CMD_SMARTCARD_READ we return field-by-field via repeated requests. */
typedef struct __attribute__((packed)) {
    uint8_t  connected;        /* 0=disconnected, 1=ACR39U detected */
    uint8_t  card_present;     /* 0=no card, 1=card in slot */
    uint8_t  card_read_ok;     /* 0=not read, 1=data available */
    uint8_t  reading;          /* 1=currently reading */
    uint8_t  usb_init_done;    /* 1=USBH CCID init complete */
    uint8_t  init_stage;       /* Init progress (0-7) */
    uint16_t vid, pid;         /* USB VID/PID of reader */
    uint8_t  atr_len;          /* ATR length (0 = no ATR) */
    uint8_t  atr[33];          /* Answer-To-Reset */
    /* Compact card data (fits within IPC_RESPONSE_DATA_MAX) */
    char     citizen_id[14];   /* 13 digits + null */
    char     name_en[101];     /* ASCII, null-terminated */
    char     birthdate[9];     /* YYYYMMDD + null */
    char     issue_date[9];    /* YYYYMMDD + null */
    char     expire_date[9];   /* YYYYMMDD + null */
} ipc_smartcard_state_t;       /* ~188 bytes — fits IPC_RESPONSE_DATA_MAX */

/* Smart Card field IDs for IPC_CMD_SMARTCARD_READ (msg.value = field ID)
 * Response data[] contains UTF-8 string for the requested field. */
#define SMARTCARD_FIELD_CID       (0)
#define SMARTCARD_FIELD_NAME_TH   (1)
#define SMARTCARD_FIELD_NAME_EN   (2)
#define SMARTCARD_FIELD_BIRTHDATE (3)
#define SMARTCARD_FIELD_ISSUE     (4)
#define SMARTCARD_FIELD_EXPIRE    (5)
#define SMARTCARD_FIELD_ADDRESS   (6)

/* Radar status (packed into ipc_response_t.data[]) */
typedef struct __attribute__((packed)) {
    uint8_t initialized;        /* 1 if radar hw init succeeded */
    uint8_t presence;           /* 1 if person detected */
    uint8_t reserved[2];
    float   energy;             /* Smoothed signal energy delta */
} ipc_radar_status_t;

/* Radar range-profile result (IPC_CMD_RADAR_RANGE, packed into
 * ipc_response_t.data[]). Produced by radar_dsp.c on CM55: Chebyshev HPF ->
 * 128-pt FFT -> dB -> anti-coupling -> first-peak search (DSP chain per the
 * Infineon micropython-radar-bgt60 reference, reimplemented in C).
 * distance_mm==0 means "no target above threshold this frame". */
typedef struct __attribute__((packed)) {
    uint8_t  initialized;       /* 1 if radar hw + dsp running */
    uint8_t  target;            /* 1 if a peak above threshold was found */
    uint16_t seq;               /* increments per processed frame */
    uint32_t distance_mm;       /* first-peak distance in millimetres */
    float    peak_db;           /* peak magnitude in dB (post anti-coupling) */
    uint16_t resolution_mm;     /* metres-per-bin * 1000 (bin width) */
    uint16_t bin;               /* peak bin index (diagnostics) */
} ipc_radar_range_t;            /* 16 bytes — fits IPC_RESPONSE_DATA_MAX */

/*******************************************************************************
 * IPC Response Structure (bidirectional: CM55 → CM33_NS via shared memory)
 *
 * Pattern:
 *   CM33_NS: sets ready=0, sends IPC command with response ptr in msg.value
 *   CM55:    processes command, fills response, sets ready=1
 *   CM33_NS: busy-waits on ready, reads data
 *******************************************************************************/
typedef struct __attribute__((packed)) {
    volatile uint8_t ready;     /* 0=pending, 1=ready (written by CM55) */
    uint8_t cmd;                /* Echo of request command */
    uint8_t status;             /* 0=success, nonzero=error code */
    uint8_t reserved;
    uint16_t data_len;          /* Bytes of valid data in data[] */
    uint16_t reserved2;
    uint8_t data[IPC_RESPONSE_DATA_MAX]; /* Response payload */
} ipc_response_t;

/*******************************************************************************
 * WiFi Scan Result Entry (fits in ipc_response_t.data[])
 * Max entries = IPC_RESPONSE_DATA_MAX / sizeof(ipc_wifi_scan_entry_t) = 6
 *******************************************************************************/
#define IPC_WIFI_SCAN_MAX_ENTRIES (6)

typedef struct __attribute__((packed)) {
    char ssid[33];              /* SSID (null-terminated, max 32 chars) */
    int8_t rssi;                /* Signal strength dBm */
    uint8_t security;           /* 0=open, 1=WEP, 2=WPA, 3=WPA2, 4=WPA3 */
    uint8_t channel;            /* WiFi channel */
} ipc_wifi_scan_entry_t;        /* 36 bytes per entry */

/*******************************************************************************
 * WiFi Status Blob Layout (IPC_CMD_WIFI_STATUS response payload)
 *
 * CM33_NS encodes this blob (sensor_auto_task.c wifi_ipc_fill_status) and the
 * CM55 side decodes it (wifi_manager.c wifi_manager_decode_status). BOTH sides
 * MUST use these names — never raw offsets — so encoder and decoder never drift.
 *
 * v1 (53 bytes): mode / connected / rssi + IP string + SSID string
 * v2 (59 bytes): v1 + 6-byte STA MAC appended at offset 53
 *******************************************************************************/
#define WIFI_STATUS_OFF_MODE      (0)   /* uint8: wifi mode */
#define WIFI_STATUS_OFF_CONNECTED (1)   /* uint8: 0/1 */
#define WIFI_STATUS_OFF_RSSI      (2)   /* int8: dBm */
#define WIFI_STATUS_OFF_IP        (4)   /* char[]: dotted-quad, NUL-terminated */
#define WIFI_STATUS_IP_MAXLEN     (15)  /* strncpy limit — keeps final NUL */
#define WIFI_STATUS_OFF_SSID      (20)  /* char[]: SSID, NUL-terminated */
#define WIFI_STATUS_OFF_MAC       (53)  /* uint8[6]: STA MAC (v2 only) */
#define WIFI_STATUS_LEN_V1        (53)  /* original blob (no MAC) */
#define WIFI_STATUS_LEN_V2        (59)  /* v1 + 6-byte MAC */
#define WIFI_MAC_ADDR_LEN         (6)

/*******************************************************************************
 * TESAIoT Credential Slot Mapping
 *
 * Slot → OID via tesaiot_slot_to_oid() lookup (NOT simple addition).
 * Slot 4 (OID 0xF1D4) is RESERVED for Protected Update confidentiality key
 * (AES-CCM shared secret used by tesaiot CSR/Protected Update workflow).
 *
 * Type 3 Data Objects (0xF1D0-0xF1DB): max 140 bytes each
 * Type 2 Data Objects (0xF1E0-0xF1E1): max 1500 bytes each (seldom changed)
 *******************************************************************************/
#define TESAIOT_SLOT_DEVICE_ID   (0)   /* OID 0xF1D0 */
#define TESAIOT_SLOT_LICENSE_KEY (1)   /* OID 0xF1D1 */
#define TESAIOT_SLOT_MQTT_USER   (2)   /* OID 0xF1D2 */
#define TESAIOT_SLOT_MQTT_PASS   (3)   /* OID 0xF1D3 */
/* Slot 4 = RESERVED (OID 0xF1D4 — Protected Update confidentiality key) */
#define TESAIOT_SLOT_WIFI_SSID   (5)   /* OID 0xF1D5 */
#define TESAIOT_SLOT_WIFI_PASS   (6)   /* OID 0xF1D6 */
#define TESAIOT_SLOT_API_KEY     (7)   /* OID 0xF1D7 */
#define TESAIOT_SLOT_USER0       (8)   /* OID 0xF1D8 */
#define TESAIOT_SLOT_USER1       (9)   /* OID 0xF1D9 */
#define TESAIOT_SLOT_USER2       (10)  /* OID 0xF1DA */
#define TESAIOT_SLOT_USER3       (11)  /* OID 0xF1DB */
#define TESAIOT_SLOT_LARGE0      (12)  /* OID 0xF1E0 — 1500 bytes max */
#define TESAIOT_SLOT_LARGE1      (13)  /* OID 0xF1E1 — 1500 bytes max */
#define TESAIOT_SLOT_COUNT       (14)
#define TESAIOT_SLOT_RESERVED    (4)   /* Protected Update — do NOT use */
#define TESAIOT_SLOT_MAX_SIZE    (140)  /* Type 3 data objects (slots 0-11) */
#define TESAIOT_SLOT_LARGE_MAX   (1500) /* Type 2 data objects (slots 12-13) */

/* Slot → OID lookup (handles gap at slot 4 and large data slots 12-13) */
static inline uint16_t tesaiot_slot_to_oid(uint8_t slot) {
    if (slot <= 3)  return 0xF1D0 + slot;       /* 0xF1D0-0xF1D3 */
    if (slot >= 5 && slot <= 11) return 0xF1D0 + slot; /* 0xF1D5-0xF1DB */
    if (slot == 12) return 0xF1E0;               /* Large data 0 */
    if (slot == 13) return 0xF1E1;               /* Large data 1 */
    return 0;  /* Invalid (slot 4 reserved, or out of range) */
}

/* Max data size for a given slot */
static inline uint16_t tesaiot_slot_max_size(uint8_t slot) {
    if (slot == 12 || slot == 13) return TESAIOT_SLOT_LARGE_MAX;
    return TESAIOT_SLOT_MAX_SIZE;
}

/*******************************************************************************
 * Function prototypes
 *******************************************************************************/

/** CM33 */
void cm33_ipc_communication_setup(void);
void cm33_ipc_communication_recover(void);
void cm33_ipc_pipe_isr(void);

/** CM55 */
void cm55_ipc_communication_setup(void);
void cm55_ipc_pipe_isr(void);
void cm55_ipc_pipe_drain_release(void);

/* Diagnostic: CM55 pipe endpoint busy flag (nonzero-stuck = wedged pipe). */
uint32_t cm55_ipc_pipe_ep_busy(void);

#endif /* SOURCE_IPC_COMMUNICATION_H */
