/*******************************************************************************
* File: ui_busy_modal.h
*
* A screen-covering "please wait" overlay for the periods when touch input is
* deliberately switched off.
*
* Why this exists: the secure element and the touch controller share one I2C
* bus, so anything that talks to OPTIGA must first stop CM55 polling touch.
* From the outside that is indistinguishable from a crash — the screen keeps
* drawing and stops responding, with nothing to say why or for how long. It has
* been mistaken for a hung board more than once.
*
* Split by task, because LVGL is not thread-safe and every widget in this
* firmware is created from the GFX task:
*
*   ui_busy_modal_request()/clear()  - any task. Records intent only; no LVGL.
*   ui_busy_modal_service()          - GFX task ONLY. Makes the screen match.
*
* Calls nest: a signature taken inside a window someone else already opened must
* not tear the overlay down when it finishes, so requests are counted and the
* overlay lifts when the last holder lets go.
*******************************************************************************/
#ifndef UI_BUSY_MODAL_H
#define UI_BUSY_MODAL_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Longest reason text carried to the screen, including the terminator. */
#define UI_BUSY_MODAL_REASON_MAX   48

/**
 * Ask for the overlay, naming what the device is doing.
 *
 * Safe from any task. Only records the request — the overlay appears on the
 * next ui_busy_modal_service() from the GFX task.
 *
 * @param reason  Short user-facing text, e.g. "Reading secure element".
 *                NULL or empty falls back to a generic message.
 */
void ui_busy_modal_request(const char *reason);

/** Release one request. The overlay lifts when the last one is released. */
void ui_busy_modal_clear(void);

/** Drop every outstanding request. For recovery paths that cannot know how
 *  many were taken — a task that died holding one, for instance. */
void ui_busy_modal_reset(void);

/** True while at least one request is outstanding. */
bool ui_busy_modal_active(void);

/**
 * Bring the screen into line with the requests. GFX task only.
 *
 * Cheap when nothing has changed: it compares the requested state with what is
 * on screen and returns immediately if they already agree, so it is safe to
 * call every frame.
 */
void ui_busy_modal_service(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_BUSY_MODAL_H */
