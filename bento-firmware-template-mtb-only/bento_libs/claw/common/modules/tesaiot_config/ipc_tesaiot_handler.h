/*******************************************************************************
 * File Name: ipc_tesaiot_handler.h
 *
 * Description: TESAIoT Config IPC handler — CM33_NS side.
 *              Receives config commands from CM55 (page_tesaiot_connect.c):
 *                IPC_CMD_TESAIOT_MODE_SWITCH (0xF5)
 *                IPC_CMD_TESAIOT_CONFIG_SET  (0xF7)
 *                IPC_CMD_TESAIOT_CONFIG_GET  (0xF8)
 *
 *              Architecture: ISR → semaphore → FreeRTOS task
 *
 * Author:      Wiroon Sriborrirux (BDH)
 *
 ******************************************************************************/

#ifndef IPC_TESAIOT_HANDLER_H
#define IPC_TESAIOT_HANDLER_H

/**
 * Initialize TESAIoT config IPC handler.
 * Creates task + semaphore, registers IPC callback.
 * Call after cm33_ipc_communication_setup() and tesaiot_config_init().
 */
void ipc_tesaiot_handler_init(void);

/**
 * Get pointer to the shared status struct (for status push from mqtt_task etc.)
 * Lives in CY_SECTION_SHAREDMEM — readable by CM55.
 */
#include "ipc_tesaiot_defs.h"
tesaiot_status_t *ipc_tesaiot_get_status(void);

/**
 * Refresh config display fields in shared status.
 * Call after tesaiot_config_init() loads .tesaiot_config from LittleFS.
 * Copies device_id, broker, port, factory_uid into shared memory.
 */
void ipc_tesaiot_refresh_status(void);

#endif /* IPC_TESAIOT_HANDLER_H */
