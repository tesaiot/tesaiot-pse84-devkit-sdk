/*******************************************************************************
* File: ipc_scb5_lock.h
*
* One lock, held across cores, for the SCB block that drives the 3V3 I2C bus
* (SCB5, P17.0/P17.1) on the TESAIoT Dev Kit.
*
* Two cores drive that one block. CM55 owns it for the display: panel MCU
* (0x45) at boot, the FT5406 touch controller every 20 ms, the CapSense
* PSoC 4000T (0x08) every 50 ms. CM33 uses it for the OPTIGA Trust M on the
* mikroBUS (OPTIGA_I2C_ON_3V3_BUS). The two sides share the TX/RX FIFOs, the
* command register, the interrupt cause and mask registers, and the clock
* divider. Without arbitration:
*
*   - CM55's touch driver is interrupt-driven and arms the master interrupt
*     sources for each read. A CM33 OPTIGA transfer that runs while they are
*     armed raises SCB5's interrupt, which only CM55 has enabled, and CM55's
*     handler services a transfer that is not its own: it clears the status
*     CM33 is polling for and drains CM33's received bytes from the RX FIFO.
*     CM33 times out and OPTIGA reports OPTIGA_COMMS_ERROR (0x0102).
*   - Each side's error recovery disables and re-enables the block under the
*     other side's transfer.
*
* Masking the interrupt sources once, from CM33, does not fix it: CM55's next
* touch read arms them again, and the PDL re-arms them on every transfer.
*
* The lock is IPC semaphore IPC_SCB5_SEMA_NUM. CM33 creates the semaphore
* array (cm33_ipc_communication_setup()); CM55 attaches to it with
* Cy_IPC_Sema_Init(IPC0_SEMA_CH_NUM, 0, NULL) before its first use.
*
* RULES
*   - Hold it for one bus transaction (or one short recovery), never across
*     an OPTIGA command: the chip spends milliseconds computing between
*     transfers, and CM55's touch reads fit in those gaps.
*   - Leave the block quiet before releasing: nothing in flight on this
*     core's context and no interrupt source armed (ipc_scb5_quiesce()), so
*     the other core's next transfer cannot wake this core's handler.
*   - CM55 never blocks the GFX task on it: touch and CapSense reads that find
*     it taken are skipped for that tick. CM33 waits, with a bound, and
*     reports the bus busy to the OPTIGA stack, which retries.
*   - CM55 holds it from before the display task starts until the display is
*     up, which covers the panel and touch bring-up at boot.
*******************************************************************************/
#ifndef IPC_SCB5_LOCK_H
#define IPC_SCB5_LOCK_H

#include <stdbool.h>
#include "cy_ipc_sema.h"
#include "cy_scb_i2c.h"
#include "cy_syslib.h"

/* 0-5 belong to the secure request framework (bsps/.../mtb_ipc_config.h) and
 * 10/11 to the PDL (CY_SYSPM_SEMA_NUM_MULTI_CORE, CY_SYSPM_SEMA_POST_TRIM_STATUS).
 * 16 is free, and in the first word of the array, which the PDL's cache
 * maintenance on CM55 covers. */
#define IPC_SCB5_SEMA_NUM   (16UL)

/* Every semaphore operation first takes the IPC channel's own hardware lock
 * (channel IPC0_SEMA_CH_NUM), and tries it exactly once: while the other core
 * is inside a Set or Clear, the call returns CY_IPC_SEMA_LOCKED having done
 * nothing. That is not "someone holds SCB5" -- it is "try again in a moment".
 * Treating it as final would, on the unlock side, leave the semaphore set for
 * good and lock both cores out of the bus. So both calls retry on LOCKED, and
 * run non-preemptable so the channel lock is held for a few instructions, not
 * across a task switch. */
#define IPC_SCB5_CHAN_RETRIES   (100000UL)   /* x ~1 us: far above a Set/Clear */

/* True when this core now holds the lock. Does not wait for the other core to
 * release it: CY_IPC_SEMA_NOT_ACQUIRED returns false at once. */
static inline bool ipc_scb5_try_lock(void)
{
    for (uint32_t i = 0UL; i < IPC_SCB5_CHAN_RETRIES; ++i) {
        cy_en_ipcsema_status_t st = Cy_IPC_Sema_Set(IPC_SCB5_SEMA_NUM, false);
        if (CY_IPC_SEMA_SUCCESS == st) return true;
        if (CY_IPC_SEMA_LOCKED != st) return false;
        Cy_SysLib_DelayUs(1U);
    }
    return false;
}

static inline void ipc_scb5_unlock(void)
{
    for (uint32_t i = 0UL; i < IPC_SCB5_CHAN_RETRIES; ++i) {
        if (CY_IPC_SEMA_LOCKED != Cy_IPC_Sema_Clear(IPC_SCB5_SEMA_NUM, false)) return;
        Cy_SysLib_DelayUs(1U);
    }
}

/* Poll for the lock for at most wait_us. A busy-poll, not a task delay: the
 * callers include the OPTIGA stack running in the FreeRTOS timer task, which
 * must not block on kernel objects, and the CM55 GFX task. */
static inline bool ipc_scb5_lock_wait_us(uint32_t wait_us)
{
    for (uint32_t t = 0UL; ; t += 100UL) {
        if (ipc_scb5_try_lock()) return true;
        if (t >= wait_us) return false;
        Cy_SysLib_DelayUs(100U);
    }
}

/* How long a CM55 driver that must not silently drop a write (RGB matrix,
 * Arduino-header devices) waits for CM33 to finish one OPTIGA transfer: the
 * longest OPTIGA frame is ~280 bytes, ~25 ms at 100 kHz. Touch and CapSense
 * polls do not wait at all; they skip the period. */
#define IPC_SCB5_CM55_WAIT_US   (30000UL)

/* Disarm every interrupt source of the block and drop anything pending, so a
 * transfer started by the other core cannot enter this core's handler. Call
 * with the lock held, after this core's transfer has finished or been reset. */
static inline void ipc_scb5_quiesce(CySCB_Type *base)
{
    SCB_I2C_CTRL(base) &= (uint32_t)~SCB_I2C_CTRL_M_READY_DATA_ACK_Msk;
    Cy_SCB_SetMasterInterruptMask(base, CY_SCB_CLEAR_ALL_INTR_SRC);
    Cy_SCB_SetSlaveInterruptMask(base, CY_SCB_CLEAR_ALL_INTR_SRC);
    Cy_SCB_SetI2CInterruptMask(base, CY_SCB_CLEAR_ALL_INTR_SRC);
    Cy_SCB_SetTxInterruptMask(base, CY_SCB_CLEAR_ALL_INTR_SRC);
    Cy_SCB_SetRxInterruptMask(base, CY_SCB_CLEAR_ALL_INTR_SRC);
    Cy_SCB_ClearMasterInterrupt(base, CY_SCB_I2C_MASTER_INTR_ALL);
    Cy_SCB_ClearSlaveInterrupt(base, CY_SCB_I2C_SLAVE_INTR);
}

#endif /* IPC_SCB5_LOCK_H */
