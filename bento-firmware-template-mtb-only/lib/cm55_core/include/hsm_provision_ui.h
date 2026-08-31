/**
 * @file hsm_provision_ui.h
 * @brief Two teaching screens on the HSM page: certificate enrolment by CSR,
 *        and Protected Update.
 *
 * Each opens a full-screen overlay, the same idiom the PIN, certificate and
 * benchmark screens use. Unlike those, the work behind these is asynchronous:
 * IPC_CMD_HSM_PROVISION returns immediately and an lv_timer polls, because the
 * operation takes seconds and a busy-wait here would freeze the screen and
 * simultaneously stop ui_busy_modal_service() from drawing the overlay that
 * would have explained the freeze.
 */
#ifndef HSM_PROVISION_UI_H
#define HSM_PROVISION_UI_H

#include "lvgl.h"

/** Open the Enrol screen — key pair, CSR, proof of possession, certificate. */
void hsm_enrol_open(void);

/** Open the Protect screen — pending change set, confirm, run, read back. */
void hsm_protect_open(void);

/** Called from page_hsm_destroy(): drop any overlay and cancel its poll timer. */
void hsm_provision_ui_teardown(void);

#endif /* HSM_PROVISION_UI_H */
