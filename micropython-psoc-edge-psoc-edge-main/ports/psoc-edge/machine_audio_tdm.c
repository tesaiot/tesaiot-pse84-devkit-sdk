/*
 * machine_audio_tdm.c — MicroPython AudioTDM (I2S TX) HAL for PSoC Edge E84
 *
 * Implements machine.AudioTDM class for audio playback via TDM0 TX.
 * Architecture: App write() → ring buffer → ISR → TX FIFO → I2S codec/DAC
 *
 * Based on machine_pdm_pcm.c pattern (RX) but mirrored for TX output.
 * Reference: Infineon voice example app_i2s.c (Apache-2.0).
 *
 * Copyright (c) 2026 BDH / TESAIoT — Wiroon Sriborrirux
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <math.h>

#include "py/runtime.h"
#include "py/stream.h"
#include "py/mphal.h"
#include "py/mperrno.h"
#include "py/ringbuf.h"

#include "modmachine.h"
#include "machine_audio_tdm.h"

#if MICROPY_PY_MACHINE_AUDIO_TDM

/* Audio codec support (Eva Kit has WM8960 on sensor I2C bus) */
#if BSP_HAS_AUDIO_CODEC
#include "wm8960.h"
#endif

/* PSoC Edge PDL headers */
#include "cy_pdl.h"
#include "cy_tdm.h"
#include "cy_sysint.h"
#include "cy_sysclk.h"
#include "cybsp.h"

/* ══════════════════════════════════════════════════════════════════════
 * Forward declarations
 * ══════════════════════════════════════════════════════════════════════ */

static void audio_tdm_tx_irq_handler(void);
static void audio_tdm_clock_configure(uint32_t sample_rate);
static void audio_tdm_hw_init(machine_audio_tdm_obj_t *self);
static void audio_tdm_hw_deinit(machine_audio_tdm_obj_t *self);
static void audio_tdm_start(machine_audio_tdm_obj_t *self);
static void audio_tdm_stop(machine_audio_tdm_obj_t *self);

/* ══════════════════════════════════════════════════════════════════════
 * Global state
 * ══════════════════════════════════════════════════════════════════════ */

/* ISR interrupt configuration */
static const cy_stc_sysint_t audio_tdm_isr_cfg = {
    .intrSrc = (IRQn_Type)tdm_0_interrupts_tx_0_IRQn,
    .intrPriority = AUDIO_TDM_ISR_PRIORITY,
};

/* Debug: ISR call counter (volatile for ISR safety) */
static volatile uint32_t audio_tdm_isr_count = 0;

/* ══════════════════════════════════════════════════════════════════════
 * Volume helpers
 * ══════════════════════════════════════════════════════════════════════ */

/*
 * Convert volume percentage (0-100) to a fixed-point scale factor (0-256).
 * 0%   → scale=0   (silence)
 * 100% → scale=256  (unity gain, no attenuation)
 *
 * Uses a simple linear mapping. For perceptual (logarithmic) volume,
 * a lookup table could be added later.
 */
static uint16_t volume_pct_to_scale(uint8_t pct) {
    if (pct >= 100) {
        return 256;
    }
    if (pct == 0) {
        return 0;
    }
    return (uint16_t)((pct * 256 + 50) / 100);
}

/*
 * Apply volume scaling to a 16-bit signed PCM sample.
 * Uses fixed-point multiply: (sample * scale) >> 8
 */
static inline int16_t apply_volume(int16_t sample, uint16_t scale) {
    if (scale == 256) {
        return sample;  /* Unity — no scaling needed */
    }
    return (int16_t)(((int32_t)sample * scale) >> 8);
}

/* ══════════════════════════════════════════════════════════════════════
 * Clock configuration
 * ══════════════════════════════════════════════════════════════════════ */

/*
 * Configure the peripheral clock divider for the requested sample rate.
 *
 * Clock tree:
 *   PLL → CLKHF → Peripheral Divider → TDM Input Clock
 *   TDM Input / clkDiv(4) → SCK (bit clock)
 *   SCK / (channels × bits) → sample rate
 *
 * Known-good dividers from Infineon voice example:
 *   48 kHz: divider = 8
 *   16 kHz: divider = 24
 */
static void audio_tdm_clock_configure(uint32_t sample_rate) {
    uint16_t clk_div;

    switch (sample_rate) {
        case AUDIO_TDM_RATE_48000:
            clk_div = AUDIO_TDM_CLK_DIV_48KHZ;
            break;
        case AUDIO_TDM_RATE_16000:
            clk_div = AUDIO_TDM_CLK_DIV_16KHZ;
            break;
        default:
            /* Fallback: calculate from 48 kHz base */
            clk_div = (AUDIO_TDM_CLK_DIV_48KHZ * AUDIO_TDM_RATE_48000) / sample_rate;
            if (clk_div < 2) {
                clk_div = 2;
            }
            break;
    }

    /* Disable divider before reprogramming */
    Cy_SysClk_PeriPclkDisableDivider(
        (en_clk_dst_t)CYBSP_TDM_CONTROLLER_0_CLK_DIV_GRP_NUM,
        CY_SYSCLK_DIV_16_5_BIT,
        CYBSP_TDM_CONTROLLER_0_CLK_DIV_NUM);

    /* Set fractional divider (integer part = clk_div - 1, frac = 0) */
    Cy_SysClk_PeriPclkSetFracDivider(
        (en_clk_dst_t)CYBSP_TDM_CONTROLLER_0_CLK_DIV_GRP_NUM,
        CY_SYSCLK_DIV_16_5_BIT,
        CYBSP_TDM_CONTROLLER_0_CLK_DIV_NUM,
        clk_div - 1, 0U);

    /* Re-enable divider */
    Cy_SysClk_PeriPclkEnableDivider(
        (en_clk_dst_t)CYBSP_TDM_CONTROLLER_0_CLK_DIV_GRP_NUM,
        CY_SYSCLK_DIV_16_5_BIT,
        CYBSP_TDM_CONTROLLER_0_CLK_DIV_NUM);

    /* Allow clock to stabilize */
    mp_hal_delay_ms(AUDIO_TDM_CLK_CHANGE_WAIT_MS);
}

/* ══════════════════════════════════════════════════════════════════════
 * Hardware init / deinit
 * ══════════════════════════════════════════════════════════════════════ */

static void audio_tdm_hw_init(machine_audio_tdm_obj_t *self) {
    cy_en_tdm_status_t status;

    /* 1. Configure clock for requested sample rate */
    audio_tdm_clock_configure(self->sample_rate);

    /* 2. Update BSP TX config if needed (word size, channels) */
    /* The BSP provides CYBSP_TDM_CONTROLLER_0_config with default values.
     * We modify the TX config struct directly since it's not const. */
    CYBSP_TDM_CONTROLLER_0_tx_config.wordSize = CY_TDM_SIZE_16;
    CYBSP_TDM_CONTROLLER_0_tx_config.channelSize = 16;
    CYBSP_TDM_CONTROLLER_0_tx_config.channelNum = self->channels;
    CYBSP_TDM_CONTROLLER_0_tx_config.fifoTriggerLevel = AUDIO_TDM_FIFO_TRIGGER_LEVEL;
    CYBSP_TDM_CONTROLLER_0_tx_config.i2sMode = true;
    CYBSP_TDM_CONTROLLER_0_tx_config.masterMode = CY_TDM_DEVICE_MASTER;
    CYBSP_TDM_CONTROLLER_0_tx_config.enable = true;

    /* Channel enable mask: bit 0 = ch0 (L), bit 1 = ch1 (R) */
    if (self->channels == 2) {
        CYBSP_TDM_CONTROLLER_0_tx_config.chEn = 0x3; /* L + R */
    } else {
        CYBSP_TDM_CONTROLLER_0_tx_config.chEn = 0x1; /* L only */
    }

    /* 3. Initialize TDM block */
    status = Cy_AudioTDM_Init(CYBSP_TDM_CONTROLLER_0_HW,
                              &CYBSP_TDM_CONTROLLER_0_config);
    if (status != CY_TDM_SUCCESS) {
        mp_raise_msg_varg(&mp_type_OSError,
            MP_ERROR_TEXT("AudioTDM init failed (0x%04x)"), (unsigned)status);
    }
    mp_printf(&mp_plat_print, "AudioTDM: init OK, HW=%p, TX=%p\n",
              (void *)CYBSP_TDM_CONTROLLER_0_HW,
              (void *)CYBSP_TDM_CONTROLLER_0_TX_HW);

    /* 4. Register TX interrupt */
    cy_en_sysint_status_t isr_status =
        Cy_SysInt_Init(&audio_tdm_isr_cfg, audio_tdm_tx_irq_handler);
    mp_printf(&mp_plat_print, "AudioTDM: ISR init=%lu, IRQn=%d, prio=%u\n",
              (unsigned long)isr_status,
              (int)audio_tdm_isr_cfg.intrSrc,
              (unsigned)audio_tdm_isr_cfg.intrPriority);
    if (isr_status != CY_SYSINT_SUCCESS) {
        mp_raise_msg_varg(&mp_type_OSError,
            MP_ERROR_TEXT("AudioTDM ISR init failed (0x%04lx)"),
            (unsigned long)isr_status);
    }

    /* 5. Initialize audio codec if present (Eva Kit: WM8960 on sensor I2C) */
    #if BSP_HAS_AUDIO_CODEC
    {
        bool codec_ok = wm8960_init_playback();
        mp_printf(&mp_plat_print, "AudioTDM: WM8960 codec init %s\n",
                  codec_ok ? "OK" : "FAILED (continuing without codec)");
    }
    #else
    mp_printf(&mp_plat_print, "AudioTDM: no audio codec (direct I2S output)\n");
    #endif

    self->state = AUDIO_TDM_STATE_INIT;
}

static void audio_tdm_hw_deinit(machine_audio_tdm_obj_t *self) {
    if (self->state == AUDIO_TDM_STATE_ACTIVE ||
        self->state == AUDIO_TDM_STATE_PAUSED) {
        audio_tdm_stop(self);
    }

    /* Disable TDM TX */
    Cy_AudioTDM_DisableTx(CYBSP_TDM_CONTROLLER_0_TX_HW);

    /* De-init TDM block */
    Cy_AudioTDM_DeInit(CYBSP_TDM_CONTROLLER_0_HW);

    /* Power down codec */
    #if BSP_HAS_AUDIO_CODEC
    wm8960_power_down();
    #endif

    self->state = AUDIO_TDM_STATE_RESET;
}

/* ══════════════════════════════════════════════════════════════════════
 * Start / Stop TX
 * ══════════════════════════════════════════════════════════════════════ */

static void audio_tdm_start(machine_audio_tdm_obj_t *self) {
    if (self->state == AUDIO_TDM_STATE_ACTIVE) {
        return;  /* Already running */
    }

    TDM_TX_STRUCT_Type *tx_hw = CYBSP_TDM_CONTROLLER_0_TX_HW;

    /*
     * Sequence matches Infineon mtb-example-psoc-edge voice app_i2s.c:
     *   app_i2s_enable():  EnableTx → ClearIntr → SetMask → Fill FIFO
     *   delay 1 tick
     *   app_i2s_activate(): NVIC_EnableIRQ → ActivateTx
     */

    /* 1. Enable TX interface (generates I2S clocks) */
    Cy_AudioTDM_EnableTx(tx_hw);

    /* 2. Clear pending + enable ALL TX interrupts (per Infineon example) */
    Cy_AudioTDM_ClearTxInterrupt(tx_hw, CY_TDM_INTR_TX_MASK);
    Cy_AudioTDM_SetTxInterruptMask(tx_hw, CY_TDM_INTR_TX_MASK);

    /* 3. Pre-fill FIFO with silence (128 words = full FIFO) */
    for (uint32_t i = 0; i < AUDIO_TDM_HW_FIFO_DEPTH; i++) {
        Cy_AudioTDM_WriteTxData(tx_hw, 0UL);
    }

    /* 4. Small delay for TX interface to stabilize */
    mp_hal_delay_ms(1);

    /* 5. Set state ACTIVE before enabling NVIC so ISR doesn't
     *    take bail-out path on level-triggered empty-FIFO interrupt */
    self->state = AUDIO_TDM_STATE_ACTIVE;

    /* 6. Enable NVIC, then activate TX FIFO (per Infineon example:
     *    NVIC first, then ActivateTx) */
    NVIC_ClearPendingIRQ(audio_tdm_isr_cfg.intrSrc);
    NVIC_EnableIRQ(audio_tdm_isr_cfg.intrSrc);
    Cy_AudioTDM_ActivateTx(tx_hw);

    mp_printf(&mp_plat_print, "AudioTDM: TX started, IRQ %d\n",
              (int)audio_tdm_isr_cfg.intrSrc);
}

static void audio_tdm_stop(machine_audio_tdm_obj_t *self) {
    /* Disable NVIC first (no more ISR calls) */
    NVIC_DisableIRQ(audio_tdm_isr_cfg.intrSrc);

    /* Deactivate TX */
    Cy_AudioTDM_DeActivateTx(CYBSP_TDM_CONTROLLER_0_TX_HW);

    /* Clear interrupt mask */
    Cy_AudioTDM_ClearTxTriggerInterruptMask(CYBSP_TDM_CONTROLLER_0_TX_HW);

    /* Disable TX interface */
    Cy_AudioTDM_DisableTx(CYBSP_TDM_CONTROLLER_0_TX_HW);

    self->state = AUDIO_TDM_STATE_INIT;
}

/* ══════════════════════════════════════════════════════════════════════
 * TX Interrupt Handler
 *
 * Called when TX FIFO drops below trigger level.
 * Reads PCM data from ring buffer, applies volume, writes to FIFO.
 * If ring buffer is empty, writes zeros (silence).
 * ══════════════════════════════════════════════════════════════════════ */

static void audio_tdm_tx_irq_handler(void) {
    audio_tdm_isr_count++;

    machine_audio_tdm_obj_t *self =
        MP_STATE_PORT(machine_audio_tdm_obj[0]);

    if (self == NULL || self->state != AUDIO_TDM_STATE_ACTIVE) {
        /* Disable interrupt mask to prevent level-triggered re-entry,
         * then clear the pending status. */
        Cy_AudioTDM_SetTxInterruptMask(CYBSP_TDM_CONTROLLER_0_TX_HW, 0);
        Cy_AudioTDM_ClearTxInterrupt(CYBSP_TDM_CONTROLLER_0_TX_HW,
                                     CY_TDM_INTR_TX_MASK);
        return;
    }

    uint32_t intr_status =
        Cy_AudioTDM_GetTxInterruptStatusMasked(CYBSP_TDM_CONTROLLER_0_TX_HW);

    if (intr_status & CY_TDM_INTR_TX_FIFO_TRIGGER) {
        uint16_t vol_scale = self->volume_scale;
        uint32_t words_written = 0;

        /*
         * Refill FIFO from ring buffer.
         * Each FIFO word = one 16-bit sample (left or right channel).
         * Ring buffer stores raw PCM bytes (little-endian int16).
         */
        for (uint32_t i = 0; i < AUDIO_TDM_FIFO_REFILL_COUNT; i++) {
            int lo = ringbuf_get(&self->ring_buffer);
            int hi = ringbuf_get(&self->ring_buffer);

            if (lo == -1 || hi == -1) {
                /* Ring buffer exhausted — fill remainder with silence */
                for (uint32_t j = i; j < AUDIO_TDM_FIFO_REFILL_COUNT; j++) {
                    Cy_AudioTDM_WriteTxData(CYBSP_TDM_CONTROLLER_0_TX_HW, 0UL);
                }
                self->underflow_count++;
                break;
            }

            /* Reconstruct 16-bit signed sample (little-endian) */
            int16_t sample = (int16_t)((uint16_t)lo | ((uint16_t)hi << 8));

            /* Apply volume */
            sample = apply_volume(sample, vol_scale);

            /* Write to TX FIFO as 32-bit word (upper bits ignored for 16-bit mode) */
            Cy_AudioTDM_WriteTxData(CYBSP_TDM_CONTROLLER_0_TX_HW,
                                    (uint32_t)(uint16_t)sample);
            words_written++;
        }

        self->total_samples_written += words_written;
    }

    if (intr_status & CY_TDM_INTR_TX_FIFO_UNDERFLOW) {
        self->underflow_count++;
    }

    /* Clear all processed interrupts */
    Cy_AudioTDM_ClearTxInterrupt(CYBSP_TDM_CONTROLLER_0_TX_HW,
                                 CY_TDM_INTR_TX_MASK);
}

/* ══════════════════════════════════════════════════════════════════════
 * MicroPython Object Allocation
 * ══════════════════════════════════════════════════════════════════════ */

static machine_audio_tdm_obj_t *audio_tdm_make_new_instance(void) {
    /* Only one TDM TX instance supported */
    if (MP_STATE_PORT(machine_audio_tdm_obj[0]) != NULL) {
        /* Return existing instance (reinit pattern) */
        return MP_STATE_PORT(machine_audio_tdm_obj[0]);
    }

    machine_audio_tdm_obj_t *self =
        mp_obj_malloc(machine_audio_tdm_obj_t, &machine_audio_tdm_type);

    MP_STATE_PORT(machine_audio_tdm_obj[0]) = self;
    self->tdm_id = 0;
    self->state = AUDIO_TDM_STATE_RESET;
    self->underflow_count = 0;
    self->total_samples_written = 0;
    self->callback_for_non_blocking = mp_const_none;

    return self;
}

/* ══════════════════════════════════════════════════════════════════════
 * Constructor: AudioTDM(rate=48000, bits=16, channels=2, ibuf=16384)
 * ══════════════════════════════════════════════════════════════════════ */

enum {
    ARG_rate,
    ARG_bits,
    ARG_channels,
    ARG_ibuf,
    ARG_volume,
};

static const mp_arg_t audio_tdm_allowed_args[] = {
    { MP_QSTR_rate,     MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 48000} },
    { MP_QSTR_bits,     MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 16} },
    { MP_QSTR_channels, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 2} },
    { MP_QSTR_ibuf,     MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
    { MP_QSTR_volume,   MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 100} },
};

static void audio_tdm_init_helper(machine_audio_tdm_obj_t *self,
                                  size_t n_args, const mp_obj_t *pos_args,
                                  mp_map_t *kw_args) {
    mp_arg_val_t args[MP_ARRAY_SIZE(audio_tdm_allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args,
                     MP_ARRAY_SIZE(audio_tdm_allowed_args),
                     audio_tdm_allowed_args, args);

    /* Validate sample rate */
    uint32_t rate = (uint32_t)args[ARG_rate].u_int;
    if (rate != 16000 && rate != 22050 && rate != 44100 && rate != 48000) {
        mp_raise_ValueError(
            MP_ERROR_TEXT("rate must be 16000, 22050, 44100, or 48000"));
    }

    /* Validate bits (only 16-bit in Phase 1) */
    uint8_t bits = (uint8_t)args[ARG_bits].u_int;
    if (bits != 16) {
        mp_raise_ValueError(MP_ERROR_TEXT("bits must be 16"));
    }

    /* Validate channels */
    uint8_t channels = (uint8_t)args[ARG_channels].u_int;
    if (channels != 1 && channels != 2) {
        mp_raise_ValueError(MP_ERROR_TEXT("channels must be 1 or 2"));
    }

    /* Volume */
    uint8_t vol = (uint8_t)args[ARG_volume].u_int;
    if (vol > 100) {
        vol = 100;
    }

    /* Deinit if already initialized (reinit pattern) */
    if (self->state != AUDIO_TDM_STATE_RESET) {
        audio_tdm_hw_deinit(self);
    }

    /* Store configuration */
    self->sample_rate = rate;
    self->bits = bits;
    self->channels = channels;
    self->volume_pct = vol;
    self->volume_scale = volume_pct_to_scale(vol);
    self->io_mode = AUDIO_TDM_IO_BLOCKING;
    self->underflow_count = 0;
    self->total_samples_written = 0;

    /* Initialize ring buffer using embedded storage (static, no alloc needed) */
    self->ring_buffer.buf = self->ring_buffer_storage;
    self->ring_buffer.size = AUDIO_TDM_RING_BUF_SIZE;
    self->ring_buffer.iget = 0;
    self->ring_buffer.iput = 0;

    /* Initialize hardware */
    audio_tdm_hw_init(self);
}

static mp_obj_t machine_audio_tdm_make_new(const mp_obj_type_t *type,
                                           size_t n_pos_args,
                                           size_t n_kw_args,
                                           const mp_obj_t *args) {
    mp_arg_check_num(n_pos_args, n_kw_args, 0, MP_OBJ_FUN_ARGS_MAX, true);

    machine_audio_tdm_obj_t *self = audio_tdm_make_new_instance();

    mp_map_t kw_args;
    mp_map_init_fixed_table(&kw_args, n_kw_args, args + n_pos_args);
    audio_tdm_init_helper(self, n_pos_args, args, &kw_args);

    return MP_OBJ_FROM_PTR(self);
}

/* ══════════════════════════════════════════════════════════════════════
 * Method: init(**kwargs) — reinitialize with new settings
 * ══════════════════════════════════════════════════════════════════════ */

static mp_obj_t machine_audio_tdm_init(size_t n_args, const mp_obj_t *args,
                                       mp_map_t *kw_args) {
    machine_audio_tdm_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    audio_tdm_init_helper(self, n_args - 1, args + 1, kw_args);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(machine_audio_tdm_init_obj, 1,
                                  machine_audio_tdm_init);

/* ══════════════════════════════════════════════════════════════════════
 * Method: deinit() — release hardware
 * ══════════════════════════════════════════════════════════════════════ */

static mp_obj_t machine_audio_tdm_deinit(mp_obj_t self_in) {
    machine_audio_tdm_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if (self->state != AUDIO_TDM_STATE_RESET) {
        audio_tdm_hw_deinit(self);
    }

    /* Release from global tracking */
    MP_STATE_PORT(machine_audio_tdm_obj[self->tdm_id]) = NULL;

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_audio_tdm_deinit_obj,
                                 machine_audio_tdm_deinit);

/* ══════════════════════════════════════════════════════════════════════
 * Method: volume([pct]) — get or set volume (0-100)
 * ══════════════════════════════════════════════════════════════════════ */

static mp_obj_t machine_audio_tdm_volume(size_t n_args,
                                         const mp_obj_t *args) {
    machine_audio_tdm_obj_t *self = MP_OBJ_TO_PTR(args[0]);

    if (n_args == 1) {
        /* Getter */
        return MP_OBJ_NEW_SMALL_INT(self->volume_pct);
    }

    /* Setter */
    int pct = mp_obj_get_int(args[1]);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    self->volume_pct = (uint8_t)pct;
    self->volume_scale = volume_pct_to_scale((uint8_t)pct);

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_audio_tdm_volume_obj,
                                           1, 2,
                                           machine_audio_tdm_volume);

/* ══════════════════════════════════════════════════════════════════════
 * Method: pause() / resume()
 * ══════════════════════════════════════════════════════════════════════ */

static mp_obj_t machine_audio_tdm_pause(mp_obj_t self_in) {
    machine_audio_tdm_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->state == AUDIO_TDM_STATE_ACTIVE) {
        Cy_AudioTDM_MuteTxFifo(CYBSP_TDM_CONTROLLER_0_TX_HW);
        self->state = AUDIO_TDM_STATE_PAUSED;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_audio_tdm_pause_obj,
                                 machine_audio_tdm_pause);

static mp_obj_t machine_audio_tdm_resume(mp_obj_t self_in) {
    machine_audio_tdm_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->state == AUDIO_TDM_STATE_PAUSED) {
        Cy_AudioTDM_UnfreezeTxFifo(CYBSP_TDM_CONTROLLER_0_TX_HW);
        self->state = AUDIO_TDM_STATE_ACTIVE;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_audio_tdm_resume_obj,
                                 machine_audio_tdm_resume);

/* ══════════════════════════════════════════════════════════════════════
 * Method: stat() — return status dict
 * ══════════════════════════════════════════════════════════════════════ */

static mp_obj_t machine_audio_tdm_stat(mp_obj_t self_in) {
    machine_audio_tdm_obj_t *self = MP_OBJ_TO_PTR(self_in);

    mp_obj_dict_t *dict = mp_obj_new_dict(5);
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_rate),
                      MP_OBJ_NEW_SMALL_INT(self->sample_rate));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_bits),
                      MP_OBJ_NEW_SMALL_INT(self->bits));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_channels),
                      MP_OBJ_NEW_SMALL_INT(self->channels));

    const char *state_str;
    switch (self->state) {
        case AUDIO_TDM_STATE_ACTIVE: state_str = "active"; break;
        case AUDIO_TDM_STATE_PAUSED: state_str = "paused"; break;
        case AUDIO_TDM_STATE_INIT:   state_str = "init";   break;
        default:                     state_str = "reset";   break;
    }
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_state),
                      mp_obj_new_str(state_str, strlen(state_str)));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_underflows),
                      MP_OBJ_NEW_SMALL_INT(self->underflow_count));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_isr_count),
                      MP_OBJ_NEW_SMALL_INT(audio_tdm_isr_count));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_ringbuf_avail),
                      MP_OBJ_NEW_SMALL_INT(ringbuf_avail(&self->ring_buffer)));

    return MP_OBJ_FROM_PTR(dict);
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_audio_tdm_stat_obj,
                                 machine_audio_tdm_stat);

/* ══════════════════════════════════════════════════════════════════════
 * Stream Protocol: write (app → ring buffer → ISR → FIFO)
 * ══════════════════════════════════════════════════════════════════════ */

static mp_uint_t machine_audio_tdm_stream_write(mp_obj_t self_in,
                                                const void *buf_in,
                                                mp_uint_t size,
                                                int *errcode) {
    machine_audio_tdm_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if (self->state == AUDIO_TDM_STATE_RESET) {
        *errcode = MP_EPERM;
        return MP_STREAM_ERROR;
    }

    /* Validate: size must be a multiple of sample frame size */
    uint8_t bytes_per_sample = (self->bits / 8) * self->channels;
    if (size % bytes_per_sample != 0) {
        *errcode = MP_EINVAL;
        return MP_STREAM_ERROR;
    }

    /* Auto-start TX on first write */
    if (self->state == AUDIO_TDM_STATE_INIT) {
        audio_tdm_start(self);
    }

    /*
     * For mono input with stereo output: duplicate each sample
     * to both L and R channels in the ring buffer.
     */
    const uint8_t *src = (const uint8_t *)buf_in;
    mp_uint_t bytes_written = 0;

    if (self->channels == 2) {
        /* Stereo: copy directly to ring buffer */
        for (mp_uint_t i = 0; i < size; i++) {
            /* Blocking: wait for space in ring buffer */
            while (ringbuf_put(&self->ring_buffer, src[i]) == -1) {
                /* Yield to allow ISR to drain ring buffer */
                MICROPY_EVENT_POLL_HOOK;
            }
            bytes_written++;
        }
    } else {
        /* Mono input → stereo output: duplicate each 16-bit sample */
        for (mp_uint_t i = 0; i < size; i += 2) {
            uint8_t lo = src[i];
            uint8_t hi = src[i + 1];

            /* Write L channel */
            while (ringbuf_put(&self->ring_buffer, lo) == -1) {
                MICROPY_EVENT_POLL_HOOK;
            }
            while (ringbuf_put(&self->ring_buffer, hi) == -1) {
                MICROPY_EVENT_POLL_HOOK;
            }

            /* Write R channel (duplicate) */
            while (ringbuf_put(&self->ring_buffer, lo) == -1) {
                MICROPY_EVENT_POLL_HOOK;
            }
            while (ringbuf_put(&self->ring_buffer, hi) == -1) {
                MICROPY_EVENT_POLL_HOOK;
            }

            bytes_written += 2;
        }
    }

    return bytes_written;
}

/* ══════════════════════════════════════════════════════════════════════
 * Stream Protocol: ioctl (for asyncio integration)
 * ══════════════════════════════════════════════════════════════════════ */

static mp_uint_t machine_audio_tdm_ioctl(mp_obj_t self_in,
                                         mp_uint_t request,
                                         uintptr_t arg,
                                         int *errcode) {
    machine_audio_tdm_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if (request == MP_STREAM_POLL) {
        mp_uint_t ret = 0;
        mp_uint_t flags = arg;

        if (flags & MP_STREAM_POLL_WR) {
            /* Check if ring buffer has space */
            size_t avail = AUDIO_TDM_RING_BUF_SIZE -
                           ringbuf_avail(&self->ring_buffer);
            if (avail > 0) {
                ret |= MP_STREAM_POLL_WR;
            }
        }
        return ret;
    }

    *errcode = MP_EINVAL;
    return MP_STREAM_ERROR;
}

/* ══════════════════════════════════════════════════════════════════════
 * Python write() method (wraps stream write)
 * ══════════════════════════════════════════════════════════════════════ */

static mp_obj_t machine_audio_tdm_write(mp_obj_t self_in, mp_obj_t buf_in) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_READ);

    int errcode;
    mp_uint_t ret = machine_audio_tdm_stream_write(self_in, bufinfo.buf,
                                                   bufinfo.len, &errcode);
    if (ret == MP_STREAM_ERROR) {
        mp_raise_OSError(errcode);
    }
    return MP_OBJ_NEW_SMALL_INT(ret);
}
static MP_DEFINE_CONST_FUN_OBJ_2(machine_audio_tdm_write_obj,
                                 machine_audio_tdm_write);

/* ══════════════════════════════════════════════════════════════════════
 * Print
 * ══════════════════════════════════════════════════════════════════════ */

static void machine_audio_tdm_print(const mp_print_t *print,
                                    mp_obj_t self_in,
                                    mp_print_kind_t kind) {
    machine_audio_tdm_obj_t *self = MP_OBJ_TO_PTR(self_in);
    (void)kind;

    const char *state_str;
    switch (self->state) {
        case AUDIO_TDM_STATE_ACTIVE: state_str = "active"; break;
        case AUDIO_TDM_STATE_PAUSED: state_str = "paused"; break;
        case AUDIO_TDM_STATE_INIT:   state_str = "init";   break;
        default:                     state_str = "reset";   break;
    }

    mp_printf(print,
        "AudioTDM(rate=%u, bits=%u, channels=%u, volume=%u, state=%s)",
        (unsigned)self->sample_rate,
        (unsigned)self->bits,
        (unsigned)self->channels,
        (unsigned)self->volume_pct,
        state_str);
}

/* ══════════════════════════════════════════════════════════════════════
 * Stream protocol table
 * ══════════════════════════════════════════════════════════════════════ */

static const mp_stream_p_t audio_tdm_stream_p = {
    .write = machine_audio_tdm_stream_write,
    .ioctl = machine_audio_tdm_ioctl,
    .is_text = false,
};

/* ══════════════════════════════════════════════════════════════════════
 * Method table (locals_dict)
 * ══════════════════════════════════════════════════════════════════════ */

static const mp_rom_map_elem_t machine_audio_tdm_locals_dict_table[] = {
    /* Methods */
    { MP_ROM_QSTR(MP_QSTR_init),    MP_ROM_PTR(&machine_audio_tdm_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit),  MP_ROM_PTR(&machine_audio_tdm_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_write),   MP_ROM_PTR(&machine_audio_tdm_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_volume),  MP_ROM_PTR(&machine_audio_tdm_volume_obj) },
    { MP_ROM_QSTR(MP_QSTR_pause),   MP_ROM_PTR(&machine_audio_tdm_pause_obj) },
    { MP_ROM_QSTR(MP_QSTR_resume),  MP_ROM_PTR(&machine_audio_tdm_resume_obj) },
    { MP_ROM_QSTR(MP_QSTR_stat),    MP_ROM_PTR(&machine_audio_tdm_stat_obj) },

    /* Finalizer */
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&machine_audio_tdm_deinit_obj) },

    /* Constants: sample rate presets */
    { MP_ROM_QSTR(MP_QSTR_RATE_16K),  MP_ROM_INT(AUDIO_TDM_RATE_16000) },
    { MP_ROM_QSTR(MP_QSTR_RATE_22K),  MP_ROM_INT(AUDIO_TDM_RATE_22050) },
    { MP_ROM_QSTR(MP_QSTR_RATE_44K),  MP_ROM_INT(AUDIO_TDM_RATE_44100) },
    { MP_ROM_QSTR(MP_QSTR_RATE_48K),  MP_ROM_INT(AUDIO_TDM_RATE_48000) },
};
static MP_DEFINE_CONST_DICT(machine_audio_tdm_locals_dict,
                            machine_audio_tdm_locals_dict_table);

/* ══════════════════════════════════════════════════════════════════════
 * Type definition
 * ══════════════════════════════════════════════════════════════════════ */

MP_REGISTER_ROOT_POINTER(struct _machine_audio_tdm_obj_t *machine_audio_tdm_obj[AUDIO_TDM_MAX_INSTANCES]);

MP_DEFINE_CONST_OBJ_TYPE(
    machine_audio_tdm_type,
    MP_QSTR_AudioTDM,
    MP_TYPE_FLAG_ITER_IS_STREAM,
    make_new, machine_audio_tdm_make_new,
    print, machine_audio_tdm_print,
    protocol, &audio_tdm_stream_p,
    locals_dict, &machine_audio_tdm_locals_dict
);

/* ══════════════════════════════════════════════════════════════════════
 * Soft-reset cleanup — called from main.c before GC sweep
 * ══════════════════════════════════════════════════════════════════════ */

void machine_audio_tdm_deinit_all(void) {
    /* Disable NVIC first to stop ISR from firing */
    NVIC_DisableIRQ(audio_tdm_isr_cfg.intrSrc);

    /* Disable TDM interrupt mask at peripheral level */
    Cy_AudioTDM_SetTxInterruptMask(CYBSP_TDM_CONTROLLER_0_TX_HW, 0);
    Cy_AudioTDM_ClearTxInterrupt(CYBSP_TDM_CONTROLLER_0_TX_HW,
                                 CY_TDM_INTR_TX_MASK);

    /* Deactivate and disable TX */
    Cy_AudioTDM_DeActivateTx(CYBSP_TDM_CONTROLLER_0_TX_HW);
    Cy_AudioTDM_DisableTx(CYBSP_TDM_CONTROLLER_0_TX_HW);

    /* Clear instance pointer so stale ISR won't dereference freed memory */
    for (int i = 0; i < AUDIO_TDM_MAX_INSTANCES; i++) {
        MP_STATE_PORT(machine_audio_tdm_obj[i]) = NULL;
    }
}

#endif /* MICROPY_PY_MACHINE_AUDIO_TDM */
