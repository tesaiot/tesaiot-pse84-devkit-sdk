/*******************************************************************************
 * File Name: nus_agent.h
 *
 * Description: Desktop -> Device agent stream handlers for the LLM-side loop
 *              that runs inside Bento Desktop Buddy. The device originates a
 *              bento.agent.ask (from the LCD ASK submode), then the desktop
 *              streams back bento.agent.token frames, optional
 *              bento.agent.tool_call notifications (log-only on the device),
 *              and finally a bento.agent.done frame carrying the full answer.
 *
 *              Contract:
 *                1. One in-flight request id at a time. Token frames whose id
 *                   doesn't match the latest ask are silently dropped.
 *                2. Accumulator is bounded (256 chars) — overflow truncates.
 *                3. On .done, the final text is forwarded to the CM55 page via
 *                   IPC_CMD_BUDDY_AGENT_RESPONSE so the ASK submode can render.
 *                4. .tool_call is observability-only: we emit a hint-text IPC
 *                   so the scrolling marquee shows which tool is running.
 *
 *              Reference: TESAIoT_PLAN/2026-4/Bento_Buddy/SPEC.md §5.4.
 *
 ******************************************************************************/

#ifndef NUS_AGENT_H
#define NUS_AGENT_H

#include <stddef.h>
#include <stdint.h>

#include "vendor/jsmn.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Returns the number of bytes currently buffered for the in-flight agent ask.
 * 0 when idle. Exposed for telemetry + host unit tests. */
size_t nus_agent_buffer_len(void);

/* Reset the accumulator — called when the LCD user leaves the ASK submode or
 * when a new ask is issued. Discards any partial text. Safe to call from any
 * task context (simple memset; single-reader single-writer on the BLE task). */
void nus_agent_reset(void);

/* Record a new in-flight ask id so subsequent token/done frames can be matched.
 * Called from page_bento_buddy before (or in parallel with) the IPC-originated
 * bento.agent.ask emission. Truncates ids longer than the internal buffer. */
void nus_agent_note_ask(const char *id);

/* Handle bento.agent.token — appends the "delta" string to the in-flight
 * request's accumulator. Returns 0 if accepted, -1 if the id doesn't match the
 * current ask (silently dropped) or the delta field is missing. */
int nus_agent_handle_token(const char *json, const jsmntok_t *toks, int n_toks);

/* Handle bento.agent.done — emits the final text to the CM55 UI via
 * IPC_CMD_BUDDY_AGENT_RESPONSE and resets the accumulator. Returns 0 on
 * success. The "text" field is authoritative; if missing, the accumulated
 * deltas are forwarded instead. */
int nus_agent_handle_done(const char *json, const jsmntok_t *toks, int n_toks);

/* Handle bento.agent.tool_call — log-only on the device (tool execution
 * happens desktop-side). Emits a short hint-text IPC to the CM55 marquee so
 * the user sees which tool is running. Returns 0. */
int nus_agent_handle_tool_call(const char *json, const jsmntok_t *toks, int n_toks);

#ifdef __cplusplus
}
#endif

#endif /* NUS_AGENT_H */
