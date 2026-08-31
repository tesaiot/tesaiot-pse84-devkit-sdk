/*******************************************************************************
 * debug_log_disable.h — Global printf suppression
 *
 * Force-included via CFLAGS (-include debug_log_disable.h) to silence ALL
 * printf output on the USB-CDC UART.  This prevents:
 *   1. UART mutex priority inversion (retarget-io CY_RTOS_NEVER_TIMEOUT)
 *   2. BentoClaw TACP binary frame corruption (text interleaving)
 *
 * MicroPython REPL is NOT affected — it uses mp_printf / mp_hal_stdout_tx_strn
 * which bypass retarget-io's printf path entirely.
 *
 * To re-enable debug logging:
 *   - Remove "-include debug_log_disable.h" from Makefile
 *   - Or define DEBUG_LOG_ENABLE before this header is included
 *
 * Copyright (c) 2026 TESAIoT
 ******************************************************************************/
#ifndef DEBUG_LOG_DISABLE_H
#define DEBUG_LOG_DISABLE_H

#ifndef DEBUG_LOG_ENABLE
  /* Suppress printf — expands to nothing, zero cost */
  #ifdef printf
    #undef printf
  #endif
  #define printf(...) ((void)0)
#endif

#endif /* DEBUG_LOG_DISABLE_H */
