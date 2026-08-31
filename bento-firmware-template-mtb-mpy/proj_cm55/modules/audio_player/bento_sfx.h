/*
 * bento_sfx.h — low-latency game sound-effects mixer for the BENTO Eva Game console.
 *
 * A self-contained polyphonic mixer that the TDM ISR pumps via
 * bento_sfx_mixer_get_sample(). Two voice kinds share one voice pool:
 *   - SYNTH  : chiptune DDS (square/triangle/saw/sine wavetable + ADSR), driven by
 *              a multi-segment "patch" so one trigger can be a blip, a pitch sweep,
 *              or a short jingle. Zero storage — generated in code.
 *   - PCM    : a 16-bit signed mono one-shot clip from RAM/QSPI (hero hits). Pure
 *              integer in the ISR.
 *
 * Triggers are fire-and-forget and lock-free (game/GFX task writes a voice, ISR
 * reads it). No malloc / printf / FatFS / blocking in the ISR.
 *
 * The DDS math mirrors the proven bento_midi synth; this module strips the MIDI
 * file/SF2/SD machinery and exposes a game-friendly fire-and-forget API instead.
 */
#ifndef BENTO_SFX_H
#define BENTO_SFX_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Waveforms (match bento_midi wavetable order). */
typedef enum {
    SFX_WAVE_SINE = 0,
    SFX_WAVE_SQUARE,
    SFX_WAVE_TRIANGLE,
    SFX_WAVE_SAW,
} sfx_wave_t;

/* High-level sound IDs — one per gameplay/UI event across all 4 games.
 * The patch table in bento_sfx.c defines how each one sounds. */
typedef enum {
    /* Global UI */
    SFX_UI_MOVE = 0,
    SFX_UI_SELECT,
    SFX_UI_BACK,
    SFX_UI_DENY,
    SFX_UI_START,
    /* Snake */
    SFX_SNAKE_EAT,
    SFX_SNAKE_TURN,
    SFX_SNAKE_DIE,
    /* Flappy */
    SFX_FLAPPY_FLAP,
    SFX_FLAPPY_SCORE,
    SFX_FLAPPY_DIE,
    /* Pong */
    SFX_PONG_WALL,
    SFX_PONG_PADDLE,
    SFX_PONG_SCORE,
    SFX_PONG_WIN,
    SFX_PONG_LOSE,
    /* Shooter */
    SFX_SHOOT_FIRE,
    SFX_SHOOT_HIT,
    SFX_SHOOT_EXPLODE,
    SFX_SHOOT_LOSE_LIFE,
    /* Shared */
    SFX_GAME_OVER,
    SFX_COUNT
    /* Note: the power-on fanfare is NOT an event ID — it is a timed multi-voice score
     * played by bento_sfx_startup() below. */
} sfx_id_t;

/* Initialise the mixer (wavetables, freq table, ADSR, clears voices). Idempotent. */
void bento_sfx_init(void);

/* Fire a designed sound effect. Fire-and-forget, returns immediately. Safe to call
 * from the game/GFX task; NOT from an ISR. */
void bento_sfx_event(sfx_id_t id);

/* Bright, relaxing 3-movement power-on fanfare for console boot (~3.8 s): a warm
 * open-fifth pad swells from near-silence (awakening), a sine->triangle lead climbs
 * through the maj7 (rising), then a wide major chord blooms low->high with a single
 * high sparkle (grand arrival), capped by a soft tonic "ready" blip. Fire-and-forget:
 * the TDM ISR sequences the whole timed score, so the caller never blocks. */
void bento_sfx_startup(void);

/* Low-level: play a single synth note (one segment). dur_ms includes the release. */
void bento_sfx_play_tone(uint8_t note, sfx_wave_t wave, uint8_t velocity, uint16_t dur_ms);

/* Play a 16-bit signed mono PCM one-shot (RAM or XIP-QSPI). gain 0..255 (256 = unity). */
void bento_sfx_play_clip(const int16_t *pcm, uint32_t len, uint16_t gain);

/* Play a synth note tagged as BACKING MUSIC: lower priority (SFX steal music voices
 * first, so gameplay sound is never starved) and rendered at a reduced level so it
 * sits UNDER the SFX. Used by the bento_music sequencer. */
void bento_sfx_play_music(uint8_t note, sfx_wave_t wave, uint8_t velocity, uint16_t dur_ms);

/* Silence all currently-playing MUSIC voices (SFX voices keep playing). */
void bento_sfx_stop_music(void);

/* Render one mono sample by summing all active voices. Called from the TDM ISR. */
int16_t bento_sfx_mixer_get_sample(void);

/* True if any voice is active (optional, for diagnostics). */
bool bento_sfx_active(void);

#ifdef __cplusplus
}
#endif

#endif /* BENTO_SFX_H */
