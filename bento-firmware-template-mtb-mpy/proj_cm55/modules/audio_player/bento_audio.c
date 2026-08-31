/*******************************************************************************
 * File Name: bento_audio.c
 *
 * Description: Audio subsystem for BENTO Eva Kit.
 *              TLV320DAC3100 codec + I2S/TDM playback.
 *
 *              Ported from Infineon mtb-example-psoc-edge-mains-powered-local-voice
 *              (proj_cm55/source/use_case/COMPONENT_MUSICPLAYER/app_i2s.c)
 *
 *              Key difference from original: runs inside BentoClaw GFX task,
 *              not a separate music_player_task. Uses existing display I2C
 *              context with semaphore protection.
 *
 *******************************************************************************/

#include "bento_audio.h"
#include "bento_sfx.h"   /* game SFX polyphonic mixer (BENTO_AUDIO_SRC_MIXER) */
/* File-streaming + SF2 sources need the SD card (FatFS). On the Game console the
 * SD-dependent stack is omitted (BSP has no CYBSP_SDHC_1_HW; assets will live in
 * internal QSPI instead). BENTO_AUDIO_NO_FILE_SOURCES compiles out WAV/MIDI-file/
 * SF2/MP3; the TONE path (Phase 1a) and the future RAM SFX mixer stay available. */
#ifndef BENTO_AUDIO_NO_FILE_SOURCES
#include "bento_wav.h"
#include "bento_midi.h"
#include "bento_tsf.h"
#include "bento_mp3.h"
#endif
#include "mtb_tlv320dac3100.h"
#include "display_i2c_config.h"
#include "cy_syslib.h"
#if BSP_HAS_POTENTIOMETER
#include "cy_autanalog_sar.h"   /* on-board pot → volume (SAR ADC, no init needed) */
#endif
#include <math.h>

/*******************************************************************************
 * State
 *******************************************************************************/
static bool s_tdm_initialized = false;
static bool s_codec_initialized = false;
static bool s_tx_active = false;

/* I2C HAL object for codec — reuses display touch I2C context */
static mtb_hal_i2c_t s_codec_i2c_hal;

/* I2C semaphore for shared bus (codec + touch). Created by tesaiot_display.c */
SemaphoreHandle_t bento_i2c_semaphore = NULL;

/*******************************************************************************
 * ISR config
 *******************************************************************************/
static const cy_stc_sysint_t s_i2s_isr_cfg = {
    .intrSrc = (IRQn_Type)tdm_0_interrupts_tx_0_IRQn,
    .intrPriority = BENTO_AUDIO_ISR_PRIORITY,
};

/*******************************************************************************
 * Sine wave test tone (pre-computed)
 *******************************************************************************/
#define SINE_TABLE_LEN  48  /* one cycle at 1 kHz when sample rate = 48 kHz */
static int16_t s_sine_table[SINE_TABLE_LEN];
static volatile uint32_t s_sine_phase = 0;
static volatile bool s_tone_active = false;

/* Audio source mode — determines what ISR pushes into FIFO */
static volatile bento_audio_src_t s_audio_src = BENTO_AUDIO_SRC_SILENCE;

static void generate_sine_table(void)
{
    for (int i = 0; i < SINE_TABLE_LEN; i++) {
        /* ~1 kHz tone at 48 kHz sample rate, amplitude ~50% to avoid clipping */
        double angle = 2.0 * 3.14159265358979 * (double)i / (double)SINE_TABLE_LEN;
        s_sine_table[i] = (int16_t)(16000.0 * sin(angle));
    }
}

/*******************************************************************************
 * I2S TX Interrupt Handler
 *
 * Directly from Infineon voice example (app_i2s.c:602).
 * When tone is active: fills FIFO with sine wave.
 * When tone is inactive: fills FIFO with zeros (silence).
 *******************************************************************************/
static void i2s_tx_interrupt_handler(void)
{
    uint32_t intr_status = Cy_AudioTDM_GetTxInterruptStatusMasked(TDM_STRUCT0_TX);

    if (CY_TDM_INTR_TX_FIFO_TRIGGER & intr_status) {
        bento_audio_src_t src = s_audio_src;

        if (src == BENTO_AUDIO_SRC_TONE && s_tone_active) {
            for (uint32_t i = 0; i < BENTO_AUDIO_HW_FIFO_SIZE; i++) {
                int16_t sample = s_sine_table[s_sine_phase % SINE_TABLE_LEN];
                Cy_AudioTDM_WriteTxData(TDM_STRUCT0_TX, (uint32_t)(uint16_t)sample);
                /* Stereo: advance phase every 2 samples (L+R pair) */
                if (i & 1) {
                    s_sine_phase++;
                }
            }
        } else if (src == BENTO_AUDIO_SRC_MIXER) {
            /* Game SFX: polyphonic mono mixer → duplicate each sample for L+R. */
            for (uint32_t i = 0; i < BENTO_AUDIO_HW_FIFO_SIZE; i += 2) {
                int16_t smp = bento_sfx_mixer_get_sample();
                uint32_t w = (uint32_t)(uint16_t)smp;
                Cy_AudioTDM_WriteTxData(TDM_STRUCT0_TX, w);  /* L */
                Cy_AudioTDM_WriteTxData(TDM_STRUCT0_TX, w);  /* R */
            }
#ifndef BENTO_AUDIO_NO_FILE_SOURCES
        } else if (src == BENTO_AUDIO_SRC_WAV) {
            for (uint32_t i = 0; i < BENTO_AUDIO_HW_FIFO_SIZE; i++) {
                int16_t sample = bento_wav_get_sample();
                Cy_AudioTDM_WriteTxData(TDM_STRUCT0_TX, (uint32_t)(uint16_t)sample);
            }
        } else if (src == BENTO_AUDIO_SRC_MIDI) {
            /* Mono source → duplicate each sample for stereo L+R pair */
            for (uint32_t i = 0; i < BENTO_AUDIO_HW_FIFO_SIZE; i += 2) {
                int16_t sample = bento_midi_get_sample();
                uint32_t s = (uint32_t)(uint16_t)sample;
                Cy_AudioTDM_WriteTxData(TDM_STRUCT0_TX, s);  /* L */
                Cy_AudioTDM_WriteTxData(TDM_STRUCT0_TX, s);  /* R */
            }
        } else if (src == BENTO_AUDIO_SRC_MIDI_SF2) {
            for (uint32_t i = 0; i < BENTO_AUDIO_HW_FIFO_SIZE; i += 2) {
                int16_t sample = bento_tsf_get_sample();
                uint32_t s = (uint32_t)(uint16_t)sample;
                Cy_AudioTDM_WriteTxData(TDM_STRUCT0_TX, s);
                Cy_AudioTDM_WriteTxData(TDM_STRUCT0_TX, s);
            }
        } else if (src == BENTO_AUDIO_SRC_MP3) {
            for (uint32_t i = 0; i < BENTO_AUDIO_HW_FIFO_SIZE; i += 2) {
                int16_t sample = bento_mp3_get_sample();
                uint32_t s = (uint32_t)(uint16_t)sample;
                Cy_AudioTDM_WriteTxData(TDM_STRUCT0_TX, s);
                Cy_AudioTDM_WriteTxData(TDM_STRUCT0_TX, s);
            }
#endif /* BENTO_AUDIO_NO_FILE_SOURCES */
        } else {
            for (uint32_t i = 0; i < BENTO_AUDIO_HW_FIFO_SIZE; i++) {
                Cy_AudioTDM_WriteTxData(TDM_STRUCT0_TX, 0UL);
            }
        }
    }

    if (CY_TDM_INTR_TX_FIFO_UNDERFLOW & intr_status) {
        /* Underflow — just clear, not fatal */
    }

    Cy_AudioTDM_ClearTxInterrupt(TDM_STRUCT0_TX, CY_TDM_INTR_TX_MASK);
}

/*******************************************************************************
 * bento_audio_init — TDM hardware init (MCLK starts here)
 *
 * From Infineon: app_i2s_init() in app_i2s.c:137
 *******************************************************************************/
void bento_audio_init(void)
{
    if (s_tdm_initialized) return;

    /* Initialize the I2S interrupt */
    Cy_SysInt_Init(&s_i2s_isr_cfg, i2s_tx_interrupt_handler);

    /* Initialize the I2S (TDM) block — MCLK output starts here */
    cy_en_tdm_status_t status = Cy_AudioTDM_Init(TDM_STRUCT0, &CYBSP_TDM_CONTROLLER_0_config);
    if (CY_TDM_SUCCESS != status) {
        return;  /* TDM init failed */
    }

    generate_sine_table();
    s_tdm_initialized = true;
}

/*******************************************************************************
 * bento_audio_codec_init — TLV320DAC3100 via I2C
 *
 * From Infineon: app_tlv_codec_init() in app_i2s.c:183
 * CRITICAL: bento_audio_init() MUST be called first (MCLK must be running)
 *******************************************************************************/
bool bento_audio_codec_init(uint32_t sample_rate_hz)
{
    if (!s_tdm_initialized) return false;
    if (s_codec_initialized) return true;

    uint32_t mclk_hz;
    uint32_t clk_div;

    if (sample_rate_hz == BENTO_AUDIO_RATE_16KHZ) {
        clk_div = BENTO_AUDIO_CLK_DIV_16KHZ;
        mclk_hz = BENTO_AUDIO_MCLK_16KHZ_HZ;
    } else {
        clk_div = BENTO_AUDIO_CLK_DIV_48KHZ;
        mclk_hz = BENTO_AUDIO_MCLK_48KHZ_HZ;
    }

    /* Set peri clock divider for TDM
     * From Infineon: app_tlv_codec_init() lines 189-208 */
    Cy_SysClk_PeriPclkDisableDivider(
        (en_clk_dst_t)CYBSP_TDM_CONTROLLER_0_CLK_DIV_GRP_NUM,
        CY_SYSCLK_DIV_16_5_BIT, 0U);
    Cy_SysClk_PeriPclkSetFracDivider(
        (en_clk_dst_t)CYBSP_TDM_CONTROLLER_0_CLK_DIV_GRP_NUM,
        CY_SYSCLK_DIV_16_5_BIT, 0U,
        clk_div - 1, 0U);
    Cy_SysClk_PeriPclkEnableDivider(
        (en_clk_dst_t)CYBSP_TDM_CONTROLLER_0_CLK_DIV_GRP_NUM,
        CY_SYSCLK_DIV_16_5_BIT, 0U);

    vTaskDelay(pdMS_TO_TICKS(50));  /* Wait for clock to stabilize */

    /* Take I2C semaphore (shared with touch controller) */
    if (bento_i2c_semaphore != NULL) {
        xSemaphoreTake(bento_i2c_semaphore, portMAX_DELAY);
    }

    /* Set up HAL I2C using display/touch I2C context
     * From Infineon: tlv_codec_i2c_init() ENABLE_GFX_UI version, app_i2s.c:410 */
    cy_rslt_t hal_result;
    hal_result = mtb_hal_i2c_setup(&s_codec_i2c_hal,
                                    &CYBSP_I2C_CONTROLLER_hal_config,
                                    &disp_touch_i2c_controller_context, NULL);
    if (CY_RSLT_SUCCESS != hal_result) {
        if (bento_i2c_semaphore != NULL) xSemaphoreGive(bento_i2c_semaphore);
        return false;
    }

    mtb_hal_i2c_cfg_t i2c_cfg = {
        .is_target = false,
        .address = BENTO_AUDIO_CODEC_I2C_ADDR,
        .frequency_hz = BENTO_AUDIO_I2C_FREQ_HZ,
        .address_mask = MTB_HAL_I2C_DEFAULT_ADDR_MASK,
        .enable_address_callback = false
    };
    hal_result = mtb_hal_i2c_configure(&s_codec_i2c_hal, &i2c_cfg);
    if (CY_RSLT_SUCCESS != hal_result) {
        if (bento_i2c_semaphore != NULL) xSemaphoreGive(bento_i2c_semaphore);
        return false;
    }

    /* TLV codec middleware init (stores I2C pointer)
     * From Infineon: app_i2s.c:221 */
    cy_rslt_t codec_result = mtb_tlv320dac3100_init(&s_codec_i2c_hal);
    if (CY_RSLT_SUCCESS != codec_result) {
        if (bento_i2c_semaphore != NULL) xSemaphoreGive(bento_i2c_semaphore);
        return false;
    }

    /* Configure clocking + DAC + output drivers
     * From Infineon: app_i2s.c:227 (USE_SPEAKER path) */
    codec_result = mtb_tlv320dac3100_configure_clocking(
        mclk_hz,
        (mtb_tlv320dac3100_dac_sample_rate_t)sample_rate_hz,
        TLV320DAC3100_I2S_WORD_SIZE_16,
        TLV320DAC3100_SPK_AUDIO_OUTPUT);
    if (CY_RSLT_SUCCESS != codec_result) {
        if (bento_i2c_semaphore != NULL) xSemaphoreGive(bento_i2c_semaphore);
        return false;
    }

    /* Keep the output-driver state we had before the driver moved to the
     * Apache-2.0 asset.
     *
     * The copy we used to vendor powered up BOTH output paths unconditionally.
     * The asset powers up exactly one, chosen by the argument above, and has no
     * enumerator for "both" -- so asking for the speaker leaves HPL/HPR powered
     * down. J8 is the speaker and is what SPK selects correctly, but anything
     * taking audio off the headphone pins would go silent, and a licence fix
     * has no business changing which outputs are live. This restores the HP
     * driver power-up the old driver did at the same point in the sequence. */
    (void)mtb_tlv320dac3100_write_byte(TLV320DAC3100_P1_REG_HP_DRV, 0xC4);

    /* Activate codec (power up NDAC/MDAC)
     * From Infineon: app_i2s.c:239 */
    mtb_tlv320dac3100_activate();

    /* Release I2C semaphore */
    if (bento_i2c_semaphore != NULL) {
        xSemaphoreGive(bento_i2c_semaphore);
    }

    /* Set volume */
    bento_audio_set_volume(BENTO_AUDIO_VOLUME_DEFAULT);

    s_codec_initialized = true;
    return true;
}

/*******************************************************************************
 * bento_audio_start — Enable TX + ISR
 *
 * From Infineon: app_i2s_enable() + app_i2s_activate() in app_i2s.c:326-283
 *******************************************************************************/
void bento_audio_start(void)
{
    if (!s_tdm_initialized || s_tx_active) return;

    /* Enable TX
     * From Infineon: app_i2s_enable() */
    Cy_AudioTDM_EnableTx(TDM_STRUCT0_TX);
    Cy_AudioTDM_ClearTxInterrupt(TDM_STRUCT0_TX, CY_TDM_INTR_TX_MASK);
    Cy_AudioTDM_SetTxInterruptMask(TDM_STRUCT0_TX, CY_TDM_INTR_TX_MASK);

    /* Fill TX FIFO with zeros to start transmission */
    for (uint32_t i = 0; i < (BENTO_AUDIO_HW_FIFO_SIZE / 2); i++) {
        Cy_AudioTDM_WriteTxData(TDM_STRUCT0_TX, 0UL);
        Cy_AudioTDM_WriteTxData(TDM_STRUCT0_TX, 0UL);
    }

    vTaskDelay(1);  /* 1 tick delay (from Infineon: I2S_OPERATION_DELAY_TICKS) */

    /* Activate: NVIC first, then TDM activate
     * From Infineon: app_i2s_activate() */
    NVIC_EnableIRQ(s_i2s_isr_cfg.intrSrc);
    Cy_AudioTDM_ActivateTx(TDM_STRUCT0_TX);

    s_tx_active = true;
}

/*******************************************************************************
 * bento_audio_stop — Stop TX
 *
 * From Infineon: app_i2s_deactivate() + app_i2s_disable()
 *******************************************************************************/
void bento_audio_stop(void)
{
    if (!s_tx_active) return;

    s_audio_src = BENTO_AUDIO_SRC_SILENCE;
    s_tone_active = false;

    NVIC_DisableIRQ(s_i2s_isr_cfg.intrSrc);

    /* Clear FIFO by disable/enable/deactivate/disable cycle
     * From Infineon: app_i2s_deactivate() app_i2s.c:298 */
    Cy_AudioTDM_DeActivateTx(TDM_STRUCT0_TX);
    Cy_AudioI2S_DisableTx(TDM_STRUCT0_TX);
    Cy_AudioI2S_EnableTx(TDM_STRUCT0_TX);
    Cy_AudioTDM_DeActivateTx(TDM_STRUCT0_TX);
    Cy_AudioI2S_DisableTx(TDM_STRUCT0_TX);

    Cy_AudioTDM_ClearTxInterrupt(TDM_STRUCT0_TX, CY_TDM_INTR_TX_MASK);
    Cy_AudioTDM_ClearTxTriggerInterruptMask(TDM_STRUCT0_TX);

    s_tx_active = false;
}

/*******************************************************************************
 * bento_audio_deinit
 *******************************************************************************/
void bento_audio_deinit(void)
{
    bento_audio_stop();

    if (s_codec_initialized) {
        if (bento_i2c_semaphore != NULL) {
            xSemaphoreTake(bento_i2c_semaphore, portMAX_DELAY);
        }
        mtb_tlv320dac3100_deactivate();
        mtb_tlv320dac3100_free();
        if (bento_i2c_semaphore != NULL) {
            xSemaphoreGive(bento_i2c_semaphore);
        }
        s_codec_initialized = false;
    }

    if (s_tdm_initialized) {
        Cy_AudioTDM_DeInit(TDM_STRUCT0);
        s_tdm_initialized = false;
    }
}

/*******************************************************************************
 * bento_audio_test_tone — Play 1 kHz sine for 2 seconds
 *******************************************************************************/
void bento_audio_test_tone(void)
{
    if (!s_tdm_initialized) {
        bento_audio_init();
    }
    if (!s_codec_initialized) {
        if (!bento_audio_codec_init(BENTO_AUDIO_RATE_48KHZ)) {
            return;  /* Codec init failed */
        }
    }
    if (!s_tx_active) {
        bento_audio_start();
    }

    s_sine_phase = 0;
    s_tone_active = true;

    /* Play for 2 seconds */
    vTaskDelay(pdMS_TO_TICKS(2000));

    s_tone_active = false;
}

/*******************************************************************************
 * bento_audio_play_tone — Non-blocking tone on/off
 *******************************************************************************/
void bento_audio_play_tone(bool on)
{
    if (on) {
        s_sine_phase = 0;
        s_tone_active = true;
        s_audio_src = BENTO_AUDIO_SRC_TONE;
    } else {
        s_tone_active = false;
        s_audio_src = BENTO_AUDIO_SRC_SILENCE;
    }
}

/*******************************************************************************
 * bento_audio_set_volume
 *******************************************************************************/
void bento_audio_set_volume(uint8_t vol)
{
    if (!s_codec_initialized) return;

    if (vol > BENTO_AUDIO_VOLUME_MAX) vol = BENTO_AUDIO_VOLUME_MAX;

    /* Square-root volume curve: makes low slider values audible.
     * Codec register = (scaled | 0x80), where register is signed 8-bit,
     * 0.5 dB/step.  scaled=0 → -64 dB (mute), scaled=125 → -1.5 dB (loud).
     *
     * Formula: scaled = sqrt(vol × 142)  (142 ≈ 125²/110)
     * Newton's method integer sqrt — 3 iterations, no FPU needed.
     *
     *   Slider 0%   → 0   → mute
     *   Slider 30%  → 68  → -30 dB  (audible)
     *   Slider 50%  → 88  → -20 dB
     *   Slider 100% → 125 → -1.5 dB (very loud) */
    uint8_t scaled;
    if (vol == 0) {
        scaled = 0;
    } else {
        uint32_t n = (uint32_t)vol * 142u;
        uint32_t x = 64u;
        x = (x + n / x) >> 1;
        x = (x + n / x) >> 1;
        x = (x + n / x) >> 1;
        scaled = (x > 125u) ? 125u : (uint8_t)x;
    }

    if (bento_i2c_semaphore != NULL) {
        xSemaphoreTake(bento_i2c_semaphore, portMAX_DELAY);
    }
    mtb_tlv320dac3100_adjust_speaker_output_volume(scaled);
    if (bento_i2c_semaphore != NULL) {
        xSemaphoreGive(bento_i2c_semaphore);
    }
}

/*******************************************************************************
 * bento_audio_pot_volume — map the on-board potentiometer to a volume.
 *
 * The Eva Kit pot is on the AutAnalog SAR ADC (GPIO channel 0 = P15.1), which
 * cybsp_init() configures on CM33 — CM55 only reads the latched result, so no
 * ADC init is needed here. 12-bit (0..4095) → 0..BENTO_AUDIO_VOLUME_MAX.
 *******************************************************************************/
uint8_t bento_audio_pot_volume(void)
{
#if BSP_HAS_POTENTIOMETER
    /* The SAR result occasionally glitches to 0. Sample a few times and take the
     * max: a true 0 (knob fully left) stays 0, but a transient 0 glitch is rejected
     * because at least one sample holds the real level. */
    int32_t best = 0;
    for (int i = 0; i < 8; i++) {
        int32_t adc = Cy_AutAnalog_SAR_ReadResult(0u, CY_AUTANALOG_SAR_INPUT_GPIO, 0u);
        if (adc > best) { best = adc; }
    }
    if (best > 4095) { best = 4095; }
    return (uint8_t)(((uint32_t)best * BENTO_AUDIO_VOLUME_MAX) / 4095u);
#else
    return BENTO_AUDIO_VOLUME_DEFAULT;
#endif
}

/*******************************************************************************
 * bento_audio_set_volume_from_pot — read the pot and apply it as the volume.
 *******************************************************************************/
void bento_audio_set_volume_from_pot(void)
{
    bento_audio_set_volume(bento_audio_pot_volume());
}

/*******************************************************************************
 * bento_audio_is_active
 *******************************************************************************/
bool bento_audio_is_active(void)
{
    return s_audio_src != BENTO_AUDIO_SRC_SILENCE;
}

/*******************************************************************************
 * bento_audio_set_source / get_source
 *******************************************************************************/
void bento_audio_set_source(bento_audio_src_t src)
{
    s_audio_src = src;
}

bento_audio_src_t bento_audio_get_source(void)
{
    return s_audio_src;
}
