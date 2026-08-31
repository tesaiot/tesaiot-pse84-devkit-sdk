/*******************************************************************************
 * File Name: claw_session.h
 *
 * Description: BentoClaw session memory — JSONL ring buffer on LittleFS.
 *              Stores last N conversation messages for context retention.
 *              Uses lfs_exec_py() pattern (MicroPython VFS bridge).
 *
 *              Storage files:
 *                /.bentoclaw_session  — JSONL ring buffer (max 20 entries)
 *                /.bentoclaw_memory   — persistent key-value memory
 *
 *******************************************************************************/

#ifndef CLAW_SESSION_H
#define CLAW_SESSION_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Maximum messages in session ring buffer */
#define CLAW_SESSION_MAX_MSGS   (10)

/* Maximum length of a single message line (JSON) */
#define CLAW_MSG_MAX_LEN        (256)

/* Maximum key/value length for persistent memory */
#define CLAW_MEM_KEY_MAX        (32)
#define CLAW_MEM_VAL_MAX        (128)

/* Message roles */
typedef enum {
    CLAW_ROLE_USER      = 0,
    CLAW_ROLE_ASSISTANT  = 1,
    CLAW_ROLE_SYSTEM     = 2,
    CLAW_ROLE_TOOL       = 3,
} claw_role_t;

/* Initialize session subsystem. Call from modbentoclaw init. */
void claw_session_init(void);

/* Add a message to the session ring buffer.
 * Automatically trims to CLAW_SESSION_MAX_MSGS.
 * role: message role
 * content: message text (truncated to CLAW_MSG_MAX_LEN)
 * Returns true on success. */
bool claw_session_add(claw_role_t role, const char *content);

/* Get message count in current session. */
uint16_t claw_session_count(void);

/* Clear session (delete file). */
void claw_session_clear(void);

/* Flush dirty session to QSPI flash.
 * Must be called from MicroPython task context. */
bool claw_session_flush(void);

/* Check if session has unflushed changes. */
bool claw_session_dirty(void);

/* Store a persistent memory entry (survives session clear).
 * key: memory key (max 32 chars)
 * value: memory value (max 128 chars)
 * Returns true on success. */
bool claw_memory_set(const char *key, const char *value);

/* Retrieve a persistent memory entry.
 * key: memory key
 * out_value: output buffer (at least CLAW_MEM_VAL_MAX bytes)
 * Returns true if found. */
bool claw_memory_get(const char *key, char *out_value, size_t out_max);

/* Build context string from recent session + memory for LLM prompt.
 * out_buf: output buffer
 * out_max: max bytes
 * Returns bytes written. */
size_t claw_session_build_context(char *out_buf, size_t out_max);

#endif /* CLAW_SESSION_H */
