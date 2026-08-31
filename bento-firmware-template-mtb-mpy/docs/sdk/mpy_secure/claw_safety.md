# claw_safety.h

BentoClaw safety guardrails — rate limiter, circuit breaker, transport trust levels. OWASP Agentic AI patterns adapted for embedded constraints: - Per-tool rate limiting (sliding window via tick counter) - Circuit breaker (3 consecutive errors -> 30s cooldown) - Transport trust levels (USB=full, HTTPS=restricted)

## Functions (exported by the archive)

### `claw_cb_allow`

```c
bool claw_cb_allow(void);
```

Check if circuit breaker allows execution. Returns true if allowed.

### `claw_cb_cooldown_remaining`

```c
uint32_t claw_cb_cooldown_remaining(void);
```

Get remaining cooldown in milliseconds (0 if not in cooldown).

### `claw_cb_failure`

```c
void claw_cb_failure(void);
```

Record failure — increments error counter, may trip breaker.

### `claw_cb_init`

```c
void claw_cb_init(void);
```

Circuit Breaker — consecutive error tracking / /* Circuit breaker states */ typedef enum { CLAW_CB_CLOSED   = 0,   /* Normal operation */ CLAW_CB_OPEN     = 1,   /* Tripped — blocking all calls */ CLAW_CB_HALFOPEN = 2,   /* Allowing one test call */ } claw_cb_state_t; /* Initialize circuit breaker.

### `claw_cb_state`

```c
claw_cb_state_t claw_cb_state(void);
```

Get current circuit breaker state.

### `claw_cb_success`

```c
void claw_cb_success(void);
```

Record success — resets error counter, closes breaker.

### `claw_rate_check`

```c
bool claw_rate_check(const char *tool_name);
```

Check if tool is allowed under current rate limit. Returns true if allowed, false if rate-limited.

### `claw_rate_init`

```c
void claw_rate_init(void);
```

File Name: claw_safety.h Description: BentoClaw safety guardrails — rate limiter, circuit breaker, transport trust levels. OWASP Agentic AI patterns adapted for embedded constraints: - Per-tool rate limiting (sliding window via tick counter) - Circuit breaker (3 consecutive errors -> 30s cooldown) - Transport trust levels (USB=full, HTTPS=restricted) / #ifndef CLAW_SAFETY_H #define CLAW_SAFETY_H #include <stdint.h> #include <stdbool.h> /******************************************************************************* Transport Trust Levels / typedef enum { CLAW_TRUST_USB   = 2,   /* Full access — all tools, all risk levels */ CLAW_TRUST_HTTPS = 1,   /* Restricted — low/medium risk only */ CLAW_TRUST_NONE  = 0,   /* Blocked — no tool execution */ } claw_trust_level_t; /******************************************************************************* Rate Limiter — per-tool sliding window / /* Max tools tracked for rate limiting */ #define CLAW_RATE_MAX_TOOLS  (16) /* Per-tool rate limit config */ typedef struct { const char *tool_name;       /* Tool identifier */ uint16_t    max_per_minute;  /* Max calls per 60-second window */ uint16_t    call_count;      /* Calls in current window */ uint32_t    window_start;    /* Window start tick (ms) */ } claw_rate_entry_t; /* Initialize rate limiter. Called once at module init.

### `claw_rate_record`

```c
void claw_rate_record(const char *tool_name);
```

Record a tool execution (increments counter).

### `claw_rate_set`

```c
void claw_rate_set(const char *tool_name, uint16_t max_per_minute);
```

Set per-tool rate limit. Default is 10/min for most tools.

### `claw_trust_allows`

```c
bool claw_trust_allows(uint8_t risk);
```

Check if tool with given risk level is allowed at current trust level. risk: 0=low, 1=medium, 2=high Returns true if allowed.

### `claw_trust_get`

```c
claw_trust_level_t claw_trust_get(void);
```

Get current transport trust level.

### `claw_trust_set`

```c
void claw_trust_set(claw_trust_level_t level);
```

Trust Level Check / /* Set current transport trust level.

## Enums

### `claw_trust_level_t`

```c
typedef enum {
    CLAW_TRUST_USB   = 2,   /* Full access — all tools, all risk levels */
    CLAW_TRUST_HTTPS = 1,   /* Restricted — low/medium risk only */
    CLAW_TRUST_NONE  = 0,   /* Blocked — no tool execution */} claw_trust_level_t;
```

### `claw_cb_state_t`

```c
typedef enum {
    CLAW_CB_CLOSED   = 0,   /* Normal operation */
    CLAW_CB_OPEN     = 1,   /* Tripped — blocking all calls */
    CLAW_CB_HALFOPEN = 2,   /* Allowing one test call */} claw_cb_state_t;
```

## Structs

### `claw_rate_entry_t`

```c
typedef struct {
    const char *tool_name;       /* Tool identifier */
    uint16_t    max_per_minute;  /* Max calls per 60-second window */
    uint16_t    call_count;      /* Calls in current window */
    uint32_t    window_start;    /* Window start tick (ms) */} claw_rate_entry_t;
```

## Constants

| Name | Value |
|---|---|
| `CLAW_SAFETY_H` | `#include` |
| `CLAW_RATE_MAX_TOOLS` | `(16)` |
