# nus_folder_push.h

Folder-push state machine for the Bento Desktop Buddy (Bento forked this protocol under its own branding — see Bento_Buddy/SPEC.md §3.1.1 Not Anthropic-compatible). Consumes `cmd:char_begin | file | chunk | file_end | char_end` frames and streams base64-decoded file content into LittleFS at /buddy/<char_name>/<file_path>. Atomic commit: writes to /buddy/.staging/<name>/, then on successful char_end renames to /buddy/<name>/. Any failure deletes the staging folder and acks {ok:false}. Name and path rules are defined in nus_folder_push.c and are split into two labelled tiers. Tier 1 (no '/' or '\\', no leading '.', not empty) is security-critical: it is the only thing preventing a peer-supplied name from becoming an arbitrary filesystem path, and it is NOT interchangeable with the \xHH escaping that prevents Python injection. Tier 2 (control bytes, UTF-8 well-formedness) is hygiene and may be relaxed. Read the block comment above validate_component() before changing either. THREADING CONTRACT ------------------ This module is a SINGLE-INSTANCE, NON-REENTRANT state machine. Every function declared below shares file-scope mutable state: the current pack name, the current file path, the base64 carry, the byte counters, and the script buffers handed to the MicroPython bridge. Callers MUST serialise all calls, including nus_fp_device_write_bytes(), which appends to whichever file handle the last nus_fp_file() opened. Two tasks driving a folder push concurrently will interleave into one another's pack — that was true before the script buffers existed (s_b64 and s_char_name have always been file-scope) and it is equally true now. In this firmware the requirement is met because every call originates from the single BLE dispatch context. A downstream consumer driving these entry points from a second task must provide its own mutex. This contract was previously undocumented; it is stated here and in dist/ble_nus/api.txt.

## Functions (exported by the archive)

### `nus_fp_char_begin`

```c
void nus_fp_char_begin(const char *json, const jsmntok_t *toks, int n);
```

File Name: nus_folder_push.h Description: Folder-push state machine for the Bento Desktop Buddy (Bento forked this protocol under its own branding — see Bento_Buddy/SPEC.md §3.1.1 Not Anthropic-compatible). Consumes `cmd:char_begin | file | chunk | file_end | char_end` frames and streams base64-decoded file content into LittleFS at /buddy/<char_name>/<file_path>. Atomic commit: writes to /buddy/.staging/<name>/, then on successful char_end renames to /buddy/<name>/. Any failure deletes the staging folder and acks {ok:false}. Name and path rules are defined in nus_folder_push.c and are split into two labelled tiers. Tier 1 (no '/' or '\\', no leading '.', not empty) is security-critical: it is the only thing preventing a peer-supplied name from becoming an arbitrary filesystem path, and it is NOT interchangeable with the \xHH escaping that prevents Python injection. Tier 2 (control bytes, UTF-8 well-formedness) is hygiene and may be relaxed. Read the block comment above validate_component() before changing either. THREADING CONTRACT ------------------ This module is a SINGLE-INSTANCE, NON-REENTRANT state machine. Every function declared below shares file-scope mutable state: the current pack name, the current file path, the base64 carry, the byte counters, and the script buffers handed to the MicroPython bridge. Callers MUST serialise all calls, including nus_fp_device_write_bytes(), which appends to whichever file handle the last nus_fp_file() opened. Two tasks driving a folder push concurrently will interleave into one another's pack — that was true before the script buffers existed (s_b64 and s_char_name have always been file-scope) and it is equally true now. In this firmware the requirement is met because every call originates from the single BLE dispatch context. A downstream consumer driving these entry points from a second task must provide its own mutex. This contract was previously undocumented; it is stated here and in dist/ble_nus/api.txt. / #ifndef NUS_FOLDER_PUSH_H #define NUS_FOLDER_PUSH_H #include <stddef.h> #include <stdint.h> #include "vendor/jsmn.h" #ifdef __cplusplus extern "C" { #endif /* Per-command entrypoints — called by nus_commands.c dispatcher. Each emits its own ack back to the desktop.

### `nus_fp_char_end`

```c
void nus_fp_char_end(const char *json, const jsmntok_t *toks, int n);
```

_No description in the header._

### `nus_fp_chunk`

```c
void nus_fp_chunk(const char *json, const jsmntok_t *toks, int n);
```

_No description in the header._

### `nus_fp_file`

```c
void nus_fp_file(const char *json, const jsmntok_t *toks, int n);
```

_No description in the header._

### `nus_fp_file_end`

```c
void nus_fp_file_end(const char *json, const jsmntok_t *toks, int n);
```

_No description in the header._

### `nus_fp_is_active`

```c
int nus_fp_is_active(void);
```

Query state (for debugging / status).

## Constants

| Name | Value |
|---|---|
| `NUS_FOLDER_PUSH_H` | `#include` |
