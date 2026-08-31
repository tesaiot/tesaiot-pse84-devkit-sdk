/*******************************************************************************
 * File Name: bento_audio.h
 *
 * Description: Audio subsystem for BENTO Eva Kit.
 *              TLV320DAC3100 codec + I2S/TDM playback.
 *              Ported from Infineon mtb-example-psoc-edge-mains-powered-local-voice.
 *
 * Init sequence (CRITICAL order):
 *   1. bento_audio_init()     — TDM hardware init (MCLK starts)
 *   2. bento_audio_codec_init() — TLV320DAC3100 via I2C (needs MCLK)
 *   3. bento_audio_start()    — enable TX + ISR
 *   4. bento_audio_feed()     — queue PCM data for ISR
 *
 *******************************************************************************/

#ifndef BENTO_AUDIO_H
#define BENTO_AUDIO_H

#include <stdint.h>
#include <stdbool.h>
#include "cy_pdl.h"
#include "cybsp.h"
#include "mtb_hal.h"
#include "FreeRTOS.h"
#include "semphr.h"

/* I2C address of TLV320DAC3100 */
#define BENTO_AUDIO_CODEC_I2C_ADDR     (0x18u)
#define BENTO_AUDIO_I2C_FREQ_HZ        (100000UL)

/* Clock dividers (from HFCLK[1] = 400 MHz) */
#define BENTO_AUDIO_CLK_DIV_48KHZ      (8u)
#define BENTO_AUDIO_CLK_DIV_16KHZ      (24u)
#define BENTO_AUDIO_MCLK_48KHZ_HZ      (6144000UL)
#define BENTO_AUDIO_MCLK_16KHZ_HZ      (2048000UL)

/* I2S hardware */
#define BENTO_AUDIO_HW_FIFO_SIZE        (64u)
#define BENTO_AUDIO_ISR_PRIORITY        (2u)

/* Volume */
#define BENTO_AUDIO_VOLUME_DEFAULT      (40u)   /* night-safe default; pot overrides */
#define BENTO_AUDIO_VOLUME_MAX          (110u)

/* Sampling rates */
#define BENTO_AUDIO_RATE_16KHZ          (16000u)
#define BENTO_AUDIO_RATE_48KHZ          (48000u)

/* Audio source mode */
typedef enum {
    BENTO_AUDIO_SRC_SILENCE = 0,
    BENTO_AUDIO_SRC_TONE,           /* 1 kHz sine wave test */
    BENTO_AUDIO_SRC_WAV,            /* WAV file from SD card */
    BENTO_AUDIO_SRC_MIDI,           /* MIDI DDS synthesizer */
    BENTO_AUDIO_SRC_MIDI_SF2,       /* MIDI via TinySoundFont (ring buffer) */
    BENTO_AUDIO_SRC_MP3,            /* MP3 via minimp3 (ring buffer) */
    BENTO_AUDIO_SRC_MIXER,          /* game SFX: bento_sfx polyphonic DDS+PCM mixer */
} bento_audio_src_t;

/* I2C semaphore (shared with display touch — declared in tesaiot_display.c) */
extern SemaphoreHandle_t bento_i2c_semaphore;

/* I2C context from display (declared in tesaiot_display.c) */
extern cy_stc_scb_i2c_context_t disp_touch_i2c_controller_context;

/*******************************************************************************
 * API
 *******************************************************************************/

/* Initialize TDM hardware (ISR + Cy_AudioTDM_Init). MCLK starts here. */
void bento_audio_init(void);

/* Initialize TLV320DAC3100 codec via I2C. MUST call after bento_audio_init(). */
bool bento_audio_codec_init(uint32_t sample_rate_hz);

/* Enable TX + ISR + activate. Call after codec_init. */
void bento_audio_start(void);

/* Stop TX + disable ISR. */
void bento_audio_stop(void);

/* Deinit everything. */
void bento_audio_deinit(void);

/* Play a test sine wave (1 kHz, 2 seconds). Blocking. */
void bento_audio_test_tone(void);

/* Non-blocking tone control: true=start, false=stop. */
void bento_audio_play_tone(bool on);

/* Set speaker volume (0-110). */
void bento_audio_set_volume(uint8_t vol);

/* Read the on-board potentiometer (SAR ADC) and map it to a volume 0..MAX.
 * Returns BENTO_AUDIO_VOLUME_DEFAULT when no pot is present. Cheap — safe to poll. */
uint8_t bento_audio_pot_volume(void);

/* Convenience: read the pot and apply it as the speaker volume in one call. */
void bento_audio_set_volume_from_pot(void);

/* Check if audio is playing */
bool bento_audio_is_active(void);

/* Set audio source mode (silence, tone, or WAV). */
void bento_audio_set_source(bento_audio_src_t src);

/* Get current audio source mode. */
bento_audio_src_t bento_audio_get_source(void);

#endif /* BENTO_AUDIO_H */
