# claw_session.h

BentoClaw session memory — JSONL ring buffer on LittleFS. Stores last N conversation messages for context retention. Uses lfs_exec_py() pattern (MicroPython VFS bridge). Storage files: /.bentoclaw_session  — JSONL ring buffer (max 20 entries) /.bentoclaw_memory   — persistent key-value memory

## Functions (exported by the archive)

### `claw_memory_get`

```c
bool claw_memory_get(const char *key, char *out_value, size_t out_max);
```

Retrieve a persistent memory entry. key: memory key out_value: output buffer (at least CLAW_MEM_VAL_MAX bytes) Returns true if found.

### `claw_memory_set`

```c
bool claw_memory_set(const char *key, const char *value);
```

Store a persistent memory entry (survives session clear). key: memory key (max 32 chars) value: memory value (max 128 chars) Returns true on success.

### `claw_session_add`

```c
bool claw_session_add(claw_role_t role, const char *content);
```

Add a message to the session ring buffer. Automatically trims to CLAW_SESSION_MAX_MSGS. role: message role content: message text (truncated to CLAW_MSG_MAX_LEN) Returns true on success.

### `claw_session_build_context`

```c
size_t claw_session_build_context(char *out_buf, size_t out_max);
```

Build context string from recent session + memory for LLM prompt. out_buf: output buffer out_max: max bytes Returns bytes written.

### `claw_session_clear`

```c
void claw_session_clear(void);
```

Clear session (delete file).

### `claw_session_count`

```c
uint16_t claw_session_count(void);
```

Get message count in current session.

### `claw_session_dirty`

```c
bool claw_session_dirty(void);
```

Check if session has unflushed changes.

### `claw_session_flush`

```c
bool claw_session_flush(void);
```

Flush dirty session to QSPI flash. Must be called from MicroPython task context.

### `claw_session_init`

```c
void claw_session_init(void);
```

File Name: claw_session.h Description: BentoClaw session memory — JSONL ring buffer on LittleFS. Stores last N conversation messages for context retention. Uses lfs_exec_py() pattern (MicroPython VFS bridge). Storage files: /.bentoclaw_session  — JSONL ring buffer (max 20 entries) /.bentoclaw_memory   — persistent key-value memory / #ifndef CLAW_SESSION_H #define CLAW_SESSION_H #include <stdint.h> #include <stddef.h> #include <stdbool.h> /* Maximum messages in session ring buffer */ #define CLAW_SESSION_MAX_MSGS   (10) /* Maximum length of a single message line (JSON) */ #define CLAW_MSG_MAX_LEN        (256) /* Maximum key/value length for persistent memory */ #define CLAW_MEM_KEY_MAX        (32) #define CLAW_MEM_VAL_MAX        (128) /* Message roles */ typedef enum { CLAW_ROLE_USER      = 0, CLAW_ROLE_ASSISTANT  = 1, CLAW_ROLE_SYSTEM     = 2, CLAW_ROLE_TOOL       = 3, } claw_role_t; /* Initialize session subsystem. Call from modbentoclaw init.

## Enums

### `claw_role_t`

```c
typedef enum {
    CLAW_ROLE_USER      = 0,
    CLAW_ROLE_ASSISTANT  = 1,
    CLAW_ROLE_SYSTEM     = 2,
    CLAW_ROLE_TOOL       = 3,} claw_role_t;
```

## Constants

| Name | Value |
|---|---|
| `CLAW_SESSION_H` | `#include` |
| `CLAW_SESSION_MAX_MSGS` | `(10)` |
| `CLAW_MSG_MAX_LEN` | `(256)` |
| `CLAW_MEM_KEY_MAX` | `(32)` |
| `CLAW_MEM_VAL_MAX` | `(128)` |
