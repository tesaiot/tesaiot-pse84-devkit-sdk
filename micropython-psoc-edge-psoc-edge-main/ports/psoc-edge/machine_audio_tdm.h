/*
 * machine_audio_tdm.h — MicroPython AudioTDM (I2S TX) HAL for PSoC Edge E84
 *
 * Provides machine.AudioTDM class for audio output via TDM0 TX.
 * Uses FIFO-based interrupt-driven transfer (no DMA required for Phase 1).
 *
 * Hardware: TDM0 TX on PSoC Edge E84
 *   P12.3 = TX_FSYNC (frame sync / LRCK)
 *   P21.1 = TX_SD    (serial data)
 *   P21.2 = TX_SCK   (serial clock / bit clock)
 *   P21.3 = TX_MCK   (master clock)
 *
 * Python API:
 *   from machine import AudioTDM
 *   tdm = AudioTDM(rate=48000, bits=16, channels=2)
 *   tdm.write(pcm_buffer)
 *   tdm.volume(75)
 *   tdm.deinit()
 */

#ifndef MICROPY_INCLUDED_PSOC_EDGE_MACHINE_AUDIO_TDM_H
#define MICROPY_INCLUDED_PSOC_EDGE_MACHINE_AUDIO_TDM_H

#include "py/obj.h"
#include "py/ringbuf.h"

/* ── Configuration ────────────────────────────────────────────────── */

#define AUDIO_TDM_MAX_INSTANCES         1       /* Only TDM0 TX available */

/* Ring buffer size: ~50 ms at 48 kHz stereo 16-bit = 9600 bytes.
 * Must be power of 2 for efficient ringbuf modulo operations. */
#define AUDIO_TDM_RING_BUF_SIZE         (16384) /* 16 KB — ~85 ms at 48k stereo */

/* TX FIFO configuration */
#define AUDIO_TDM_HW_FIFO_DEPTH         128     /* Hardware FIFO depth (words) */
#define AUDIO_TDM_FIFO_TRIGGER_LEVEL    63      /* Interrupt when FIFO < this */
#define AUDIO_TDM_FIFO_REFILL_COUNT     64      /* Words to write per ISR */

/* Interrupt priority (same as PDM_PCM = 2) */
#define AUDIO_TDM_ISR_PRIORITY          2

/* Supported sample rates */
#define AUDIO_TDM_RATE_16000            16000
#define AUDIO_TDM_RATE_22050            22050
#define AUDIO_TDM_RATE_44100            44100
#define AUDIO_TDM_RATE_48000            48000

/* Clock dividers for peripheral clock (from PLL ~400 MHz path)
 * These are the fractional divider integer parts.
 * Actual divider = value - 1 when passed to Cy_SysClk_PeriPclkSetFracDivider.
 *
 * For 48 kHz family (base clock from PLL):
 *   48 kHz: divider = 8  → TDM input ~6.144 MHz
 *   16 kHz: divider = 24 → TDM input ~2.048 MHz
 *
 * For 44.1 kHz family (requires DPLL_LP_1 reconfiguration — Phase 2):
 *   44.1 kHz: TBD
 *   22.05 kHz: TBD
 */
#define AUDIO_TDM_CLK_DIV_48KHZ        8
#define AUDIO_TDM_CLK_DIV_16KHZ        24

/* Time to wait after changing clock divider (ms) */
#define AUDIO_TDM_CLK_CHANGE_WAIT_MS   50

/* ── Data types ───────────────────────────────────────────────────── */

typedef enum {
    AUDIO_TDM_STATE_RESET = 0,      /* Not initialized */
    AUDIO_TDM_STATE_INIT,           /* Initialized but not active */
    AUDIO_TDM_STATE_ACTIVE,         /* TX active, interrupt running */
    AUDIO_TDM_STATE_PAUSED,         /* TX paused (muted) */
} audio_tdm_state_t;

typedef enum {
    AUDIO_TDM_IO_BLOCKING = 0,
    AUDIO_TDM_IO_NON_BLOCKING,
} audio_tdm_io_mode_t;

/* ── Instance object ──────────────────────────────────────────────── */

typedef struct _machine_audio_tdm_obj_t {
    mp_obj_base_t base;

    /* Identification */
    uint8_t tdm_id;

    /* Configuration (set at construction) */
    uint32_t sample_rate;           /* 16000, 48000, etc. */
    uint8_t bits;                   /* 16 (Phase 1 only) */
    uint8_t channels;               /* 1=mono, 2=stereo */

    /* Volume (0-100, applied in ISR as right-shift scaling) */
    uint8_t volume_pct;
    /* Pre-computed volume scale: 0..256 (fixed-point 8.8) */
    uint16_t volume_scale;

    /* I/O mode */
    audio_tdm_io_mode_t io_mode;
    mp_obj_t callback_for_non_blocking;

    /* Ring buffer: app write() → ISR reads → TX FIFO */
    ringbuf_t ring_buffer;
    uint8_t ring_buffer_storage[AUDIO_TDM_RING_BUF_SIZE];

    /* State tracking */
    audio_tdm_state_t state;

    /* Stats (for debugging) */
    uint32_t underflow_count;
    uint32_t total_samples_written;

} machine_audio_tdm_obj_t;

/* ── Extern declarations ──────────────────────────────────────────── */

extern const mp_obj_type_t machine_audio_tdm_type;

/* Called from main.c during soft reset to stop ISR before GC sweep */
void machine_audio_tdm_deinit_all(void);

#endif /* MICROPY_INCLUDED_PSOC_EDGE_MACHINE_AUDIO_TDM_H */
