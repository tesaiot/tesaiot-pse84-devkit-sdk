/*******************************************************************************
 * cm55_sensor_poll.c — CM55-local base-board sensor reading for the
 *                       TESAIoT Dev Kit (AI Kit SoM + QWA309 base board).
 *
 * Reads the QWA309 CapSense (external PSoC 4000T @ I2C 0x08) and the four
 * potentiometers (VR1-4 on AutAnalog SAR GPIO ch 4-7 / P15.4-7) directly on
 * CM55, reusing the display I2C controller context. Feeds data into
 * ipc_sensorhub via the local feed API — no IPC from CM33_NS needed.
 *
 * Knob-to-channel order is NOT identity — see s_pot_adc_ch[] below (the
 * VR1/VR2 traces are swapped on the QWA309 PCB).
 *
 * Adapted from the Eva Kit cm55_sensor_poll driver. Differences:
 *   - BMI270 read REMOVED (AI Kit services the IMU on CM33_NS).
 *   - Potentiometer extended from 1 channel (Eva P15[1]) to 4 channels
 *     (QWA309 VR1-4 on SAR GPIO ch 4-7); channel 0 (VR1) feeds the single-value
 *     ipc_sensor_pot_t so the existing Controls page + pot MicroPython snapshot
 *     work unchanged, while all four raw values are exposed via
 *     cm55_pot_read_all() for the QWA309 pot MicroPython API.
 *
 * SAR channel mapping: design.modus configures SAR GPIO channels 4-7 on
 * pins P15.4-7 (CYBSP_SAR_ADC_GPIO_CH_4..7 + SCAN_1..4); CM33_NS main.c
 * runs Cy_AutAnalog_Init + StartAutonomousControl at boot. s_pot_ok is
 * gated on Cy_AutAnalog_SAR_GetHSchanResultStatus() reporting live results
 * on all four channels — a 0 reading alone is NOT treated as proof of life.
 * See TESAIoT_PLAN/2026-7/TESAIoT_DevKit_Merge/PLAN.md (Wave 1 risk register).
 ******************************************************************************/

#include "cm55_sensor_poll.h"
#include "ipc_sensorhub.h"
#include "ipc_communication.h"
#include "display_i2c_config.h"
#include "lv_port_indev.h"
#include "cy_scb_i2c.h"
#include "bsp_feature_flags.h"
#if BSP_HAS_POTENTIOMETER
#include "cy_autanalog_sar.h"
#endif
#include <stdio.h>
#include <string.h>

//! [cm55_disp_touch_i2c_shared_extern]
/*******************************************************************************
 * I2C shared with display — extern from lv_port_indev / display init
 ******************************************************************************/
extern cy_stc_scb_i2c_context_t disp_touch_i2c_controller_context;

#define SENSOR_I2C_HW       DISPLAY_I2C_CONTROLLER_HW
#define SENSOR_I2C_CTX      (&disp_touch_i2c_controller_context)
/* BOUNDED timeout per PDL byte call. This read runs in the GFX task at MAX-1
 * priority: with the old 0 (= block forever), a wedged/clock-stretching 4000T
 * parked the GFX task on the bus and starved every lower-priority CM55 task —
 * the prime suspect for the USB joystick HID stream stall (JOYSTICK_EVIDENCE).
 * 2 ms x ~5 byte-calls caps the worst case at ~10 ms, once, then backoff. */
#define I2C_TIMEOUT_MS      (2U)
//! [cm55_disp_touch_i2c_shared_extern]
/* After a failed CapSense transaction, skip this many 50 ms ticks before
 * retrying so a sick sensor costs ~10 ms per 500 ms, not per tick. */
#define CAPSENSE_FAIL_BACKOFF_TICKS  (10U)

/*******************************************************************************
 * CapSense (external PSoC 4000T over EZI2C)
 ******************************************************************************/
#define CAPSENSE_ADDR           (0x08U)
#define CAPSENSE_READ_SIZE      (3U)

/*******************************************************************************
 * Potentiometers — AutAnalog SAR ADC on the CM55 domain.
 * QWA309 VR1-4 occupy P15.4-7 = SAR GPIO channels 4-7 (12-bit, 0-1.8V ref),
 * but not in knob order — see s_pot_adc_ch[].
 ******************************************************************************/
#if BSP_HAS_POTENTIOMETER
#define POT_SAR_IDX         (0U)    /* SAR ADC index 0 */
#define POT_ADC_MAX         (4095)  /* 12-bit SAR ADC */

/* Logical pot index (0=VR1 .. 3=VR4) → SAR GPIO channel.
 * The QWA309 PCB routes P15.4 (ch 4) to the VR2 knob and P15.5 (ch 5) to
 * the VR1 knob — the first two traces are swapped on the board, verified
 * on hardware 2026-07-31. VR3/VR4 are wired straight. */
static const uint8_t s_pot_adc_ch[QWA309_POT_COUNT] = { 5U, 4U, 6U, 7U };
/* All four pot channels must have produced a result in the last scan. */
#define POT_ADC_READY_MASK  ((uint8_t)(CY_AUTANALOG_SAR_CHAN_MASK_GPIO4 | \
                                       CY_AUTANALOG_SAR_CHAN_MASK_GPIO5 | \
                                       CY_AUTANALOG_SAR_CHAN_MASK_GPIO6 | \
                                       CY_AUTANALOG_SAR_CHAN_MASK_GPIO7))
/* CM33_NS starts the AutAnalog scan before CM55 boots, but allow a few
 * poll ticks of grace before declaring the pots absent for the session. */
#define POT_PROBE_TICKS     (50U)
#endif

/*******************************************************************************
 * State
 ******************************************************************************/
static bool s_capsense_ok = false;
#if BSP_HAS_POTENTIOMETER
static bool     s_pot_ok = false;
static uint8_t  s_pot_probe_left = POT_PROBE_TICKS;
static uint16_t s_pot_raw[QWA309_POT_COUNT] = {0};
#endif
static uint16_t s_seq = 0;

/* CapSense baseline (idle state captured on init) */
static uint8_t s_caps_idle_btn0 = 0;
static uint8_t s_caps_idle_btn1 = 0;
static bool    s_caps_have_baseline = false;
static bool    s_init_called = false;

/* Latest decoded CapSense state (for the IPC_CMD_CONTROLS_STATE snapshot) +
 * failure backoff so a sick sensor can't monopolize the GFX task. */
static uint8_t s_caps_btn0 = 0;
static uint8_t s_caps_btn1 = 0;
static uint8_t s_caps_slider = 0;
static bool    s_caps_live = false;
static uint8_t s_caps_backoff = 0;

/*******************************************************************************
 * CapSense read (direct 3-byte read, no register address)
 ******************************************************************************/
static uint8_t capsense_normalize_btn(uint8_t raw)
{
    if (raw >= 0x30 && raw <= 0x39) return (uint8_t)(raw - 0x30); /* ASCII '0'-'9' */
    if (raw >= 30 && raw <= 32)     return (uint8_t)(raw - 30);   /* BLE variant */
    if (raw <= 2)                   return raw;                   /* Direct 0-2 */
    return (raw != 0) ? 1 : 0;
}

static bool capsense_read_raw(uint8_t buf[CAPSENSE_READ_SIZE])
{
    cy_en_scb_i2c_status_t st;

    st = Cy_SCB_I2C_MasterSendStart(SENSOR_I2C_HW, CAPSENSE_ADDR,
            CY_SCB_I2C_READ_XFER, I2C_TIMEOUT_MS, SENSOR_I2C_CTX);
    if (st != CY_SCB_I2C_SUCCESS) {
        Cy_SCB_I2C_MasterSendStop(SENSOR_I2C_HW, I2C_TIMEOUT_MS, SENSOR_I2C_CTX);
        return false;
    }

    for (int i = 0; i < CAPSENSE_READ_SIZE; i++) {
        cy_en_scb_i2c_command_t ack = (i < CAPSENSE_READ_SIZE - 1)
                                    ? CY_SCB_I2C_ACK
                                    : CY_SCB_I2C_NAK;
        st = Cy_SCB_I2C_MasterReadByte(SENSOR_I2C_HW, ack,
                &buf[i], I2C_TIMEOUT_MS, SENSOR_I2C_CTX);
        if (st != CY_SCB_I2C_SUCCESS) break;
    }

    Cy_SCB_I2C_MasterSendStop(SENSOR_I2C_HW, I2C_TIMEOUT_MS, SENSOR_I2C_CTX);
    return (st == CY_SCB_I2C_SUCCESS);
}

static bool capsense_read_and_feed(void)
{
    uint8_t buf[CAPSENSE_READ_SIZE] = {0};

    if (!capsense_read_raw(buf)) return false;

    if (!s_caps_have_baseline) {
        s_caps_idle_btn0 = capsense_normalize_btn(buf[0]);
        s_caps_idle_btn1 = capsense_normalize_btn(buf[1]);
        s_caps_have_baseline = true;
    }

    uint8_t btn0_code = capsense_normalize_btn(buf[0]);
    uint8_t btn1_code = capsense_normalize_btn(buf[1]);

    //! [ipc_sensorhub_feed_capsense_fields]
    /* ...context: inside capsense_read_and_feed() - GFX task context ... */
    ipc_sensor_capsense_t d;
    d.btn0_pressed = (btn0_code != s_caps_idle_btn0) ? 1 : 0;
    d.btn1_pressed = (btn1_code != s_caps_idle_btn1) ? 1 : 0;
    d.slider = buf[2];
    d.reserved = 0;
    d.sequence = s_seq++;

    /* Cache the decoded state for the CM33 IPC_CMD_CONTROLS_STATE snapshot
     * (byte-size fields — single-writer GFX task, torn reads impossible). */
    s_caps_btn0   = d.btn0_pressed;
    s_caps_btn1   = d.btn1_pressed;
    s_caps_slider = d.slider;
    s_caps_live   = true;

    ipc_sensorhub_feed_capsense(&d);
    return true;
    //! [ipc_sensorhub_feed_capsense_fields]
}

/*******************************************************************************
 * Potentiometer — read 4 SAR channels, feed VR1 to the single-value ipc path
 ******************************************************************************/
#if BSP_HAS_POTENTIOMETER
static int32_t s_pot_prev[QWA309_POT_COUNT] = {-1, -1, -1, -1};
static uint8_t s_pot_rej[QWA309_POT_COUNT]  = {0, 0, 0, 0};

/* Real readiness check: the SAR sets a per-channel status bit only after
 * that channel was actually sampled in the previous scan (i.e. CM33_NS ran
 * Cy_AutAnalog_Init + StartAutonomousControl AND channels 4-7 are configured
 * in the scan sequence). Cy_AutAnalog_SAR_ReadResult() returning 0 is NOT
 * proof of life — an unconfigured channel also reads 0. */
static bool pot_channels_ready(void)
{
    uint8_t st = Cy_AutAnalog_SAR_GetHSchanResultStatus(POT_SAR_IDX);
    return (st & POT_ADC_READY_MASK) == POT_ADC_READY_MASK;
}

static uint16_t pot_read_channel(uint8_t idx)
{
    int32_t adc_count = Cy_AutAnalog_SAR_ReadResult(
        POT_SAR_IDX, CY_AUTANALOG_SAR_INPUT_GPIO, s_pot_adc_ch[idx]);

    if (adc_count < 0) adc_count = 0;
    if (adc_count > POT_ADC_MAX) adc_count = POT_ADC_MAX;

    /* Spike filter: reject a sudden drop to near-zero ONCE (ADC glitch);
     * a second consecutive low reading is the knob really at the end stop —
     * accept it, or the filter latches the stale value until the pot moves. */
    if (s_pot_prev[idx] >= 0) {
        int32_t delta = adc_count - s_pot_prev[idx];
        if (delta < 0) delta = -delta;
        if (delta > 200 && adc_count < 50 && s_pot_rej[idx] == 0U) {
            s_pot_rej[idx] = 1U;
            adc_count = s_pot_prev[idx];
        } else {
            s_pot_rej[idx] = 0U;
        }
    }
    s_pot_prev[idx] = adc_count;
    return (uint16_t)adc_count;
}

//! [ipc_sensorhub_feed_pot_normalised]
static bool pot_read_and_feed(void)
{
    for (uint8_t i = 0; i < QWA309_POT_COUNT; i++) {
        s_pot_raw[i] = pot_read_channel(i);
    }

    /* Feed VR1 (channel 0) to the single-value pot IPC so the Controls page
     * and the pot MicroPython snapshot light up unchanged. */
    uint16_t adc0 = s_pot_raw[0];
    ipc_sensor_pot_t d;
    d.raw         = (uint16_t)((uint32_t)adc0 * 65535U / POT_ADC_MAX);
    d.percent_x10 = (uint16_t)((uint32_t)adc0 * 1000U / POT_ADC_MAX);
    d.sequence    = s_seq++;

    ipc_sensorhub_feed_pot(&d);
    return true;
}
//! [ipc_sensorhub_feed_pot_normalised]
#endif /* BSP_HAS_POTENTIOMETER */

/*******************************************************************************
 * Public API
 ******************************************************************************/
bool cm55_sensor_poll_init(void)
{
    s_init_called = true;

#if BSP_HAS_CAPSENSE
    {
        uint8_t caps_buf[CAPSENSE_READ_SIZE];
        if (capsense_read_raw(caps_buf)) {
            s_capsense_ok = true;
            s_caps_idle_btn0 = capsense_normalize_btn(caps_buf[0]);
            s_caps_idle_btn1 = capsense_normalize_btn(caps_buf[1]);
            s_caps_have_baseline = true;
        }
    }
#endif

#if BSP_HAS_POTENTIOMETER
    s_pot_ok = pot_channels_ready();
#endif

    return s_capsense_ok
#if BSP_HAS_POTENTIOMETER
           /* Keep the poll timer alive while the grace-period probe may
            * still bring the pots up (caller only creates the LVGL tick
            * timer when this returns true). */
           || s_pot_ok || (s_pot_probe_left > 0U)
#endif
           ;
}

void cm55_capsense_tick(void)
{
#if BSP_HAS_CAPSENSE
    if (!s_capsense_ok) {
        return;
    }
    /* Shared 3V3 bus paused — CM33_NS owns the SCB during OPTIGA
     * transactions until IPC_CMD_TOUCH_RESUME. */
    if (lv_port_indev_touch_paused()) {
        return;
    }
    if (s_caps_backoff > 0U) {
        s_caps_backoff--;
        return;
    }
    if (!capsense_read_and_feed()) {
        s_caps_live = false;
        s_caps_backoff = CAPSENSE_FAIL_BACKOFF_TICKS;
    }
#endif
}

void cm55_sensor_poll_tick(void)
{
#if BSP_HAS_POTENTIOMETER
    if (!s_pot_ok && s_pot_probe_left > 0U) {
        /* Grace-period re-probe: covers CM55 polling before the SAR
         * completed its first full scan of channels 4-7. */
        s_pot_probe_left--;
        s_pot_ok = pot_channels_ready();
    }
    if (s_pot_ok) {
        pot_read_and_feed();
    }
#endif
}

uint8_t cm55_sensor_poll_status(void)
{
    uint8_t flags = 0;
    if (s_capsense_ok) flags |= 0x02;
#if BSP_HAS_POTENTIOMETER
    if (s_pot_ok)      flags |= 0x04;
#endif
    return flags;
}

uint8_t cm55_pot_read_all(uint16_t out[QWA309_POT_COUNT])
{
#if BSP_HAS_POTENTIOMETER
    if (!s_pot_ok || out == NULL) return 0;
    memcpy(out, s_pot_raw, sizeof(s_pot_raw));
    return QWA309_POT_COUNT;
#else
    (void)out;
    return 0;
#endif
}

/* Strong override of the weak default in shared ipc_service.c — serves
 * IPC_CMD_CONTROLS_STATE (CM33 game-rate CapSense/pot reads) from this
 * module's cache. Runs in the IPC_Svc task while the GFX task writes the
 * cache; all fields are naturally-aligned byte/u16 single-writer values, so
 * reads cannot tear, and cross-field skew of one 50 ms tick is harmless for
 * game input. */
bool cm55_controls_snapshot(ipc_controls_state_t *out)
{
    if (out == NULL) {
        return false;
    }
    out->caps_valid = (s_capsense_ok && s_caps_live) ? 1 : 0;
    out->btn0       = s_caps_btn0;
    out->btn1       = s_caps_btn1;
    out->slider     = s_caps_slider;
#if BSP_HAS_POTENTIOMETER
    out->pot_valid  = s_pot_ok ? 1 : 0;
    for (uint32_t i = 0; i < QWA309_POT_COUNT; i++) {
        out->pot_raw[i] = s_pot_raw[i];
    }
#else
    out->pot_valid  = 0;
    memset(out->pot_raw, 0, sizeof(out->pot_raw));
#endif
    out->reserved = 0;
    return true;
}

uint16_t cm55_sensor_poll_diag(uint8_t *buf, uint16_t max_len)
{
    if (buf == NULL || max_len < 4) return 0;
    buf[0] = s_init_called ? 1 : 0;
    buf[1] = cm55_sensor_poll_status();
    buf[2] = s_caps_have_baseline ? 1 : 0;
    buf[3] = 0;
    return 4;
}
