/*
 * bento_sfx.c — low-latency polyphonic game SFX mixer (DDS chiptune + PCM one-shots).
 *
 * The DDS primitives (wavetable, note→phase-inc table, ADSR) mirror the proven
 * bento_midi synth. This module adds: a fire-and-forget multi-segment patch model
 * (so one trigger can blip / sweep / arpeggiate), a shared voice pool that also holds
 * 16-bit PCM one-shots, and a single mixer the TDM ISR pumps. No malloc/printf/FatFS
 * in the render path; the PCM path is pure integer. See [[project_game_sfx_decision]].
 */
#include "bento_sfx.h"
#include "cy_pdl.h"      /* __DMB */
#include <math.h>
#include <string.h>

#define SFX_SAMPLE_RATE     48000u
#define SFX_WAVETABLE_LEN   256u
#define SFX_WAVE_COUNT      4u
#define SFX_MAX_VOICES      8u      /* ISR budget: ~50-100us of 1.3ms at 48kHz */

/* ---- DDS tables (built once) ---- */
static int16_t  s_wavetable[SFX_WAVE_COUNT][SFX_WAVETABLE_LEN];
static uint32_t s_note_phase_inc[128];
static bool     s_inited;

/* ---- ADSR ---- default envelope (snappy, for gameplay SFX). Per-voice copies let
 * the startup fanfare use slow-attack pads and long-release tails (see sfx_env_t). */
#define SFX_ATTACK_MS    4.0f
#define SFX_DECAY_MS     40.0f
#define SFX_SUSTAIN_LVL  0.75f
#define SFX_RELEASE_MS   90.0f
static float s_attack_inc, s_decay_inc, s_release_inc;
static float s_spm;      /* samples per millisecond (sample_rate / 1000) */

/* Per-voice envelope spec in human units; NULL at a call site = use the snappy default. */
typedef struct { float a_ms, d_ms, s_lvl, r_ms; } sfx_env_t;

typedef enum { ENV_OFF = 0, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE } env_stage_t;
enum { VK_SYNTH = 0, VK_PCM = 1 };

/* ---- A patch = a sequence of segments (pitch steps / glides) under one envelope ---- */
typedef struct {
    uint8_t  note;     /* MIDI note at segment start */
    uint8_t  target;   /* glide target note (== note for a flat tone) */
    uint8_t  wave;     /* sfx_wave_t */
    uint16_t dur_ms;   /* segment length */
} sfx_seg_t;

typedef struct {
    const sfx_seg_t *segs;
    uint8_t          nsegs;
    uint8_t          vel;     /* 1..127 */
} sfx_patch_t;

/* ---- Runtime voice (synth or pcm) ---- */
typedef struct {
    volatile bool active;     /* set TRUE last by the writer, with a barrier */
    uint8_t  kind;
    uint8_t  is_music;        /* 1 = backing-music voice (lower level, steal-first) */

    /* synth */
    const sfx_seg_t *segs;
    uint8_t  nsegs, seg_idx, wave, vel;
    uint32_t phase, phase_inc, phase_inc_target;
    int32_t  phase_inc_slope;
    uint32_t seg_samp_left;
    float    env_level;
    uint8_t  env_stage;
    /* Per-voice ADSR rates (copied from the default, or overridden via sfx_env_t). */
    float    env_a_inc, env_d_inc, env_s_lvl, env_r_inc;

    /* pcm */
    const int16_t *pcm;
    uint32_t pcm_len, pcm_cursor;
    uint16_t pcm_gain;        /* 256 = unity */

    /* backing store for bento_sfx_play_tone()'s transient single segment, so the
     * voice's segs pointer stays valid for the voice's lifetime */
    sfx_seg_t _oneshot_seg;

    /* Scheduled start: while > 0 the voice is reserved (active) but renders silence
     * and the ISR decrements it each sample; the voice begins sounding when it hits
     * 0. Lets one fire-and-forget call lay down a timed, multi-layer score (e.g. the
     * 3-phase power-on fanfare) without the writer blocking between layers. */
    uint32_t start_delay;

    uint32_t age;
} sfx_voice_t;

static sfx_voice_t s_voices[SFX_MAX_VOICES];
static uint32_t    s_age;

/* ===========================================================================
 * SFX patch table — default arcade voices for every SFX ID. The sound-design
 * pass refines these; this set is fully playable today. Notes are MIDI numbers
 * (60 = middle C). target!=note = pitch sweep over the segment.
 * ========================================================================= */
#define SQ  SFX_WAVE_SQUARE
#define TRI SFX_WAVE_TRIANGLE
#define SAW SFX_WAVE_SAW
#define SIN SFX_WAVE_SINE

static const sfx_seg_t SEG_UI_MOVE[]    = { {84, 84, SQ, 30} };
static const sfx_seg_t SEG_UI_SELECT[]  = { {84, 84, SQ, 40}, {91, 91, SQ, 70} };
static const sfx_seg_t SEG_UI_BACK[]    = { {79, 79, SQ, 40}, {72, 72, SQ, 70} };
static const sfx_seg_t SEG_UI_DENY[]    = { {52, 49, SAW, 160} };
static const sfx_seg_t SEG_UI_START[]   = { {72,72,SQ,60},{76,76,SQ,60},{79,79,SQ,60},{84,84,SQ,120} };

static const sfx_seg_t SEG_SNAKE_EAT[]  = { {76, 83, SQ, 70} };          /* up blip */
static const sfx_seg_t SEG_SNAKE_TURN[] = { {64, 64, TRI, 18} };         /* tiny tick */
static const sfx_seg_t SEG_SNAKE_DIE[]  = { {72, 36, SAW, 450} };        /* down crash */

static const sfx_seg_t SEG_FLAP[]       = { {60, 72, TRI, 70} };         /* whoosh up */
static const sfx_seg_t SEG_FLAP_SCORE[] = { {88, 88, SQ, 50}, {95, 95, SQ, 80} };  /* coin */
static const sfx_seg_t SEG_FLAP_DIE[]   = { {64, 30, SAW, 380} };        /* splat */

static const sfx_seg_t SEG_PONG_WALL[]  = { {67, 67, SQ, 35} };
static const sfx_seg_t SEG_PONG_PADL[]  = { {76, 76, SQ, 40} };
static const sfx_seg_t SEG_PONG_SCORE[] = { {72, 79, SQ, 120} };
static const sfx_seg_t SEG_PONG_WIN[]   = { {72,72,SQ,90},{76,76,SQ,90},{79,79,SQ,90},{84,84,SQ,180} };
static const sfx_seg_t SEG_PONG_LOSE[]  = { {64,64,SAW,120},{60,60,SAW,120},{55,55,SAW,240} };

static const sfx_seg_t SEG_SHOOT_FIRE[] = { {100, 76, SAW, 90} };        /* laser zap */
static const sfx_seg_t SEG_SHOOT_HIT[]  = { {84, 78, SQ, 55} };
static const sfx_seg_t SEG_SHOOT_BOOM[] = { {48, 24, SAW, 320} };        /* explosion */
static const sfx_seg_t SEG_SHOOT_LIFE[] = { {55, 34, SAW, 300} };

static const sfx_seg_t SEG_GAME_OVER[]  = { {67,67,TRI,200},{63,63,TRI,200},{60,60,TRI,200},{48,48,TRI,500} };

/* Power-on fanfare v3 — evidence-based redesign (music-psychology + game-audio research,
 * see TESAIoT_PLAN/2026-6/Game_Audio_SFX). Goal: bright, RELAXED, "ready to play".
 *
 * LEAD line (one voice, sequential segments): a calm sine note wakes, then a triangle
 * ascent climbs through the maj7 (B4 — gentle anticipation) and resolves/holds on the
 * tonic C5 at the arrival. Warm timbres (sine/triangle) per the "even-harmonic = friendly"
 * finding; the brightness build comes from layered voices entering over time, not volume.
 *   awakening:  C4 sine, slow & unhurried
 *   rising:     E4 G4 B4 triangle, climbing (B4 = maj7 colour)
 *   arrival:    C5 held (melodic apex; the harmony blooms around it in bento_sfx_startup) */
static const sfx_seg_t SEG_STARTUP_LEAD[] = {
    {60,60,SIN,520},                                  /* awakening: C4 */
    {64,64,TRI,240},{67,67,TRI,240},{71,71,TRI,240},  /* rising: E4 G4 B4 */
    {72,72,TRI,620}                                   /* arrival apex: C5 held */
};

/* Envelope specs (human units) for the fanfare layers. Default gameplay ADSR is snappy;
 * the fanfare wants slow swells and long, soft release tails (research: long attack =
 * calm; faded tail = "resolved, not sharp"). */
static const sfx_env_t ENV_FANFARE_PAD     = { .a_ms = 350, .d_ms = 200, .s_lvl = 0.80f, .r_ms = 550 };
static const sfx_env_t ENV_FANFARE_LEAD    = { .a_ms = 55,  .d_ms = 70,  .s_lvl = 0.78f, .r_ms = 300 };
static const sfx_env_t ENV_FANFARE_ARRIVAL = { .a_ms = 70,  .d_ms = 120, .s_lvl = 0.80f, .r_ms = 650 };
static const sfx_env_t ENV_FANFARE_SPARKLE = { .a_ms = 10,  .d_ms = 160, .s_lvl = 0.30f, .r_ms = 350 };
static const sfx_env_t ENV_FANFARE_BLIP    = { .a_ms = 22,  .d_ms = 90,  .s_lvl = 0.45f, .r_ms = 320 };

#define PATCH(arr, v)  { (arr), (uint8_t)(sizeof(arr)/sizeof((arr)[0])), (v) }

static const sfx_patch_t s_patches[SFX_COUNT] = {
    [SFX_UI_MOVE]       = PATCH(SEG_UI_MOVE,    70),
    [SFX_UI_SELECT]     = PATCH(SEG_UI_SELECT,  90),
    [SFX_UI_BACK]       = PATCH(SEG_UI_BACK,    80),
    [SFX_UI_DENY]       = PATCH(SEG_UI_DENY,    90),
    [SFX_UI_START]      = PATCH(SEG_UI_START,  100),
    [SFX_SNAKE_EAT]     = PATCH(SEG_SNAKE_EAT,  95),
    [SFX_SNAKE_TURN]    = PATCH(SEG_SNAKE_TURN, 55),
    [SFX_SNAKE_DIE]     = PATCH(SEG_SNAKE_DIE, 110),
    [SFX_FLAPPY_FLAP]   = PATCH(SEG_FLAP,       80),
    [SFX_FLAPPY_SCORE]  = PATCH(SEG_FLAP_SCORE, 95),
    [SFX_FLAPPY_DIE]    = PATCH(SEG_FLAP_DIE,  110),
    [SFX_PONG_WALL]     = PATCH(SEG_PONG_WALL,  80),
    [SFX_PONG_PADDLE]   = PATCH(SEG_PONG_PADL,  90),
    [SFX_PONG_SCORE]    = PATCH(SEG_PONG_SCORE, 95),
    [SFX_PONG_WIN]      = PATCH(SEG_PONG_WIN,  100),
    [SFX_PONG_LOSE]     = PATCH(SEG_PONG_LOSE,  95),
    [SFX_SHOOT_FIRE]    = PATCH(SEG_SHOOT_FIRE, 85),
    [SFX_SHOOT_HIT]     = PATCH(SEG_SHOOT_HIT,  85),
    [SFX_SHOOT_EXPLODE] = PATCH(SEG_SHOOT_BOOM,110),
    [SFX_SHOOT_LOSE_LIFE]= PATCH(SEG_SHOOT_LIFE,100),
    [SFX_GAME_OVER]     = PATCH(SEG_GAME_OVER, 100),
    /* SFX_STARTUP has no event/patch entry: the power-on fanfare is played by
     * bento_sfx_startup() as a timed, multi-voice scheduled score, not a single patch. */
};

/* ===========================================================================
 * Init
 * ========================================================================= */
static void build_tables(void)
{
    for (uint32_t i = 0; i < SFX_WAVETABLE_LEN; i++) {
        float ph = (float)i / (float)SFX_WAVETABLE_LEN;
        s_wavetable[SFX_WAVE_SINE][i]     = (int16_t)(16000.0f * sinf(2.0f * 3.14159265f * ph));
        s_wavetable[SFX_WAVE_SQUARE][i]   = (int16_t)(ph < 0.5f ? 12000 : -12000);
        s_wavetable[SFX_WAVE_TRIANGLE][i] = (int16_t)(16000.0f * (ph < 0.5f ? (4.0f * ph - 1.0f) : (3.0f - 4.0f * ph)));
        s_wavetable[SFX_WAVE_SAW][i]      = (int16_t)(16000.0f * (2.0f * ph - 1.0f));
    }
    for (int n = 0; n < 128; n++) {
        float f = 440.0f * powf(2.0f, ((float)n - 69.0f) / 12.0f);
        s_note_phase_inc[n] = (uint32_t)((f * (float)SFX_WAVETABLE_LEN * 65536.0f) / (float)SFX_SAMPLE_RATE);
    }
    float spm = (float)SFX_SAMPLE_RATE / 1000.0f;
    s_spm = spm;
    s_attack_inc  = 1.0f / (SFX_ATTACK_MS * spm);
    s_decay_inc   = (1.0f - SFX_SUSTAIN_LVL) / (SFX_DECAY_MS * spm);
    s_release_inc = SFX_SUSTAIN_LVL / (SFX_RELEASE_MS * spm);
}

void bento_sfx_init(void)
{
    if (s_inited) return;
    build_tables();
    memset(s_voices, 0, sizeof(s_voices));
    s_age = 0;
    s_inited = true;
}

/* ===========================================================================
 * Voice allocation (writer / task context) — lock-free vs the ISR
 * ========================================================================= */
static sfx_voice_t *alloc_voice(void)
{
    for (uint32_t i = 0; i < SFX_MAX_VOICES; i++) {
        if (!s_voices[i].active) return &s_voices[i];
    }
    /* No free voice — steal the oldest MUSIC voice first (gameplay SFX must never be
     * starved by the backing track); fall back to the oldest voice overall. */
    uint32_t oldest = UINT32_MAX; uint32_t idx = 0; int found_music = 0;
    for (uint32_t i = 0; i < SFX_MAX_VOICES; i++) {
        if (s_voices[i].is_music && s_voices[i].age < oldest) { oldest = s_voices[i].age; idx = i; found_music = 1; }
    }
    if (!found_music) {
        for (uint32_t i = 0; i < SFX_MAX_VOICES; i++) {
            if (s_voices[i].age < oldest) { oldest = s_voices[i].age; idx = i; }
        }
    }
    s_voices[idx].active = false;   /* take it off-line before refilling */
    __DMB();
    return &s_voices[idx];
}

static uint32_t ms_to_samples(uint16_t ms)
{
    return (uint32_t)ms * (SFX_SAMPLE_RATE / 1000u);
}

/* Load segment seg_idx's pitch/glide/timing into the voice. */
static void load_segment(sfx_voice_t *v)
{
    const sfx_seg_t *s = &v->segs[v->seg_idx];
    uint8_t n  = (s->note   < 128) ? s->note   : 127;
    uint8_t tg = (s->target < 128) ? s->target : 127;
    v->wave            = (s->wave < SFX_WAVE_COUNT) ? s->wave : SFX_WAVE_SQUARE;
    v->phase_inc       = s_note_phase_inc[n];
    v->phase_inc_target= s_note_phase_inc[tg];
    v->seg_samp_left   = ms_to_samples(s->dur_ms);
    if (v->seg_samp_left == 0) v->seg_samp_left = 1;
    v->phase_inc_slope = (int32_t)((int64_t)((int32_t)v->phase_inc_target - (int32_t)v->phase_inc)
                                   / (int32_t)v->seg_samp_left);
}

/* Set a voice's ADSR rates from a spec, or from the snappy default when env == NULL. */
static void set_voice_env(sfx_voice_t *v, const sfx_env_t *env)
{
    if (env == NULL) {
        v->env_a_inc = s_attack_inc;
        v->env_d_inc = s_decay_inc;
        v->env_s_lvl = SFX_SUSTAIN_LVL;
        v->env_r_inc = s_release_inc;
        return;
    }
    float a = (env->a_ms > 0.1f) ? env->a_ms : 0.1f;
    float d = (env->d_ms > 0.1f) ? env->d_ms : 0.1f;
    float r = (env->r_ms > 0.1f) ? env->r_ms : 0.1f;
    float s = env->s_lvl;
    if (s < 0.0f) s = 0.0f; else if (s > 1.0f) s = 1.0f;
    v->env_a_inc = 1.0f / (a * s_spm);
    v->env_d_inc = (1.0f - s) / (d * s_spm);
    v->env_s_lvl = s;
    v->env_r_inc = (s > 0.0f ? s : 1.0f) / (r * s_spm);
}

static void start_synth(sfx_voice_t *v, const sfx_seg_t *segs, uint8_t nsegs, uint8_t vel,
                        uint8_t is_music, uint32_t delay_samples, const sfx_env_t *env)
{
    v->kind       = VK_SYNTH;
    v->is_music   = is_music;
    v->segs       = segs;
    v->nsegs      = nsegs;
    v->seg_idx    = 0;
    v->vel        = (vel == 0) ? 1 : vel;
    v->phase      = 0;
    v->env_level  = 0.0f;
    v->env_stage  = ENV_ATTACK;
    v->start_delay= delay_samples;   /* 0 = sound now; >0 = scheduled (silent until then) */
    set_voice_env(v, env);
    v->age        = ++s_age;
    load_segment(v);
    __DMB();
    v->active     = true;   /* publish last */
}

/* Schedule a single sustained note (uses the voice's persistent _oneshot_seg store).
 * delay_ms staggers when the note enters; is_music drops it under the SFX level. */
static void schedule_note(uint8_t note, sfx_wave_t wave, uint8_t vel,
                          uint16_t dur_ms, uint8_t is_music, uint16_t delay_ms,
                          const sfx_env_t *env)
{
    sfx_voice_t *v = alloc_voice();
    v->_oneshot_seg.note   = note;
    v->_oneshot_seg.target = note;
    v->_oneshot_seg.wave   = (uint8_t)wave;
    v->_oneshot_seg.dur_ms = dur_ms;
    start_synth(v, &v->_oneshot_seg, 1, vel, is_music, ms_to_samples(delay_ms), env);
}

/* Schedule a multi-segment patch (segs must point to storage that outlives the voice —
 * a static const array). delay_ms staggers when the line enters. */
static void schedule_patch(const sfx_seg_t *segs, uint8_t nsegs, uint8_t vel,
                           uint16_t delay_ms, const sfx_env_t *env)
{
    sfx_voice_t *v = alloc_voice();
    start_synth(v, segs, nsegs, vel, 0, ms_to_samples(delay_ms), env);
}

void bento_sfx_play_tone(uint8_t note, sfx_wave_t wave, uint8_t velocity, uint16_t dur_ms)
{
    static sfx_seg_t tmp;   /* single transient note; copied into the voice path below */
    if (!s_inited) bento_sfx_init();
    tmp.note = note; tmp.target = note; tmp.wave = (uint8_t)wave; tmp.dur_ms = dur_ms;
    sfx_voice_t *v = alloc_voice();
    /* copy the segment into a per-voice store so the pointer stays valid */
    v->kind = VK_SYNTH;
    v->_oneshot_seg = tmp;            /* see struct note below */
    start_synth(v, &v->_oneshot_seg, 1, velocity, 0, 0, NULL);
}

void bento_sfx_play_clip(const int16_t *pcm, uint32_t len, uint16_t gain)
{
    if (!s_inited) bento_sfx_init();
    if (pcm == NULL || len == 0) return;
    sfx_voice_t *v = alloc_voice();
    v->kind       = VK_PCM;
    v->pcm        = pcm;
    v->pcm_len    = len;
    v->pcm_cursor = 0;
    v->pcm_gain   = (gain == 0) ? 256 : gain;
    v->start_delay= 0;
    v->age        = ++s_age;
    __DMB();
    v->active     = true;
}

void bento_sfx_event(sfx_id_t id)
{
    if (!s_inited) bento_sfx_init();
    if ((uint32_t)id >= (uint32_t)SFX_COUNT) return;
    const sfx_patch_t *p = &s_patches[id];
    if (p->segs == NULL || p->nsegs == 0) return;
    sfx_voice_t *v = alloc_voice();
    start_synth(v, p->segs, p->nsegs, p->vel, 0, 0, NULL);
}

/* ===========================================================================
 * Backing music — a single note tagged as a low-priority, lower-level voice.
 * Reuses the synth path via the per-voice _oneshot_seg store.
 * ========================================================================= */
void bento_sfx_play_music(uint8_t note, sfx_wave_t wave, uint8_t velocity, uint16_t dur_ms)
{
    if (!s_inited) bento_sfx_init();
    sfx_voice_t *v = alloc_voice();
    v->_oneshot_seg.note   = note;
    v->_oneshot_seg.target = note;
    v->_oneshot_seg.wave   = (uint8_t)wave;
    v->_oneshot_seg.dur_ms = dur_ms;
    start_synth(v, &v->_oneshot_seg, 1, velocity, 1, 0, NULL);
}

void bento_sfx_stop_music(void)
{
    for (uint32_t i = 0; i < SFX_MAX_VOICES; i++) {
        if (s_voices[i].active && s_voices[i].is_music) {
            s_voices[i].env_stage = ENV_RELEASE;   /* let it fade, not click */
        }
    }
}

void bento_sfx_startup(void)
{
    if (!s_inited) bento_sfx_init();
    /* Three-movement power-on fanfare (~3.8 s) laid down in one fire-and-forget call;
     * the ISR sequences the layers via each voice's scheduled start, so the boot task is
     * never blocked. Evidence-based (music-psychology + game-audio research):
     *   AWAKENING: a warm open-fifth pad (C3+G3 sine) swells from near-silence (slow
     *              attack) while the sine lead enters — calm, unhurried, "from nothing".
     *   RISING:    the lead climbs E4-G4-B4 (maj7 = gentle anticipation) to the tonic.
     *   ARRIVAL:   the major third (E4) then a high octave double (C6) bloom in, staggered
     *              low->high so the chord OPENS UP; the lone square sparkle (E6) enters
     *              LAST so brightness rises without raising loudness = grand, not harsh.
     *              Long releases let it ring.
     *   READY:     a soft triangle tonic blip (C5), low and gently faded = "press start"
     *              as a warm invitation, not an alert.
     * 7 voices total (<= 8 budget); all warm timbres except the single square sparkle.
     * Delays/durations in ms (codec is 48 kHz). */

    /* AWAKENING — open-fifth pad bed, slow swell (music level, sits under the lead). */
    schedule_note(48, SIN, 88, 3500, 1,   0, &ENV_FANFARE_PAD);   /* C3 root drone  */
    schedule_note(55, SIN, 84, 3350, 1, 150, &ENV_FANFARE_PAD);   /* G3 fifth drone */

    /* LEAD — sine wake -> triangle ascent -> held tonic C5 (full level). */
    schedule_patch(SEG_STARTUP_LEAD,
                   (uint8_t)(sizeof(SEG_STARTUP_LEAD)/sizeof(SEG_STARTUP_LEAD[0])),
                   100, 450, &ENV_FANFARE_LEAD);

    /* ARRIVAL — the chord blooms: major third, then a high octave double, staggered. */
    schedule_note(64, TRI, 90, 1450, 0, 2050, &ENV_FANFARE_ARRIVAL);  /* E4 (completes C major) */
    schedule_note(84, TRI, 80, 1300, 0, 2200, &ENV_FANFARE_ARRIVAL);  /* C6 octave double (grandeur) */
    schedule_note(88, SQ,  72,  700, 0, 2450, &ENV_FANFARE_SPARKLE);  /* E6 sparkle (enters last) */

    /* READY — soft, warm tonic blip; the gentle "press start" invitation. */
    schedule_note(72, TRI, 72,  340, 0, 3150, &ENV_FANFARE_BLIP);     /* C5 */
}

/* ===========================================================================
 * Mixer — called from the TDM ISR (mono sample)
 * ========================================================================= */
static int32_t render_synth(sfx_voice_t *v)
{
    /* envelope (per-voice rates) */
    switch (v->env_stage) {
        case ENV_ATTACK:  v->env_level += v->env_a_inc;
                          if (v->env_level >= 1.0f) { v->env_level = 1.0f; v->env_stage = ENV_DECAY; } break;
        case ENV_DECAY:   v->env_level -= v->env_d_inc;
                          if (v->env_level <= v->env_s_lvl) { v->env_level = v->env_s_lvl; v->env_stage = ENV_SUSTAIN; } break;
        case ENV_SUSTAIN: break;
        case ENV_RELEASE: v->env_level -= v->env_r_inc;
                          if (v->env_level <= 0.0f) { v->env_level = 0.0f; v->active = false; return 0; } break;
        default:          v->active = false; return 0;
    }

    uint8_t idx = (uint8_t)(v->phase >> 16);
    int16_t raw = s_wavetable[v->wave][idx];
    /* Music sits ~45% under the SFX so gameplay cues always cut through. */
    float lvl = v->env_level * ((float)v->vel / 127.0f);
    if (v->is_music) { lvl *= 0.45f; }
    int32_t s = (int32_t)((float)raw * lvl);

    v->phase += v->phase_inc;

    /* pitch glide toward the segment target */
    if (v->phase_inc != v->phase_inc_target) {
        int32_t ni = (int32_t)v->phase_inc + v->phase_inc_slope;
        if ((v->phase_inc_slope >= 0 && (uint32_t)ni >= v->phase_inc_target) ||
            (v->phase_inc_slope <  0 && (uint32_t)ni <= v->phase_inc_target)) {
            v->phase_inc = v->phase_inc_target;
        } else {
            v->phase_inc = (uint32_t)ni;
        }
    }

    /* segment timing (only while not releasing) */
    if (v->env_stage != ENV_RELEASE) {
        if (v->seg_samp_left > 0) v->seg_samp_left--;
        if (v->seg_samp_left == 0) {
            if ((uint32_t)(v->seg_idx + 1) < v->nsegs) {
                v->seg_idx++;
                v->phase = 0;
                load_segment(v);
                /* small re-attack so stepped notes (jingles) re-pluck */
                v->env_stage = ENV_ATTACK;
            } else {
                v->env_stage = ENV_RELEASE;
            }
        }
    }
    return s;
}

static int32_t render_pcm(sfx_voice_t *v)
{
    /* Snapshot the descriptor once and bounds-check before indexing, so a voice
     * stolen/refilled by the writer mid-render can never index out of range
     * (worst case a single stale sample = an imperceptible click, never a fault;
     * PCM clips live in const flash/RAM which is always mapped). */
    const int16_t *p = v->pcm;
    uint32_t len = v->pcm_len;
    uint32_t cur = v->pcm_cursor;
    if (p == NULL || cur >= len) { v->active = false; return 0; }
    int32_t s = ((int32_t)p[cur] * (int32_t)v->pcm_gain) >> 8;
    v->pcm_cursor = cur + 1;
    if (cur + 1 >= len) v->active = false;
    return s;
}

int16_t bento_sfx_mixer_get_sample(void)
{
    int32_t mix = 0;
    int n = 0;
    for (uint32_t i = 0; i < SFX_MAX_VOICES; i++) {
        sfx_voice_t *v = &s_voices[i];
        if (!v->active) continue;
        if (v->start_delay) { v->start_delay--; continue; }  /* scheduled: reserved but silent */
        mix += (v->kind == VK_PCM) ? render_pcm(v) : render_synth(v);
        n++;
    }
    if (n > 1) mix /= (n > 4 ? 4 : n);
    if (mix >  32767) mix =  32767;
    if (mix < -32768) mix = -32768;
    return (int16_t)mix;
}

bool bento_sfx_active(void)
{
    for (uint32_t i = 0; i < SFX_MAX_VOICES; i++) {
        if (s_voices[i].active) return true;
    }
    return false;
}
