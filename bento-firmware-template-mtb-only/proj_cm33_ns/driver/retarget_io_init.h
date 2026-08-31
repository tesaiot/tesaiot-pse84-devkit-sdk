/******************************************************************************
* File Name:   retarget_io_init.h
*
* Description: Public interface for retarget-io UART initialization.
*              Used by MicroPython port for serial I/O (REPL).
*              Adapted from Rutronik reference project.
*
******************************************************************************/

#ifndef _RETARGET_IO_INIT_H_
#define _RETARGET_IO_INIT_H_

#include "cybsp.h"
#include "mtb_hal.h"
#include "cy_retarget_io.h"
#include "mtb_syspm_callbacks.h"

/* retarget-io deepsleep callback macros */
#define DEBUG_UART_RTS_PORT     (NULL)
#define DEBUG_UART_RTS_PIN      (0U)

/* Default syspm callback configuration elements */
#define SYSPM_SKIP_MODE         (0U)
#define SYSPM_CALLBACK_ORDER    (1U)

void init_retarget_io(void);

__STATIC_INLINE void handle_app_error(void)
{
    __disable_irq();
    CY_ASSERT(0);
    while(true);
}

#endif /* _RETARGET_IO_INIT_H_ */
