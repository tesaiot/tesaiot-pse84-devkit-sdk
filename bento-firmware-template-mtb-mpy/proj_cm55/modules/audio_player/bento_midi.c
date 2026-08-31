/*******************************************************************************
 * File Name: bento_midi.c
 *
 * Description: MIDI file parser + 16-voice DDS tone synthesizer.
 *              Reads Standard MIDI Files from SD Card, dispatches Note On/Off
 *              events in real-time, synthesizes audio via wavetable DDS.
 *
 *              MIDI Event Processing:
 *                - All events loaded into RAM array at parse time
 *                - Sorted by absolute time (milliseconds)
 *                - feed() advances current_event pointer by elapsed time
 *                - Note On → allocate voice, Note Off → release envelope
 *
 *              Audio Synthesis (ISR context):
 *                - 16 voices mixed per sample
 *                - Fixed-point phase accumulator (32-bit)
 *                - 256-sample wavetable lookup
 *                - ADSR envelope per voice
 *
 *******************************************************************************/

#include "bento_midi.h"
#include "bento_tsf.h"
#include "bento_sdcard.h"
#include <string.h>
#include <math.h>

/*******************************************************************************
 * Wavetable (generated once at init)
 *******************************************************************************/
static int16_t s_wavetable[MIDI_WAVE_COUNT][BENTO_MIDI_WAVETABLE_LEN];

static void generate_wavetables(void)
{
    for (int i = 0; i < BENTO_MIDI_WAVETABLE_LEN; i++) {
        float phase = (float)i / (float)BENTO_MIDI_WAVETABLE_LEN;

        /* Sine */
        s_wavetable[MIDI_WAVE_SINE][i] =
            (int16_t)(16000.0f * sinf(2.0f * 3.14159265f * phase));

        /* Square */
        s_wavetable[MIDI_WAVE_SQUARE][i] =
            (int16_t)(phase < 0.5f ? 12000 : -12000);

        /* Triangle */
        s_wavetable[MIDI_WAVE_TRIANGLE][i] =
            (int16_t)(16000.0f * (phase < 0.5f
                ? (4.0f * phase - 1.0f)
                : (3.0f - 4.0f * phase)));

        /* Sawtooth */
        s_wavetable[MIDI_WAVE_SAWTOOTH][i] =
            (int16_t)(16000.0f * (2.0f * phase - 1.0f));
    }
}

/*******************************************************************************
 * MIDI Note → Frequency table (128 notes, fixed-point phase increment)
 *
 * phase_inc = (freq * WAVETABLE_LEN * 65536) / SAMPLE_RATE
 * where freq = 440 * 2^((note - 69) / 12)
 *******************************************************************************/
static uint32_t s_note_phase_inc[128];

static void generate_freq_table(void)
{
    for (int n = 0; n < 128; n++) {
        float freq = 440.0f * powf(2.0f, ((float)n - 69.0f) / 12.0f);
        s_note_phase_inc[n] = (uint32_t)(
            (freq * (float)BENTO_MIDI_WAVETABLE_LEN * 65536.0f)
            / (float)BENTO_MIDI_SAMPLE_RATE);
    }
}

/*******************************************************************************
 * ADSR Envelope
 *******************************************************************************/
#define ADSR_ATTACK_MS    10.0f
#define ADSR_DECAY_MS     50.0f
#define ADSR_SUSTAIN_LVL   0.7f
#define ADSR_RELEASE_MS  100.0f

/* Envelope increments per sample (pre-computed) */
static float s_attack_inc;   /* 1.0 / (attack_ms * samples_per_ms) */
static float s_decay_inc;    /* (1.0 - sustain) / (decay_ms * samples_per_ms) */
static float s_release_inc;  /* sustain / (release_ms * samples_per_ms) */

static void compute_adsr_rates(void)
{
    float spm = (float)BENTO_MIDI_SAMPLE_RATE / 1000.0f; /* samples per ms */
    s_attack_inc  = 1.0f / (ADSR_ATTACK_MS * spm);
    s_decay_inc   = (1.0f - ADSR_SUSTAIN_LVL) / (ADSR_DECAY_MS * spm);
    s_release_inc = ADSR_SUSTAIN_LVL / (ADSR_RELEASE_MS * spm);
}

/* Forward declarations */
static void note_off(uint8_t note);

/*******************************************************************************
 * Voice structure
 *******************************************************************************/
typedef enum {
    ENV_OFF = 0,
    ENV_ATTACK,
    ENV_DECAY,
    ENV_SUSTAIN,
    ENV_RELEASE,
} env_stage_t;

typedef struct {
    bool        active;
    uint8_t     note;
    uint8_t     velocity;
    uint32_t    phase;         /* Fixed-point phase accumulator */
    uint32_t    phase_inc;     /* Per-sample phase increment */
    float       env_level;     /* Current envelope amplitude 0..1 */
    env_stage_t env_stage;
    uint32_t    age;           /* For voice stealing (oldest note) */
} voice_t;

static voice_t s_voices[BENTO_MIDI_MAX_VOICES];
static uint32_t s_voice_age_counter = 0;

/*******************************************************************************
 * MIDI Event storage (loaded into RAM)
 *******************************************************************************/
typedef struct {
    uint32_t time_ms;     /* Absolute time in milliseconds */
    uint8_t  status;      /* MIDI status byte (0x80-0xFF) */
    uint8_t  data1;       /* First data byte (note/CC number) */
    uint8_t  data2;       /* Second data byte (velocity/value) */
} midi_event_t;

static midi_event_t s_events[BENTO_MIDI_MAX_EVENTS];
static uint32_t s_num_events = 0;
static uint32_t s_current_event = 0;

/*******************************************************************************
 * Playback state
 *******************************************************************************/
static bento_midi_state_t s_state = MIDI_STATE_IDLE;
static bento_midi_info_t  s_info;
static bento_midi_wave_t  s_waveform = MIDI_WAVE_SINE;
static float              s_playback_time_ms = 0.0f;
static uint32_t           s_last_tick_ms = 0;
static bool               s_tables_ready = false;

/* Simple tick counter — incremented by feed timer */
static volatile uint32_t  s_tick_ms = 0;

/*******************************************************************************
 * Helper: read variable-length quantity from buffer
 *******************************************************************************/
static uint32_t read_vlq(const uint8_t *buf, uint32_t *offset, uint32_t max)
{
    uint32_t val = 0;
    uint32_t pos = *offset;
    for (int i = 0; i < 4 && pos < max; i++) {
        uint8_t b = buf[pos++];
        val = (val << 7) | (b & 0x7F);
        if (!(b & 0x80)) break;
    }
    *offset = pos;
    return val;
}

/*******************************************************************************
 * Helper: read big-endian uint16/uint32 from buffer
 *******************************************************************************/
static uint16_t read_be16(const uint8_t *buf)
{
    return ((uint16_t)buf[0] << 8) | buf[1];
}

static uint32_t read_be32(const uint8_t *buf)
{
    return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8) | buf[3];
}

/*******************************************************************************
 * MIDI File Parser — load all events into s_events[]
 *
 * Timing model (per MIDI.org spec):
 *   Tempo changes affect only ticks AFTER the change, not retroactively.
 *   We build a tempo map from Track 0 (Format 1) or inline (Format 0),
 *   then convert each track's absolute ticks → ms using cumulative time.
 *
 * Reference: https://midi.org/community/midi-specifications/
 *            calculation-of-the-delta-time-when-changing-the-tempo
 *******************************************************************************/
#define MIDI_READ_BUF_SIZE  (32 * 1024)
static uint8_t s_midi_buf[MIDI_READ_BUF_SIZE];

/* Tempo map — built from Track 0 (or inline for Format 0) */
#define MAX_TEMPO_ENTRIES 64
typedef struct {
    uint32_t tick;         /* absolute tick of tempo change */
    float    ms_per_tick;  /* new ms_per_tick from this point */
} tempo_entry_t;

static tempo_entry_t s_tempo_map[MAX_TEMPO_ENTRIES];
static uint32_t s_tempo_map_count = 0;

/* Convert absolute ticks → ms using tempo map (cumulative, per MIDI spec) */
static float ticks_to_ms(uint32_t abs_ticks, uint16_t ticks_per_beat)
{
    float ms = 0.0f;
    uint32_t prev_tick = 0;
    /* Default: 120 BPM = 500000 us/beat */
    float prev_mpt = 500000.0f / ((float)ticks_per_beat * 1000.0f);

    for (uint32_t i = 0; i < s_tempo_map_count; i++) {
        if (s_tempo_map[i].tick >= abs_ticks) break;
        ms += (float)(s_tempo_map[i].tick - prev_tick) * prev_mpt;
        prev_tick = s_tempo_map[i].tick;
        prev_mpt = s_tempo_map[i].ms_per_tick;
    }
    ms += (float)(abs_ticks - prev_tick) * prev_mpt;
    return ms;
}

/* Parse one track, extracting tempo map entries and/or MIDI events.
 * If collect_tempo_only == true, only collects tempo events into s_tempo_map.
 * If false, stores Note/CC/ProgramChange events using the tempo map. */
static void parse_track(const uint8_t *buf, uint32_t start, uint32_t end,
                         uint16_t ticks_per_beat, bool collect_tempo_only)
{
    uint32_t tpos = start;
    uint32_t abs_ticks = 0;
    uint8_t running_status = 0;

    /* For Format 0: inline tempo tracking (cumulative) */
    float cum_ms = 0.0f;
    uint32_t last_tempo_tick = 0;
    float cur_mpt = 500000.0f / ((float)ticks_per_beat * 1000.0f);

    while (tpos < end && s_num_events < BENTO_MIDI_MAX_EVENTS) {
        uint32_t delta = read_vlq(s_midi_buf, &tpos, end);
        abs_ticks += delta;

        if (tpos >= end) break;
        uint8_t byte = s_midi_buf[tpos];

        if (byte == 0xFF) {
            /* Meta event */
            tpos++;
            if (tpos >= end) break;
            uint8_t meta_type = s_midi_buf[tpos++];
            uint32_t meta_len = read_vlq(s_midi_buf, &tpos, end);

            if (meta_type == 0x51 && meta_len == 3 && tpos + 3 <= end) {
                /* Tempo change */
                uint32_t us_per_beat = ((uint32_t)s_midi_buf[tpos] << 16) |
                                       ((uint32_t)s_midi_buf[tpos + 1] << 8) |
                                       s_midi_buf[tpos + 2];
                float new_mpt = (float)us_per_beat /
                                ((float)ticks_per_beat * 1000.0f);

                if (collect_tempo_only) {
                    if (s_tempo_map_count < MAX_TEMPO_ENTRIES) {
                        s_tempo_map[s_tempo_map_count].tick = abs_ticks;
                        s_tempo_map[s_tempo_map_count].ms_per_tick = new_mpt;
                        s_tempo_map_count++;
                    }
                } else {
                    /* Format 0: update inline cumulative time */
                    cum_ms += (float)(abs_ticks - last_tempo_tick) * cur_mpt;
                    last_tempo_tick = abs_ticks;
                    cur_mpt = new_mpt;
                }
            }
            tpos += meta_len;
        } else if (byte == 0xF0 || byte == 0xF7) {
            tpos++;
            uint32_t sysex_len = read_vlq(s_midi_buf, &tpos, end);
            tpos += sysex_len;
        } else {
            /* Channel message */
            uint8_t status, d1, d2;

            if (byte & 0x80) {
                status = byte;
                running_status = byte;
                tpos++;
            } else {
                status = running_status;
            }

            uint8_t type = status & 0xF0;
            d1 = (tpos < end) ? s_midi_buf[tpos++] : 0;

            if (type == 0x80 || type == 0x90 || type == 0xA0 ||
                type == 0xB0 || type == 0xE0) {
                d2 = (tpos < end) ? s_midi_buf[tpos++] : 0;
            } else {
                d2 = 0;
            }

            if (!collect_tempo_only) {
                /* Store Note On/Off, Program Change, CC (sustain/volume),
                 * Pitch Bend — all needed for proper MIDI playback */
                if (type == 0x90 || type == 0x80 || type == 0xC0 ||
                    type == 0xB0 || type == 0xE0) {
                    midi_event_t *ev = &s_events[s_num_events];
                    /* Use tempo map for Format 1, cumulative for Format 0 */
                    if (s_tempo_map_count > 0) {
                        ev->time_ms = (uint32_t)ticks_to_ms(abs_ticks,
                                                             ticks_per_beat);
                    } else {
                        /* Format 0: inline cumulative */
                        ev->time_ms = (uint32_t)(cum_ms +
                            (float)(abs_ticks - last_tempo_tick) * cur_mpt);
                    }
                    ev->status = status;
                    ev->data1 = d1;
                    ev->data2 = d2;
                    s_num_events++;
                }
            }
        }
    }
}

static bool parse_midi_file(const char *path)
{
    FIL fp;
    if (!bento_sdcard_open(path, &fp)) return false;

    UINT br = 0;
    FRESULT fr = f_read(&fp, s_midi_buf, MIDI_READ_BUF_SIZE, &br);
    bento_sdcard_close(&fp);
    if (fr != FR_OK || br < 14) return false;

    if (memcmp(s_midi_buf, "MThd", 4) != 0) return false;
    uint32_t hdr_len = read_be32(&s_midi_buf[4]);
    if (hdr_len < 6) return false;

    s_info.format = read_be16(&s_midi_buf[8]);
    s_info.num_tracks = read_be16(&s_midi_buf[10]);
    s_info.ticks_per_beat = read_be16(&s_midi_buf[12]);

    if (s_info.format > 1) return false;

    uint32_t pos = 8 + hdr_len;
    s_num_events = 0;
    s_tempo_map_count = 0;

    /* Format 1: First pass — build tempo map from Track 0 */
    if (s_info.format == 1 && s_info.num_tracks > 1 && pos + 8 <= br) {
        if (memcmp(&s_midi_buf[pos], "MTrk", 4) == 0) {
            uint32_t trk_len = read_be32(&s_midi_buf[pos + 4]);
            uint32_t trk_start = pos + 8;
            uint32_t trk_end = trk_start + trk_len;
            if (trk_end > br) trk_end = br;
            parse_track(s_midi_buf, trk_start, trk_end,
                        s_info.ticks_per_beat, true);
        }
    }

    /* Second pass — parse all tracks for events */
    pos = 8 + hdr_len;
    for (uint16_t trk = 0; trk < s_info.num_tracks && pos + 8 <= br; trk++) {
        if (memcmp(&s_midi_buf[pos], "MTrk", 4) != 0) break;
        uint32_t trk_len = read_be32(&s_midi_buf[pos + 4]);
        uint32_t trk_start = pos + 8;
        uint32_t trk_end = trk_start + trk_len;
        if (trk_end > br) trk_end = br;

        parse_track(s_midi_buf, trk_start, trk_end,
                    s_info.ticks_per_beat, false);

        pos = trk_start + trk_len;
    }

    /* Sort events by time (insertion sort — events are mostly sorted) */
    for (uint32_t i = 1; i < s_num_events; i++) {
        midi_event_t tmp = s_events[i];
        uint32_t j = i;
        while (j > 0 && s_events[j - 1].time_ms > tmp.time_ms) {
            s_events[j] = s_events[j - 1];
            j--;
        }
        s_events[j] = tmp;
    }

    s_info.num_events = s_num_events;
    s_info.duration_sec = (s_num_events > 0)
        ? (float)s_events[s_num_events - 1].time_ms / 1000.0f
        : 0.0f;

    return s_num_events > 0;
}

/*******************************************************************************
 * Voice allocation
 *******************************************************************************/
static voice_t *alloc_voice(uint8_t note)
{
    /* First: reuse voice already playing the same note */
    for (int i = 0; i < BENTO_MIDI_MAX_VOICES; i++) {
        if (s_voices[i].active && s_voices[i].note == note) {
            return &s_voices[i];
        }
    }

    /* Second: find an inactive voice */
    for (int i = 0; i < BENTO_MIDI_MAX_VOICES; i++) {
        if (!s_voices[i].active) {
            return &s_voices[i];
        }
    }

    /* Third: steal oldest voice */
    uint32_t oldest_age = UINT32_MAX;
    int oldest_idx = 0;
    for (int i = 0; i < BENTO_MIDI_MAX_VOICES; i++) {
        uint32_t a = s_voice_age_counter - s_voices[i].age;
        if (a > (UINT32_MAX - oldest_age)) {
            /* This voice is older */
        }
        if (s_voices[i].age < oldest_age) {
            oldest_age = s_voices[i].age;
            oldest_idx = i;
        }
    }
    return &s_voices[oldest_idx];
}

static void note_on(uint8_t note, uint8_t velocity)
{
    if (note > 127 || velocity == 0) {
        /* Velocity 0 = Note Off per MIDI spec */
        note_off(note);
        return;
    }

    voice_t *v = alloc_voice(note);
    v->active = true;
    v->note = note;
    v->velocity = velocity;
    v->phase = 0;
    v->phase_inc = s_note_phase_inc[note];
    v->env_level = 0.0f;
    v->env_stage = ENV_ATTACK;
    v->age = s_voice_age_counter++;
}

static void note_off(uint8_t note)
{
    for (int i = 0; i < BENTO_MIDI_MAX_VOICES; i++) {
        if (s_voices[i].active && s_voices[i].note == note &&
            s_voices[i].env_stage != ENV_RELEASE) {
            s_voices[i].env_stage = ENV_RELEASE;
        }
    }
}

static void all_notes_off(void)
{
    for (int i = 0; i < BENTO_MIDI_MAX_VOICES; i++) {
        s_voices[i].active = false;
        s_voices[i].env_stage = ENV_OFF;
        s_voices[i].env_level = 0.0f;
    }
}

/*******************************************************************************
 * bento_midi_play
 *******************************************************************************/
bool bento_midi_play(const char *path)
{
    bento_midi_stop();

    /* Initialize tables once */
    if (!s_tables_ready) {
        generate_wavetables();
        generate_freq_table();
        compute_adsr_rates();
        s_tables_ready = true;
    }

    if (!parse_midi_file(path)) {
        s_state = MIDI_STATE_ERROR;
        return false;
    }

    s_current_event = 0;
    s_playback_time_ms = 0.0f;
    s_last_tick_ms = s_tick_ms;
    all_notes_off();

    s_state = MIDI_STATE_PLAYING;

    /* Dispatch t=0 events directly (feed can't — elapsed=0 means no steps).
     * Many MIDI files have Program Change + Note On at t=0. Without this,
     * pre-fill renders 200ms of silence → audible delay before first note. */
    if (bento_tsf_is_loaded()) {
        while (s_current_event < s_num_events) {
            midi_event_t *ev = &s_events[s_current_event];
            if (ev->time_ms > 0) break;
            uint8_t type    = ev->status & 0xF0;
            uint8_t channel = ev->status & 0x0F;
            if (type == 0x90 && ev->data2 > 0) {
                bento_tsf_channel_note_on(channel, ev->data1, ev->data2);
            } else if (type == 0x80 || (type == 0x90 && ev->data2 == 0)) {
                bento_tsf_channel_note_off(channel, ev->data1);
            } else if (type == 0xC0) {
                int bank = (channel == 9) ? 128 : 0;
                bento_tsf_channel_set_bank_preset(channel, bank, ev->data1);
            } else if (type == 0xB0) {
                bento_tsf_channel_midi_control(channel, ev->data1, ev->data2);
            } else if (type == 0xE0) {
                int pw = (int)ev->data1 | ((int)ev->data2 << 7);
                bento_tsf_channel_set_pitchwheel(channel, pw);
            }
            s_current_event++;
        }
        /* Pre-fill ring buffer (200ms = ~20 chunks) with actual note audio.
         * Needed because the first real feed fires ~73ms later. */
        for (int i = 0; i < 20; i++) {
            if (bento_tsf_render() == 0) break;
        }
    }

    return true;
}

void bento_midi_pause(void)
{
    if (s_state == MIDI_STATE_PLAYING)
        s_state = MIDI_STATE_PAUSED;
}

void bento_midi_resume(void)
{
    if (s_state == MIDI_STATE_PAUSED) {
        s_last_tick_ms = s_tick_ms;
        s_state = MIDI_STATE_PLAYING;
    }
}

void bento_midi_stop(void)
{
    s_state = MIDI_STATE_IDLE;
    all_notes_off();
    bento_tsf_all_notes_off();
    s_current_event = 0;
    s_playback_time_ms = 0.0f;
}

/*******************************************************************************
 * bento_midi_get_sample — ISR context (called at 48 kHz)
 *******************************************************************************/
int16_t bento_midi_get_sample(void)
{
    if (s_state != MIDI_STATE_PLAYING) return 0;

    const int16_t *wt = s_wavetable[s_waveform];
    int32_t mix = 0;
    int active_count = 0;

    for (int i = 0; i < BENTO_MIDI_MAX_VOICES; i++) {
        voice_t *v = &s_voices[i];
        if (!v->active) continue;

        /* Advance ADSR envelope */
        switch (v->env_stage) {
            case ENV_ATTACK:
                v->env_level += s_attack_inc;
                if (v->env_level >= 1.0f) {
                    v->env_level = 1.0f;
                    v->env_stage = ENV_DECAY;
                }
                break;
            case ENV_DECAY:
                v->env_level -= s_decay_inc;
                if (v->env_level <= ADSR_SUSTAIN_LVL) {
                    v->env_level = ADSR_SUSTAIN_LVL;
                    v->env_stage = ENV_SUSTAIN;
                }
                break;
            case ENV_SUSTAIN:
                /* Hold at sustain level */
                break;
            case ENV_RELEASE:
                v->env_level -= s_release_inc;
                if (v->env_level <= 0.0f) {
                    v->env_level = 0.0f;
                    v->active = false;
                    v->env_stage = ENV_OFF;
                    continue;
                }
                break;
            default:
                v->active = false;
                continue;
        }

        /* Wavetable lookup (8-bit index from 32-bit phase) */
        uint8_t idx = (uint8_t)(v->phase >> 16);
        int16_t raw = wt[idx];

        /* Scale by envelope and velocity */
        float vel_scale = (float)v->velocity / 127.0f;
        int32_t sample = (int32_t)((float)raw * v->env_level * vel_scale);
        mix += sample;

        /* Advance phase */
        v->phase += v->phase_inc;

        active_count++;
    }

    /* Mix down — prevent clipping */
    if (active_count > 1) {
        mix = mix / (active_count > 4 ? 4 : active_count);
    }

    /* Clamp to int16 */
    if (mix > 32767) mix = 32767;
    if (mix < -32768) mix = -32768;

    /* Output stereo: duplicate sample for both channels.
     * The ISR alternates L/R, so we just return the same sample. */
    return (int16_t)mix;
}

/*******************************************************************************
 * bento_midi_feed — call from LVGL timer (~10ms)
 *
 * Advances playback time and dispatches MIDI events.
 *******************************************************************************/
void bento_midi_feed(void)
{
    if (s_state != MIDI_STATE_PLAYING) return;

    /* Compute elapsed time since last feed */
    uint32_t now = s_tick_ms;
    uint32_t total_elapsed = now - s_last_tick_ms;
    s_last_tick_ms = now;

    bool use_tsf = bento_tsf_is_loaded();

    /* ── Interleaved dispatch + render (10ms steps) ───────────────────
     *
     * LVGL calls us every ~73ms. We process time in 10ms steps:
     * each step dispatches events then renders 2 chunks (20ms audio).
     *
     * Why 2 chunks per step:
     *   - 1 chunk = 10ms matches the dispatched time (correct audio)
     *   - 1 extra chunk = 10ms margin for ring buffer stability
     *     (ISR consumes ~1.04 chunks/10ms, so 2 chunks adds net +460/step)
     *   - The extra chunk uses identical note state (max 10ms lookahead)
     *     which is inaudible — no large stale-audio block
     *
     * NO separate "Phase 2 top-up" — that created 186ms of stale audio
     * causing audible wobble artifacts every 73ms feed cycle.
     */
    uint32_t remaining = total_elapsed;
    while (remaining > 0) {
        uint32_t step = (remaining > 10) ? 10 : remaining;
        remaining -= step;
        s_playback_time_ms += (float)step;

        /* Dispatch events for this 10ms slice */
        while (s_current_event < s_num_events) {
            midi_event_t *ev = &s_events[s_current_event];
            if ((float)ev->time_ms > s_playback_time_ms) break;

            uint8_t type    = ev->status & 0xF0;
            uint8_t channel = ev->status & 0x0F;

            if (use_tsf) {
                if (type == 0x90 && ev->data2 > 0) {
                    bento_tsf_channel_note_on(channel, ev->data1, ev->data2);
                } else if (type == 0x80 || (type == 0x90 && ev->data2 == 0)) {
                    bento_tsf_channel_note_off(channel, ev->data1);
                } else if (type == 0xC0) {
                    int bank = (channel == 9) ? 128 : 0;
                    bento_tsf_channel_set_bank_preset(channel, bank, ev->data1);
                } else if (type == 0xB0) {
                    bento_tsf_channel_midi_control(channel, ev->data1, ev->data2);
                } else if (type == 0xE0) {
                    int pw = (int)ev->data1 | ((int)ev->data2 << 7);
                    bento_tsf_channel_set_pitchwheel(channel, pw);
                }
            } else {
                if (type == 0x90 && ev->data2 > 0) {
                    note_on(ev->data1, ev->data2);
                } else if (type == 0x80 || (type == 0x90 && ev->data2 == 0)) {
                    note_off(ev->data1);
                }
            }

            s_current_event++;
        }

        /* Render 2 chunks per step (20ms audio) */
        if (use_tsf) {
            bento_tsf_render();
            bento_tsf_render();
        }
    }

    /* DDS fallback: no ring buffer, just batch-rendered in ISR */

    /* Check if playback is done (all events processed + no active voices) */
    if (s_current_event >= s_num_events) {
        if (use_tsf) {
            if (bento_tsf_ring_available() == 0) {
                s_state = MIDI_STATE_IDLE;
            }
        } else {
            bool any_active = false;
            for (int i = 0; i < BENTO_MIDI_MAX_VOICES; i++) {
                if (s_voices[i].active) { any_active = true; break; }
            }
            if (!any_active) {
                s_state = MIDI_STATE_IDLE;
            }
        }
    }
}

/* Called by the feed timer to increment tick counter */
void bento_midi_tick(uint32_t elapsed_ms)
{
    s_tick_ms += elapsed_ms;
}

/*******************************************************************************
 * Getters
 *******************************************************************************/
bento_midi_state_t bento_midi_get_state(void)
{
    return s_state;
}

const bento_midi_info_t *bento_midi_get_info(void)
{
    return &s_info;
}

float bento_midi_get_position(void)
{
    return s_playback_time_ms / 1000.0f;
}

float bento_midi_get_duration(void)
{
    return s_info.duration_sec;
}

void bento_midi_set_waveform(bento_midi_wave_t wave)
{
    if (wave < MIDI_WAVE_COUNT) {
        s_waveform = wave;
    }
}

/*******************************************************************************
 * SF2 SoundFont Wrappers
 ******************************************************************************/
bool bento_midi_load_sf2(const char *path)
{
    return bento_tsf_load_sf2(path);
}

void bento_midi_unload_sf2(void)
{
    bento_tsf_unload();
}

bool bento_midi_is_sf2_loaded(void)
{
    return bento_tsf_is_loaded();
}
