/*******************************************************************************
 * File Name: ipc_bentoclaw_defs.h
 *
 * Description: Shared defines for BentoClaw IPC between CM33_NS and CM55.
 *              Agent states, UI update types, IPC commands, client IDs.
 *
 *******************************************************************************/

#ifndef IPC_BENTOCLAW_DEFS_H
#define IPC_BENTOCLAW_DEFS_H

/*******************************************************************************
 * BentoClaw IPC Commands (CM33_NS → CM55)
 *******************************************************************************/
#define IPC_CMD_CLAW_UI_UPDATE    (0xCA)  /* UI field update (data[0]=type, data[1..]=text) */
#define IPC_CMD_CLAW_STREAM_TOKEN (0xCB)  /* Streaming token (data[]=UTF-8 text fragment) */

/*******************************************************************************
 * BentoClaw IPC Client ID (CM55 callback registration)
 *******************************************************************************/
#define CM55_IPC_CLAW_CLIENT_ID   (3UL)

/*******************************************************************************
 * Agent State Machine
 *******************************************************************************/
#define CLAW_STATE_IDLE       (0)
#define CLAW_STATE_EXECUTING  (1)
#define CLAW_STATE_THINKING   (2)
#define CLAW_STATE_ERROR      (3)

/*******************************************************************************
 * UI Update Types (data[0] in IPC_CMD_CLAW_UI_UPDATE)
 * Each type maps to a deferred buffer index in page_bentoclaw.c
 *******************************************************************************/
#define CLAW_UI_STATUS_TEXT   (0)   /* Connection/agent status string */
#define CLAW_UI_LAST_CMD      (1)   /* Last command/prompt sent */
#define CLAW_UI_LAST_RESULT   (2)   /* Last result/response received */
#define CLAW_UI_PROGRESS      (3)   /* Tool execution progress */
#define CLAW_UI_ERROR         (4)   /* Error message */
#define CLAW_UI_CLEAR         (5)   /* Clear all display fields */
#define CLAW_UI_STATS         (6)   /* Agent statistics (token count etc.) */

#endif /* IPC_BENTOCLAW_DEFS_H */
