/*******************************************************************************
 * File Name: bento_tsf.c
 *
 * Description: TinySoundFont wrapper — FatFS stream adapter, lock-free SPSC
 *              ring buffer, and TSF render/control integration for BENTO
 *              Eva Kit audio subsystem.
 *
 *              Memory: SF2 sample data allocated via malloc() which maps to
 *              secondary SRAM (1.5 MB) via heap_3.  Ring buffer lives in
 *              internal SRAM for fast ISR access.
 *
 ******************************************************************************/

#include "bento_tsf.h"
#include "bento_sdcard.h"
#include "ff.h"
#include "cmsis_compiler.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>  /* malloc/free for heap probe */

/*******************************************************************************
 * TSF Configuration — MUST be before #include "tsf.h"
 ******************************************************************************/
#define TSF_IMPLEMENTATION
#define TSF_NO_STDIO

/* Use float versions of math functions (CM55 has FPU, avoid double) */
#define TSF_POW    powf
#define TSF_POWF   powf
#define TSF_EXPF   expf
#define TSF_LOG    logf
#define TSF_TAN    tanf
#define TSF_LOG10  log10f
#define TSF_SQRT   sqrtf
#define TSF_SQRTF  sqrtf

/* Suppress warnings from tsf.h */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#include "tsf.h"
#pragma GCC diagnostic pop

/*******************************************************************************
 * FatFS Stream Adapter for tsf_load()
 ******************************************************************************/
typedef struct {
    FIL  fp;
    bool valid;
} fatfs_ctx_t;

static int fatfs_stream_read(void *data, void *ptr, unsigned int size)
{
    fatfs_ctx_t *ctx = (fatfs_ctx_t *)data;
    UINT br = 0;
    FRESULT fr = f_read(&ctx->fp, ptr, size, &br);
    return (fr == FR_OK) ? (int)br : -1;
}

static int fatfs_stream_skip(void *data, unsigned int count)
{
    fatfs_ctx_t *ctx = (fatfs_ctx_t *)data;
    FSIZE_t cur = f_tell(&ctx->fp);
    return (f_lseek(&ctx->fp, cur + count) == FR_OK) ? 1 : 0;
}

/*******************************************************************************
 * Module State
 ******************************************************************************/
static tsf *s_tsf = NULL;

/* SPSC lock-free ring buffer */
static int16_t          s_ring_buf[BENTO_TSF_RING_SIZE];
static volatile uint32_t s_ring_wr = 0;   /* written by render (task) */
static volatile uint32_t s_ring_rd = 0;   /* read by ISR */

#define RING_MASK  (BENTO_TSF_RING_SIZE - 1)

/* Diagnostics */
static volatile uint32_t s_underrun_count = 0;
static int s_load_error = 0;  /* 0=ok, 1=file open fail, 2=tsf parse fail, 3=voices fail */
static uint32_t s_file_size = 0;     /* SF2 file size for diagnostics */
static uint32_t s_heap_free = 0;     /* Free heap before tsf_load for diagnostics */
static char s_sf2_name[32] = {0};    /* Basename of loaded SF2 for diagnostics */

/*******************************************************************************
 * Ring buffer helpers
 ******************************************************************************/
static inline uint32_t ring_available(void)
{
    return (s_ring_wr - s_ring_rd) & RING_MASK;
}

static inline uint32_t ring_free(void)
{
    /* One slot reserved to distinguish full from empty */
    return BENTO_TSF_RING_SIZE - 1 - ring_available();
}

/*******************************************************************************
 * SF2 Load / Unload
 ******************************************************************************/
bool bento_tsf_load_sf2(const char *path)
{
    /* Unload previous SF2 if any */
    bento_tsf_unload();
    s_load_error = 0;

    /* Save basename for diagnostics */
    const char *bn = strrchr(path, '/');
    bn = bn ? bn + 1 : path;
    strncpy(s_sf2_name, bn, sizeof(s_sf2_name) - 1);
    s_sf2_name[sizeof(s_sf2_name) - 1] = '\0';

    static fatfs_ctx_t s_fatfs_ctx;
    memset(&s_fatfs_ctx, 0, sizeof(s_fatfs_ctx));

    if (!bento_sdcard_open(path, &s_fatfs_ctx.fp)) {
        s_load_error = 1;  /* file open failed */
        return false;
    }

    s_fatfs_ctx.valid = true;

    /* Diagnostics: record file size */
    s_file_size = f_size(&s_fatfs_ctx.fp);
    /* Probe available heap with progressively smaller malloc attempts */
    s_heap_free = 0;
    for (uint32_t probe = 1024*1024; probe >= 64*1024; probe /= 2) {
        void *p = malloc(probe);
        if (p) { free(p); s_heap_free = probe; break; }
    }

    struct tsf_stream stream;
    stream.data = &s_fatfs_ctx;
    stream.read = (int (*)(void *, void *, unsigned int))fatfs_stream_read;
    stream.skip = (int (*)(void *, unsigned int))fatfs_stream_skip;

    s_tsf = tsf_load(&stream);

    f_close(&s_fatfs_ctx.fp);
    s_fatfs_ctx.valid = false;

    if (!s_tsf) {
        s_load_error = 2;  /* TSF parse/malloc failed */
        return false;
    }

    /* Configure: mono, 48 kHz, -6 dB gain.
     * -6 dB (×0.5) balances volume vs clipping headroom.
     * At 0 dB, 4+ simultaneous notes clip int16 → harsh distortion.
     * -6 dB allows up to ~6 voices before clipping while keeping
     * volume comparable to WAV/MP3 sources at the same codec level. */
    tsf_set_output(s_tsf, TSF_MONO, 48000, -6.0f);

    /* Pre-allocate voice pool — 32 voices reduces voice stealing artifacts.
     * At 16 voices, busy passages trigger hard-kill stealing → clicks. */
    if (!tsf_set_max_voices(s_tsf, 32)) {
        s_load_error = 3;  /* voice alloc failed — continue anyway */
    }

    /* Set up default GM channel presets:
     * Ch 0-8, 10-15 = Piano (bank 0, program 0)
     * Ch 9 = Drum kit (bank 128, program 0) */
    for (int ch = 0; ch < 16; ch++) {
        if (ch == 9) {
            tsf_channel_set_bank_preset(s_tsf, ch, 128, 0);
        } else {
            tsf_channel_set_bank_preset(s_tsf, ch, 0, 0);
        }
    }

    /* Reset ring buffer */
    s_ring_wr = 0;
    s_ring_rd = 0;
    s_underrun_count = 0;

    return true;
}

void bento_tsf_unload(void)
{
    if (s_tsf) {
        tsf_close(s_tsf);
        s_tsf = NULL;
    }
    s_ring_wr = 0;
    s_ring_rd = 0;
}

bool bento_tsf_is_loaded(void)
{
    return (s_tsf != NULL);
}

/*******************************************************************************
 * MIDI Channel Control
 ******************************************************************************/
void bento_tsf_channel_set_bank_preset(uint8_t channel, int bank,
                                        int preset_number)
{
    if (!s_tsf || channel > 15) return;
    tsf_channel_set_bank_preset(s_tsf, channel, bank, preset_number);
}

void bento_tsf_channel_note_on(uint8_t channel, uint8_t key, uint8_t velocity)
{
    if (!s_tsf || channel > 15) return;
    float vel = (float)velocity / 127.0f;
    tsf_channel_note_on(s_tsf, channel, key, vel);
}

void bento_tsf_channel_note_off(uint8_t channel, uint8_t key)
{
    if (!s_tsf || channel > 15) return;
    tsf_channel_note_off(s_tsf, channel, key);
}

void bento_tsf_channel_midi_control(uint8_t channel, uint8_t controller,
                                     uint8_t value)
{
    if (!s_tsf || channel > 15) return;
    tsf_channel_midi_control(s_tsf, channel, controller, value);
}

void bento_tsf_channel_set_pitchwheel(uint8_t channel, int pitch_wheel)
{
    if (!s_tsf || channel > 15) return;
    tsf_channel_set_pitchwheel(s_tsf, channel, pitch_wheel);
}

void bento_tsf_all_notes_off(void)
{
    if (!s_tsf) return;
    tsf_note_off_all(s_tsf);

    /* Flush ring buffer */
    s_ring_wr = 0;
    s_ring_rd = 0;
}

/*******************************************************************************
 * Render (call from LVGL timer context, ~10ms)
 ******************************************************************************/
uint32_t bento_tsf_render(void)
{
    if (!s_tsf) return 0;

    uint32_t free_space = ring_free();
    if (free_space < BENTO_TSF_CHUNK_SIZE) return 0;

    /* Render into temporary stack buffer */
    int16_t tmp[BENTO_TSF_CHUNK_SIZE];
    tsf_render_short(s_tsf, tmp, BENTO_TSF_CHUNK_SIZE, 0);

    /* Copy to ring buffer */
    uint32_t wr = s_ring_wr;
    for (uint32_t i = 0; i < BENTO_TSF_CHUNK_SIZE; i++) {
        s_ring_buf[wr & RING_MASK] = tmp[i];
        wr++;
    }

    /* Memory barrier before publishing write pointer */
    __DMB();
    s_ring_wr = wr;

    return BENTO_TSF_CHUNK_SIZE;
}

/*******************************************************************************
 * ISR Interface: get one sample from ring buffer
 ******************************************************************************/
int16_t bento_tsf_get_sample(void)
{
    uint32_t rd = s_ring_rd;
    uint32_t wr = s_ring_wr;

    if (rd == wr) {
        /* Ring buffer empty — underrun */
        s_underrun_count++;
        return 0;
    }

    /* DMB after reading wr ensures we see the data the producer wrote
     * before it published s_ring_wr.  Without this barrier the CPU may
     * speculatively load stale s_ring_buf[] content. */
    __DMB();

    int16_t sample = s_ring_buf[rd & RING_MASK];
    s_ring_rd = rd + 1;
    return sample;
}

/*******************************************************************************
 * Diagnostics
 ******************************************************************************/
uint32_t bento_tsf_get_underrun_count(void)
{
    return s_underrun_count;
}

uint32_t bento_tsf_ring_available(void)
{
    return ring_available();
}

int bento_tsf_get_load_error(void)
{
    return s_load_error;
}

uint32_t bento_tsf_get_file_size(void)
{
    return s_file_size;
}

uint32_t bento_tsf_get_heap_free(void)
{
    return s_heap_free;
}

const char *bento_tsf_get_sf2_name(void)
{
    return s_sf2_name;
}
