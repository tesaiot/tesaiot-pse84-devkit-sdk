/*******************************************************************************
 * File Name: ble_nus_lazy.h
 *
 * Description: Public interface for the deferred Bento Buddy BLE bring-up.
 *              See ble_nus_lazy.c for behavioural contract.
 *
 ******************************************************************************/

#ifndef BLE_NUS_LAZY_H
#define BLE_NUS_LAZY_H

#ifdef __cplusplus
extern "C" {
#endif

/* Bring up the AIROC BLE host stack + NUS advertising on demand.
 * Returns: 0 = newly started, 1 = already running, -1 = init failed. */
int  bento_buddy_request_start(void);

/* Tear down the stack if it is up. Idempotent. */
void bento_buddy_request_stop(void);

/* Spawn a one-shot FreeRTOS task that brings BLE up ~3 s after the
 * scheduler starts. Intended for headless smoke tests where no human
 * taps Start BLE on the LCD. Safe to call from main() before
 * vTaskStartScheduler. */
void bento_buddy_auto_start_install(void);

#ifdef __cplusplus
}
#endif

#endif /* BLE_NUS_LAZY_H */
