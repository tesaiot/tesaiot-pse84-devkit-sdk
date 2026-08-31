# ble_nus_lazy.h

Public interface for the deferred Bento Buddy BLE bring-up. See ble_nus_lazy.c for behavioural contract.

## Functions (exported by the archive)

### `bento_buddy_auto_start_install`

```c
void bento_buddy_auto_start_install(void);
```

Spawn a one-shot FreeRTOS task that brings BLE up ~3 s after the scheduler starts. Intended for headless smoke tests where no human taps Start BLE on the LCD. Safe to call from main() before vTaskStartScheduler.

### `bento_buddy_request_start`

```c
int bento_buddy_request_start(void);
```

File Name: ble_nus_lazy.h Description: Public interface for the deferred Bento Buddy BLE bring-up. See ble_nus_lazy.c for behavioural contract. / #ifndef BLE_NUS_LAZY_H #define BLE_NUS_LAZY_H #ifdef __cplusplus extern "C" { #endif /* Bring up the AIROC BLE host stack + NUS advertising on demand. Returns: 0 = newly started, 1 = already running, -1 = init failed.

### `bento_buddy_request_stop`

```c
void bento_buddy_request_stop(void);
```

Tear down the stack if it is up. Idempotent.

## Constants

| Name | Value |
|---|---|
| `BLE_NUS_LAZY_H` | `#ifdef` |
