/*******************************************************************************
 * File Name: claw_safety.h
 *
 * Description: BentoClaw safety guardrails — rate limiter, circuit breaker,
 *              transport trust levels.
 *
 *              OWASP Agentic AI patterns adapted for embedded constraints:
 *                - Per-tool rate limiting (sliding window via tick counter)
 *                - Circuit breaker (3 consecutive errors -> 30s cooldown)
 *                - Transport trust levels (USB=full, HTTPS=restricted)
 *
 *******************************************************************************/

#ifndef CLAW_SAFETY_H
#define CLAW_SAFETY_H

#include <stdint.h>
#include <stdbool.h>

/*******************************************************************************
 * Transport Trust Levels
 ******************************************************************************/
typedef enum {
    CLAW_TRUST_USB   = 2,   /* Full access — all tools, all risk levels */
    CLAW_TRUST_HTTPS = 1,   /* Restricted — low/medium risk only */
    CLAW_TRUST_NONE  = 0,   /* Blocked — no tool execution */
} claw_trust_level_t;

/*******************************************************************************
 * Rate Limiter — per-tool sliding window
 ******************************************************************************/

/* Max tools tracked for rate limiting */
#define CLAW_RATE_MAX_TOOLS  (16)

/* Per-tool rate limit config */
typedef struct {
    const char *tool_name;       /* Tool identifier */
    uint16_t    max_per_minute;  /* Max calls per 60-second window */
    uint16_t    call_count;      /* Calls in current window */
    uint32_t    window_start;    /* Window start tick (ms) */
} claw_rate_entry_t;

/* Initialize rate limiter. Called once at module init. */
void claw_rate_init(void);

/* Check if tool is allowed under current rate limit.
 * Returns true if allowed, false if rate-limited. */
bool claw_rate_check(const char *tool_name);

/* Record a tool execution (increments counter). */
void claw_rate_record(const char *tool_name);

/* Set per-tool rate limit. Default is 10/min for most tools. */
void claw_rate_set(const char *tool_name, uint16_t max_per_minute);

/*******************************************************************************
 * Circuit Breaker — consecutive error tracking
 ******************************************************************************/

/* Circuit breaker states */
typedef enum {
    CLAW_CB_CLOSED   = 0,   /* Normal operation */
    CLAW_CB_OPEN     = 1,   /* Tripped — blocking all calls */
    CLAW_CB_HALFOPEN = 2,   /* Allowing one test call */
} claw_cb_state_t;

/* Initialize circuit breaker. */
void claw_cb_init(void);

/* Check if circuit breaker allows execution.
 * Returns true if allowed. */
bool claw_cb_allow(void);

/* Record success — resets error counter, closes breaker. */
void claw_cb_success(void);

/* Record failure — increments error counter, may trip breaker. */
void claw_cb_failure(void);

/* Get current circuit breaker state. */
claw_cb_state_t claw_cb_state(void);

/* Get remaining cooldown in milliseconds (0 if not in cooldown). */
uint32_t claw_cb_cooldown_remaining(void);

/*******************************************************************************
 * Trust Level Check
 ******************************************************************************/

/* Set current transport trust level. */
void claw_trust_set(claw_trust_level_t level);

/* Get current transport trust level. */
claw_trust_level_t claw_trust_get(void);

/* Check if tool with given risk level is allowed at current trust level.
 * risk: 0=low, 1=medium, 2=high
 * Returns true if allowed. */
bool claw_trust_allows(uint8_t risk);

#endif /* CLAW_SAFETY_H */
