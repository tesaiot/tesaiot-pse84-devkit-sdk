/*******************************************************************************
* File: ipc_xip_guard.h
*
* Keeps CM55 from fetching instructions while CM33 erases or programs the
* serial NOR that CM55 is executing from.
*
* On the KIT_PSE84_AI SoM the CM55 image runs in place from the same S25HS512T
* that carries the LittleFS volume (SMIF0, slave slot 1, mapped at 0x60000000;
* the volume starts at offset 0x00C00000). The CM33 images live on the same
* chip. A 256 KB sector erase takes about 0.8 s typically and up to 5.9 s
* worst case (cycfg_qspi_memslot.c, eraseTime), and for that whole time the
* device does not answer reads. KIT_PSE84_HMI has the same arrangement on its
* S28HS01GT, where this guard was first proven. The first instruction CM55 needs that is not
* already in cache never arrives, and the core stops. A page program is the
* same thing, for up to about 2 ms.
*
* The PDL protects only the core that issues the command: Cy_SMIF_MemEraseSector
* and Cy_SMIF_MemWrite disable interrupts on the CALLING core while XIP is on,
* and the CM33 driver code is linked to SRAM. Nothing covers the other core.
*
* The handshake is deliberately small and lives in shared memory that CM55's MPU
* maps non-cacheable (cycfg_system.c, cycfg_mpu_cm55_ns_0_config region 1,
* 0x240FD000-0x240FFFFF), so both cores see each other's writes without cache
* maintenance:
*
*   CM33                                  CM55 (33 ms UI tick)
*   request = T  ---------------------->  sees it, calls the ITCM-resident park
*   wait for parked == T <--------------  __disable_irq(); parked = T; spin
*   erase / program                       (no fetch from flash at all)
*   request = 0  ---------------------->  leaves the spin, parked = 0, irq back
*
* T is a non-zero token that changes with every request, so a park that
* answers an earlier, abandoned request is never mistaken for an answer to
* the current one. A CM33 half that always uses T = 1 (the MicroPython QSPI
* driver) still works; it just does not get that protection.
*
* Everything CM55 touches between "parked = 1" and "parked = 0" has to be in
* ITCM or RAM. That is why the spin is its own noinline function in .cy_itcm
* and why interrupts are off: any ISR still living in the XIP window would
* fault the moment it was entered.
*
*******************************************************************************
* WHAT HAPPENS WITHOUT IT
*
* The symptom does not look like a storage bug. Depending on what CM55 was
* doing, it either faults at a different address in an unrelated module every
* time (UNDEFINSTR / UNALIGNED / IMPRECISERR -- an instruction fetch that never
* returned), or it simply stops: the GFX loop halts mid-frame, touch dies, and
* the CM33 -> CM55 IPC pipe stays busy for ever. Chasing it by moving the
* faulting function does not converge; there is no faulting function.
*
* BOTH HALVES ARE REQUIRED and either one alone does nothing:
*   CM55   modules/xip_guard/xip_guard.c: xip_guard_init() before the first UI
*          tick, xip_guard_poll() first in the tick. Build flag BENTO_XIP_GUARD=1.
*   CM33   whatever erases or programs the chip takes the handshake:
*            - C storage layer (storage_c/bento_storage.c), mtb-only variant
*            - MicroPython QSPI driver when the board sets EXT_FLASH_XIP_GUARD=1
*
* THE ACKNOWLEDGEMENT IS NOT ADVISORY. The CM33 half must refuse to erase when
* CM55 has not parked. A guard that erases anyway after a timeout makes the
* failure intermittent instead of absent, which is worse: the same write passes
* after a power cycle and kills the core after a warm reset.
*
* Two waits, for two different situations, and both must be generous:
*   - the magic: CM33 can reach the flash before CM55 has published, and the
*     block is ordinary RAM that a warm reset does not clear -- so a STALE
*     magic from the previous run can read as "ready" about a core that is
*     still starting. Clear the block from CM33 before starting CM55 (the SDK
*     template's proj_cm33_ns/main.c does, in both variants).
*   - parked: the poll runs on the UI tick, and a busy tick misses its slot.
*
* A refused write is an error the caller can retry. An erase into a running
* CM55 is a dead core and a power cycle. Fail the write.
*
* READS ARE SAFE. Only erase and program stall the part.
*******************************************************************************/
#ifndef IPC_XIP_GUARD_H
#define IPC_XIP_GUARD_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    volatile uint32_t magic;    /* IPC_XIP_GUARD_MAGIC once CM55 has set up  */
    volatile uint32_t request;  /* CM33 -> CM55: park now; non-zero token    */
    volatile uint32_t parked;   /* CM55 -> CM33: the token it parked for     */
    volatile uint32_t parks;    /* diagnostic: completed parks               */
} ipc_xip_guard_t;

#define IPC_XIP_GUARD_MAGIC       (0x58495047u)   /* "XIPG" */

/* Diagnostic bit in `parks`: set when CM55 answers from its headless poller
 * (the display did not come up) rather than from the UI tick. */
#define IPC_XIP_GUARD_PARKS_HEADLESS (0x80000000u)

/* How long CM55 stays parked before it gives up on a CM33 that never clears
 * `request`. It must cover the chip's worst-case 256 KB sector erase (5869 ms
 * in cycfg_qspi_memslot.c) with margin: leaving the park early re-opens the
 * exact window the guard exists to close. It is still bounded, so a CM33 that
 * dies mid-erase cannot freeze the display for ever. */
#define IPC_XIP_GUARD_MAX_PARK_MS (8000u)

/* How long CM33 waits for CM55 to reach the spin before giving up. The poll
 * runs every 33 ms; a second is thirty polls. */
#define IPC_XIP_GUARD_ACK_MS      (1000u)

/* How long CM33 waits, once, for CM55 to publish the block after CM55 has been
 * started. The UI comes up a few seconds after CM55 starts; if it never does
 * (panel absent, CM55 running headless) the block is never published and every
 * write is refused -- CM55 is still executing from the chip, so an unguarded
 * erase would still stop it. */
#define IPC_XIP_GUARD_MAGIC_WAIT_MS (15000u)

/* A fixed address rather than a linker section, because .cy_sharedmem is NOT
 * one block: each core's copy goes into its own region -- CM33's into
 * m33_allocatable_shared (0x240FD000 on KIT_PSE84_AI, 0x240FE000 on
 * KIT_PSE84_HMI), CM55's into m55_allocatable_shared at 0x240FF000. Both regions are visible from both cores, so the two sides agree
 * on one literal address instead and no linker cooperation is required.
 *
 * The last 16 bytes of the CM55 shared region: that region is 4 KB and CM55's
 * .cy_sharedmem uses under 3 KB of it today (check the CM55 map when adding
 * shared objects). `magic` is written last by CM55 and checked by CM33, so a
 * stale or unmapped block is detected rather than trusted. */
#define IPC_XIP_GUARD_ADDR (0x240FFFF0u)

static inline ipc_xip_guard_t *ipc_xip_guard_get(void)
{
    return (ipc_xip_guard_t *)IPC_XIP_GUARD_ADDR;
}

#endif /* IPC_XIP_GUARD_H */
