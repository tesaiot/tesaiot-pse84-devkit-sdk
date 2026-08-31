/*******************************************************************************
 * File Name        : cm33_ipc_communication.c
 *
 * Description      : IPC pipe communication setup for CM33 CPU.
 *
 * Author           : Asst.Prof.Santi Nuratch, Ph.D
 *                    Thailand Embedded Systems Association (TESA)
 *
 *******************************************************************************/

#include "ipc_communication.h"

/*******************************************************************************
 * Global Variable(s)
 *******************************************************************************/
/* Create an array of endpoint structures */
static cy_stc_ipc_pipe_ep_t cm33_ipc_pipe_ep_array[CY_IPC_MAX_ENDPOINTS];

/* CB Array for EP1 */
static cy_ipc_pipe_callback_ptr_t ep1_cb_array[CY_IPC_CYPIPE_CLIENT_CNT];

/* Allocate and initialize semaphores for the system operations. */
CY_SECTION_SHAREDMEM static uint32_t ipc_sema_array[CY_IPC_SEMA_COUNT / CY_IPC_SEMA_PER_WORD];
static bool s_cm33_ipc_initialized = false;

/* clang-format off */
static const cy_stc_ipc_pipe_config_t cm33_ipc_pipe_config = {
    /* receiver endpoint CM33 */
    {
      .ipcNotifierNumber = CY_IPC_INTR_CYPIPE_EP1,
      .ipcNotifierPriority = CY_IPC_INTR_CYPIPE_PRIOR_EP1,
      .ipcNotifierMuxNumber = CY_IPC_INTR_CYPIPE_MUX_EP1,
      .epAddress = CM33_IPC_PIPE_EP_ADDR,
      {
        .epChannel = CY_IPC_CHAN_CYPIPE_EP1,
        .epIntr = CY_IPC_INTR_CYPIPE_EP1,
        .epIntrmask = CY_IPC_CYPIPE_INTR_MASK
      }
    },

    /* sender endpoint CM55 */
    {
      .ipcNotifierNumber = CY_IPC_INTR_CYPIPE_EP2,
      .ipcNotifierPriority = CY_IPC_INTR_CYPIPE_PRIOR_EP2,
      .ipcNotifierMuxNumber = CY_IPC_INTR_CYPIPE_MUX_EP2,
      .epAddress = CM55_IPC_PIPE_EP_ADDR,
      {
        .epChannel = CY_IPC_CHAN_CYPIPE_EP2,
        .epIntr = CY_IPC_INTR_CYPIPE_EP2,
        .epIntrmask = CY_IPC_CYPIPE_INTR_MASK
      }
    },
    .endpointClientsCount = CY_IPC_CYPIPE_CLIENT_CNT,
    .endpointsCallbacksArray = ep1_cb_array,
    .userPipeIsrHandler = &cm33_ipc_pipe_isr
};
/* clang-format on */

/*******************************************************************************
 * Function Name: cm33_ipc_pipe_isr
 ********************************************************************************
 *
 * This is the interrupt service routine for the system pipe.
 *
 *******************************************************************************/
void cm33_ipc_pipe_isr(void)
{
  Cy_IPC_Pipe_ExecuteCallback(CM33_IPC_PIPE_EP_ADDR);
}

static void cm33_ipc_communication_setup_impl(bool force_reinit)
{
  if (s_cm33_ipc_initialized && !force_reinit) {
    return;
  }

  Cy_IPC_Sema_Init(IPC0_SEMA_CH_NUM, CY_IPC_SEMA_COUNT, ipc_sema_array);
  Cy_IPC_Pipe_Config(cm33_ipc_pipe_ep_array);
  Cy_IPC_Pipe_Init(&cm33_ipc_pipe_config);
  s_cm33_ipc_initialized = true;
}

/*******************************************************************************
 * Function Name: cm33_ipc_communication_setup
 ********************************************************************************
 * Summary:
 * This function...
 * 1. Initializes IPC Semaphore.
 * 2. Configures IPC Pipe for CM33 to CM55 communication.
 *
 *******************************************************************************/
void cm33_ipc_communication_setup(void)
{
  cm33_ipc_communication_setup_impl(false);
}

/*******************************************************************************
 * Function Name: cm33_ipc_communication_recover
 ********************************************************************************
 * Summary:
 * Forces IPC pipe/semaphore re-initialization on CM33.
 * Used by UI module when repeated probe failures suggest stale IPC state
 * after MicroPython soft-reset and rapid Run/REPL transitions.
 *
 *******************************************************************************/
void cm33_ipc_communication_recover(void)
{
  cm33_ipc_communication_setup_impl(true);
}
