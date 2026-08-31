/*******************************************************************************
 * File Name: bento_wav.c
 *
 * Description: WAV file parser + streaming playback.
 *              Double-buffered: feed task reads SD → buffer,
 *              ISR reads buffer → TX FIFO.
 *
 *******************************************************************************/

#include "bento_wav.h"
#include "bento_sdcard.h"
#include <string.h>

/*******************************************************************************
 * WAV Header Structures
 *******************************************************************************/
#pragma pack(push, 1)
typedef struct {
    char     riff_id[4];       /* "RIFF" */
    uint32_t file_size;
    char     wave_id[4];       /* "WAVE" */
} wav_riff_header_t;

typedef struct {
    char     chunk_id[4];
    uint32_t chunk_size;
} wav_chunk_header_t;

typedef struct {
    uint16_t audio_format;     /* 1 = PCM */
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
} wav_fmt_chunk_t;
#pragma pack(pop)

/*******************************************************************************
 * Double Buffer for ISR
 *******************************************************************************/
#define NUM_BUFS  2

static int16_t s_pcm_buf[NUM_BUFS][BENTO_WAV_BUF_SIZE / sizeof(int16_t)];
static volatile uint32_t s_buf_samples[NUM_BUFS];  /* Valid samples in each buf */
static volatile uint32_t s_read_idx;                /* ISR read position in current buf */
static volatile uint8_t  s_isr_buf;                 /* Buffer ISR is reading from */
static volatile uint8_t  s_fill_buf;                /* Buffer feed task is filling */
static volatile bool     s_buf_ready[NUM_BUFS];     /* true = buf has data for ISR */

/*******************************************************************************
 * Playback State
 *******************************************************************************/
static FIL             s_fp;
static bool            s_file_open = false;
static bento_wav_info_t s_info;
static bento_wav_state_t s_state = WAV_STATE_IDLE;
static uint32_t        s_bytes_played = 0;

/*******************************************************************************
 * bento_wav_parse_header
 *******************************************************************************/
bool bento_wav_parse_header(FIL *fp, bento_wav_info_t *info)
{
    wav_riff_header_t riff;
    UINT br;

    /* Read RIFF header */
    f_lseek(fp, 0);
    if (FR_OK != f_read(fp, &riff, sizeof(riff), &br) || br != sizeof(riff))
        return false;
    if (memcmp(riff.riff_id, "RIFF", 4) != 0 || memcmp(riff.wave_id, "WAVE", 4) != 0)
        return false;

    /* Search for fmt and data chunks */
    bool found_fmt = false;
    bool found_data = false;
    wav_chunk_header_t chunk;

    while (!found_data) {
        if (FR_OK != f_read(fp, &chunk, sizeof(chunk), &br) || br != sizeof(chunk))
            break;

        if (memcmp(chunk.chunk_id, "fmt ", 4) == 0) {
            wav_fmt_chunk_t fmt;
            if (FR_OK != f_read(fp, &fmt, sizeof(fmt), &br) || br < 16)
                return false;

            if (fmt.audio_format != 1) return false;  /* Only PCM */

            info->num_channels    = fmt.num_channels;
            info->sample_rate     = fmt.sample_rate;
            info->bits_per_sample = fmt.bits_per_sample;
            found_fmt = true;

            /* Skip rest of fmt chunk if larger than 16 bytes */
            if (chunk.chunk_size > sizeof(fmt)) {
                f_lseek(fp, f_tell(fp) + (chunk.chunk_size - sizeof(fmt)));
            }
        } else if (memcmp(chunk.chunk_id, "data", 4) == 0) {
            info->data_offset = (uint32_t)f_tell(fp);
            info->data_size   = chunk.chunk_size;
            found_data = true;
        } else {
            /* Skip unknown chunk */
            f_lseek(fp, f_tell(fp) + chunk.chunk_size);
            /* Align to 16-bit boundary */
            if (chunk.chunk_size & 1) {
                f_lseek(fp, f_tell(fp) + 1);
            }
        }
    }

    return found_fmt && found_data && (info->bits_per_sample == 16);
}

/*******************************************************************************
 * Internal: fill one buffer from SD card
 *******************************************************************************/
static bool fill_buffer(uint8_t buf_idx)
{
    if (!s_file_open) return false;

    uint32_t bytes_remaining = s_info.data_size - s_bytes_played;
    if (bytes_remaining == 0) return false;  /* EOF */

    uint32_t to_read = BENTO_WAV_BUF_SIZE;
    if (to_read > bytes_remaining) to_read = bytes_remaining;

    UINT br = 0;
    FRESULT fr = f_read(&s_fp, s_pcm_buf[buf_idx], to_read, &br);
    if (FR_OK != fr || br == 0) return false;

    s_buf_samples[buf_idx] = br / sizeof(int16_t);
    s_bytes_played += br;
    return true;
}

/*******************************************************************************
 * bento_wav_play
 *******************************************************************************/
bool bento_wav_play(const char *path)
{
    /* Stop any current playback */
    bento_wav_stop();

    /* Open file */
    if (!bento_sdcard_open(path, &s_fp)) {
        s_state = WAV_STATE_ERROR;
        return false;
    }
    s_file_open = true;

    /* Parse header */
    if (!bento_wav_parse_header(&s_fp, &s_info)) {
        bento_sdcard_close(&s_fp);
        s_file_open = false;
        s_state = WAV_STATE_ERROR;
        return false;
    }

    /* Seek to data start */
    f_lseek(&s_fp, s_info.data_offset);

    /* Reset buffers */
    s_bytes_played = 0;
    s_read_idx = 0;
    s_isr_buf = 0;
    s_fill_buf = 0;
    s_buf_ready[0] = false;
    s_buf_ready[1] = false;

    /* Pre-fill both buffers */
    if (fill_buffer(0)) {
        s_buf_ready[0] = true;
        s_fill_buf = 1;
    }
    if (fill_buffer(1)) {
        s_buf_ready[1] = true;
        s_fill_buf = 0;
    }

    s_state = WAV_STATE_PLAYING;
    return true;
}

/*******************************************************************************
 * bento_wav_pause / resume / stop
 *******************************************************************************/
void bento_wav_pause(void)
{
    if (s_state == WAV_STATE_PLAYING) {
        s_state = WAV_STATE_PAUSED;
    }
}

void bento_wav_resume(void)
{
    if (s_state == WAV_STATE_PAUSED) {
        s_state = WAV_STATE_PLAYING;
    }
}

void bento_wav_stop(void)
{
    s_state = WAV_STATE_IDLE;
    if (s_file_open) {
        bento_sdcard_close(&s_fp);
        s_file_open = false;
    }
    s_bytes_played = 0;
    s_buf_ready[0] = false;
    s_buf_ready[1] = false;
}

/*******************************************************************************
 * bento_wav_get_sample — Called from audio ISR
 *******************************************************************************/
int16_t bento_wav_get_sample(void)
{
    if (s_state != WAV_STATE_PLAYING) return 0;

    uint8_t cur = s_isr_buf;
    if (!s_buf_ready[cur]) return 0;  /* Buffer underrun */

    int16_t sample = s_pcm_buf[cur][s_read_idx++];

    if (s_read_idx >= s_buf_samples[cur]) {
        /* Done with this buffer — mark empty, switch to next */
        s_buf_ready[cur] = false;
        s_read_idx = 0;
        s_isr_buf = 1 - cur;

        /* If next buffer also empty, we'll return 0 next call */
    }

    return sample;
}

/*******************************************************************************
 * bento_wav_feed — Call from LVGL timer (~20ms) to refill buffers
 *******************************************************************************/
void bento_wav_feed(void)
{
    if (s_state != WAV_STATE_PLAYING) return;

    /* Fill whichever buffer the ISR isn't using */
    for (int i = 0; i < NUM_BUFS; i++) {
        if (!s_buf_ready[i]) {
            if (fill_buffer(i)) {
                s_buf_ready[i] = true;
            } else {
                /* EOF reached — check if other buffer also done */
                if (!s_buf_ready[1 - i]) {
                    s_state = WAV_STATE_IDLE;
                    if (s_file_open) {
                        bento_sdcard_close(&s_fp);
                        s_file_open = false;
                    }
                }
                break;
            }
        }
    }
}

/*******************************************************************************
 * Getters
 *******************************************************************************/
bento_wav_state_t bento_wav_get_state(void)
{
    return s_state;
}

float bento_wav_get_position(void)
{
    if (s_info.sample_rate == 0 || s_info.num_channels == 0) return 0.0f;
    uint32_t samples = s_bytes_played / (s_info.bits_per_sample / 8);
    return (float)samples / (float)(s_info.sample_rate * s_info.num_channels);
}

float bento_wav_get_duration(void)
{
    if (s_info.sample_rate == 0 || s_info.num_channels == 0) return 0.0f;
    uint32_t total_samples = s_info.data_size / (s_info.bits_per_sample / 8);
    return (float)total_samples / (float)(s_info.sample_rate * s_info.num_channels);
}

const bento_wav_info_t *bento_wav_get_info(void)
{
    return &s_info;
}
