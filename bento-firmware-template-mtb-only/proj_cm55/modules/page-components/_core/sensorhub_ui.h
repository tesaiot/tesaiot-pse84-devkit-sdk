/*******************************************************************************
 * File Name: sensorhub_ui.h
 *
 * Description: TESAIoT Sensor Hub — Page Manager Dashboard UI.
 *              Page-based navigation with BSP-conditional pages:
 *              Home / Dashboard / Motion / Environ / Controls / Joystick / Playground.
 *              Uses 33ms LVGL timer for dirty-flag optimized rendering.
 *
 *******************************************************************************/

#ifndef SENSORHUB_UI_H
#define SENSORHUB_UI_H

#include <stdbool.h>

/**
 * Initialize the TESAIoT sensor dashboard UI.
 * Creates Home card grid with page-based navigation (BSP-conditional pages),
 * and a 33ms timer for periodic snapshot-based rendering.
 * Must be called after LVGL display is ready and IPC sensorhub is initialized.
 *
 * @param parent  LVGL parent object (screen) to place dashboard on.
 * @return true on success.
 */
bool sensorhub_ui_init(void *parent);

/**
 * Get the Playground page's UX/UI scrollable container.
 * Used by ipc_ui_init() to set as parent for user-created widgets.
 * Must be called after sensorhub_ui_init().
 *
 * @return LVGL container object, or NULL if Playground page is not active.
 */
void *sensorhub_ui_get_uxui_container(void);

/**
 * Get the Playground page's terminal container.
 * Used by ipc_lcd_init() as the parent for lazy-created terminal widgets.
 * Must be called after sensorhub_ui_init().
 *
 * @return LVGL container object, or NULL if Playground page is not active.
 */
void *sensorhub_ui_get_terminal_container(void);

/**
 * Navigate to the Playground page.
 * Called automatically when MicroPython creates its first UI widget,
 * so the user sees their widgets immediately without manual page switch.
 */
void sensorhub_ui_switch_to_uxui(void);

/**
 * Navigate to the Playground page (terminal view).
 */
void sensorhub_ui_switch_to_terminal(void);

/**
 * Show or hide "IDE Connected" in the status bar.
 * Called from IPC UI handler when IDE sends IDE_STATUS command.
 *
 * @param connected  true = show "IDE Connected", false = hide.
 */
void sensorhub_ui_set_ide_connected(bool connected);

#endif /* SENSORHUB_UI_H */
