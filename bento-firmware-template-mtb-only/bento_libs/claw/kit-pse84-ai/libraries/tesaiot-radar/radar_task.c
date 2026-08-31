/******************************************************************************
 * \file radar_task.c
 *
 * \brief
 *     TESAIoT Radar Task — BGT60TR13C 60 GHz radar presence detection.
 *
 *     Ported from Infineon DEEPCRAFT radar example (bare-metal) to FreeRTOS.
 *     Uses energy-based presence detection instead of gesture classification.
 *
 *     Data flow:
 *       BGT60TR13C (SPI, 200 Hz) -> Ring Buffer -> Energy Calculation
 *       -> Presence flag (read by lcd_task for LCD power management)
 *
 *******************************************************************************
 * SPDX-FileCopyrightText: 2025-2026 TESAIoT Foundation Platform
 *
 * \author Assoc. Prof. Wiroon Sriborrirux and TESA Developer Team
 *
 * Link: https://tesaiot.github.io/developer-hub/
 *
 * Based on: mtb-example-psoc-edge-ml-deepcraft-deploy-radar
 * (c) 2025, Infineon Technologies AG
 ******************************************************************************/

#include "radar_task.h"
#include "radar_settings.h"
#include "radar_dsp.h"
#include "cybsp.h"
#include "cy_scb_spi.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include <math.h>

/* Radar sensor library (xensiv-bgt60trxx) */
#include "xensiv_bgt60trxx_mtb.h"
#include "xensiv_bgt60trxx_regs.h"

/*******************************************************************************
 * Macros
 ******************************************************************************/
#define XENSIV_BGT60TRXX_IRQ_PRIORITY               (5U)
#define SPI_INTR_NUM        ((IRQn_Type) CYBSP_SPI_CONTROLLER_IRQ)
#define SPI_INTR_PRIORITY   (5U)

/*******************************************************************************
 * Global Variables
 ******************************************************************************/

/** Presence detection flag — read by lcd_task */
volatile bool tesaiot_radar_presence_detected = false;

/** Current signal energy — for debugging */
volatile float tesaiot_radar_current_energy = 0.0f;

/** Radar initialization flag */
volatile bool tesaiot_radar_initialized = false;

/*******************************************************************************
 * Private Types
 ******************************************************************************/

/** Radar device context.
 * Keep in regular RAM for stable boot in SensorHub mode.
 * .cy_xip buffers depend on PSRAM/XIP runtime bring-up used by Face runtime. */
typedef struct {
    xensiv_bgt60trxx_mtb_t bgt60_obj;
    uint16_t bgt60_buffer0[8192];       /**< SPI command/TX buffer */
    uint16_t bgt60_buffer1[8192];       /**< SPI RX buffer (FIFO data) */
    float    energy_buffer[128];        /**< Frame samples for energy calc */
} radar_device_t;

static radar_device_t radar_dev;


/*******************************************************************************
 * Private Variables
 ******************************************************************************/

/** Ring buffer for radar samples (regular RAM for deterministic availability at boot). */
static uint16_t radar_ring_buffer[RADAR_RING_BUFFER_SIZE];
static int ring_write_idx = 0;
static int ring_read_idx  = 0;
static int ring_level     = 0;

/** SPI context */
static cy_stc_scb_spi_context_t spi_context;
static cy_stc_sysint_t radar_irq_cfg;

/** Radar config */
static struct radar_config radar_cfg;

/** Presence detection state */
static int presence_count = 0;
static int absence_count  = 0;
/** DRDY event counter from ISR (avoids missing short INT pulses). */
static volatile uint32_t radar_drdy_events = 0;

/*******************************************************************************
 * ISR Handlers
 ******************************************************************************/

/** SPI interrupt handler */
static void radar_spi_isr(void)
{
    Cy_SCB_SPI_Interrupt(CYBSP_SPI_CONTROLLER_HW, &spi_context);
}

/** Radar data-ready interrupt handler */
static void radar_data_ready_isr(void)
{
    if (radar_drdy_events < 0xFFFFFFFFu) {
        radar_drdy_events++;
    }
    Cy_GPIO_ClearInterrupt(CYBSP_RADAR_INT_PORT, CYBSP_RADAR_INT_NUM);
    NVIC_ClearPendingIRQ(radar_irq_cfg.intrSrc);
}

/*******************************************************************************
 * Private: Load radar presets from radar_settings.h
 ******************************************************************************/
static void load_presets(void)
{
    radar_cfg.start_freq           = P0_XENSIV_BGT60TRXX_CONF_START_FREQ_HZ;
    radar_cfg.end_freq             = P0_XENSIV_BGT60TRXX_CONF_END_FREQ_HZ;
    radar_cfg.samples_per_chirp    = P0_XENSIV_BGT60TRXX_CONF_NUM_SAMPLES_PER_CHIRP;
    radar_cfg.chirps_per_frame     = P0_XENSIV_BGT60TRXX_CONF_NUM_CHIRPS_PER_FRAME;
    radar_cfg.rx_antennas          = P0_XENSIV_BGT60TRXX_CONF_NUM_RX_ANTENNAS;
    radar_cfg.tx_antennas          = P0_XENSIV_BGT60TRXX_CONF_NUM_TX_ANTENNAS;
    radar_cfg.sample_rate          = P0_XENSIV_BGT60TRXX_CONF_SAMPLE_RATE;
    radar_cfg.chirp_repetition_time = P0_XENSIV_BGT60TRXX_CONF_CHIRP_REPETITION_TIME_S;
    radar_cfg.frame_repetition_time = P0_XENSIV_BGT60TRXX_CONF_FRAME_REPETITION_TIME_S;
    radar_cfg.frame_rate           = (int)(0.5 + 1.0 / radar_cfg.frame_repetition_time);
    radar_cfg.num_samples_per_frame =
        radar_cfg.samples_per_chirp *
        radar_cfg.chirps_per_frame *
        radar_cfg.rx_antennas;
    radar_cfg.fifo_int_level       = RADAR_DEFAULT_FIFO_SETTING;
    /* Keep FIFO burst bounded to reduce SPI/CPU spikes on CM55. */
    if (radar_cfg.fifo_int_level < radar_cfg.num_samples_per_frame) {
        radar_cfg.fifo_int_level = radar_cfg.num_samples_per_frame;
    } else if (radar_cfg.fifo_int_level > (radar_cfg.num_samples_per_frame * 4U)) {
        radar_cfg.fifo_int_level = radar_cfg.num_samples_per_frame * 4U;
    }
    radar_cfg.number_of_regs       = P0_XENSIV_BGT60TRXX_CONF_NUM_REGS;
    radar_cfg.register_list        = register_list_p0;
}

/*******************************************************************************
 * Private: Radar hardware initialization
 ******************************************************************************/
static cy_rslt_t radar_hw_init(void)
{
    cy_rslt_t result;

    /* Initialize SPI (SCB3) */
    result = Cy_SCB_SPI_Init(CYBSP_SPI_CONTROLLER_HW,
                             &CYBSP_SPI_CONTROLLER_config, &spi_context);
    if (result != CY_RSLT_SUCCESS) {
        return result;
    }

    /* Setup SPI interrupt */
    cy_stc_sysint_t spi_intr_cfg = {
        .intrSrc      = SPI_INTR_NUM,
        .intrPriority = SPI_INTR_PRIORITY,
    };
    Cy_SysInt_Init(&spi_intr_cfg, &radar_spi_isr);
    NVIC_EnableIRQ(SPI_INTR_NUM);

    /* Set active slave select and enable SPI */
    Cy_SCB_SPI_SetActiveSlaveSelect(CYBSP_SPI_CONTROLLER_HW, CY_SCB_SPI_SLAVE_SELECT1);
    Cy_SCB_SPI_Enable(CYBSP_SPI_CONTROLLER_HW);

    /* Load radar register presets */
    load_presets();

    /* Configure radar interface pins */
    radar_dev.bgt60_obj.iface.scb_inst = CYBSP_SPI_CONTROLLER_HW;
    radar_dev.bgt60_obj.iface.spi      = &spi_context;
    radar_dev.bgt60_obj.iface.sel_port = CYBSP_RSPI_CS_PORT;
    radar_dev.bgt60_obj.iface.sel_pin  = CYBSP_RSPI_CS_PIN;
    radar_dev.bgt60_obj.iface.rst_port = CYBSP_RADAR_RESET_PORT;
    radar_dev.bgt60_obj.iface.rst_pin  = CYBSP_RADAR_RESET_PIN;
    radar_dev.bgt60_obj.iface.irq_port = CYBSP_RADAR_INT_PORT;
    radar_dev.bgt60_obj.iface.irq_pin  = CYBSP_RADAR_INT_PIN;
    radar_dev.bgt60_obj.iface.irq_num  = CYBSP_RADAR_INT_IRQ;

    /* Reduce drive strength for EMI */
    Cy_GPIO_SetSlewRate(CYBSP_RSPI_MOSI_PORT, CYBSP_RSPI_MOSI_PIN, CY_GPIO_SLEW_FAST);
    Cy_GPIO_SetDriveSel(CYBSP_RSPI_MOSI_PORT, CYBSP_RSPI_MOSI_PIN, CY_GPIO_DRIVE_1_8);
    Cy_GPIO_SetSlewRate(CYBSP_RSPI_CLK_PORT, CYBSP_RSPI_CLK_PIN, CY_GPIO_SLEW_FAST);
    Cy_GPIO_SetDriveSel(CYBSP_RSPI_CLK_PORT, CYBSP_RSPI_CLK_PIN, CY_GPIO_DRIVE_1_8);

    /* Configure radar interrupt */
    radar_irq_cfg.intrSrc      = (IRQn_Type)radar_dev.bgt60_obj.iface.irq_num;
    radar_irq_cfg.intrPriority = XENSIV_BGT60TRXX_IRQ_PRIORITY;

    /* Initialize BGT60TR13C */
    result = xensiv_bgt60trxx_mtb_init(&radar_dev.bgt60_obj,
                                        radar_cfg.register_list,
                                        radar_cfg.number_of_regs);
    if (result != CY_RSLT_SUCCESS) {
        return result;
    }

    result = xensiv_bgt60trxx_mtb_interrupt_init(&radar_dev.bgt60_obj,
                                                  radar_cfg.num_samples_per_frame);
    if (result != CY_RSLT_SUCCESS) {
        return result;
    }

    /* Setup radar data-ready interrupt */
    Cy_SysInt_Init(&radar_irq_cfg, radar_data_ready_isr);
    NVIC_ClearPendingIRQ(radar_irq_cfg.intrSrc);
    NVIC_EnableIRQ(radar_irq_cfg.intrSrc);
    Cy_GPIO_ClearInterrupt(CYBSP_RADAR_INT_PORT, CYBSP_RADAR_INT_NUM);

    /* Wait for radar to stabilize */
    Cy_SysLib_Delay(500);

    /* Set FIFO limit and start frame capture */
    xensiv_bgt60trxx_set_fifo_limit(&radar_dev.bgt60_obj.dev,
                                     radar_cfg.fifo_int_level);

    xensiv_bgt60trxx_soft_reset(&radar_dev.bgt60_obj.dev,
                                 XENSIV_BGT60TRXX_RESET_FIFO);

    /* Reset ring buffer */
    ring_write_idx = 0;
    ring_read_idx  = 0;
    ring_level     = 0;
    radar_drdy_events = 0;

    /* Start continuous frame capture */
    xensiv_bgt60trxx_start_frame(&radar_dev.bgt60_obj.dev, true);

    return CY_RSLT_SUCCESS;
}

/*******************************************************************************
 * Private: Process radar data using public xensiv_bgt60trxx API
 *
 * Uses xensiv_bgt60trxx_get_fifo_data() which handles SPI burst mode
 * internally (command header, 12-bit word width, CS management).
 ******************************************************************************/

#if defined(BENTO_HAS_EDGE_AI) && (BENTO_HAS_EDGE_AI == 1)
/*******************************************************************************
 * Edge AI chirp snapshot
 *
 * Single writer (this radar task) / single reader (CM55 ai_engine's feed_radar).
 * Publishes the newest raw int16 128-sample chirp under a seqlock so the reader
 * never sees a torn frame — same shape as radar_dsp_snapshot. The whole block is
 * gated on BENTO_HAS_EDGE_AI so Eva/Game (no Edge AI) build byte-identical: the
 * shared lib rebuild those projects incur adds nothing to their image.
 ******************************************************************************/
/* Ring of the last 8 chirps, drained IN ORDER by the reader. A single
 * latest-chirp slot lost every chirp but the last of each processing burst
 * (measured: the model received 88/s of the radar's 200/s), which stretched
 * the inter-chirp time axis the DEEPCRAFT net keys on — Push hypersensitivity,
 * Circle never recognised. The temporal model needs EVERY chirp, consecutively,
 * exactly as the reference enqueues its ring. Single writer (radar task) /
 * single reader (ai_engine); lock-free, bounded, no spin (the old unbounded
 * seqlock retry livelocked the prio-4 reader against this prio-3 writer). */
#define RADAR_AI_RING_DEPTH   (8u)                 /* power of two */
static int16_t           s_ai_ring[RADAR_AI_RING_DEPTH][RADAR_DSP_N];
static volatile uint32_t s_ai_wr = 0U;             /* monotonic chirp count */

static void radar_ai_frame_publish(const int16_t *frame, int n)
{
    int16_t *slot = s_ai_ring[s_ai_wr % RADAR_AI_RING_DEPTH];
    for (int i = 0; (i < n) && (i < (int)RADAR_DSP_N); i++) {
        slot[i] = frame[i];
    }
    __DMB();                                       /* data before index */
    s_ai_wr = s_ai_wr + 1U;
}

bool radar_ai_frame_next(int16_t *out, uint32_t *cursor)
{
    if (out == NULL || cursor == NULL) {
        return false;
    }
    uint32_t wr = s_ai_wr;
    if (*cursor > wr) {                            /* fresh/reset cursor */
        *cursor = wr;
        return false;
    }
    if (*cursor == wr) {
        return false;                              /* nothing new */
    }
    if ((wr - *cursor) >= RADAR_AI_RING_DEPTH) {
        /* Fell behind a whole ring: resync to the oldest still-valid chirp. */
        *cursor = wr - (RADAR_AI_RING_DEPTH - 1u);
    }
    const int16_t *slot = s_ai_ring[*cursor % RADAR_AI_RING_DEPTH];
    for (int i = 0; i < (int)RADAR_DSP_N; i++) {
        out[i] = slot[i];
    }
    __DMB();
    /* Torn iff the writer lapped this slot DURING the copy — detectable from
     * the index distance alone, no retry loop, no spin. */
    if ((s_ai_wr - *cursor) >= RADAR_AI_RING_DEPTH) {
        *cursor = s_ai_wr - (RADAR_AI_RING_DEPTH - 1u);
        return false;
    }
    (*cursor)++;
    return true;
}
#endif /* BENTO_HAS_EDGE_AI */

/* Monotonic count of chirps the producer has actually emitted, build-agnostic
 * (s_ai_wr above only exists when Edge AI is compiled in). The stall watchdog in
 * the main loop watches this: the sensor free-runs at 200 fps, so it MUST keep
 * climbing whenever the capture pipeline is healthy. */
static volatile uint32_t s_frames_total = 0U;

/* Consecutive 5 ms main-loop cycles with no new frame before the watchdog forces
 * a sequencer restart. ~200 cycles ≈ 1 s -- long enough that a brief scheduling
 * gap (a model switch, a long render) never false-triggers, short enough that a
 * genuine wedge self-heals almost immediately. */
#define RADAR_STALL_LIMIT_CYCLES   (200U)

/* Full frame-sequencer restart. The overflow path used to do only
 * soft_reset(RESET_FIFO) + start_frame(true), which clears the FIFO but cannot
 * restart a HALTED sequencer: start_frame(true) on a sequencer the chip still
 * considers "running" (but which has stopped emitting) is a no-op. After a
 * starvation-induced overflow the BGT60 could land exactly there -- FSTAT then
 * reads empty forever with no error bit, s_frames_total freezes, and nothing
 * short of a hard power cycle recovered it. Explicitly STOP first, reset the
 * FIFO, re-arm the limit, clear the SW ring, then start again -- a clean,
 * unconditional restart from a known state. */
/* Every step is checked now. It used to discard all four return codes, which
 * made the one failure mode that matters invisible: if the SPI link itself is
 * down, all four calls fail, the sequencer never restarts, and this loops once
 * a second forever reporting nothing. Observed 2026-08-09 -- Energy on the
 * Radar dashboard runs for one to two minutes and then freezes, repeatably,
 * with no model loaded and nothing in the log.
 *
 * These counters exist to answer one question that no amount of reading the
 * source can settle: is recovery not running, or running and failing? Read them
 * through tesaiot_radar_recover_stats(); the Edge AI page prints them. */
/* Where the task is, and how many times it has been round. The stall watchdog
 * sits at the BOTTOM of the main loop, so it can only fire if the loop reaches
 * it -- and rec 0/0 with the frames frozen (measured 2026-08-09) says it never
 * did. Nothing in this file waits without a bound, but every SPI call here ends
 * in an unbounded spin inside the vendor platform layer
 * (xensiv_bgt60trxx_edge.c:153 and :181) on a flag that only the SCB interrupt
 * clears. If that interrupt is ever lost, the task spins there forever at
 * priority 3 and no watchdog downstream of it can run.
 *
 * loop frozen + phase parked on an SPI phase proves that. loop climbing with
 * frames frozen would disprove it and point back at the sensor. */
#define RADAR_PH_TOP        (1U)   /* top of the for(;;)                        */
#define RADAR_PH_FSTAT      (2U)   /* inside get_reg(FSTAT) -- SPI              */
#define RADAR_PH_FIFO       (3U)   /* inside get_fifo_data  -- SPI              */
#define RADAR_PH_DSP        (4U)   /* CPU-only frame processing                 */
#define RADAR_PH_RECOVER    (5U)   /* inside radar_recover  -- SPI              */
#define RADAR_PH_WATCHDOG   (6U)   /* reached the stall check                   */
static volatile uint32_t s_loop_iters = 0U;
static volatile uint32_t s_phase = 0U;

static volatile uint32_t s_recover_tries = 0U;
static volatile uint32_t s_recover_fails = 0U;
static volatile int32_t  s_recover_last_rc = 0;

static void radar_recover(void)
{
    int32_t rc;
    int32_t first_err = 0;

    s_recover_tries++;
    s_phase = RADAR_PH_RECOVER;

    rc = xensiv_bgt60trxx_start_frame(&radar_dev.bgt60_obj.dev, false);
    if ((rc != XENSIV_BGT60TRXX_STATUS_OK) && (first_err == 0)) { first_err = rc; }

    rc = xensiv_bgt60trxx_soft_reset(&radar_dev.bgt60_obj.dev,
                                     XENSIV_BGT60TRXX_RESET_FIFO);
    if ((rc != XENSIV_BGT60TRXX_STATUS_OK) && (first_err == 0)) { first_err = rc; }

    rc = xensiv_bgt60trxx_set_fifo_limit(&radar_dev.bgt60_obj.dev,
                                         radar_cfg.fifo_int_level);
    if ((rc != XENSIV_BGT60TRXX_STATUS_OK) && (first_err == 0)) { first_err = rc; }

    ring_write_idx = 0;
    ring_read_idx  = 0;
    ring_level     = 0;

    rc = xensiv_bgt60trxx_start_frame(&radar_dev.bgt60_obj.dev, true);
    if ((rc != XENSIV_BGT60TRXX_STATUS_OK) && (first_err == 0)) { first_err = rc; }

    s_recover_last_rc = first_err;
    if (first_err != 0) {
        s_recover_fails++;
    }
}

void tesaiot_radar_loop_stats(uint32_t *loops, uint32_t *frames, uint32_t *phase)
{
    if (loops  != NULL) { *loops  = s_loop_iters; }
    if (frames != NULL) { *frames = s_frames_total; }
    if (phase  != NULL) { *phase  = s_phase; }
}

void tesaiot_radar_recover_stats(uint32_t *tries, uint32_t *fails, int32_t *last_rc)
{
    if (tries   != NULL) { *tries   = s_recover_tries; }
    if (fails   != NULL) { *fails   = s_recover_fails; }
    if (last_rc != NULL) { *last_rc = s_recover_last_rc; }
}

static void radar_process_data(void)
{
    uint32_t fstat = 0U;
    s_phase = RADAR_PH_FSTAT;
    int32_t reg_status = xensiv_bgt60trxx_get_reg(
        &radar_dev.bgt60_obj.dev,
        XENSIV_BGT60TRXX_REG_FSTAT_TR13C,
        &fstat);
    if (reg_status != XENSIV_BGT60TRXX_STATUS_OK) {
        return;
    }

    if ((fstat & (XENSIV_BGT60TRXX_REG_FSTAT_FOF_ERR_MSK |
                  XENSIV_BGT60TRXX_REG_FSTAT_FUF_ERR_MSK |
                  XENSIV_BGT60TRXX_REG_FSTAT_SPI_BURST_ERR_MSK |
                  XENSIV_BGT60TRXX_REG_FSTAT_CLK_NUM_ERR_MSK)) != 0U) {
        /* Overflow/underflow/burst error: clean sequencer restart, not a bare
         * FIFO reset -- the latter left the sequencer halted often enough that a
         * switch into Radar mode wedged the feed until a power cycle. */
        radar_recover();
        return;
    }

    if ((fstat & XENSIV_BGT60TRXX_REG_FSTAT_EMPTY_MSK) != 0U) {
        return;
    }

    uint32_t avail_words = (fstat & XENSIV_BGT60TRXX_REG_FSTAT_FILL_STATUS_MSK);
    uint32_t avail_samples = avail_words * 2U;
    if (avail_samples < radar_cfg.num_samples_per_frame) {
        return;
    }

    /* Drain EVERY complete frame waiting in the FIFO, capped at 4 per cycle.
     * The sensor free-runs at 200 fps (5.00407 ms hardware frame timer), so a
     * single-frame read per ~5-6 ms task cycle cannot keep up and the FIFO
     * backs up toward overflow; draining the backlog holds the pipeline at
     * the sensor's own rate. */
    uint32_t num_samples = radar_cfg.num_samples_per_frame;
    for (int rd = 0; (rd < 4) && (avail_samples >= num_samples);
         rd++, avail_samples -= num_samples) {
        s_phase = RADAR_PH_FIFO;
        int32_t status = xensiv_bgt60trxx_get_fifo_data(
            &radar_dev.bgt60_obj.dev,
            radar_dev.bgt60_buffer1,
            num_samples);

        if (status != XENSIV_BGT60TRXX_STATUS_OK) {
            break;  /* SPI read failed — retry next cycle */
        }

        /* Copy FIFO data to ring buffer */
        uint32_t *ring32 = (uint32_t *)radar_ring_buffer;
        int fifo_size_32 = (int)num_samples >> 1;

        for (int x = 0; x < fifo_size_32; x++) {
            ring32[ring_write_idx] = ((uint32_t *)(radar_dev.bgt60_buffer1))[x];
            if (ring_level <= (RADAR_RING_BUFFER_SIZE - 2)) {
                ring_level += 2;
            } else {
                /* Overflow protection: keep newest samples, drop oldest two. */
                ring_level = RADAR_RING_BUFFER_SIZE;
                ring_read_idx = (ring_read_idx + 2) & RADAR_RING_BUFFER_MASK;
            }
            ring_write_idx++;
            ring_write_idx &= RADAR_RING_BUFFER_MASK32;
        }
    }

    /* Extract frames and calculate energy */
    int frames_processed = 0;
    while ((ring_level >= (int)radar_cfg.num_samples_per_frame) &&
           (frames_processed < RADAR_MAX_FRAMES_PER_CYCLE)) {
        /* One frame = one 128-sample chirp (P0: 1 chirp x 1 antenna), so the
         * frame is also the DSP input — captured raw for the range profile. */
        static int16_t dsp_frame[RADAR_DSP_N];
        float energy = 0.0f;

        for (int i = 0; i < (int)radar_cfg.num_samples_per_frame; i++) {
            int16_t raw = ((int16_t *)radar_ring_buffer)[ring_read_idx];
            float sample = (float)raw;
            if (i < (int)RADAR_DSP_N) {
                dsp_frame[i] = raw;
            }
            energy += sample * sample;
            ring_level--;
            ring_read_idx++;
            ring_read_idx &= RADAR_RING_BUFFER_MASK;
        }

        /* Normalize energy */
        energy /= (float)radar_cfg.num_samples_per_frame;

        /* Range-profile DSP (HPF -> FFT -> dB -> anti-coupling -> peak).
         * ~100us at 128 pts with FPU — negligible beside the SPI reads. */
        radar_dsp_process(dsp_frame, RADAR_DSP_N);

#if defined(BENTO_HAS_EDGE_AI) && (BENTO_HAS_EDGE_AI == 1)
        /* Publish this raw chirp for the Edge AI radar model (feed_radar reads
         * the newest one). The model wants the raw int16 ADC value — no scaling
         * here; feed_radar casts to float x1.0 per Infineon's reference. */
        radar_ai_frame_publish(dsp_frame, RADAR_DSP_N);
#endif
        s_frames_total++;   /* liveness tick for the stall watchdog (all builds) */

        /* NO per-frame re-trigger. The P0 register list (training-rig config)
         * free-runs the sensor at FRAME_REPETITION_TIME = 5.00407 ms (200 fps)
         * from the single start_frame() in init. Re-writing FRAME_START here
         * restarted the frame sequencer every processed frame, so the real
         * capture rate collapsed to the task loop's pace (~88 fps measured) —
         * a 2.3x time-stretch that broke the DEEPCRAFT radar model's
         * inter-chirp Doppler pattern (Push hypersensitive, Circle never
         * seen). The hardware timer owns the cadence; we only drain. */

        /* --- Motion-based presence detection ---
         * Absolute energy is dominated by static clutter (~2M) and doesn't
         * distinguish presence. Instead, detect micro-motion (breathing,
         * small movements) via smoothed frame-to-frame energy delta. */
        static float prev_energy = 0.0f;
        static float smoothed_delta = 0.0f;

        float delta = energy - prev_energy;
        if (delta < 0.0f) delta = -delta;  /* fabsf */
        smoothed_delta = 0.1f * delta + 0.9f * smoothed_delta;
        prev_energy = energy;

        /* Export absolute frame energy so dashboard value visibly changes. */
        tesaiot_radar_current_energy = energy;

        /* --- Presence detection with debounce --- */
        if (smoothed_delta > RADAR_PRESENCE_THRESHOLD) {
            presence_count++;
            absence_count = 0;
            if (presence_count >= RADAR_PRESENCE_ON_FRAMES) {
                if (!tesaiot_radar_presence_detected) {
                    /* printf("[RADAR] Presence: DETECTED (delta=%.0f)\r\n", smoothed_delta); */
                }
                tesaiot_radar_presence_detected = true;
                presence_count = RADAR_PRESENCE_ON_FRAMES; /* cap */
            }
        } else {
            absence_count++;
            presence_count = 0;
            if (absence_count >= RADAR_ABSENCE_OFF_FRAMES) {
                if (tesaiot_radar_presence_detected) {
                    /* printf("[RADAR] Presence: ABSENT (delta=%.0f)\r\n", smoothed_delta); */
                }
                tesaiot_radar_presence_detected = false;
                absence_count = RADAR_ABSENCE_OFF_FRAMES; /* cap */
            }
        }

        frames_processed++;
    }
}

/*******************************************************************************
 * Public: Radar FreeRTOS Task
 ******************************************************************************/
//! [cm55_display_ready_radar_wait]
void tesaiot_radar_task(void *arg)
{
    (void)arg;
    cy_rslt_t result;

    /* Wait until display/IPC core is up to avoid startup bus contention with
     * GFX/IPC bring-up on CM55. */
    extern volatile uint8_t tesaiot_display_ready;
    uint32_t wait_ms = 0U;
    while ((tesaiot_display_ready == 0U) && (wait_ms < 10000U)) {
        vTaskDelay(pdMS_TO_TICKS(50));
        wait_ms += 50U;
    }
    //! [cm55_display_ready_radar_wait]

    /* Keep initialization lightweight: only clear required state.
     * Large SOCMEM buffers are filled by producer paths before use. */
    memset(&radar_dev.bgt60_obj, 0, sizeof(radar_dev.bgt60_obj));
    ring_write_idx = 0;
    ring_read_idx  = 0;
    ring_level     = 0;
    radar_drdy_events = 0;
    tesaiot_radar_presence_detected = false;
    tesaiot_radar_current_energy = 0.0f;
    tesaiot_radar_initialized = false;

    /* Initialize radar hardware */
    result = radar_hw_init();
    if (result != CY_RSLT_SUCCESS) {
        /* Keep system alive even if radar init fails (Home/UI must boot). */
        tesaiot_radar_initialized = false;
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    /* Range-profile DSP tables + resolution from the P0 chirp bandwidth. */
    radar_dsp_init((double)(P0_XENSIV_BGT60TRXX_CONF_END_FREQ_HZ -
                            P0_XENSIV_BGT60TRXX_CONF_START_FREQ_HZ));

    tesaiot_radar_initialized = true;

    /* Main processing loop */
    uint32_t last_frames = s_frames_total;
    uint32_t stall_cycles = 0U;
    for (;;) {
        s_loop_iters++;
        s_phase = RADAR_PH_TOP;
        radar_process_data();
        s_phase = RADAR_PH_WATCHDOG;

        /* Stall watchdog. The sensor free-runs at 200 fps, so s_frames_total must
         * keep climbing whenever the pipeline is healthy. If it stops for
         * RADAR_STALL_LIMIT_CYCLES (~1 s) the frame sequencer has halted -- the
         * starvation-into-overflow case that lands with FSTAT reading empty and no
         * error bit, which the per-read overflow check never sees. Restart the
         * sequencer here so a switch into Radar mode self-heals in ~1 s instead of
         * needing a hard power cycle. */
        uint32_t frames_now = s_frames_total;
        if (frames_now != last_frames) {
            last_frames  = frames_now;
            stall_cycles = 0U;
        } else if (++stall_cycles >= RADAR_STALL_LIMIT_CYCLES) {
            radar_recover();
            last_frames  = s_frames_total;
            stall_cycles = 0U;
        }

        /* Match the sensor frame cadence to avoid needlessly preempting UI work. */
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
