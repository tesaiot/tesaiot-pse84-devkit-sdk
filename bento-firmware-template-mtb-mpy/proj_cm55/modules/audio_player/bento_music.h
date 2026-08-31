/*
 * bento_music.h — tiny live chiptune backing-track sequencer for the BENTO games.
 *
 * Zero storage: each game's music is a short looping note pattern (bass + lead)
 * played live through the bento_sfx DDS mixer as low-priority "music" voices that
 * sit under the SFX. A step clock (16th-note grid) is advanced from the CM55 GFX
 * loop via bento_music_tick(now_ms); notes are fired at their step and the pattern
 * loops. No SD card, no files — fits the "sound without an SD card" requirement.
 */
#ifndef BENTO_MUSIC_H
#define BENTO_MUSIC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BENTO_MUSIC_NONE = 0,
    BENTO_MUSIC_SNAKE,
    BENTO_MUSIC_FLAPPY,
    BENTO_MUSIC_PONG,
    BENTO_MUSIC_SHOOTER,
} bento_music_track_id_t;

/* Start looping a game's backing track (BENTO_MUSIC_NONE stops). Idempotent if the
 * same track is already playing. */
void bento_music_start(bento_music_track_id_t track);

/* Stop the backing track (fades out the music voices). */
void bento_music_stop(void);

/* Advance the sequencer. Call every GFX-loop iteration with a millisecond clock
 * (e.g. xTaskGetTickCount()*portTICK_PERIOD_MS). Cheap; fires due notes + loops. */
void bento_music_tick(uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* BENTO_MUSIC_H */
