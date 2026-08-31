/*******************************************************************************
 * File Name: ipc_service.h
 *
 * Description: CM55 IPC Service for bidirectional WiFi/MQTT/TESAIoT commands.
 *              Receives IPC commands from CM33_NS MicroPython modules,
 *              processes them in a FreeRTOS task, writes responses to
 *              shared memory for CM33_NS to poll.
 *
 *******************************************************************************/

#ifndef IPC_SERVICE_H
#define IPC_SERVICE_H

#include <stdbool.h>

/**
 * Initialize the IPC service.
 * Creates queue, registers IPC callback, starts handler task.
 * Must be called after cm55_ipc_communication_setup() and wifi_manager_init().
 *
 * @return true on success
 */
bool ipc_service_init(void);

#endif /* IPC_SERVICE_H */
