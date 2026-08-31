/*******************************************************************************
 * File Name: ui_widget_mgr.h
 *
 * Description: Widget handle table and LVGL widget lifecycle manager.
 *              Maps handle IDs (0-31) to lv_obj_t* pointers.
 *              Creates, modifies, deletes LVGL widgets in GFX task context.
 *
 *******************************************************************************/

#ifndef UI_WIDGET_MGR_H
#define UI_WIDGET_MGR_H

#include "ipc_ui_protocol.h"
#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * Initialize the widget manager.
 * @param parent  The LVGL parent object (UX/UI tab scrollable container).
 */
void ui_widget_mgr_init(lv_obj_t *parent);

/**
 * Update the parent container (for page-based navigation).
 * Clears all existing widgets and sets new parent.
 *
 * @param parent  New LVGL parent object, or NULL to invalidate.
 */
void ui_widget_mgr_set_parent(lv_obj_t *parent);

/**
 * Check if the widget manager has no parent container.
 * @return true if parent is NULL (needs container before creating widgets).
 */
bool ui_widget_mgr_needs_container(void);

/**
 * Create a widget from IPC CREATE payload.
 * @param cfg  Parsed CREATE payload.
 * @return Handle ID (0-31) on success, -1 if table full, -2 if invalid type.
 */
int ui_widget_mgr_create(const ipc_ui_create_t *cfg);

/**
 * Create an image sprite from a compiled C descriptor. Reuses the widget
 * handle table so position/visibility/delete work through the normal ops.
 * @param dsc  Compiled sprite descriptor (resolved per-project from a SPR id).
 * @return Handle ID (0-31), -1 if table full, -2 if invalid/no sprite engine.
 */
int ui_widget_mgr_create_sprite(const lv_image_dsc_t *dsc, int16_t x, int16_t y);

/**
 * Swap a sprite's image (directional head, enemy kind, explosion frame).
 * No-op on invalid handle or NULL descriptor.
 */
void ui_widget_mgr_set_sprite_image(int handle, const lv_image_dsc_t *dsc);

/**
 * Delete a widget by handle.
 * @param handle  Handle ID (0-31).
 */
void ui_widget_mgr_delete(int handle);

/**
 * Delete all widgets and reset the handle table.
 */
void ui_widget_mgr_clear_all(void);

/**
 * Set text on a widget.
 */
void ui_widget_mgr_set_text(int handle, const char *text);

/**
 * Set numeric value on a widget.
 */
void ui_widget_mgr_set_value(int handle, int32_t value);

/**
 * Set position of a widget.
 */
void ui_widget_mgr_set_position(int handle, int16_t x, int16_t y);

/**
 * Set size of a widget.
 */
void ui_widget_mgr_set_size(int handle, int16_t w, int16_t h);

/**
 * Set primary color of a widget.
 */
void ui_widget_mgr_set_color(int handle, uint32_t color);

/**
 * Show or hide a widget.
 */
void ui_widget_mgr_set_visible(int handle, bool visible);

/**
 * Show or hide ALL managed widgets at once.
 * Used by Console/UI toggle to switch between widget view and console view.
 */
void ui_widget_mgr_set_all_visible(bool visible);

/**
 * Get current value of a widget.
 * @return Widget value or 0 if invalid handle.
 */
int32_t ui_widget_mgr_get_value(int handle);

/**
 * Set dot matrix bitmap data.
 */
void ui_widget_mgr_set_dotmatrix(int handle, const uint8_t *bitmap, uint8_t len);

/**
 * Set image pixel data (chunked RGB565 transfer).
 * @param handle  Widget handle (must be UI_WIDGET_IMAGE).
 * @param offset  Byte offset into the canvas pixel buffer.
 * @param data    Chunk of RGB565 pixel data.
 * @param len     Chunk length in bytes.
 */
void ui_widget_mgr_set_image(int handle, uint16_t offset, const uint8_t *data, uint8_t len);

/**
 * Push an event into the ring buffer (called from LVGL event callbacks).
 */
void ui_widget_mgr_event_push(uint8_t handle, uint8_t event_type, int32_t value);

/**
 * Drain events from the ring buffer.
 * @param out        Output array.
 * @param max_events Maximum events to drain.
 * @return Number of events drained.
 */
int ui_widget_mgr_event_drain(ipc_ui_event_t *out, int max_events);

/**
 * Get current widget count.
 */
int ui_widget_mgr_count(void);

/**
 * List all active widgets (handle + type).
 * @param out        Output array of ipc_ui_widget_info_t.
 * @param max_items  Maximum items to fill.
 * @return Number of active widgets written to out.
 */
int ui_widget_mgr_list(ipc_ui_widget_info_t *out, int max_items);

/**
 * Get the current parent container object.
 * @return The LVGL parent object, or NULL if not set.
 */
lv_obj_t *ui_widget_mgr_get_parent(void);

/**
 * Set screen dimensions and reset auto-layout grid.
 * @param width   Screen width in pixels (auto-layout wraps at width - 100).
 * @param height  Screen height in pixels (reserved for future use).
 */
void ui_widget_mgr_set_screen(int16_t width, int16_t height);

/**
 * Add a series to a chart widget.
 * @param handle  Widget handle (must be UI_WIDGET_CHART).
 * @param color   Series color (0xRRGGBB).
 * @return Series index (0-3) on success, -1 on error.
 */
int ui_widget_mgr_chart_add_series(int handle, uint32_t color);

/**
 * Set next value for a specific chart series.
 * @param handle      Widget handle (must be UI_WIDGET_CHART).
 * @param series_idx  Series index (0-3).
 * @param value       Data value.
 */
void ui_widget_mgr_chart_set_next(int handle, uint8_t series_idx, int32_t value);


/*******************************************************************************
 * Collections and containers (IPC_CMD_UI_ITEM_ADD / _ITEM_CLEAR / _SET_PROP)
 *
 * These three functions MUST exist, and behave the same way for the same widget
 * type, in BOTH ui_widget_mgr.c implementations — claw's and game's. The two
 * Infineon Game kits compile claw's ipc_ui.c against game's ui_widget_mgr.c, so
 * a type served here and not there renders on some boards and returns
 * UI_STATUS_INVALID_TYPE on others, with no build error anywhere.
 *******************************************************************************/

/** ui_widget_mgr_item_add() returned successfully but produced no child object
 *  (a data widget appended an item). Distinct from an error, and from a handle. */
#define UI_ITEM_ADD_OK_NO_CHILD   (-3)

/**
 * Append one item to a collection or container widget.
 *
 * @param it  Parsed ITEM_ADD payload; the meaning of a/b/flags/text is per
 *            widget type and is tabulated beside ui_widget_type_t.
 * @return    >= 0                      handle of the child page a container
 *                                      created (Tabview / Tileview / Win),
 *            UI_ITEM_ADD_OK_NO_CHILD   item appended, no child object,
 *            -1                        handle table full,
 *            -2                        bad handle, wrong widget type, or the
 *                                      item could not be added.
 */
int ui_widget_mgr_item_add(const ipc_ui_item_add_t *it);

/**
 * Empty a collection widget (all rows / options / buttons / list entries).
 * No-op on a bad handle or a widget type with no collection.
 */
void ui_widget_mgr_item_clear(int handle);

/**
 * Set one per-widget property.
 * @param handle   Widget handle.
 * @param prop_id  UI_PROP_*.
 * @param value    Property value; two props carry a hi16/lo16 pair (see header).
 */
void ui_widget_mgr_set_prop(int handle, uint8_t prop_id, int32_t value);

/**
 * The LVGL object behind a handle, or NULL if the slot is empty.
 *
 * The manager IS the handle table, so answering this is its job. Nothing on
 * the device calls it — it exists so a host harness can drive an object the
 * way a finger would (lv_obj_send_event), which is the only way to photograph
 * behaviour that only a tap can start, such as a menu row loading its page.
 */
lv_obj_t *ui_widget_mgr_get_object(int handle);

#endif /* UI_WIDGET_MGR_H */
