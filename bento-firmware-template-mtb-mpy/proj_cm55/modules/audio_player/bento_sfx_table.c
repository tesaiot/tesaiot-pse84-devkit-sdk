/*******************************************************************************
 * File Name: bento_sfx_table.c
 *
 * Description: Chiptune sound-effect (SFX) data table for the BENTO Eva Game
 *              console. Pure data + tiny helpers. The on-device 16-voice DDS
 *              synth (bento_midi.c) does the actual synthesis; this file only
 *              describes WHAT to play for every game/UI event:
 *
 *                  waveform  +  note/pitch-sweep sequence  +  per-note duration
 *                            +  per-SFX ADSR envelope.
 *
 *              Every entry is indexable by an SFX-ID enum (bento_sfx_id_t).
 *              A player calls:  bento_sfx_play(SFX_ID);
 *
 * Engine field mapping (verified against bento_midi.{h,c}):
 *   - Waveform values are bento_midi_wave_t:
 *       0 SINE, 1 SQUARE, 2 TRIANGLE, 3 SAWTOOTH   (MIDI_WAVE_* enum)
 *   - Notes are MIDI note numbers (0..127). The engine pre-computes a
 *       phase-increment per note in s_note_phase_inc[note]
 *       (phase_inc = freq*WAVETABLE_LEN*65536/SAMPLE_RATE, freq=440*2^((n-69)/12)).
 *       The player reuses that table directly; for a pitch SWEEP it interpolates
 *       phase_inc from note s_note_phase_inc[note] -> s_note_phase_inc[note_to]
 *       across the step's duration (the engine already advances by phase_inc).
 *   - Velocity is 0..127 (engine scales raw sample by velocity/127).
 *   - ADSR uses the same four parameters the engine's compute_adsr_rates() uses:
 *       attack_ms, decay_ms, sustain_lvl (0..1), release_ms.
 *       (The live engine hard-codes one global ADSR; this table carries it
 *        per-SFX so the SFX player applies it per voice — the natural per-voice
 *        generalisation of the engine's existing envelope math.)
 *
 * Two-voice tricks expressed purely as data (no synthesis here):
 *   - step.chord_semis != 0  -> sound a SECOND note (note + chord_semis) at the
 *       SAME time as the step's note, on the same waveform (octave-doubling,
 *       dyad clusters, harmony thirds). Used for game-over bodies, the pass-pipe
 *       octave body, ui-error minor-2nd clusters, etc.
 *   - entry.layer_wave != SFX_WAVE_NONE  -> a parallel layer voice that plays the
 *       same step timeline on a different waveform, transposed by
 *       entry.layer_semis (e.g. SAWTOOTH harmony a 3rd above a SQUARE lead).
 *
 * This file is host-compilable with no external dependencies (it does NOT
 * include bento_midi.h so it can be unit-tested off-target). On-device, the
 * SFX_WAVE_* values are byte-identical to MIDI_WAVE_* so they pass straight
 * into bento_midi_set_waveform()/the per-voice DDS path.
 *
 *******************************************************************************/

#include <stdint.h>
#include <stddef.h>

/*******************************************************************************
 * Waveform — values MUST match bento_midi_wave_t (MIDI_WAVE_*).
 *******************************************************************************/
typedef enum {
    SFX_WAVE_SINE     = 0,   /* == MIDI_WAVE_SINE     */
    SFX_WAVE_SQUARE   = 1,   /* == MIDI_WAVE_SQUARE   */
    SFX_WAVE_TRIANGLE = 2,   /* == MIDI_WAVE_TRIANGLE */
    SFX_WAVE_SAWTOOTH = 3,   /* == MIDI_WAVE_SAWTOOTH */
    SFX_WAVE_NONE     = 255, /* sentinel: "no second layer"            */
} bento_sfx_wave_t;

/*******************************************************************************
 * ADSR — same parameters as the DDS engine's envelope.
 *   attack_ms / decay_ms / release_ms : milliseconds
 *   sustain_q8                         : sustain level, Q0.8 fixed point
 *                                        (0..255 == 0.0..~1.0; 217 ~= 0.85)
 * Fixed-point sustain keeps the whole table integer-only (no float in flash,
 * trivial to unit-test). The player converts: sustain = sustain_q8 / 255.0f.
 *******************************************************************************/
typedef struct {
    uint16_t attack_ms;
    uint16_t decay_ms;
    uint8_t  sustain_q8;   /* 0..255  -> 0.0..1.0 */
    uint16_t release_ms;
} bento_sfx_adsr_t;

/* Convenience macro: ADSR with a float-looking sustain 0..1 (compile-time). */
#define ADSR(a, d, s, r)  { (uint16_t)(a), (uint16_t)(d), \
                            (uint8_t)((s) * 255.0f + 0.5f), (uint16_t)(r) }

/*******************************************************************************
 * One note step.
 *   note       : MIDI note to start on (0..127)
 *   note_to    : MIDI target note for a pitch sweep/bend; == note for no sweep
 *   dur_ms     : how long this step sounds
 *   velocity   : 0..127
 *   chord_semis: if != 0, a SECOND note (note + chord_semis) sounds in unison
 *                with this step on the same waveform (octave body / dyad / 3rd)
 *******************************************************************************/
typedef struct {
    uint8_t  note;
    uint8_t  note_to;
    uint16_t dur_ms;
    uint8_t  velocity;
    int8_t   chord_semis;
} bento_sfx_step_t;

/*******************************************************************************
 * One SFX entry.
 *   name        : debug string (also handy for a serial "SFX: <name>" trace)
 *   wave        : primary waveform (bento_sfx_wave_t)
 *   adsr        : envelope applied to every step of this SFX
 *   steps       : pointer to the note timeline
 *   n_steps     : number of steps
 *   layer_wave  : SFX_WAVE_NONE, or a parallel layer's waveform
 *   layer_semis : transpose (semitones) of the parallel layer vs the primary
 *   layer_vel   : velocity of the parallel layer (0 -> reuse step velocity)
 *******************************************************************************/
typedef struct {
    const char             *name;
    uint8_t                 wave;        /* bento_sfx_wave_t */
    bento_sfx_adsr_t        adsr;
    const bento_sfx_step_t *steps;
    uint8_t                 n_steps;
    uint8_t                 layer_wave;  /* bento_sfx_wave_t / SFX_WAVE_NONE */
    int8_t                  layer_semis;
    uint8_t                 layer_vel;
} bento_sfx_t;

/*******************************************************************************
 * SFX-ID enum — indexes bento_sfx_table[] 1:1.
 * Naming: SFX_<GAME>_<EVENT>. GLOBAL = shared UI cues.
 * Only CHIPTUNE sounds live here; PCM hero hits (milestone coin, die-wall,
 * die-self, hit-pipe/ground, explosion, win-crowd, score "pock", new-best
 * cheer) are played from the PCM voice path, not this table.
 *******************************************************************************/
typedef enum {
    /* ---- Snake (chiptune) ---- */
    SFX_SNAKE_EAT = 0,
    SFX_SNAKE_STEP,
    SFX_SNAKE_TURN,
    SFX_SNAKE_NEAR_MISS,
    SFX_SNAKE_GAME_OVER,
    SFX_SNAKE_HIGH_SCORE,
    SFX_SNAKE_RESTART,
    SFX_SNAKE_PAGE_ENTER,
    SFX_SNAKE_UI_NAV,
    SFX_SNAKE_UI_SELECT,
    SFX_SNAKE_UI_BACK,
    SFX_SNAKE_UI_ERROR,

    /* ---- Flappy (chiptune) ---- */
    SFX_FLAPPY_FLAP,
    SFX_FLAPPY_SCORE,
    SFX_FLAPPY_NEAR_MISS,
    SFX_FLAPPY_GAME_OVER,
    SFX_FLAPPY_RESTART,
    SFX_FLAPPY_UI_NAV,
    SFX_FLAPPY_UI_SELECT,
    SFX_FLAPPY_UI_BACK,
    SFX_FLAPPY_UI_ERROR,
    SFX_FLAPPY_PAGE_ENTER,

    /* ---- Pong (chiptune) ---- */
    SFX_PONG_PAGE_ENTER,         /* serve-ready chime */
    SFX_PONG_SERVE,              /* ball launch sweep */
    SFX_PONG_HIT_PLAYER,         /* left paddle (SQUARE) */
    SFX_PONG_HIT_AI,             /* right paddle (TRIANGLE) */
    SFX_PONG_HIT_WALL,           /* top/bottom bounce */
    SFX_PONG_SCORE_PLAYER,       /* ascending confirm arpeggio (chip part) */
    SFX_PONG_SCORE_AI,           /* descending confirm arpeggio (chip part) */
    SFX_PONG_MATCH_POINT,        /* tension swell at score 9 */
    SFX_PONG_WIN_FANFARE,        /* chiptune fanfare overlaid on win PCM */
    SFX_PONG_LOSE,               /* descending "wah" sting */
    SFX_PONG_RESTART,
    SFX_PONG_UI_NAV,
    SFX_PONG_UI_SELECT,
    SFX_PONG_UI_BACK,
    SFX_PONG_UI_ERROR,

    /* ---- Space Shooter (chiptune) ---- */
    SFX_SHOOTER_FIRE,
    SFX_SHOOTER_ENEMY_HIT,       /* combo-pitched kill confirm */
    SFX_SHOOTER_LOSE_LIFE,
    SFX_SHOOTER_GAME_OVER,
    SFX_SHOOTER_SCORE,           /* quiet point tick */
    SFX_SHOOTER_SCORE_MILESTONE, /* every 10th */
    SFX_SHOOTER_WAVE,            /* difficulty ramp riser */
    SFX_SHOOTER_UI_NAV,
    SFX_SHOOTER_UI_SELECT,
    SFX_SHOOTER_UI_BACK,         /* also pause */
    SFX_SHOOTER_UI_ERROR,
    SFX_SHOOTER_PAGE_ENTER,

    SFX_ID_COUNT
} bento_sfx_id_t;

/*******************************************************************************
 * Step timelines.
 * NOTE = no sweep / no chord (terse helper). SWEEP carries note_to. CHORD adds
 * a unison second note. All durations/velocities/ADSR come straight from the
 * audio-director design digest.
 *******************************************************************************/
#define N(note, dur, vel)            { (note), (note), (dur), (vel),  0 }
#define SWEEP(n0, n1, dur, vel)      { (n0),   (n1),   (dur), (vel),  0 }
#define CHORD(note, dur, vel, semis) { (note), (note), (dur), (vel), (semis) }

/* ===========================  SNAKE  ============================= */

/* eat-food: SQUARE 2-note arpeggio. Base ladder is computed live by the player
 * (A = 72 + score%13, B = A+5); table stores the CANONICAL apple-1 pitches
 * (C5 -> F5) and the snappy ADSR/timing. Player offsets both notes by score. */
static const bento_sfx_step_t k_snake_eat[] = {
    N(72, 45, 110),   /* A: C5, 45ms */
    N(77, 75, 110),   /* B: F5 (A+5 perfect 4th), 75ms */
};
/* step-move: TRIANGLE 18ms click. Pitch is length-tied live (48 + len/12,
 * clamp 48..60); table holds canonical low C + low velocity. */
static const bento_sfx_step_t k_snake_step[] = {
    N(48, 18, 45),
};
/* turn: thin SQUARE 25% 'tk' (engine has 50% square; player may narrow gate). */
static const bento_sfx_step_t k_snake_turn[] = {
    N(79, 22, 60),    /* G5 */
};
/* near-miss: SAWTOOTH fast up-sweep 60 -> 72 over 60ms. */
static const bento_sfx_step_t k_snake_near_miss[] = {
    SWEEP(60, 72, 60, 70),
};
/* game-over: SQUARE lead + TRIANGLE octave-below body, descending 4-note. */
static const bento_sfx_step_t k_snake_game_over[] = {
    CHORD(69, 140, 90, -12),   /* A4 */
    CHORD(67, 140, 90, -12),   /* G4 */
    CHORD(64, 140, 90, -12),   /* E4 */
    CHORD(60, 230, 90, -12),   /* C4 let ring */
};
/* high-score fanfare: SQUARE lead (+ SAW 3rd-above layer), rising + trill. */
static const bento_sfx_step_t k_snake_high_score[] = {
    N(60, 120, 105),  /* C5 */
    N(64, 120, 105),  /* E5 */
    N(67, 120, 105),  /* G5 */
    N(72, 200, 105),  /* C6 */
    N(72,  60, 105),  /* trill 72 */
    N(74,  60, 105),  /* trill 74 */
    N(72,  60, 105),  /* trill 72 */
    N(72, 220, 105),  /* C6 let ring */
};
/* restart: SQUARE two notes ascending G5 -> D6. */
static const bento_sfx_step_t k_snake_restart[] = {
    N(67, 60, 95),    /* G5 */
    N(74, 110, 95),   /* D6 */
};
/* page-enter chime: TRIANGLE ascending major arpeggio C5 E5 G5 C6. */
static const bento_sfx_step_t k_snake_page_enter[] = {
    N(60, 70, 80),    /* C5 */
    N(64, 70, 80),    /* E5 */
    N(67, 70, 80),    /* G5 */
    N(72, 160, 80),   /* C6 let ring */
};
/* ui-navigate: thin SQUARE 25% single click C5. */
static const bento_sfx_step_t k_snake_ui_nav[] = {
    N(72, 28, 65),
};
/* ui-select: SQUARE two notes C5 -> G5. */
static const bento_sfx_step_t k_snake_ui_select[] = {
    N(72, 35, 85),    /* C5 */
    N(79, 70, 85),    /* G5 */
};
/* ui-back: SQUARE two notes descending D6 -> G5. */
static const bento_sfx_step_t k_snake_ui_back[] = {
    N(74, 35, 75),    /* D6 */
    N(67, 70, 75),    /* G5 */
};
/* ui-error: SAW minor-2nd cluster F3 + F#3 together, 90ms. */
static const bento_sfx_step_t k_snake_ui_error[] = {
    CHORD(53, 90, 70, +1),   /* F3 + F#3 */
};

/* ===========================  FLAPPY  ============================ */

/* flap: TRIANGLE up-sweep G5 -> C6, 70ms pluck. Player adds velocity-based
 * pitch offset from bird_vy at trigger time. */
static const bento_sfx_step_t k_flappy_flap[] = {
    SWEEP(79, 84, 70, 58),   /* ~45% of 127 */
};
/* pass-pipe/score: SQUARE + TRIANGLE octave-below body. Grace note then snap.
 * Pitch from pentatonic[score%8] live; table holds the canonical C5 step. */
static const bento_sfx_step_t k_flappy_score[] = {
    N(70, 18, 76),            /* grace: note-2 (~D-2) */
    CHORD(72, 112, 76, -12),  /* snap to C5 + octave body */
};
/* near-miss: SAW down-sweep C6 -> F5, soft swell, low amplitude. */
static const bento_sfx_step_t k_flappy_near_miss[] = {
    SWEEP(84, 77, 110, 28),  /* ~22% */
};
/* game-over: SQUARE lead + TRIANGLE 3rd-below harmony, 3 descending + droop. */
static const bento_sfx_step_t k_flappy_game_over[] = {
    CHORD(76, 180, 64, -4),  /* E5 */
    CHORD(74, 180, 64, -4),  /* D5 */
    SWEEP(72, 70, 420, 64),  /* C5 bends DOWN ~2 semis over the tail */
};
/* restart: SQUARE two rising G5 -> C6, 2nd note bends up a semitone. */
static const bento_sfx_step_t k_flappy_restart[] = {
    N(79, 90, 64),           /* G5 */
    SWEEP(84, 85, 90, 64),   /* C6 -> +1 lift */
};
/* menu-nav: SQUARE single A5, uniform. */
static const bento_sfx_step_t k_flappy_ui_nav[] = {
    N(81, 40, 45),
};
/* select: SQUARE E5 -> A5. */
static const bento_sfx_step_t k_flappy_ui_select[] = {
    N(76, 70, 58),           /* E5 */
    N(81, 70, 58),           /* A5 */
};
/* back: TRIANGLE A5 -> E5 descending. */
static const bento_sfx_step_t k_flappy_ui_back[] = {
    N(81, 70, 51),           /* A5 */
    N(76, 70, 51),           /* E5 */
};
/* error: SQUARE low minor-2nd cluster C4 + Db4, static buzz. */
static const bento_sfx_step_t k_flappy_ui_error[] = {
    CHORD(48, 130, 48, +1),  /* C4 + Db4 */
};
/* page-enter: TRIANGLE 3 rising notes C5 E5 G5 that ring into a C chord. */
static const bento_sfx_step_t k_flappy_page_enter[] = {
    N(72, 160, 53),          /* C5 */
    N(76, 160, 53),          /* E5 */
    N(79, 160, 53),          /* G5 */
};

/* ============================  PONG  ============================= */

/* page-enter/serve-ready: TRIANGLE C6 then G6, sparkle handled by layer. */
static const bento_sfx_step_t k_pong_page_enter[] = {
    N(84, 70, 90),           /* C6 */
    N(91, 70, 70),           /* G6 (sparkle G7 via layer +12) */
};
/* serve: TRIANGLE down-sweep A6 -> A5 over 60ms. */
static const bento_sfx_step_t k_pong_serve[] = {
    SWEEP(81, 69, 60, 90),
};
/* hit player paddle: SQUARE transient + TRIANGLE octave-below body. Base note
 * climbs with ball speed live; table holds the serve-speed base E5. */
static const bento_sfx_step_t k_pong_hit_player[] = {
    CHORD(64, 45, 110, -12), /* E5 + octave body */
};
/* hit AI paddle: TRIANGLE (hollow), base C5, optional SINE sub via layer. */
static const bento_sfx_step_t k_pong_hit_ai[] = {
    N(60, 50, 95),           /* C5 */
};
/* hit wall: TRIANGLE A3 with fast 2-semitone down micro-bend. */
static const bento_sfx_step_t k_pong_hit_wall[] = {
    SWEEP(57, 55, 38, 70),   /* A3 -> ~G3 */
};
/* score (player): SQUARE ascending arpeggio C6 E6 G6 (chip confirm part). */
static const bento_sfx_step_t k_pong_score_player[] = {
    N(84, 50, 90),           /* C6 */
    N(88, 50, 90),           /* E6 */
    N(91, 50, 90),           /* G6 */
};
/* score (AI): SQUARE descending arpeggio G5 E5 C5. */
static const bento_sfx_step_t k_pong_score_ai[] = {
    N(79, 50, 90),           /* G5 */
    N(76, 50, 90),           /* E5 */
    N(72, 50, 90),           /* C5 */
};
/* match point: SAW hollow fifth A2 + E3 (chord +7), slow swell. */
static const bento_sfx_step_t k_pong_match_point[] = {
    CHORD(45, 600, 85, +7),  /* A2 + E3 */
};
/* win fanfare (chip overlay on PCM crowd): SQUARE C6 E6 G6 C7 + TRIANGLE 3rd. */
static const bento_sfx_step_t k_pong_win_fanfare[] = {
    N(84, 90, 110),          /* C6 */
    N(88, 90, 110),          /* E6 */
    N(91, 90, 110),          /* G6 */
    CHORD(96, 250, 110, -4), /* C7 hold, harmony 3rd below (E6) */
};
/* lose: TRIANGLE G4 E4 C4 each with a -2 droop, then SAW C3 thud. */
static const bento_sfx_step_t k_pong_lose[] = {
    SWEEP(67, 65, 180, 85),  /* G4 droop */
    SWEEP(64, 62, 180, 78),  /* E4 droop */
    SWEEP(60, 58, 180, 70),  /* C4 droop */
    N(48, 220, 60),          /* C3 thud (SAW via layer/own wave) */
};
/* restart: SQUARE up-sweep C5 -> C6 then TRIANGLE G6 accent. */
static const bento_sfx_step_t k_pong_restart[] = {
    SWEEP(60, 72, 110, 100), /* C5 -> C6 */
    N(91, 40, 100),          /* G6 accent */
};
/* menu navigate: SQUARE single A5 (player climbs +1 on repeats live). */
static const bento_sfx_step_t k_pong_ui_nav[] = {
    N(81, 22, 75),
};
/* select: TRIANGLE rising fifth E6 -> B6 (+ SQUARE sparkle via layer). */
static const bento_sfx_step_t k_pong_ui_select[] = {
    N(88, 45, 100),          /* E6 */
    N(95, 45, 100),          /* B6 */
};
/* back: TRIANGLE falling fifth B5 -> E5. */
static const bento_sfx_step_t k_pong_ui_back[] = {
    N(83, 45, 85),           /* B5 */
    N(76, 45, 85),           /* E5 */
};
/* error: SQUARE two low pulses F3 then E3. */
static const bento_sfx_step_t k_pong_ui_error[] = {
    N(53, 60, 80),           /* F3 */
    N(52, 60, 80),           /* E3 */
};

/* =========================  SPACE SHOOTER  ====================== */

/* fire-shot: SAW down-sweep C7 -> C5 over 50ms, percussive. */
static const bento_sfx_step_t k_shooter_fire[] = {
    SWEEP(96, 72, 50, 90),
};
/* enemy-hit/kill: SQUARE 'thunk' C4 + TRIANGLE up-tick C5 -> G5 confirm.
 * Player transposes whole SFX +1 per combo (cap +12). Layer = the TRIANGLE
 * up-tick a perfect octave+ above the thunk. */
static const bento_sfx_step_t k_shooter_enemy_hit[] = {
    N(60, 30, 100),          /* C4 thunk */
    SWEEP(72, 79, 25, 100),  /* C5 -> G5 ding */
};
/* lose-life: SQUARE descending glide G4 -> G3 -> C3 with wobble + SAW sub. */
static const bento_sfx_step_t k_shooter_lose_life[] = {
    N(67, 80, 115),          /* G4 */
    N(55, 80, 115),          /* G3 */
    CHORD(48, 80, 115, -12), /* C3 + SAW-ish octave-below body */
};
/* game-over: TRIANGLE lead descending phrase + SQUARE bass on final tone. */
static const bento_sfx_step_t k_shooter_game_over[] = {
    N(72, 140, 100),         /* C5 */
    N(67, 140, 100),         /* G4 */
    N(63, 140, 100),         /* D#4 */
    N(60, 140, 100),         /* C4 */
    CHORD(55, 400, 100, -19),/* G3 held + SQUARE bass C2 (-19 semis) */
};
/* score tick: SINE quiet C7. */
static const bento_sfx_step_t k_shooter_score[] = {
    N(96, 25, 55),
};
/* score milestone: TRIANGLE ascending arpeggio C6 E6 G6. */
static const bento_sfx_step_t k_shooter_score_milestone[] = {
    N(84, 50, 90),           /* C6 */
    N(88, 50, 90),           /* E6 */
    N(91, 50, 90),           /* G6 */
};
/* wave/ramp: SAW continuous riser C3 -> C6 over 400ms (+ SQUARE siren layer). */
static const bento_sfx_step_t k_shooter_wave[] = {
    SWEEP(48, 84, 400, 95),
};
/* menu-move: SQUARE single note. UP = G5, DOWN = E5; table holds UP, player
 * substitutes E5 (76) for a down-move (spatial mapping). */
static const bento_sfx_step_t k_shooter_ui_nav[] = {
    N(79, 40, 70),           /* G5 (up) / player uses 76 E5 for down */
};
/* select/confirm/start: SQUARE rising fifth C5 -> G5 (+ TRIANGLE C6 sparkle). */
static const bento_sfx_step_t k_shooter_ui_select[] = {
    N(72, 60, 100),          /* C5 */
    N(79, 100, 100),         /* G5 */
};
/* back/cancel/pause: SQUARE falling G5 -> C5. */
static const bento_sfx_step_t k_shooter_ui_back[] = {
    N(79, 50, 80),           /* G5 */
    N(72, 90, 80),           /* C5 */
};
/* error: SQUARE tritone cluster C3 + F#3, then drop to A2. */
static const bento_sfx_step_t k_shooter_ui_error[] = {
    CHORD(48, 90, 85, +6),   /* C3 + F#3 tritone */
    N(45, 60, 85),           /* A2 */
};
/* page-enter: TRIANGLE wide ascending arpeggio C4 G4 C5 E5 G5 (+ SINE shimmer). */
static const bento_sfx_step_t k_shooter_page_enter[] = {
    N(60, 60, 90),           /* C4 */
    N(67, 60, 90),           /* G4 */
    N(72, 60, 90),           /* C5 */
    N(76, 60, 90),           /* E5 */
    N(79, 210, 90),          /* G5 held + SINE octave shimmer via layer */
};

/*******************************************************************************
 * The master table — indexed directly by bento_sfx_id_t.
 *
 * Layout per row:
 *   { name, primary_wave, ADSR(a,d,s,r), steps, n_steps,
 *     layer_wave, layer_semis, layer_vel }
 *******************************************************************************/
#define SFX(arr) (arr), (uint8_t)(sizeof(arr) / sizeof((arr)[0]))

static const bento_sfx_t bento_sfx_table[SFX_ID_COUNT] = {
    /* -------------------------------- SNAKE -------------------------------- */
    [SFX_SNAKE_EAT]        = { "snake.eat",        SFX_WAVE_SQUARE,   ADSR(2,20,0.85f,30),  SFX(k_snake_eat),        SFX_WAVE_NONE,      0,   0 },
    [SFX_SNAKE_STEP]       = { "snake.step",       SFX_WAVE_TRIANGLE, ADSR(1, 6,0.00f, 8),  SFX(k_snake_step),       SFX_WAVE_NONE,      0,   0 },
    [SFX_SNAKE_TURN]       = { "snake.turn",       SFX_WAVE_SQUARE,   ADSR(1,10,0.00f, 8),  SFX(k_snake_turn),       SFX_WAVE_NONE,      0,   0 },
    [SFX_SNAKE_NEAR_MISS]  = { "snake.nearmiss",   SFX_WAVE_SAWTOOTH, ADSR(2, 0,1.00f,25),  SFX(k_snake_near_miss),  SFX_WAVE_NONE,      0,   0 },
    [SFX_SNAKE_GAME_OVER]  = { "snake.gameover",   SFX_WAVE_SQUARE,   ADSR(4,40,0.60f,120), SFX(k_snake_game_over),  SFX_WAVE_TRIANGLE,-12,  70 },
    [SFX_SNAKE_HIGH_SCORE] = { "snake.highscore",  SFX_WAVE_SQUARE,   ADSR(3,30,0.75f,150), SFX(k_snake_high_score), SFX_WAVE_SAWTOOTH, +4,  80 },
    [SFX_SNAKE_RESTART]    = { "snake.restart",    SFX_WAVE_SQUARE,   ADSR(2,25,0.70f,40),  SFX(k_snake_restart),    SFX_WAVE_NONE,      0,   0 },
    [SFX_SNAKE_PAGE_ENTER] = { "snake.pageenter",  SFX_WAVE_TRIANGLE, ADSR(6,50,0.60f,180), SFX(k_snake_page_enter), SFX_WAVE_NONE,      0,   0 },
    [SFX_SNAKE_UI_NAV]     = { "snake.ui.nav",     SFX_WAVE_SQUARE,   ADSR(1,12,0.00f,10),  SFX(k_snake_ui_nav),     SFX_WAVE_NONE,      0,   0 },
    [SFX_SNAKE_UI_SELECT]  = { "snake.ui.select",  SFX_WAVE_SQUARE,   ADSR(1,18,0.50f,30),  SFX(k_snake_ui_select),  SFX_WAVE_NONE,      0,   0 },
    [SFX_SNAKE_UI_BACK]    = { "snake.ui.back",    SFX_WAVE_SQUARE,   ADSR(1,18,0.50f,30),  SFX(k_snake_ui_back),    SFX_WAVE_NONE,      0,   0 },
    [SFX_SNAKE_UI_ERROR]   = { "snake.ui.error",   SFX_WAVE_SAWTOOTH, ADSR(2,20,0.40f,40),  SFX(k_snake_ui_error),   SFX_WAVE_NONE,      0,   0 },

    /* -------------------------------- FLAPPY ------------------------------- */
    [SFX_FLAPPY_FLAP]       = { "flappy.flap",      SFX_WAVE_TRIANGLE, ADSR(2,20,0.00f, 8),  SFX(k_flappy_flap),      SFX_WAVE_NONE,      0,   0 },
    [SFX_FLAPPY_SCORE]      = { "flappy.score",     SFX_WAVE_SQUARE,   ADSR(1,60,0.25f,45),  SFX(k_flappy_score),     SFX_WAVE_TRIANGLE,-12,  60 },
    [SFX_FLAPPY_NEAR_MISS]  = { "flappy.nearmiss",  SFX_WAVE_SAWTOOTH, ADSR(14,0,0.60f,55),  SFX(k_flappy_near_miss), SFX_WAVE_NONE,      0,   0 },
    [SFX_FLAPPY_GAME_OVER]  = { "flappy.gameover",  SFX_WAVE_SQUARE,   ADSR(4,40,0.50f,120), SFX(k_flappy_game_over), SFX_WAVE_TRIANGLE, -4,  60 },
    [SFX_FLAPPY_RESTART]    = { "flappy.restart",   SFX_WAVE_SQUARE,   ADSR(1,45,0.00f,30),  SFX(k_flappy_restart),   SFX_WAVE_NONE,      0,   0 },
    [SFX_FLAPPY_UI_NAV]     = { "flappy.ui.nav",    SFX_WAVE_SQUARE,   ADSR(1,18,0.00f,10),  SFX(k_flappy_ui_nav),    SFX_WAVE_NONE,      0,   0 },
    [SFX_FLAPPY_UI_SELECT]  = { "flappy.ui.select", SFX_WAVE_SQUARE,   ADSR(1,35,0.10f,25),  SFX(k_flappy_ui_select), SFX_WAVE_NONE,      0,   0 },
    [SFX_FLAPPY_UI_BACK]    = { "flappy.ui.back",   SFX_WAVE_TRIANGLE, ADSR(2,35,0.10f,25),  SFX(k_flappy_ui_back),   SFX_WAVE_NONE,      0,   0 },
    [SFX_FLAPPY_UI_ERROR]   = { "flappy.ui.error",  SFX_WAVE_SQUARE,   ADSR(1, 0,0.70f,40),  SFX(k_flappy_ui_error),  SFX_WAVE_NONE,      0,   0 },
    [SFX_FLAPPY_PAGE_ENTER] = { "flappy.pageenter", SFX_WAVE_TRIANGLE, ADSR(6,60,0.40f,120), SFX(k_flappy_page_enter),SFX_WAVE_NONE,      0,   0 },

    /* --------------------------------- PONG -------------------------------- */
    [SFX_PONG_PAGE_ENTER]   = { "pong.pageenter",   SFX_WAVE_TRIANGLE, ADSR(2,40,0.00f,20),  SFX(k_pong_page_enter),  SFX_WAVE_SQUARE,  +12,  70 },
    [SFX_PONG_SERVE]        = { "pong.serve",       SFX_WAVE_TRIANGLE, ADSR(1, 0,0.00f, 8),  SFX(k_pong_serve),       SFX_WAVE_NONE,      0,   0 },
    [SFX_PONG_HIT_PLAYER]   = { "pong.hit.player",  SFX_WAVE_SQUARE,   ADSR(0,38,0.00f, 7),  SFX(k_pong_hit_player),  SFX_WAVE_NONE,      0,   0 },
    [SFX_PONG_HIT_AI]       = { "pong.hit.ai",      SFX_WAVE_TRIANGLE, ADSR(0,42,0.00f, 8),  SFX(k_pong_hit_ai),      SFX_WAVE_SINE,    -12,  35 },
    [SFX_PONG_HIT_WALL]     = { "pong.hit.wall",    SFX_WAVE_TRIANGLE, ADSR(0,30,0.00f, 8),  SFX(k_pong_hit_wall),    SFX_WAVE_NONE,      0,   0 },
    [SFX_PONG_SCORE_PLAYER] = { "pong.score.player",SFX_WAVE_SQUARE,   ADSR(1,40,0.00f, 8),  SFX(k_pong_score_player),SFX_WAVE_NONE,      0,   0 },
    [SFX_PONG_SCORE_AI]     = { "pong.score.ai",    SFX_WAVE_SQUARE,   ADSR(1,40,0.00f, 8),  SFX(k_pong_score_ai),    SFX_WAVE_NONE,      0,   0 },
    [SFX_PONG_MATCH_POINT]  = { "pong.matchpoint",  SFX_WAVE_SAWTOOTH, ADSR(180,0,0.60f,300),SFX(k_pong_match_point), SFX_WAVE_NONE,      0,   0 },
    [SFX_PONG_WIN_FANFARE]  = { "pong.win",         SFX_WAVE_SQUARE,   ADSR(2,60,0.40f,120), SFX(k_pong_win_fanfare), SFX_WAVE_TRIANGLE, -4,  80 },
    [SFX_PONG_LOSE]         = { "pong.lose",        SFX_WAVE_TRIANGLE, ADSR(4,120,0.30f,120),SFX(k_pong_lose),        SFX_WAVE_NONE,      0,   0 },
    [SFX_PONG_RESTART]      = { "pong.restart",     SFX_WAVE_SQUARE,   ADSR(1, 0,0.50f,20),  SFX(k_pong_restart),     SFX_WAVE_NONE,      0,   0 },
    [SFX_PONG_UI_NAV]       = { "pong.ui.nav",      SFX_WAVE_SQUARE,   ADSR(0,18,0.00f, 4),  SFX(k_pong_ui_nav),      SFX_WAVE_NONE,      0,   0 },
    [SFX_PONG_UI_SELECT]    = { "pong.ui.select",   SFX_WAVE_TRIANGLE, ADSR(1,38,0.00f, 8),  SFX(k_pong_ui_select),   SFX_WAVE_SQUARE,  +12,  55 },
    [SFX_PONG_UI_BACK]      = { "pong.ui.back",     SFX_WAVE_TRIANGLE, ADSR(1,40,0.00f, 8),  SFX(k_pong_ui_back),     SFX_WAVE_NONE,      0,   0 },
    [SFX_PONG_UI_ERROR]     = { "pong.ui.error",    SFX_WAVE_SQUARE,   ADSR(0,50,0.00f, 8),  SFX(k_pong_ui_error),    SFX_WAVE_NONE,      0,   0 },

    /* ------------------------------ SPACE SHOOTER -------------------------- */
    [SFX_SHOOTER_FIRE]            = { "shooter.fire",       SFX_WAVE_SAWTOOTH, ADSR(1, 8,0.00f,12),  SFX(k_shooter_fire),            SFX_WAVE_NONE,      0,   0 },
    [SFX_SHOOTER_ENEMY_HIT]       = { "shooter.enemyhit",   SFX_WAVE_SQUARE,   ADSR(1, 6,0.00f,10),  SFX(k_shooter_enemy_hit),       SFX_WAVE_TRIANGLE,  0,   0 },
    [SFX_SHOOTER_LOSE_LIFE]       = { "shooter.loselife",   SFX_WAVE_SQUARE,   ADSR(3,40,0.50f,90),  SFX(k_shooter_lose_life),       SFX_WAVE_SAWTOOTH,-12,  90 },
    [SFX_SHOOTER_GAME_OVER]       = { "shooter.gameover",   SFX_WAVE_TRIANGLE, ADSR(5,60,0.60f,120), SFX(k_shooter_game_over),       SFX_WAVE_SQUARE,  -12,  90 },
    [SFX_SHOOTER_SCORE]           = { "shooter.score",      SFX_WAVE_SINE,     ADSR(1,10,0.00f, 8),  SFX(k_shooter_score),           SFX_WAVE_NONE,      0,   0 },
    [SFX_SHOOTER_SCORE_MILESTONE] = { "shooter.milestone",  SFX_WAVE_TRIANGLE, ADSR(2,20,0.00f,30),  SFX(k_shooter_score_milestone), SFX_WAVE_NONE,      0,   0 },
    [SFX_SHOOTER_WAVE]            = { "shooter.wave",       SFX_WAVE_SAWTOOTH, ADSR(20,0,1.00f,80),  SFX(k_shooter_wave),            SFX_WAVE_SQUARE,    0,  60 },
    [SFX_SHOOTER_UI_NAV]          = { "shooter.ui.nav",     SFX_WAVE_SQUARE,   ADSR(1, 8,0.00f,10),  SFX(k_shooter_ui_nav),          SFX_WAVE_NONE,      0,   0 },
    [SFX_SHOOTER_UI_SELECT]       = { "shooter.ui.select",  SFX_WAVE_SQUARE,   ADSR(2,20,0.40f,50),  SFX(k_shooter_ui_select),       SFX_WAVE_TRIANGLE,+12,  60 },
    [SFX_SHOOTER_UI_BACK]         = { "shooter.ui.back",    SFX_WAVE_SQUARE,   ADSR(1,15,0.30f,45),  SFX(k_shooter_ui_back),         SFX_WAVE_NONE,      0,   0 },
    [SFX_SHOOTER_UI_ERROR]        = { "shooter.ui.error",   SFX_WAVE_SQUARE,   ADSR(1,30,0.00f,20),  SFX(k_shooter_ui_error),        SFX_WAVE_NONE,      0,   0 },
    [SFX_SHOOTER_PAGE_ENTER]      = { "shooter.pageenter",  SFX_WAVE_TRIANGLE, ADSR(3,30,0.50f,100), SFX(k_shooter_page_enter),      SFX_WAVE_SINE,    +12,  45 },
};

/*******************************************************************************
 * Tiny helper API (the heavy lifting stays in the DDS engine).
 *
 * bento_sfx_get(id)   -> const descriptor, or NULL if id out of range.
 * bento_sfx_count()   -> number of SFX in the table.
 *
 * A platform player wires these into the engine, e.g.:
 *
 *   void bento_sfx_play(bento_sfx_id_t id) {
 *       const bento_sfx_t *s = bento_sfx_get(id);
 *       if (!s) return;
 *       sfx_seq_start(s);           // per-voice: set waveform + ADSR, then
 *                                   // walk steps: note_on(step.note,vel) for
 *                                   // step.dur_ms, sweep phase_inc note->note_to,
 *                                   // add unison note+chord_semis if nonzero,
 *                                   // and run a parallel layer voice when
 *                                   // layer_wave != SFX_WAVE_NONE.
 *   }
 *
 * The engine already owns: s_note_phase_inc[] (note->phase_inc), the wavetable
 * for each SFX_WAVE_*, the ADSR state machine, and the 48 kHz ISR mixer.
 *******************************************************************************/
const bento_sfx_t *bento_sfx_get(bento_sfx_id_t id)
{
    if ((unsigned)id >= (unsigned)SFX_ID_COUNT) {
        return NULL;
    }
    return &bento_sfx_table[id];
}

unsigned bento_sfx_count(void)
{
    return (unsigned)SFX_ID_COUNT;
}
