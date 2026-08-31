# hsm_provision_ui.h

## Functions (exported by the archive)

### `hsm_enrol_open`

```c
void hsm_enrol_open(void);
```

@file hsm_provision_ui.h @brief Two teaching screens on the HSM page: certificate enrolment by CSR, and Protected Update. Each opens a full-screen overlay, the same idiom the PIN, certificate and benchmark screens use. Unlike those, the work behind these is asynchronous: IPC_CMD_HSM_PROVISION returns immediately and an lv_timer polls, because the operation takes seconds and a busy-wait here would freeze the screen and simultaneously stop ui_busy_modal_service() from drawing the overlay that would have explained the freeze. / #ifndef HSM_PROVISION_UI_H #define HSM_PROVISION_UI_H #include "lvgl.h" /** Open the Enrol screen — key pair, CSR, proof of possession, certificate.

### `hsm_protect_open`

```c
void hsm_protect_open(void);
```

Open the Protect screen — pending change set, confirm, run, read back.

### `hsm_provision_ui_teardown`

```c
void hsm_provision_ui_teardown(void);
```

Called from page_hsm_destroy(): drop any overlay and cancel its poll timer.

## Constants

| Name | Value |
|---|---|
| `HSM_PROVISION_UI_H` | `#include` |
