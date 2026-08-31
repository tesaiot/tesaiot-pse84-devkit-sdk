/*******************************************************************************
 * File Name: bento_tsf.h
 *
 * Description: TinySoundFont wrapper for BENTO Eva Kit.
 *              Loads SF2 SoundFont files from SD Card via FatFS,
 *              renders audio into a lock-free ring buffer consumed
 *              by the TDM TX ISR.
 *
 *              Usage:
 *                1. bento_tsf_load_sf2("/midi/PocketSongsGM.sf2")
 *                2. bento_tsf_note_on/off() from MIDI event dispatch
 *                3. bento_tsf_render() from LVGL timer (~10ms)
 *                4. bento_tsf_get_sample() from TDM ISR (48 kHz)
 *
 ******************************************************************************/

#ifndef BENTO_TSF_H
#define BENTO_TSF_H

#include <stdint.h>
#include <stdbool.h>

/* Ring buffer: 16384 mono samples = ~341ms at 48 kHz (power-of-2). */
#define BENTO_TSF_RING_SIZE     16384

/* Pre-render chunk: 480 samples = 10ms at 48 kHz.
 * Larger chunks amortize TSF per-call overhead better on CM55. */
#define BENTO_TSF_CHUNK_SIZE    480

/*******************************************************************************
 * SF2 Load / Unload
 ******************************************************************************/

/* Load SF2 from SD Card via FatFS stream.
 * path: FatFS path e.g. "/midi/PocketSongsGM.sf2"
 * Returns true on success. Call from task context only. */
bool bento_tsf_load_sf2(const char *path);

/* Unload SF2 and free all memory. */
void bento_tsf_unload(void);

/* Check if an SF2 is loaded and TSF is ready. */
bool bento_tsf_is_loaded(void);

/*******************************************************************************
 * MIDI Channel Control (call from bento_midi_feed context)
 ******************************************************************************/

/* Set bank + program for a MIDI channel (0-15).
 * channel 9 = drums (bank 128 in GM). */
void bento_tsf_channel_set_bank_preset(uint8_t channel, int bank,
                                        int preset_number);

/* Note On/Off on a MIDI channel. */
void bento_tsf_channel_note_on(uint8_t channel, uint8_t key, uint8_t velocity);
void bento_tsf_channel_note_off(uint8_t channel, uint8_t key);

/* MIDI Control Change (CC7=volume, CC10=pan, CC64=sustain, etc.) */
void bento_tsf_channel_midi_control(uint8_t channel, uint8_t controller,
                                     uint8_t value);

/* Pitch Bend (14-bit value: 0..16383, center=8192). */
void bento_tsf_channel_set_pitchwheel(uint8_t channel, int pitch_wheel);

/* All notes off (stop all sound). */
void bento_tsf_all_notes_off(void);

/*******************************************************************************
 * Render + ISR Interface
 ******************************************************************************/

/* Pre-render audio into ring buffer.
 * Call from LVGL timer (~10ms). Renders one chunk if space available.
 * Returns number of samples rendered (0 if ring full or no SF2). */
uint32_t bento_tsf_render(void);

/* ISR-callable: read one sample from ring buffer.
 * Returns 0 (silence) if ring buffer is empty. */
int16_t bento_tsf_get_sample(void);

/*******************************************************************************
 * Diagnostics
 ******************************************************************************/

/* Number of times ISR found ring buffer empty (audio glitch indicator). */
uint32_t bento_tsf_get_underrun_count(void);

/* Last load error: 0=ok, 1=file open fail, 2=tsf parse/malloc fail, 3=voice alloc fail */
int bento_tsf_get_load_error(void);

/* Ring buffer fill level (0 .. BENTO_TSF_RING_SIZE). */
uint32_t bento_tsf_ring_available(void);

/* Diagnostics: SF2 file size and free heap at load time */
uint32_t bento_tsf_get_file_size(void);
uint32_t bento_tsf_get_heap_free(void);

/* Get the basename of the loaded SF2 file (e.g. "PocketSongsGM.sf2") */
const char *bento_tsf_get_sf2_name(void);

#endif /* BENTO_TSF_H */
