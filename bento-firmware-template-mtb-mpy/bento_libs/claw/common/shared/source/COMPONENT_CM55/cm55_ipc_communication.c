/*******************************************************************************
 * File Name        : cm55_ipc_communication.c
 *
 * Description      : IPC pipe communication setup for CM55 CPU.
 *
 * Author           : Asst.Prof.Santi Nuratch, Ph.D
 *                    Thailand Embedded Systems Association (TESA)
 *
 *******************************************************************************/

#include "ipc_communication.h"
#include "cy_sysint.h"

/*******************************************************************************
 * Global Variable(s)
 *******************************************************************************/
/* Create an array of endpoint structures */
static cy_stc_ipc_pipe_ep_t cm55_ipc_pipe_array[CY_IPC_MAX_ENDPOINTS];

/* CB Array for EP2 */
static cy_ipc_pipe_callback_ptr_t ep2_cb_array[CY_IPC_CYPIPE_CLIENT_CNT];

/*******************************************************************************
 * Function Name: Cy_SysIpcPipeIsrCm55
 ********************************************************************************
 * Summary:
 * This is the interrupt service routine for the system pipe.
 *
 *******************************************************************************/
void Cy_SysIpcPipeIsrCm55(void)
{
  Cy_IPC_Pipe_ExecuteCallback(CM55_IPC_PIPE_EP_ADDR);
}

/*******************************************************************************
 * Function Name: cm55_ipc_communication_setup
 ********************************************************************************
 * Summary:
 *  This function configures IPC Pipe for CM55 to CM33 communication.
 *
 *******************************************************************************/
//! [j2_cm55_ipc_setup]
void cm55_ipc_communication_setup(void)
{

  /******************************************************/
  /* IPC pipe endpoint-1 and endpoint-2. CM55 <--> CM33 */
  /******************************************************/

  /* clang-format off */
    static const cy_stc_ipc_pipe_config_t cm55_ipc_pipe_config =
    {
      /* receiver endpoint CM55 */
      {
        .ipcNotifierNumber     = CY_IPC_INTR_CYPIPE_EP2,
        .ipcNotifierPriority   = CY_IPC_INTR_CYPIPE_PRIOR_EP2,
        .ipcNotifierMuxNumber  = CY_IPC_INTR_CYPIPE_MUX_EP2,
        .epAddress             = CM55_IPC_PIPE_EP_ADDR,
        {
          .epChannel         = CY_IPC_CHAN_CYPIPE_EP2,
          .epIntr            = CY_IPC_INTR_CYPIPE_EP2,
          .epIntrmask        = CY_IPC_CYPIPE_INTR_MASK
        }
      },

      /* sender endpoint CM33 */
      {
        .ipcNotifierNumber     = CY_IPC_INTR_CYPIPE_EP1,
        .ipcNotifierPriority   = CY_IPC_INTR_CYPIPE_PRIOR_EP1,
        .ipcNotifierMuxNumber  = CY_IPC_INTR_CYPIPE_MUX_EP1,
        .epAddress             = CM33_IPC_PIPE_EP_ADDR,
        {
          .epChannel         = CY_IPC_CHAN_CYPIPE_EP1,
          .epIntr            = CY_IPC_INTR_CYPIPE_EP1,
          .epIntrmask        = CY_IPC_CYPIPE_INTR_MASK
        }
      },
    .endpointClientsCount          = CY_IPC_CYPIPE_CLIENT_CNT,
    .endpointsCallbacksArray       = ep2_cb_array,
    .userPipeIsrHandler            = &Cy_SysIpcPipeIsrCm55
    };
  /* clang-format on */

  Cy_IPC_Pipe_Config(cm55_ipc_pipe_array);

  Cy_IPC_Pipe_Init(&cm55_ipc_pipe_config);

  {
    cy_stc_sysint_t ep2_intr_cfg = {
      .intrSrc      = (IRQn_Type)CY_IPC_INTR_CYPIPE_MUX_EP2,
      .intrPriority = (uint32_t)CY_IPC_INTR_CYPIPE_PRIOR_EP2
    };
    (void)Cy_SysInt_Init(&ep2_intr_cfg, &Cy_SysIpcPipeIsrCm55);
    NVIC_EnableIRQ((IRQn_Type)CY_IPC_INTR_CYPIPE_MUX_EP2);
    //! [j2_cm55_ipc_setup]
  }
}

/*******************************************************************************
 * Function Name: cm55_ipc_pipe_drain_release
 ********************************************************************************
 * Summary:
 *  Full IPC pipe drain: clears BOTH the software busy flag AND any stuck
 *  hardware channel lock.  Call before Cy_IPC_Pipe_SendMessage.
 *
 *  Two independent failure modes are handled:
 *
 *  1. Software busy flag stuck — fromEp->busy was set by a previous
 *     SendMessage but the release ISR on CM55 never fired (timing/preemption).
 *     Fix: force-process pending ISR, then force-clear the flag.
 *
 *  2. Hardware channel lock stuck — CM55 acquired the IPC channel (via
 *     LockAcquire inside SendMessage) but CM33's ISR never released it.
 *     This happens when CM33's IPC interrupt is delayed by high-priority
 *     activity (TLS handshake, WiFi driver critical section).
 *     Fix: force-release from CM55 side.  LockRelease only succeeds if
 *     CM55 holds the lock (hardware-enforced), so it's safe.
 *
 *  This accesses the endpoint array directly because the pipe driver's
 *  internal pointer is static and has no public reset API.
 *******************************************************************************/
void cm55_ipc_pipe_drain_release(void)
{
  /* Step 1: Clear software busy flag on CM55's endpoint (EP2) */
  cy_stc_ipc_pipe_ep_t *ep = &cm55_ipc_pipe_array[CM55_IPC_PIPE_EP_ADDR];

  if (ep->busy != CY_IPC_PIPE_ENDPOINT_NOTBUSY) {
    /* Force-process the ISR in case a release interrupt is pending */
    Cy_IPC_Pipe_ExecuteCallback(CM55_IPC_PIPE_EP_ADDR);

    /* If still busy after ISR replay, force clear.
     * Safe because CM55 only sends from the LVGL task (single writer). */
    ep->busy = CY_IPC_PIPE_ENDPOINT_NOTBUSY;
  }

  /* Step 2: Force-release hardware channel lock if CM55 still holds it.
   * Channel 4 (CM33 EP1) is the target channel for CM55→CM33 sends.
   * If CM55 locked it via SendMessage but CM33's ISR never released it,
   * the channel stays locked permanently.  Cy_IPC_Drv_LockRelease only
   * succeeds if the calling bus master (CM55) holds the lock — if CM33
   * or system holds it, the write has no effect (hardware enforced). */
  IPC_STRUCT_Type *ch = Cy_IPC_Drv_GetIpcBaseAddress(CY_IPC_CHAN_CYPIPE_EP1);
  if (Cy_IPC_Drv_IsLockAcquired(ch)) {
    Cy_IPC_Drv_LockRelease(ch, 0);  /* Release without interrupt notification */
  }
}

/*******************************************************************************
 * Function Name: cm55_ipc_pipe_ep_busy
 ********************************************************************************
 * Summary:
 *  Diagnostic accessor: current busy flag of CM55's own pipe endpoint.
 *  A nonzero value that never clears is the signature of the wedge that
 *  cm55_ipc_pipe_drain_release() exists to break — exposing it lets a
 *  watchman task publish the flag to a black box without reaching into
 *  this file's static state.
 *******************************************************************************/
uint32_t cm55_ipc_pipe_ep_busy(void)
{
  return cm55_ipc_pipe_array[CM55_IPC_PIPE_EP_ADDR].busy;
}
