/*******************************************************************************
 * File Name: bento_mp3.h
 *
 * Description: MP3 file decoder + streaming playback for BENTO Eva Kit.
 *              Uses minimp3 (CC0) for decoding, lock-free ring buffer for
 *              ISR-safe sample delivery.
 *
 *              Supported: MPEG-1/2/2.5 Layer III, all bitrates, mono/stereo.
 *
 *              Usage:
 *                1. bento_mp3_play("/music/song.mp3")
 *                2. bento_mp3_feed() from LVGL timer (~10ms)
 *                3. bento_mp3_get_sample() from TDM ISR (48 kHz)
 *
 ******************************************************************************/

#ifndef BENTO_MP3_H
#define BENTO_MP3_H

#include <stdint.h>
#include <stdbool.h>

/* Ring buffer: 8192 mono samples = ~170ms at 48 kHz (power-of-2). */
#define BENTO_MP3_RING_SIZE     8192

/* Compressed data read buffer (holds ~5-6 MP3 frames at 320kbps). */
#define BENTO_MP3_READ_BUF_SIZE 8192

/* MP3 file info */
typedef struct {
    int      channels;
    int      sample_rate;
    int      bitrate_kbps;
    int      layer;
    uint32_t file_size;
    float    duration_sec;     /* Estimated from bitrate */
} bento_mp3_info_t;

/* Playback state */
typedef enum {
    MP3_STATE_IDLE = 0,
    MP3_STATE_PLAYING,
    MP3_STATE_PAUSED,
    MP3_STATE_ERROR,
} bento_mp3_state_t;

/*******************************************************************************
 * API
 ******************************************************************************/

/* Start MP3 playback from SD card.
 * path: FatFS path e.g. "/music/song.mp3"
 * Returns true if playback started. */
bool bento_mp3_play(const char *path);

/* Pause playback (can resume). */
void bento_mp3_pause(void);

/* Resume paused playback. */
void bento_mp3_resume(void);

/* Stop playback and close file. */
void bento_mp3_stop(void);

/* Get current state. */
bento_mp3_state_t bento_mp3_get_state(void);

/* Get playback position in seconds. */
float bento_mp3_get_position(void);

/* Get total duration in seconds (estimate). */
float bento_mp3_get_duration(void);

/* Get info of currently loaded MP3. */
const bento_mp3_info_t *bento_mp3_get_info(void);

/* ISR-callable: get one PCM sample from ring buffer.
 * Returns 0 (silence) if buffer empty. */
int16_t bento_mp3_get_sample(void);

/* Feed: call from LVGL timer (~10ms) to decode + fill ring buffer. */
void bento_mp3_feed(void);

#endif /* BENTO_MP3_H */
