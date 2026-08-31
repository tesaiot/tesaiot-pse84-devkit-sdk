/* AUTO-GENERATED from gen_game_audio.py WAV assets, UPSAMPLED to 48 kHz to match
 * the codec/mixer rate (1:1 cursor advance in render_pcm). Embedded 16-bit mono
 * PCM hero SFX in flash for the bento_sfx PCM voice path. */
#ifndef BENTO_SFX_PCM_ASSETS_H
#define BENTO_SFX_PCM_ASSETS_H
#include <stdint.h>

extern const int16_t SFX_PCM_BOOM[];
#define SFX_PCM_BOOM_LEN 8640u   /* 180 ms @ 48000Hz */
extern const int16_t SFX_PCM_SNAKE_WALL[];
#define SFX_PCM_SNAKE_WALL_LEN 8640u   /* 180 ms @ 48000Hz */
extern const int16_t SFX_PCM_SNAKE_SELF[];
#define SFX_PCM_SNAKE_SELF_LEN 10560u   /* 220 ms @ 48000Hz */
extern const int16_t SFX_PCM_COIN[];
#define SFX_PCM_COIN_LEN 15360u   /* 320 ms @ 48000Hz */

#endif
