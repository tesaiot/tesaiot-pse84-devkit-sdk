/*******************************************************************************
 * File Name: nus_folder_push.h
 *
 * Description: Folder-push state machine for the Bento Desktop Buddy
 *              (Bento forked this protocol under its own branding —
 *              see Bento_Buddy/SPEC.md §3.1.1 Not Anthropic-compatible).
 *              Consumes `cmd:char_begin | file | chunk | file_end | char_end`
 *              frames and streams base64-decoded file content into LittleFS
 *              at /buddy/<char_name>/<file_path>.
 *
 *              Atomic commit: writes to /buddy/.staging/<name>/, then on
 *              successful char_end renames to /buddy/<name>/. Any failure
 *              deletes the staging folder and acks {ok:false}.
 *
 *              Path validation per REFERENCE.md §Folder Push: reject `..`,
 *              absolute paths, and NUL / control characters.
 *
 ******************************************************************************/

#ifndef NUS_FOLDER_PUSH_H
#define NUS_FOLDER_PUSH_H

#include <stddef.h>
#include <stdint.h>

#include "vendor/jsmn.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Per-command entrypoints — called by nus_commands.c dispatcher.
 * Each emits its own ack back to the desktop. */
void nus_fp_char_begin(const char *json, const jsmntok_t *toks, int n);
void nus_fp_file(const char *json, const jsmntok_t *toks, int n);
void nus_fp_chunk(const char *json, const jsmntok_t *toks, int n);
void nus_fp_file_end(const char *json, const jsmntok_t *toks, int n);
void nus_fp_char_end(const char *json, const jsmntok_t *toks, int n);

/* Query state (for debugging / status). */
int  nus_fp_is_active(void);

#ifdef __cplusplus
}
#endif

#endif /* NUS_FOLDER_PUSH_H */
