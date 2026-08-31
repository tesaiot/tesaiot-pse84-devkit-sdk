/*
 * bento_music.c — live chiptune backing-track sequencer (see bento_music.h).
 *
 * Each track is a 32-step (two-bar) loop of bass + lead notes distilled from the
 * sound-design backing-track arrangements. Notes play through bento_sfx as
 * low-level "music" voices (≈2 sounding at once) so 6 voices stay free for SFX.
 */
#include "bento_music.h"
#include "bento_sfx.h"

/* step = a 16th note. note = MIDI. wave = sfx_wave_t. vel 1..127. dur = steps held. */
typedef struct {
    uint16_t step;
    uint8_t  note;
    uint8_t  wave;
    uint8_t  vel;
    uint8_t  dur;
} music_ev_t;

typedef struct {
    const music_ev_t *evs;
    uint16_t          n_evs;
    uint16_t          n_steps;   /* loop length in 16th-note steps */
    uint16_t          bpm;
} music_track_t;

#define SQ  SFX_WAVE_SQUARE
#define TR  SFX_WAVE_TRIANGLE
#define SAW SFX_WAVE_SAW
#define SI  SFX_WAVE_SINE

/* Each game gets a genuinely distinct identity — different mode, tempo, lead/bass
 * timbre, rhythm and density — not just a transposed copy. (Verdict from the music
 * audit: the old tracks were a flat bass+lead reduction; these restore per-game
 * character within the DDS budget: <=3 music voices, leaving >=5 for SFX.) */

/* ---- Snake: 120 BPM C LYDIAN (raised-4th "wonder"), playful arcade, 64-step.
        TR bounce bass + SQ Lydian hook + quiet SQ 16th arp (bars 2-4). ---- */
static const music_ev_t TRK_SNAKE[] = {
  {0,36,TR,95,3},{4,43,TR,80,3},{8,36,TR,90,3},{12,48,TR,80,3},
  {16,33,TR,95,3},{20,40,TR,80,3},{24,33,TR,90,3},{28,45,TR,80,3},
  {32,41,TR,95,3},{36,48,TR,80,3},{40,41,TR,90,3},{44,53,TR,80,3},
  {48,43,TR,95,3},{52,50,TR,80,3},{56,43,TR,90,3},{60,55,TR,80,3},
  {0,72,SQ,100,2},{3,74,SQ,100,2},{6,78,SQ,100,2},{8,79,SQ,100,2},{12,76,SQ,100,2},
  {16,74,SQ,100,2},{19,72,SQ,100,2},{22,78,SQ,100,2},{24,79,SQ,100,2},{28,81,SQ,100,2},
  {32,72,SQ,100,2},{35,76,SQ,100,2},{38,79,SQ,100,2},{40,78,SQ,100,2},{44,74,SQ,100,2},
  {48,79,SQ,100,2},{51,81,SQ,100,2},{54,83,SQ,100,2},{56,84,SQ,100,2},{60,79,SQ,100,2},
  {16,69,SQ,42,1},{17,72,SQ,42,1},{18,76,SQ,42,1},{19,69,SQ,42,1},{20,72,SQ,42,1},{21,76,SQ,42,1},{22,69,SQ,42,1},{23,72,SQ,42,1},
  {24,76,SQ,42,1},{25,69,SQ,42,1},{26,72,SQ,42,1},{27,76,SQ,42,1},{28,69,SQ,42,1},{29,72,SQ,42,1},{30,76,SQ,42,1},{31,69,SQ,42,1},
  {32,65,SQ,42,1},{33,69,SQ,42,1},{34,72,SQ,42,1},{35,65,SQ,42,1},{36,69,SQ,42,1},{37,72,SQ,42,1},{38,65,SQ,42,1},{39,69,SQ,42,1},
  {40,72,SQ,42,1},{41,65,SQ,42,1},{42,69,SQ,42,1},{43,72,SQ,42,1},{44,65,SQ,42,1},{45,69,SQ,42,1},{46,72,SQ,42,1},{47,65,SQ,42,1},
  {48,67,SQ,42,1},{49,71,SQ,42,1},{50,74,SQ,42,1},{51,67,SQ,42,1},{52,71,SQ,42,1},{53,74,SQ,42,1},{54,67,SQ,42,1},{55,71,SQ,42,1},
  {56,74,SQ,42,1},{57,67,SQ,42,1},{58,71,SQ,42,1},{59,74,SQ,42,1},{60,67,SQ,42,1},{61,71,SQ,42,1},{62,74,SQ,42,1},{63,67,SQ,42,1},
};
/* ---- Flappy: 138 BPM C MAJOR, casual/sunny, 32-step. Soft TRIANGLE lead +
        SQ oompah bass + SAW hi-hat ticks. ---- */
static const music_ev_t TRK_FLAPPY[] = {
  {0,48,SQ,72,1},{2,55,SQ,72,1},{4,53,SQ,72,1},{6,60,SQ,72,1},{8,55,SQ,72,1},{10,62,SQ,72,1},{12,53,SQ,72,1},{14,60,SQ,72,1},
  {16,48,SQ,72,1},{18,55,SQ,72,1},{20,57,SQ,72,1},{22,64,SQ,72,1},{24,55,SQ,72,1},{26,62,SQ,72,1},{28,53,SQ,72,1},{30,60,SQ,72,1},
  {2,76,TR,98,3},{6,79,TR,98,3},{10,77,TR,98,3},{14,81,TR,98,3},{18,79,TR,98,3},{22,84,TR,98,3},{26,81,TR,98,3},{30,79,TR,98,3},
  {4,96,SAW,40,1},{12,96,SAW,40,1},{20,96,SAW,40,1},{28,96,SAW,40,1},
};
/* ---- Pong: 96 BPM A MINOR (Aeolian), sparse retro lounge, 32-step. SINE plucks +
        TR half-note walk; big rests so the pock SFX owns the foreground. ---- */
static const music_ev_t TRK_PONG[] = {
  {0,45,TR,80,8},{8,50,TR,80,8},{16,52,TR,80,8},{24,45,TR,80,8},
  {2,69,SI,85,4},{10,72,SI,85,4},{18,76,SI,85,5},{26,72,SI,85,4},
  {6,81,SI,50,2},{22,79,SI,50,2},
};
/* ---- Shooter: 168 BPM E PHRYGIAN (b2 menace), driving space, 64-step. Relentless
        SAW 8th pedal bass + SQ Phrygian stabs + SAW 16th arp (back half). ---- */
static const music_ev_t TRK_SHOOTER[] = {
  {0,28,SAW,92,2},{2,40,SAW,92,2},{4,40,SAW,92,2},{6,40,SAW,92,2},{8,28,SAW,92,2},{10,40,SAW,92,2},{12,40,SAW,92,2},{14,40,SAW,92,2},
  {16,28,SAW,92,2},{18,40,SAW,92,2},{20,40,SAW,92,2},{22,40,SAW,92,2},{24,28,SAW,92,2},{26,40,SAW,92,2},{28,40,SAW,92,2},{30,40,SAW,92,2},
  {32,28,SAW,92,2},{34,40,SAW,92,2},{36,40,SAW,92,2},{38,40,SAW,92,2},{40,28,SAW,92,2},{42,40,SAW,92,2},{44,40,SAW,92,2},{46,40,SAW,92,2},
  {48,28,SAW,92,2},{50,40,SAW,92,2},{52,40,SAW,92,2},{54,40,SAW,92,2},{56,28,SAW,92,2},{58,40,SAW,92,2},{60,40,SAW,92,2},{62,40,SAW,92,2},
  {0,64,SQ,100,2},{3,65,SQ,100,2},{6,67,SQ,100,2},{10,64,SQ,100,2},{16,67,SQ,100,2},{19,65,SQ,100,2},{22,64,SQ,100,2},{26,62,SQ,100,2},
  {32,64,SQ,100,2},{35,65,SQ,100,2},{38,67,SQ,100,2},{42,71,SQ,100,2},{48,72,SQ,100,2},{51,71,SQ,100,2},{54,67,SQ,100,2},{58,65,SQ,100,2},
  {32,76,SAW,48,1},{33,79,SAW,48,1},{34,83,SAW,48,1},{35,76,SAW,48,1},{36,79,SAW,48,1},{37,83,SAW,48,1},{38,76,SAW,48,1},{39,79,SAW,48,1},
  {40,83,SAW,48,1},{41,76,SAW,48,1},{42,79,SAW,48,1},{43,83,SAW,48,1},{44,76,SAW,48,1},{45,79,SAW,48,1},{46,83,SAW,48,1},{47,76,SAW,48,1},
  {48,79,SAW,48,1},{49,83,SAW,48,1},{50,76,SAW,48,1},{51,79,SAW,48,1},{52,83,SAW,48,1},{53,76,SAW,48,1},{54,79,SAW,48,1},{55,83,SAW,48,1},
  {56,76,SAW,48,1},{57,79,SAW,48,1},{58,83,SAW,48,1},{59,76,SAW,48,1},{60,79,SAW,48,1},{61,83,SAW,48,1},{62,76,SAW,48,1},{63,79,SAW,48,1},
};

#define TRK(arr, steps, bpm)  { (arr), (uint16_t)(sizeof(arr)/sizeof((arr)[0])), (steps), (bpm) }
static const music_track_t s_tracks[] = {
    [BENTO_MUSIC_NONE]    = { 0, 0, 0, 0 },
    [BENTO_MUSIC_SNAKE]   = TRK(TRK_SNAKE,   64, 120),
    [BENTO_MUSIC_FLAPPY]  = TRK(TRK_FLAPPY,  32, 138),
    [BENTO_MUSIC_PONG]    = TRK(TRK_PONG,    32,  96),
    [BENTO_MUSIC_SHOOTER] = TRK(TRK_SHOOTER, 64, 168),
};
#define N_TRACKS  (sizeof(s_tracks)/sizeof(s_tracks[0]))

static bento_music_track_id_t s_cur;
static const music_track_t   *s_trk;
static uint32_t               s_step_ms;
static uint32_t               s_last_ms;
static int32_t                s_step;     /* current step; -1 = not yet started */

static void fire_step(int32_t step)
{
    for (uint16_t i = 0; i < s_trk->n_evs; i++) {
        if (s_trk->evs[i].step == (uint16_t)step) {
            const music_ev_t *e = &s_trk->evs[i];
            uint16_t dur_ms = (uint16_t)(e->dur * s_step_ms);
            bento_sfx_play_music(e->note, (sfx_wave_t)e->wave, e->vel, dur_ms);
        }
    }
}

void bento_music_start(bento_music_track_id_t track)
{
    if ((uint32_t)track >= N_TRACKS) return;
    if (track == s_cur) return;                 /* already playing this one */
    bento_sfx_stop_music();
    s_cur = track;
    if (track == BENTO_MUSIC_NONE) { s_trk = 0; return; }
    s_trk     = &s_tracks[track];
    s_step_ms = (s_trk->bpm > 0) ? (60000u / (s_trk->bpm * 4u)) : 120u;
    s_step    = -1;                             /* first tick fires step 0 */
    s_last_ms = 0;
}

void bento_music_stop(void)
{
    s_cur = BENTO_MUSIC_NONE;
    s_trk = 0;
    bento_sfx_stop_music();
}

void bento_music_tick(uint32_t now_ms)
{
    if (s_trk == 0 || s_trk->n_steps == 0) return;

    if (s_step < 0) {                           /* kick off on the first tick */
        s_step = 0; s_last_ms = now_ms; fire_step(0); return;
    }
    /* Advance whole steps that have elapsed (cap catch-up so a stalled frame
     * doesn't dump a burst of notes). */
    int guard = 4;
    while ((now_ms - s_last_ms) >= s_step_ms && guard-- > 0) {
        s_last_ms += s_step_ms;
        s_step++;
        if (s_step >= (int32_t)s_trk->n_steps) s_step = 0;
        fire_step(s_step);
    }
}
