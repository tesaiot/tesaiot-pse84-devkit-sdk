/*******************************************************************************
 * File Name: nus_base64.h
 *
 * Description: Stateful base64 decoder for the Bento Desktop Buddy folder
 *              push protocol (Bento forked this protocol under its own
 *              branding — see Bento_Buddy/SPEC.md §3.1.1 Not
 *              Anthropic-compatible). Chunks arrive one "chunk" command at
 *              a time —
 *              each chunk carries a "d":"<b64>" field that may split a 4-char
 *              quantum across chunk boundaries. This decoder keeps the
 *              leftover 1..3 bytes between calls so the caller can feed each
 *              chunk as-received.
 *
 *              Output goes through a consumer callback so the caller can
 *              stream to flash (LittleFS) without buffering the whole file
 *              in RAM — important for Bento's pack size limit of 1.8 MB.
 *
 ******************************************************************************/

#ifndef NUS_BASE64_H
#define NUS_BASE64_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*nus_b64_sink_t)(const uint8_t *bytes, size_t len, void *ctx);

typedef struct {
    uint8_t carry[4];        /* up to 3 half-quantum bytes + 1 spare */
    uint8_t carry_n;         /* 0..3 */
    uint32_t bytes_out;      /* total decoded bytes written */
    bool    got_padding;     /* '=' seen — no more input expected */
    nus_b64_sink_t sink;
    void *sink_ctx;
} nus_b64_state_t;

void nus_b64_init(nus_b64_state_t *s, nus_b64_sink_t sink, void *ctx);

/* Feed one chunk of base64 text. Returns 0 on success, negative on error
 * (invalid char, sink failure, stray data after padding). */
int nus_b64_feed(nus_b64_state_t *s, const uint8_t *b64, size_t len);

/* Flush the last quantum (if any trailing carry exists). Called at file_end. */
int nus_b64_flush(nus_b64_state_t *s);

#ifdef __cplusplus
}
#endif

#endif /* NUS_BASE64_H */
