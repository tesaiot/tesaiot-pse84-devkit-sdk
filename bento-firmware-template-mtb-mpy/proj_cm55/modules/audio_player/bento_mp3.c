/*******************************************************************************
 * File Name: bento_mp3.c
 *
 * Description: MP3 decoder + streaming playback using minimp3.
 *              Reads compressed data from SD card via FatFS,
 *              decodes to PCM, stores in SPSC lock-free ring buffer
 *              consumed by the TDM TX ISR.
 *
 *              Memory: decoder state (~5KB) + read buffer (4KB) +
 *              ring buffer (8KB) = ~17KB total in internal SRAM.
 *
 ******************************************************************************/

#include "bento_mp3.h"
#include "bento_sdcard.h"
#include "ff.h"
#include "cmsis_compiler.h"
#include <string.h>

/*******************************************************************************
 * minimp3 Configuration — MUST be before #include "minimp3.h"
 ******************************************************************************/
#define MINIMP3_IMPLEMENTATION

/* Suppress warnings from minimp3.h */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#include "minimp3.h"
#pragma GCC diagnostic pop

/*******************************************************************************
 * Module State
 ******************************************************************************/
static mp3dec_t s_dec;
static FIL      s_fp;
static bool     s_file_open = false;

static bento_mp3_info_t  s_info;
static bento_mp3_state_t s_state = MP3_STATE_IDLE;

/* Compressed data read buffer */
static uint8_t  s_read_buf[BENTO_MP3_READ_BUF_SIZE];
static uint32_t s_read_buf_len  = 0;   /* Valid bytes in read buffer */
static uint32_t s_read_buf_pos  = 0;   /* Current parse position */

/* SPSC lock-free ring buffer */
static int16_t           s_ring_buf[BENTO_MP3_RING_SIZE];
static volatile uint32_t s_ring_wr = 0;
static volatile uint32_t s_ring_rd = 0;

#define RING_MASK  (BENTO_MP3_RING_SIZE - 1)

/* Position tracking */
static uint32_t s_bytes_decoded = 0;   /* Compressed bytes consumed */
static uint32_t s_samples_decoded = 0; /* Total PCM samples decoded */

/* Resampler state (source rate → 48000 Hz) */
static float s_resample_step = 1.0f;  /* source_rate / 48000 */
static float s_resample_pos  = 0.0f;  /* fractional position in source buffer */

/* Diagnostics */
static volatile uint32_t s_underrun_count = 0;

/*******************************************************************************
 * Ring buffer helpers
 ******************************************************************************/
static inline uint32_t ring_available(void)
{
    return (s_ring_wr - s_ring_rd) & RING_MASK;
}

static inline uint32_t ring_free(void)
{
    return BENTO_MP3_RING_SIZE - 1 - ring_available();
}

/*******************************************************************************
 * Internal: write mono PCM to ring buffer with optional resampling
 *
 * src_mono[] has `count` mono samples at source sample rate.
 * If source rate != 48000, linearly interpolate to produce 48kHz output.
 ******************************************************************************/
static void ring_write_resampled(const int16_t *src_mono, int count)
{
    uint32_t wr = s_ring_wr;

    if (count <= 0) return;

    if (s_resample_step >= 0.999f && s_resample_step <= 1.001f) {
        /* No resampling needed (48kHz source) — 1:1 copy */
        uint32_t free_space = ring_free();
        int n = (count < (int)free_space) ? count : (int)free_space;
        for (int j = 0; j < n; j++) {
            s_ring_buf[wr & RING_MASK] = src_mono[j];
            wr++;
        }
        s_samples_decoded += n;
    } else {
        /* Linear interpolation resampler.
         * s_resample_pos carries fractional position from previous frame.
         * We consume source samples at `s_resample_step` per output sample. */
        float limit = (float)(count - 1);
        while (s_resample_pos < limit) {
            if (ring_free() == 0) break;  /* ring full — stop, pos preserved */
            int idx = (int)s_resample_pos;
            float frac = s_resample_pos - (float)idx;
            int32_t s0 = src_mono[idx];
            int32_t s1 = src_mono[idx + 1];
            int16_t out = (int16_t)(s0 + (int32_t)(frac * (float)(s1 - s0)));
            s_ring_buf[wr & RING_MASK] = out;
            wr++;
            s_resample_pos += s_resample_step;
            s_samples_decoded++;
        }
        /* Carry fractional position into next frame.
         * Subtract consumed source samples (count-1 because we need
         * idx+1 look-ahead). Only subtract what we actually consumed. */
        s_resample_pos -= limit;
        /* Clamp: if ring was full before we consumed all source samples,
         * s_resample_pos will be negative — this means we still have
         * unconsumed source data, but it's already decoded and gone.
         * Accept the small loss (< 1 sample) rather than corrupting state. */
        if (s_resample_pos < 0.0f) s_resample_pos = 0.0f;
    }

    __DMB();
    s_ring_wr = wr;
}

/*******************************************************************************
 * Internal: decode one frame, downmix stereo→mono, write to ring via resampler
 ******************************************************************************/
static void decode_and_write(int16_t *pcm_tmp, int samples,
                              const mp3dec_frame_info_t *fi)
{
    if (samples <= 0) return;

    /* Temporary mono buffer (stack) */
    int16_t mono[1152];
    if (fi->channels == 2) {
        for (int j = 0; j < samples; j++) {
            mono[j] = (int16_t)(((int32_t)pcm_tmp[j * 2] +
                                  (int32_t)pcm_tmp[j * 2 + 1]) / 2);
        }
    } else {
        for (int j = 0; j < samples; j++) {
            mono[j] = pcm_tmp[j];
        }
    }
    ring_write_resampled(mono, samples);
}

/*******************************************************************************
 * Internal: refill compressed data buffer from SD card
 ******************************************************************************/
static bool refill_read_buf(void)
{
    if (!s_file_open) return false;

    /* Move unconsumed data to front of buffer */
    uint32_t remaining = s_read_buf_len - s_read_buf_pos;
    if (remaining > 0 && s_read_buf_pos > 0) {
        memmove(s_read_buf, s_read_buf + s_read_buf_pos, remaining);
    }
    s_read_buf_len = remaining;
    s_read_buf_pos = 0;

    /* Fill rest of buffer from SD card */
    uint32_t space = BENTO_MP3_READ_BUF_SIZE - s_read_buf_len;
    if (space == 0) return true;

    UINT br = 0;
    FRESULT fr = f_read(&s_fp, s_read_buf + s_read_buf_len, space, &br);
    if (fr != FR_OK) return false;

    s_read_buf_len += br;
    return (s_read_buf_len > 0);
}

/*******************************************************************************
 * Internal: scan first frame to get MP3 info
 ******************************************************************************/
static bool scan_first_frame(void)
{
    mp3dec_frame_info_t frame_info;
    int16_t pcm_tmp[MINIMP3_MAX_SAMPLES_PER_FRAME];

    int samples = mp3dec_decode_frame(&s_dec, s_read_buf, s_read_buf_len,
                                       pcm_tmp, &frame_info);

    if (frame_info.frame_bytes == 0) return false;

    s_info.channels     = frame_info.channels;
    s_info.sample_rate  = frame_info.hz;
    s_info.bitrate_kbps = frame_info.bitrate_kbps;
    s_info.layer        = frame_info.layer;

    /* Estimate duration from bitrate */
    if (frame_info.bitrate_kbps > 0) {
        s_info.duration_sec = (float)s_info.file_size /
                              (float)(frame_info.bitrate_kbps * 125);
    }

    /* Don't advance — re-init decoder so first frame is decoded again in feed */
    mp3dec_init(&s_dec);
    return true;
}

/*******************************************************************************
 * bento_mp3_play
 ******************************************************************************/
bool bento_mp3_play(const char *path)
{
    bento_mp3_stop();

    if (!bento_sdcard_open(path, &s_fp)) {
        s_state = MP3_STATE_ERROR;
        return false;
    }
    s_file_open = true;

    /* Get file size for duration estimate */
    s_info.file_size = f_size(&s_fp);

    /* Initialize decoder */
    mp3dec_init(&s_dec);

    /* Reset buffers */
    s_read_buf_len  = 0;
    s_read_buf_pos  = 0;
    s_ring_wr       = 0;
    s_ring_rd       = 0;
    s_bytes_decoded = 0;
    s_samples_decoded = 0;
    s_underrun_count = 0;
    memset(&s_info, 0, sizeof(s_info));

    /* Initial fill of read buffer */
    if (!refill_read_buf()) {
        bento_sdcard_close(&s_fp);
        s_file_open = false;
        s_state = MP3_STATE_ERROR;
        return false;
    }

    /* Get file size again (cleared by memset above) */
    s_info.file_size = f_size(&s_fp);

    /* Scan first frame for metadata */
    if (!scan_first_frame()) {
        bento_sdcard_close(&s_fp);
        s_file_open = false;
        s_state = MP3_STATE_ERROR;
        return false;
    }

    /* Configure resampler: source rate → 48000 Hz */
    s_resample_step = (s_info.sample_rate > 0)
        ? (float)s_info.sample_rate / 48000.0f : 1.0f;
    s_resample_pos = 0.0f;

    /* Seek back to start for actual playback */
    f_lseek(&s_fp, 0);
    s_read_buf_len = 0;
    s_read_buf_pos = 0;
    refill_read_buf();

    /* Pre-fill ring buffer with decoded frames */
    mp3dec_frame_info_t fi;
    int16_t pcm_tmp[MINIMP3_MAX_SAMPLES_PER_FRAME];

    for (int i = 0; i < 8; i++) {
        uint32_t avail = s_read_buf_len - s_read_buf_pos;
        if (avail == 0) { if (!refill_read_buf()) break; avail = s_read_buf_len - s_read_buf_pos; }
        if (avail == 0) break;

        int samples = mp3dec_decode_frame(&s_dec,
            s_read_buf + s_read_buf_pos, avail, pcm_tmp, &fi);

        if (fi.frame_bytes > 0) {
            s_read_buf_pos += fi.frame_bytes;
            s_bytes_decoded += fi.frame_bytes;
        } else {
            break;
        }

        decode_and_write(pcm_tmp, samples, &fi);
    }

    s_state = MP3_STATE_PLAYING;
    return true;
}

/*******************************************************************************
 * Pause / Resume / Stop
 ******************************************************************************/
void bento_mp3_pause(void)
{
    if (s_state == MP3_STATE_PLAYING) {
        s_state = MP3_STATE_PAUSED;
    }
}

void bento_mp3_resume(void)
{
    if (s_state == MP3_STATE_PAUSED) {
        s_state = MP3_STATE_PLAYING;
    }
}

void bento_mp3_stop(void)
{
    s_state = MP3_STATE_IDLE;
    if (s_file_open) {
        bento_sdcard_close(&s_fp);
        s_file_open = false;
    }
    s_ring_wr = 0;
    s_ring_rd = 0;
    s_read_buf_len = 0;
    s_read_buf_pos = 0;
    s_bytes_decoded = 0;
    s_samples_decoded = 0;
    s_resample_pos = 0.0f;
}

/*******************************************************************************
 * bento_mp3_feed — Call from LVGL timer (~10ms)
 *
 * Decode MP3 frames and fill ring buffer until full or no more data.
 ******************************************************************************/
void bento_mp3_feed(void)
{
    if (s_state != MP3_STATE_PLAYING) return;

    /* Decode up to 12 frames per feed call to stay ahead of ISR.
     * ISR consumes 48000 samples/sec = 480 samples/10ms.
     * Each frame produces ~1152 source samples → ~1253 at 48kHz.
     * 12 frames × 1253 ≈ 15036 samples = ~313ms of audio. */
    for (int iter = 0; iter < 12; iter++) {
        /* Need space for one resampled frame (~1253 samples for 44.1→48k) */
        if (ring_free() < 1536) break;

        /* Ensure read buffer has data */
        if (s_read_buf_len - s_read_buf_pos < 2048) {
            refill_read_buf();
        }
        if (s_read_buf_pos >= s_read_buf_len) {
            if (!refill_read_buf()) {
                /* EOF — wait for ring buffer to drain, then stop */
                if (ring_available() == 0) {
                    s_state = MP3_STATE_IDLE;
                    if (s_file_open) {
                        bento_sdcard_close(&s_fp);
                        s_file_open = false;
                    }
                }
                return;
            }
        }

        /* Decode one frame */
        mp3dec_frame_info_t fi;
        int16_t pcm_tmp[MINIMP3_MAX_SAMPLES_PER_FRAME];
        uint32_t avail = s_read_buf_len - s_read_buf_pos;

        int samples = mp3dec_decode_frame(&s_dec,
            s_read_buf + s_read_buf_pos, avail, pcm_tmp, &fi);

        if (fi.frame_bytes > 0) {
            s_read_buf_pos += fi.frame_bytes;
            s_bytes_decoded += fi.frame_bytes;
        } else if (fi.frame_bytes == 0) {
            /* Need more data or corrupt — try refill */
            if (!refill_read_buf() || s_read_buf_len == 0) {
                if (ring_available() == 0) {
                    s_state = MP3_STATE_IDLE;
                    if (s_file_open) {
                        bento_sdcard_close(&s_fp);
                        s_file_open = false;
                    }
                }
                return;
            }
            continue;
        }

        decode_and_write(pcm_tmp, samples, &fi);
    }
}

/*******************************************************************************
 * ISR Interface: get one sample from ring buffer
 ******************************************************************************/
int16_t bento_mp3_get_sample(void)
{
    uint32_t rd = s_ring_rd;
    if (rd == s_ring_wr) {
        s_underrun_count++;
        return 0;
    }

    int16_t sample = s_ring_buf[rd & RING_MASK];
    __DMB();
    s_ring_rd = rd + 1;
    return sample;
}

/*******************************************************************************
 * Getters
 ******************************************************************************/
bento_mp3_state_t bento_mp3_get_state(void)
{
    return s_state;
}

float bento_mp3_get_position(void)
{
    if (s_info.sample_rate == 0) return 0.0f;
    return (float)s_samples_decoded / (float)s_info.sample_rate;
}

float bento_mp3_get_duration(void)
{
    return s_info.duration_sec;
}

const bento_mp3_info_t *bento_mp3_get_info(void)
{
    return &s_info;
}
