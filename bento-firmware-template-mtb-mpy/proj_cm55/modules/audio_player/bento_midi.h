/*******************************************************************************
 * File Name: bento_midi.h
 *
 * Description: MIDI file parser + 16-voice DDS tone synthesizer.
 *              Parses Standard MIDI Files (.mid) from SD Card,
 *              synthesizes audio via wavetable DDS with ADSR envelope.
 *
 *              Architecture:
 *                - bento_midi_play()  : open .mid file, parse header + tracks
 *                - bento_midi_tick()  : advance MIDI events by elapsed time
 *                - bento_midi_render(): ISR-callable, mix active voices → sample
 *                - bento_midi_feed()  : call from LVGL timer (~10ms) to advance
 *
 *              Supported:
 *                - Standard MIDI Format 0 and 1
 *                - Note On / Note Off / Program Change / Tempo
 *                - 16-voice polyphony with voice stealing (oldest note)
 *                - ADSR envelope (Attack/Decay/Sustain/Release)
 *                - Sine + Square + Triangle + Sawtooth waveforms
 *
 *******************************************************************************/

#ifndef BENTO_MIDI_H
#define BENTO_MIDI_H

#include <stdint.h>
#include <stdbool.h>

/*******************************************************************************
 * Configuration
 *******************************************************************************/
#define BENTO_MIDI_MAX_VOICES     16
#define BENTO_MIDI_MAX_EVENTS     4096   /* Max MIDI events loaded into RAM */
#define BENTO_MIDI_SAMPLE_RATE    48000
#define BENTO_MIDI_WAVETABLE_LEN  256

/* Waveform types */
typedef enum {
    MIDI_WAVE_SINE = 0,
    MIDI_WAVE_SQUARE,
    MIDI_WAVE_TRIANGLE,
    MIDI_WAVE_SAWTOOTH,
    MIDI_WAVE_COUNT,
} bento_midi_wave_t;

/* Playback state */
typedef enum {
    MIDI_STATE_IDLE = 0,
    MIDI_STATE_PLAYING,
    MIDI_STATE_PAUSED,
    MIDI_STATE_ERROR,
} bento_midi_state_t;

/* MIDI file info (after parsing) */
typedef struct {
    uint16_t format;          /* 0 or 1 */
    uint16_t num_tracks;
    uint16_t ticks_per_beat;  /* aka "division" */
    uint32_t num_events;      /* total events parsed */
    float    duration_sec;    /* estimated total duration */
} bento_midi_info_t;

/*******************************************************************************
 * API
 *******************************************************************************/

/* Parse and start playback of a .mid file from SD Card.
 * path: FatFS path, e.g. "/midi/song.mid"
 * Returns true if playback started. */
bool bento_midi_play(const char *path);

/* Pause / Resume / Stop */
void bento_midi_pause(void);
void bento_midi_resume(void);
void bento_midi_stop(void);

/* Get current state */
bento_midi_state_t bento_midi_get_state(void);

/* Get file info of currently loaded MIDI */
const bento_midi_info_t *bento_midi_get_info(void);

/* Get current playback position in seconds */
float bento_midi_get_position(void);

/* Get total duration in seconds */
float bento_midi_get_duration(void);

/* Set waveform for synthesis */
void bento_midi_set_waveform(bento_midi_wave_t wave);

/* ISR-callable: get next mixed audio sample (called at 48 kHz).
 * Returns 16-bit signed PCM sample. */
int16_t bento_midi_get_sample(void);

/* Feed task: call from LVGL timer (~10ms) to advance MIDI events.
 * Must be called regularly during playback. */
void bento_midi_feed(void);

/* Increment internal tick counter by elapsed_ms. Call before bento_midi_feed(). */
void bento_midi_tick(uint32_t elapsed_ms);

/*******************************************************************************
 * SoundFont (SF2) Integration — TinySoundFont
 ******************************************************************************/

/* Load an SF2 SoundFont for MIDI playback.
 * When loaded, MIDI synthesis uses TSF instead of DDS.
 * path: FatFS path e.g. "/midi/PocketSongsGM.sf2"
 * Returns true if loaded successfully. */
bool bento_midi_load_sf2(const char *path);

/* Unload SF2 and revert to DDS synthesis. */
void bento_midi_unload_sf2(void);

/* Check if SF2 is loaded. */
bool bento_midi_is_sf2_loaded(void);

#endif /* BENTO_MIDI_H */
