# BENTO Audio Subsystem — Developer Knowledge Base

> Complete reference for WAV, MIDI, MP3, and SoundFont playback on PSoC Edge CM55.
> Written from hard-won experience developing the BENTO Eva Kit audio pipeline.

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [Hardware: TDM + Codec](#2-hardware-tdm--codec)
3. [Core Audio Engine (bento_audio)](#3-core-audio-engine-bento_audio)
4. [WAV Playback (bento_wav)](#4-wav-playback-bento_wav)
5. [MIDI Playback (bento_midi)](#5-midi-playback-bento_midi)
6. [SoundFont Synthesis (bento_tsf)](#6-soundfont-synthesis-bento_tsf)
7. [MP3 Playback (bento_mp3)](#7-mp3-playback-bento_mp3)
8. [Ring Buffer Pattern](#8-ring-buffer-pattern)
9. [Memory Budget](#9-memory-budget)
10. [Timing Budget](#10-timing-budget)
11. [Thread Safety](#11-thread-safety)
12. [Lessons Learned & Gotchas](#12-lessons-learned--gotchas)
13. [Debugging Checklist](#13-debugging-checklist)

---

## 1. Architecture Overview

```
┌──────────────────────────────────────────────────────────┐
│                     LVGL GFX Task                        │
│                                                          │
│  page_audio_player.c                                     │
│  ┌──────────────┐   feed_timer_cb (10ms LVGL timer)      │
│  │ File Browser │──→ bento_wav_feed()                    │
│  │ Transport UI │──→ bento_midi_tick() + _feed()         │
│  │ Volume Slider│──→ bento_mp3_feed()                    │
│  └──────────────┘                                        │
│         │                                                │
│         ▼                                                │
│  ┌─────────────────────────────────────────────────┐     │
│  │              Ring Buffers (int16_t)             │     │
│  │  WAV: 32KB dual-buf  │ MP3: 8192  │ TSF: 16384  │     │
│  └─────────────────────────────────────────────────┘     │
└──────────────────────────────────────────────────────────┘
                          │
                          ▼ ISR reads samples
┌──────────────────────────────────────────────────────────┐
│               TDM TX ISR (48 kHz, priority 2)            │
│                                                          │
│  bento_audio.c :: i2s_tx_interrupt_handler()             │
│  ┌────────────────────┐                                  │
│  │ switch(audio_src): │                                  │
│  │  WAV  → wav_get()  │──→ TDM FIFO (64 entries)         │
│  │  MIDI → midi_get() │    ┌─────────────────┐           │
│  │  SF2  → tsf_get()  │──→ │ Cy_AudioTDM_    │           │
│  │  MP3  → mp3_get()  │    │ WriteTxData()   │           │
│  │  TONE → sine LUT   │    └────────┬────────┘           │ 
│  └────────────────────┘             │                    │
└─────────────────────────────────────│────────────────────┘
                                       ▼
                              TLV320DAC3100 Codec
                              (I2C addr 0x18)
                                       │
                                       ▼
                                   Speaker
```

### Key Design Decisions

1. **ISR never touches SD card** — all file I/O happens in task context via feed timers.
2. **Lock-free SPSC ring buffers** — single producer (feed task), single consumer (ISR).
  No mutexes, no disabling interrupts.
3. **Mono rendering, stereo output** — MIDI/MP3/TSF render mono samples; ISR duplicates
  L=R for the codec's stereo TDM interface.
4. **48 kHz unified rate** — all sources output at 48 kHz. MP3 resamples internally.

---

## 2. Hardware: TDM + Codec

### Clock Chain

```
HFCLK[1] = 400 MHz
  └─→ TDM_CLK_DIV (÷8 for 48 kHz, ÷24 for 16 kHz)
       └─→ MCLK = 6.144 MHz (48 kHz) or 2.048 MHz (16 kHz)
            └─→ TLV320DAC3100 NDAC × MDAC × DOSR
                 └─→ DAC_Fs = 48 kHz
```

### TLV320DAC3100 Codec


| Parameter      | Value                                |
| -------------- | ------------------------------------ |
| I2C Address    | 0x18                                 |
| I2C Bus        | Shared with display touch controller |
| Protection     | `bento_i2c_semaphore` (FreeRTOS)     |
| Volume Range   | 0–110 (0.5 dB steps)                 |
| Default Volume | 90                                   |


### TDM ISR


| Parameter     | Value                   |
| ------------- | ----------------------- |
| ISR Priority  | 2                       |
| FIFO Depth    | 64 entries              |
| ISR Frequency | ~750 Hz (48000 / 64)    |
| ISR Budget    | < 1.3 ms per invocation |


**Critical**: The ISR writes 64 samples per invocation (32 stereo pairs for mono sources).
Each mono sample is written twice (L + R):

```c
int16_t sample = bento_tsf_get_sample();
uint32_t s = (uint32_t)(uint16_t)sample;
Cy_AudioTDM_WriteTxData(TDM_STRUCT0_TX, s);   // Left
Cy_AudioTDM_WriteTxData(TDM_STRUCT0_TX, s);   // Right
```

---

## 3. Core Audio Engine (bento_audio)

### Source Enum

```c
typedef enum {
    BENTO_AUDIO_SRC_SILENCE = 0,   // Fill FIFO with zeros
    BENTO_AUDIO_SRC_TONE,          // 1 kHz sine test (phase accumulator)
    BENTO_AUDIO_SRC_WAV,           // PCM from SD card
    BENTO_AUDIO_SRC_MIDI,          // DDS wavetable synthesis
    BENTO_AUDIO_SRC_MIDI_SF2,      // TinySoundFont ring buffer
    BENTO_AUDIO_SRC_MP3,           // minimp3 ring buffer
} bento_audio_src_t;
```

### Initialization Sequence (MUST follow this order)

```c
bento_audio_init();                              // 1. TDM hardware + MCLK
bento_audio_codec_init(BENTO_AUDIO_RATE_48KHZ);  // 2. Codec via I2C
bento_audio_start();                             // 3. Enable TX + ISR
```

### Public API

```c
void bento_audio_init(void);
bool bento_audio_codec_init(uint32_t sample_rate_hz);
void bento_audio_start(void);
void bento_audio_stop(void);
void bento_audio_deinit(void);
void bento_audio_test_tone(void);               // 1 kHz, 2 sec, blocking
void bento_audio_play_tone(bool on);            // Non-blocking tone
void bento_audio_set_volume(uint8_t vol);       // 0-110
bool bento_audio_is_active(void);
void bento_audio_set_source(bento_audio_src_t src);
bento_audio_src_t bento_audio_get_source(void);
```

### Key Constants

```c
#define BENTO_AUDIO_HW_FIFO_SIZE   64u       // TDM FIFO depth
#define BENTO_AUDIO_RATE_48KHZ     48000u
#define BENTO_AUDIO_RATE_16KHZ     16000u
#define BENTO_AUDIO_VOLUME_DEFAULT 90
#define BENTO_AUDIO_VOLUME_MAX     110
#define BENTO_AUDIO_ISR_PRIORITY   2
#define BENTO_AUDIO_CLK_DIV_48KHZ  8
#define BENTO_AUDIO_CLK_DIV_16KHZ  24
#define BENTO_AUDIO_MCLK_48KHZ_HZ 6144000
#define BENTO_AUDIO_MCLK_16KHZ_HZ 2048000
```

---

## 4. WAV Playback (bento_wav)

### Architecture: Double Buffer

```
SD Card ──f_read()──→ [Buffer A 16KB] ←──ISR reads──→ Speaker
                      [Buffer B 16KB] ←─── swap when A exhausted
```

- 32 KB total (2 x 16 KB)
- ~170 ms of audio at 48 kHz stereo
- Feed task refills the inactive buffer while ISR consumes the active one

### API

```c
bool bento_wav_play(const char *path);      // Parse header + start
void bento_wav_pause(void);
void bento_wav_resume(void);
void bento_wav_stop(void);
void bento_wav_feed(void);                  // Refill buffer from SD card
int16_t bento_wav_get_sample(void);         // ISR: next sample

bento_wav_state_t bento_wav_get_state(void);
float bento_wav_get_position(void);         // Seconds
float bento_wav_get_duration(void);         // Seconds
const bento_wav_info_t *bento_wav_get_info(void);
```

### WAV Info Structure

```c
typedef struct {
    uint16_t num_channels;     // 1=mono, 2=stereo
    uint32_t sample_rate;      // 8k, 16k, 22.05k, 44.1k, 48k Hz
    uint16_t bits_per_sample;  // typically 16
    uint32_t data_offset;      // byte offset to PCM data chunk
    uint32_t data_size;        // total PCM bytes
} bento_wav_info_t;
```

### WAV Header Parsing

Supports standard RIFF WAV with:

- 16-bit PCM only (`bits_per_sample == 16`)
- Mono or stereo
- Any sample rate (8k-48k)
- Handles `fmt` + `data` chunks, skips others

### Feed Timing

Call `bento_wav_feed()` from LVGL timer every 10 ms.
At 48 kHz stereo (192 KB/s), each 16 KB buffer lasts ~85 ms.
Safe margin: ~75 ms between feeds before underrun.

---

## 5. MIDI Playback (bento_midi)

### Architecture: Event Array + Voice Pool

```
Parse Phase (one-time):
  .mid file ──→ midi_event_t[4096] sorted by absolute time (ms)

Playback Phase (real-time):
  feed_timer_cb() ──→ bento_midi_tick(elapsed_ms)
                  ──→ bento_midi_feed()
                       ├─→ dispatch events up to current time
                       ├─→ Note On/Off → TSF or DDS voices
                       └─→ bento_tsf_render() x N (fill ring buffer)
```

### API

```c
bool bento_midi_play(const char *path);
void bento_midi_pause(void);
void bento_midi_resume(void);
void bento_midi_stop(void);
void bento_midi_feed(void);                 // Dispatch events + render
void bento_midi_tick(uint32_t elapsed_ms);  // Advance clock

// SoundFont integration
bool bento_midi_load_sf2(const char *path);
void bento_midi_unload_sf2(void);
bool bento_midi_is_sf2_loaded(void);

// DDS waveform (when no SF2)
void bento_midi_set_waveform(bento_midi_wave_t wave);

// ISR (DDS mode only)
int16_t bento_midi_get_sample(void);

// Info
bento_midi_state_t bento_midi_get_state(void);
float bento_midi_get_position(void);
float bento_midi_get_duration(void);
const bento_midi_info_t *bento_midi_get_info(void);
```

### MIDI Event Types Handled


| Status    | Type           | Action                                         |
| --------- | -------------- | ---------------------------------------------- |
| 0x90      | Note On        | `tsf_channel_note_on()` or DDS `note_on()`     |
| 0x80      | Note Off       | `tsf_channel_note_off()` or DDS `note_off()`   |
| 0xC0      | Program Change | `tsf_channel_set_bank_preset()`                |
| 0xB0      | Control Change | `tsf_channel_midi_control()` (CC7, CC10, CC64) |
| 0xE0      | Pitch Bend     | `tsf_channel_set_pitchwheel()`                 |
| 0xFF 0x51 | Tempo          | Update ms_per_tick conversion                  |


### DDS Voice Pool (Fallback when no SF2)

- 16 voices max, oldest-voice stealing
- 256-sample wavetable (sine/square/triangle/sawtooth)
- Fixed-point phase accumulation: `phase_inc = (freq x 256 x 65536) / 48000`
- ADSR: Attack 10 ms, Decay 50 ms, Sustain 0.7, Release 100 ms

### Tempo Handling (Format 1 MIDI)

Track 0 is scanned for `0xFF 0x51` tempo events to build a tempo map.
Each entry: `(tick, microseconds_per_beat)`.
Conversion: `ms_per_tick = us_per_beat / (ticks_per_beat x 1000)`.

**Trick**: Real elapsed time via `lv_tick_get()` prevents slow playback
caused by LVGL timer jitter (~73 ms actual vs 10 ms requested).

### Pre-fill at Start

When SF2 is loaded, `bento_midi_play()` dispatches all t=0 events
and pre-fills 20 render chunks (200 ms) into the ring buffer before
returning. This prevents the initial ~73 ms silence gap.

```c
// Dispatch t=0 events first
while (s_current_event < s_num_events) {
    midi_event_t *ev = &s_events[s_current_event];
    if (ev->time_ms > 0) break;
    // dispatch Note On, Program Change, etc.
    s_current_event++;
}
// Pre-fill ring buffer
for (int i = 0; i < 20; i++) {
    if (bento_tsf_render() == 0) break;
}
```

---

## 6. SoundFont Synthesis (bento_tsf)

### What is TinySoundFont?

Single-header C99 library (`tsf.h`) that loads SF2 SoundFont files and
renders wavetable-based audio. Transforms MIDI events into realistic
instrument sounds using sampled waveforms from the SF2 file.

### Architecture: FatFS Stream -> TSF -> Ring Buffer

```
SF2 on SD Card
  |
  |-- fatfs_stream_read() ---> tsf_load() ---> s_tsf (in-memory)
  |                                            ~1 MB in secondary SRAM
  |
  |-- bento_tsf_channel_note_on/off()  --->  TSF voice state
  |
  +-- bento_tsf_render()
       +---> tsf_render_short(480 samples)
              +---> s_ring_buf[16384] ---> ISR: bento_tsf_get_sample()
```

### Ring Buffer Constants

```c
#define BENTO_TSF_RING_SIZE   16384    // ~341 ms at 48 kHz (power-of-2)
#define BENTO_TSF_CHUNK_SIZE  480      // 10 ms at 48 kHz per render call
```

### TSF Configuration

```c
// MUST be set before #include "tsf.h"
#define TSF_IMPLEMENTATION
#define TSF_NO_STDIO              // No stdio — use FatFS stream

// Float math (CM55 has FPU — avoid double promotion)
#define TSF_POW    powf
#define TSF_POWF   powf
#define TSF_EXPF   expf
#define TSF_LOG    logf
#define TSF_TAN    tanf
#define TSF_LOG10  log10f
#define TSF_SQRT   sqrtf
#define TSF_SQRTF  sqrtf

// After load:
tsf_set_output(s_tsf, TSF_MONO, 48000, -12.0f);  // Mono, 48kHz, -12 dB
tsf_set_max_voices(s_tsf, 32);                    // 32-voice polyphony
```

### Why -12 dB Gain?

TSF output is the sum of all active voices. At 0 dB, 4+ simultaneous
notes overflow int16 range -> harsh distortion/wobble. -12 dB (x0.25
linear) allows up to ~12 voices before clipping.

Formula: `linear = 10^(dB/20)`, so `-12 dB = 10^(-0.6) = 0.25`.

### GM Channel Setup (Auto on SF2 Load)

```
Channel 0-8, 10-15: Bank 0, Program 0 (Piano)
Channel 9:          Bank 128, Program 0 (Drum Kit)
```

Program Change events from the MIDI file override these defaults.

### FatFS Stream Adapter

TSF needs a `tsf_stream` with `read()` and `skip()` callbacks.
We wrap FatFS `f_read()` and `f_lseek()`:

```c
static int fatfs_stream_read(void *data, void *ptr, unsigned int size) {
    fatfs_ctx_t *ctx = (fatfs_ctx_t *)data;
    UINT br = 0;
    FRESULT fr = f_read(&ctx->fp, ptr, size, &br);
    return (fr == FR_OK) ? (int)br : -1;
}

static int fatfs_stream_skip(void *data, unsigned int count) {
    fatfs_ctx_t *ctx = (fatfs_ctx_t *)data;
    FSIZE_t cur = f_tell(&ctx->fp);
    return (f_lseek(&ctx->fp, cur + count) == FR_OK) ? 1 : 0;
}
```

### SF2 File Selection — CRITICAL

**You MUST use a General MIDI (GM) SoundFont** for MIDI playback.
A GM SoundFont contains all 128 instruments mapped to the correct
program numbers. Single-instrument SF2 files (e.g., "Boston_Brass.sf2")
will produce wrong sounds on every channel.

Candidate priority (auto-loaded at first MIDI play):

1. `Creative_1mgm.sf2` — 1.0 MB, Sound Blaster full GM (best quality)
2. `DroidMobioteBank.sf2` — 337 KB, Android GM bank
3. `Nokia_6230i.sf2` — 222 KB, Nokia GM
4. `GXSCC_gm_033.sf2` — 126 KB, 8-bit chiptune GM (smallest)

### Public API

```c
bool bento_tsf_load_sf2(const char *path);
void bento_tsf_unload(void);
bool bento_tsf_is_loaded(void);

void bento_tsf_channel_set_bank_preset(uint8_t channel, int bank, int preset_number);
void bento_tsf_channel_note_on(uint8_t channel, uint8_t key, uint8_t velocity);
void bento_tsf_channel_note_off(uint8_t channel, uint8_t key);
void bento_tsf_channel_midi_control(uint8_t channel, uint8_t controller, uint8_t value);
void bento_tsf_channel_set_pitchwheel(uint8_t channel, int pitch_wheel);
void bento_tsf_all_notes_off(void);

uint32_t bento_tsf_render(void);             // Pre-render chunk into ring buffer
int16_t  bento_tsf_get_sample(void);         // ISR: read from ring buffer

// Diagnostics
uint32_t    bento_tsf_get_underrun_count(void);
int         bento_tsf_get_load_error(void);  // 0=ok, 1=file, 2=parse, 3=voice
uint32_t    bento_tsf_ring_available(void);
uint32_t    bento_tsf_get_file_size(void);
uint32_t    bento_tsf_get_heap_free(void);
const char *bento_tsf_get_sf2_name(void);
```

---

## 7. MP3 Playback (bento_mp3)

### Architecture: minimp3 + Resampling + Ring Buffer

```
SD Card --f_read(8KB)--> [read_buf 8KB]
                              |
                          minimp3_decode_frame()
                              |
                          resample to 48 kHz (linear interpolation)
                              |
                         +----v----+
                         |Ring 8192|---> ISR: bento_mp3_get_sample()
                         +---------+
```

### API

```c
bool bento_mp3_play(const char *path);
void bento_mp3_pause(void);
void bento_mp3_resume(void);
void bento_mp3_stop(void);
void bento_mp3_feed(void);              // Decode + fill ring
int16_t bento_mp3_get_sample(void);     // ISR

bento_mp3_state_t bento_mp3_get_state(void);
float bento_mp3_get_position(void);
float bento_mp3_get_duration(void);     // Estimated from bitrate
const bento_mp3_info_t *bento_mp3_get_info(void);
```

### Resampling

minimp3 outputs at the MP3's native rate (commonly 44.1 kHz).
Linear interpolation resamples to 48 kHz:

```c
float step = source_rate / 48000.0f;
// For each output sample:
int idx = (int)pos;
float frac = pos - idx;
out = src[idx] + frac * (src[idx+1] - src[idx]);
pos += step;
```

Fractional position carried across frame boundaries to prevent clicks.

### Constants

```c
#define BENTO_MP3_RING_SIZE      8192   // ~170 ms at 48 kHz
#define BENTO_MP3_READ_BUF_SIZE  8192   // Holds 5-6 MP3 frames at 320 kbps
```

### Feed Rate

Decodes up to 12 frames per 10 ms feed call.
Each frame ~~1152 samples (~~26 ms at 44.1 kHz).
Pre-buffers ~313 ms of audio.

---

## 8. Ring Buffer Pattern

All streaming sources (MP3, TSF) use the same SPSC lock-free ring buffer:

```c
static int16_t          s_ring_buf[RING_SIZE];
static volatile uint32_t s_ring_wr = 0;    // Task writes
static volatile uint32_t s_ring_rd = 0;    // ISR reads

#define RING_MASK  (RING_SIZE - 1)          // Power-of-2 required!

// Available samples
static inline uint32_t ring_available(void) {
    return (s_ring_wr - s_ring_rd) & RING_MASK;
}

// Free space (one slot reserved to distinguish full from empty)
static inline uint32_t ring_free(void) {
    return RING_SIZE - 1 - ring_available();
}
```

### Producer (Task Context)

```c
for (uint32_t i = 0; i < chunk_size; i++) {
    s_ring_buf[wr & RING_MASK] = tmp[i];
    wr++;
}
__DMB();                    // ARM Data Memory Barrier
s_ring_wr = wr;            // Publish new write position
```

### Consumer (ISR Context)

```c
if (rd == wr) {
    s_underrun_count++;     // Glitch indicator
    return 0;               // Silence on underrun
}
__DMB();                    // See producer's data
int16_t sample = s_ring_buf[rd & RING_MASK];
s_ring_rd = rd + 1;
return sample;
```

### Why `__DMB()`?

ARM Cortex-M55 can reorder memory accesses. Without `__DMB()`:

- Producer: CPU might publish `s_ring_wr` before the sample data is written
-> ISR reads stale/garbage data
- Consumer: CPU might read `s_ring_buf[]` before seeing the updated `s_ring_wr`
-> ISR reads old data

`__DMB()` enforces that all prior stores complete before subsequent loads.

---

## 9. Memory Budget


| Component        | Size                               | Region         | Notes                 |
| ---------------- | ---------------------------------- | -------------- | --------------------- |
| WAV dual buffer  | 32 KB                              | Internal SRAM  | 2 x 16 KB PCM buffers |
| MP3 ring buffer  | 16 KB                              | Internal SRAM  | 8192 x int16_t        |
| MP3 read buffer  | 8 KB                               | Internal SRAM  | Raw MP3 data          |
| TSF ring buffer  | 32 KB                              | Internal SRAM  | 16384 x int16_t       |
| TSF library code | ~30 KB                             | Flash          | Compiled tsf.h        |
| SF2 sample data  | ~1 MB                              | Secondary SRAM | Creative_1mgm.sf2     |
| TSF voice state  | ~2 KB                              | Secondary SRAM | 32 voices             |
| MIDI event array | ~32 KB                             | Internal SRAM  | 4096 x midi_event_t   |
| DDS wavetable    | 2 KB                               | Flash          | 4 x 256 x int16_t     |
| minimp3 state    | ~5 KB                              | Stack          | Decoder context       |
| **Total**        | ~125 KB internal + ~1 MB secondary |                |                       |


Secondary SRAM (1.5 MB) is mapped via `malloc()` -> `heap_3` (FreeRTOS).
Ring buffers live in internal SRAM for fast ISR access.

---

## 10. Timing Budget


| Operation               | Context | Duration    | Budget | Status |
| ----------------------- | ------- | ----------- | ------ | ------ |
| ISR: 64 FIFO writes     | ISR     | ~50-100 us  | 1.3 ms | OK     |
| WAV feed: f_read 16KB   | Task    | ~2-5 ms     | 10 ms  | OK     |
| MP3 decode: 12 frames   | Task    | ~3-8 ms     | 10 ms  | OK     |
| TSF render: 480 samples | Task    | ~200-500 us | 10 ms  | OK     |
| SF2 load (one-time)     | Task    | ~200-500 ms | 2 s    | OK     |
| MIDI parse (one-time)   | Task    | ~10-50 ms   | 500 ms | OK     |


**The real constraint**: LVGL timer fires every ~73 ms (not 10 ms) because
`vg_lite_finish()` GPU drain blocks the GFX task. All ring buffer sizes
account for this worst-case feed interval.

---

## 11. Thread Safety

### What's Safe from ISR


| Function                   | Safe?  | Why                                 |
| -------------------------- | ------ | ----------------------------------- |
| `bento_wav_get_sample()`   | Yes    | Array read + pointer increment      |
| `bento_midi_get_sample()`  | Yes    | Wavetable lookup + fixed-point math |
| `bento_tsf_get_sample()`   | Yes    | Ring buffer read + `__DMB()`        |
| `bento_mp3_get_sample()`   | Yes    | Ring buffer read + `__DMB()`        |
| `bento_audio_set_volume()` | **NO** | I2C transaction                     |
| `bento_tsf_render()`       | **NO** | TSF internal state + malloc         |
| `bento_wav_feed()`         | **NO** | SD card file I/O                    |
| Any FatFS function         | **NO** | SD card hardware access             |


### I2C Bus Sharing

The TLV320DAC3100 codec shares I2C with the display touch controller.
Protected by `bento_i2c_semaphore` (FreeRTOS binary semaphore).

```c
xSemaphoreTake(bento_i2c_semaphore, portMAX_DELAY);
mtb_tlv320dac3100_adjust_speaker_output_volume(vol);
xSemaphoreGive(bento_i2c_semaphore);
```

---

## 12. Lessons Learned & Gotchas

### ISSUE: MIDI sounds "off-pitch" or "wobbly"

**Root Cause**: Using a single-instrument SF2 file instead of a full
General MIDI SF2. When a MIDI file requests program 0 (Piano) on
channel 0, a trumpet-only SF2 maps everything to trumpet samples
at wrong pitches.

**Fix**: Always use a GM SoundFont with all 128 instruments.
`Creative_1mgm.sf2` (1.0 MB) is the recommended choice.

### ISSUE: Pre-fill renders silence

**Root Cause**: `bento_midi_feed(elapsed=0)` dispatches no events.
If a MIDI file has Note On events at t=0, they never reach TSF
before pre-fill, so `tsf_render_short()` outputs zeros.

**Fix**: Explicitly dispatch all t=0 events before calling
`bento_tsf_render()` in the pre-fill loop.

### ISSUE: MIDI plays too slowly

**Root Cause**: LVGL timer claims 10 ms interval but actually fires
every ~73 ms. If `bento_midi_tick()` always receives 10 ms, the MIDI
clock runs 7x too slow.

**Fix**: Use actual elapsed time via `lv_tick_get()` delta:

```c
uint32_t now = lv_tick_get();
uint32_t elapsed = now - s_midi_last_tick;
if (elapsed > 250) elapsed = 250;   // Clamp to prevent jumps
bento_midi_tick(elapsed);
s_midi_last_tick = now;
```

### ISSUE: Audio clicks/pops during playback

**Root Cause**: Ring buffer underrun. The ISR consumed all samples
before the feed task could refill.

**Diagnosis**: Check `bento_tsf_get_underrun_count()` — if non-zero,
ring is too small or feed interval too long.

**Fix**: Increase ring buffer size (current 16384 = 341 ms margin).
Or call `bento_tsf_render()` multiple times per feed to pre-fill more.

### ISSUE: TSF uses `double` math (slow on CM55)

**Root Cause**: `tsf.h` internally uses `pow()`, `exp()`, `log()` etc.
which default to `double` on ARM GCC.

**Fix**: Override with `float` versions via `#define` before including
`tsf.h`. The CM55 has a single-precision FPU; double emulation is 10x
slower.

### ISSUE: Compiler warnings from tsf.h

**Fix**: Wrap the include with pragmas:

```c
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#include "tsf.h"
#pragma GCC diagnostic pop
```

### ISSUE: TSF clipping with many voices

At 0 dB, 4+ simultaneous MIDI notes overflow int16 -> harsh distortion.

**Fix**: Set TSF gain to -6 dB: `tsf_set_output(tsf, TSF_MONO, 48000, -6.0f)`.
This is a true dB value (not linear). -6 dB (x0.5) allows up to ~6 voices
before clipping while keeping MIDI volume comparable to WAV/MP3 sources.

### ISSUE: Volume slider barely audible below 50%

**Root Cause**: The TLV320DAC3100 DAC volume register is signed 8-bit with
0.5 dB/step. The codec function does `volume | 0x80`, and a linear slider
0-110 maps to -64 dB to -9 dB — far too wide and not matching the init
level (-1.5 dB at 0xFD). Below 50% the attenuation is too severe.

**Fix**: Square-root volume curve in `bento_audio_set_volume()`:

```c
// scaled = sqrt(vol * 142), Newton's method integer sqrt
uint32_t n = (uint32_t)vol * 142u;
uint32_t x = 64u;
x = (x + n / x) >> 1;  // 3 iterations
x = (x + n / x) >> 1;
x = (x + n / x) >> 1;
scaled = (x > 125u) ? 125u : (uint8_t)x;
```

Result:
- Slider 0% -> mute
- Slider 30% -> -30 dB (audible)
- Slider 50% -> -20 dB (clear)
- Slider 100% -> -1.5 dB (very loud, matches codec init)

The square-root curve compresses the loud end and expands the quiet end,
matching human logarithmic perception of loudness.

### ISSUE: Volume drops when slider first touched

**Root Cause**: Codec init writes 0xFD (-1.5 dB) directly, but the slider
default (90) produces a different register value. Until the user touches
the slider, the codec stays at init level. First touch causes a volume jump.

**Fix**: The square-root curve maps slider 82% (default 90) to register
0xF1 (-7.5 dB), which is much closer to the init level (-1.5 dB) than
the old linear mapping (which gave -19 dB at the same slider position).

### ISSUE: SF2 fails to load (error code 2)

**Root Cause**: Heap exhausted. SF2 sample data allocated via `malloc()`
-> secondary SRAM (1.5 MB). If SF2 > available heap, `tsf_load()` returns
NULL.

**Diagnosis**: `bento_tsf_get_heap_free()` shows available heap at load time.
`bento_tsf_get_file_size()` shows SF2 file size.

**Fix**: Use smaller SF2 files (< 1 MB). Creative_1mgm.sf2 at 1.0 MB is
near the limit.

---

## 13. Debugging Checklist

### No audio output at all

1. Is `bento_audio_init()` + `bento_audio_codec_init()` + `bento_audio_start()` called?
2. Is audio source set? Check `bento_audio_get_source() != SILENCE`
3. Is volume > 0? Check `bento_audio_set_volume()` value
4. Is codec powered? Check I2C communication with 0x18
5. Is TDM clock running? Check MCLK on scope (P5_0 or similar)

### Audio plays but sounds wrong

1. **Wrong pitch**: Check SF2 file is GM. Check `bento_tsf_get_sf2_name()`
2. **Too slow**: Check `bento_midi_tick()` receives real elapsed time, not fixed 10 ms
3. **Clipping/distortion**: Check TSF gain is -12 dB. Check underrun count.
4. **Clicks/pops**: Check `bento_tsf_get_underrun_count()`. If > 0, ring underrunning.

### MIDI doesn't start

1. Is `.mid` file valid? Check `bento_midi_play()` return value
2. Is SF2 loaded? Check `bento_midi_is_sf2_loaded()` or `bento_tsf_is_loaded()`
3. Are SF2 files on SD card? Check `/midi/Creative_1mgm.sf2` exists
4. Did parse succeed? Check `bento_midi_get_info()->num_events > 0`

### Ring buffer diagnostics

```c
printf("TSF underruns: %u\n", bento_tsf_get_underrun_count());
printf("TSF ring fill: %u / %u\n", bento_tsf_ring_available(), BENTO_TSF_RING_SIZE);
printf("TSF load error: %d\n", bento_tsf_get_load_error());
printf("SF2: %s (%u bytes)\n", bento_tsf_get_sf2_name(), bento_tsf_get_file_size());
printf("Heap at load: %u bytes\n", bento_tsf_get_heap_free());
```

---

## File Map

```
modules/audio_player/
├── bento_audio.h/c      Core audio engine (TDM + codec + ISR dispatch)
├── bento_wav.h/c         WAV file streaming (double buffer)
├── bento_midi.h/c        MIDI parser + DDS synth + TSF dispatch
├── bento_tsf.h/c         TinySoundFont wrapper (SF2 -> ring buffer)
├── bento_mp3.h/c         MP3 decoder (minimp3 -> ring buffer)
├── tsf.h                 TinySoundFont library (single header, MIT)
├── minimp3.h             minimp3 library (single header, CC0)
└── README.md             This file

(the TLV320DAC3100 codec driver is not in this tree -- it is fetched from
 github.com/Infineon/audio-codec-tlv320dac3100, pinned by
 proj_cm55/deps/audio-codec-tlv320dac3100.mtb. See THIRD_PARTY.md.)

modules/page-components/audio_player/
├── page_audio_player.h/c UI: file browser + transport + volume
└── (page lifecycle: create/render/destroy)
```

