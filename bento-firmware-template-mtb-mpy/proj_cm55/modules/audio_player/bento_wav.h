/*******************************************************************************
 * File Name: bento_wav.h
 *
 * Description: WAV file parser + streaming playback for BENTO Eva Kit.
 *              Reads PCM data from SD card and feeds bento_audio ISR via
 *              double-buffered ring buffer.
 *
 *              Supported formats:
 *                - 16-bit PCM (uncompressed)
 *                - Mono or Stereo
 *                - 8 kHz, 16 kHz, 22.05 kHz, 44.1 kHz, 48 kHz
 *
 *******************************************************************************/

#ifndef BENTO_WAV_H
#define BENTO_WAV_H

#include <stdint.h>
#include <stdbool.h>
#include "ff.h"

/* PCM read buffer size per buffer (16 KB × 2 = 32 KB total).
 * At 48 kHz stereo 16-bit (192 KB/s), each buffer holds ~85 ms.
 * Feed timer at 10 ms keeps both buffers topped up. */
#define BENTO_WAV_BUF_SIZE      (16 * 1024)

/* WAV file header info */
typedef struct {
    uint16_t num_channels;
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    uint32_t data_offset;      /* Byte offset where PCM data starts */
    uint32_t data_size;        /* Total PCM data bytes */
} bento_wav_info_t;

/* Playback state */
typedef enum {
    WAV_STATE_IDLE = 0,
    WAV_STATE_PLAYING,
    WAV_STATE_PAUSED,
    WAV_STATE_ERROR,
} bento_wav_state_t;

/*******************************************************************************
 * API
 *******************************************************************************/

/* Parse WAV header. Returns true if valid PCM WAV. */
bool bento_wav_parse_header(FIL *fp, bento_wav_info_t *info);

/* Start playback of a WAV file from SD card.
 * path: e.g. "/music/song.wav"
 * Returns true if playback started successfully. */
bool bento_wav_play(const char *path);

/* Pause playback (can resume). */
void bento_wav_pause(void);

/* Resume paused playback. */
void bento_wav_resume(void);

/* Stop playback and close file. */
void bento_wav_stop(void);

/* Get current playback state. */
bento_wav_state_t bento_wav_get_state(void);

/* Get current playback position in seconds. */
float bento_wav_get_position(void);

/* Get total duration in seconds. */
float bento_wav_get_duration(void);

/* Get the WAV info of currently loaded file. */
const bento_wav_info_t *bento_wav_get_info(void);

/* Call this from the audio ISR to get the next sample.
 * Returns 0 (silence) if no data available.
 * Called at sample_rate × num_channels per second. */
int16_t bento_wav_get_sample(void);

/* Feed task: call periodically from LVGL timer or FreeRTOS task
 * to refill the PCM buffer from SD card. ~20ms interval recommended. */
void bento_wav_feed(void);

#endif /* BENTO_WAV_H */
