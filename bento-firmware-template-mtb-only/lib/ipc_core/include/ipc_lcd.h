/*******************************************************************************
 * File Name: ipc_lcd.h
 *
 * Description: CM55 IPC receiver for LCD text commands from CM33_NS MicroPython.
 *
 *******************************************************************************/

#ifndef IPC_LCD_H
#define IPC_LCD_H

#include "lvgl.h"
#include <stdbool.h>

/**
 * Initialize IPC LCD receiver. Call after LVGL and display are ready.
 * Sets up IPC pipe, registers callback, and creates an LVGL timer
 * that polls the receive queue and updates a terminal-like renderer.
 *
 * @param parent Pointer to LVGL parent container (Terminal tab).
 * @return true on success.
 */
bool ipc_lcd_init(lv_obj_t *parent);

/**
 * Update the terminal container (for page-based navigation).
 * Called when Playground page is created/destroyed.
 * Pass NULL to invalidate (page destroyed), non-NULL to activate.
 *
 * @param parent  LVGL container or NULL.
 */
void ipc_lcd_set_container(lv_obj_t *parent);

/**
 * Toggle terminal panel visibility (show/hide).
 * If the terminal has not been created yet, force-creates it first.
 * Used by Playground Console Log button.
 */
void ipc_lcd_toggle_panel(void);

/**
 * Check if the terminal panel is currently visible.
 * @return true if visible, false if hidden or not yet created.
 */
bool ipc_lcd_is_panel_visible(void);

/**
 * Reset the one-shot auto-navigate flag.
 * Called on UI CLEAR_ALL (new MicroPython session) so lcd.print()
 * can auto-navigate to Playground again.
 */
void ipc_lcd_reset_auto_nav(void);

/**
 * Check if new text arrived while the console panel was hidden.
 * Used by Playground to show a notification badge on the Console button.
 * @return true if there is unread console output.
 */
bool ipc_lcd_has_unread(void);

/**
 * Clear the unread flag.  Called when the user opens the Console panel.
 */
void ipc_lcd_clear_unread(void);

#endif /* IPC_LCD_H */
