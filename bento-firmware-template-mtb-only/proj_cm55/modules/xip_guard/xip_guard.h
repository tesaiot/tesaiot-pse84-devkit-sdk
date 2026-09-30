/*******************************************************************************
* File: xip_guard.h   (CM55)
*
* The CM55 half of the XIP guard. See ipc_xip_guard.h for the handshake.
*******************************************************************************/
#ifndef XIP_GUARD_H
#define XIP_GUARD_H

/* Publish the handshake block. Call once, before the first UI tick. */
void xip_guard_init(void);

/* As xip_guard_init(), for a CM55 whose display did not come up: marks the
 * block (IPC_XIP_GUARD_PARKS_HEADLESS) so CM33 can tell which poller answers. */
void xip_guard_init_headless(void);

/* Park in ITCM if CM33 has asked. Call first thing in the UI tick. */
void xip_guard_poll(void);

#endif /* XIP_GUARD_H */
