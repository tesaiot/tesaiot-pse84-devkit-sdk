/*******************************************************************************
 * File Name: sensor_capsense_ipc.c
 *
 * Description: CapSense backend for QWA309-based boards — IPC snapshot, NOT
 *              direct I2C. Drop-in implementation of the sensor_capsense.h
 *              API, selected by capsense.mk when BSP_HAS_QWA309_BASEBOARD=1.
 *
 *              On the TESAIoT Dev Kit / Game Console the PSoC 4000T sits on
 *              the CM55-owned display/touch I2C bus (P17.0/1), not the CM33
 *              sensor bus — the direct-I2C backend reads the wrong bus there
 *              (and pointing CM33 at the display bus would create a
 *              two-master hazard). CM55's cm55_sensor_poll reads the 4000T
 *              every 50 ms in GFX-task context; this backend fetches that
 *              cached snapshot in one IPC_CMD_CONTROLS_STATE round trip.
 *
 *******************************************************************************/

#include "sensor_capsense.h"

#include "cy_pdl.h"
#include "ipc_communication.h"
#include <string.h>

#define CAPS_IPC_SEND_RETRIES        (20)
#define CAPS_IPC_RETRY_DELAY_US      (100)
#define CAPS_IPC_RESPONSE_TIMEOUT_MS (100)

CY_SECTION_SHAREDMEM static ipc_msg_t      caps_ipc_msg;
CY_SECTION_SHAREDMEM static ipc_response_t caps_ipc_response;

static bool caps_ipc_initialized = false;

static bool capsense_fetch_snapshot(ipc_controls_state_t *out)
{
    if (!caps_ipc_initialized) {
        cm33_ipc_communication_setup();
        caps_ipc_initialized = true;
    }

    memset((void *)&caps_ipc_response, 0, sizeof(caps_ipc_response));
    caps_ipc_response.ready = 0;

    memset(&caps_ipc_msg, 0, sizeof(caps_ipc_msg));
    caps_ipc_msg.client_id = CM55_IPC_SERVICE_CLIENT_ID;
    caps_ipc_msg.intr_mask = CY_IPC_CYPIPE_INTR_MASK_EP1;
    caps_ipc_msg.cmd       = IPC_CMD_CONTROLS_STATE;
    caps_ipc_msg.value     = (uint32_t)&caps_ipc_response;

    int retries = CAPS_IPC_SEND_RETRIES;
    cy_en_ipc_pipe_status_t status;
    do {
        status = Cy_IPC_Pipe_SendMessage(
            CM55_IPC_PIPE_EP_ADDR, CM33_IPC_PIPE_EP_ADDR,
            (void *)&caps_ipc_msg, NULL);
        if (CY_IPC_PIPE_SUCCESS == status) break;
        Cy_SysLib_DelayUs(CAPS_IPC_RETRY_DELAY_US);
    } while (--retries > 0);
    if (retries <= 0) {
        return false;
    }

    uint32_t timeout = CAPS_IPC_RESPONSE_TIMEOUT_MS;
    while (!caps_ipc_response.ready && timeout > 0) {
        Cy_SysLib_Delay(1);
        timeout--;
    }
    if (!caps_ipc_response.ready || caps_ipc_response.status != 0) {
        return false;
    }

    memcpy(out, (const void *)caps_ipc_response.data, sizeof(*out));
    return (out->caps_valid == 1);
}

bool capsense_init(void)
{
    /* No local hardware to bring up — CM55 owns the 4000T. Report ready if
     * the CM55 cache answers with live CapSense data. */
    ipc_controls_state_t st;
    return capsense_fetch_snapshot(&st);
}

bool capsense_read(capsense_data_t *data)
{
    ipc_controls_state_t st;
    if (data == NULL || !capsense_fetch_snapshot(&st)) {
        return false;
    }
    data->btn0_pressed = (st.btn0 != 0);
    data->btn1_pressed = (st.btn1 != 0);
    data->slider = st.slider;
    return true;
}

bool capsense_read_buttons(bool *btn0, bool *btn1)
{
    ipc_controls_state_t st;
    if (btn0 == NULL || btn1 == NULL || !capsense_fetch_snapshot(&st)) {
        return false;
    }
    *btn0 = (st.btn0 != 0);
    *btn1 = (st.btn1 != 0);
    return true;
}

bool capsense_read_slider(uint8_t *pos)
{
    ipc_controls_state_t st;
    if (pos == NULL || !capsense_fetch_snapshot(&st)) {
        return false;
    }
    *pos = st.slider;
    return true;
}
