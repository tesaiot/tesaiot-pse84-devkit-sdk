/*******************************************************************************
* File: xip_guard.c   (CM55)
*
* The CM55 half of the XIP guard described in ipc_xip_guard.h. Parks the core
* in ITCM while CM33 erases or programs the serial NOR that CM55 executes from.
*******************************************************************************/
#include "cy_pdl.h"
#include "ipc_xip_guard.h"
#include "xip_guard.h"

/* Not a linker-placed object: both cores address the block by the literal in
 * ipc_xip_guard.h. See the comment there for why. */
#define s_guard (*ipc_xip_guard_get())

/* Whether DWT->CYCCNT is counting. Measured once in xip_guard_init(); when it
 * is not, the park falls back to an instruction-count budget. */
static bool s_cyccnt_ok;

/* A request the park gave up on (CM33 never cleared it). Not parked for again,
 * or a CM33 that died mid-erase would keep the display frozen on every tick. */
static uint32_t s_abandoned_token;

/*******************************************************************************
* The spin itself. Three properties matter and all three are load bearing:
*
*   .cy_itcm   - it must not be fetched from the flash being erased.
*   noinline   - inlining it into the XIP-resident poll below would put the
*                spin back in the window and defeat the whole thing.
*   optimize   - no call, no table, no literal pool the compiler might place
*                somewhere else; the loop has to be self-contained.
*
* Interrupts are off for the duration. Any ISR still living in the XIP window
* would fault the instant it was entered, and the SysTick/FreeRTOS tick that is
* missed here is worth far less than a core that stops.
*
* The deadline is read from the core's own cycle counter, a register in the
* private peripheral bus: no driver code, nothing fetched from flash. It has to
* be a real time bound, not a guess at cycles per iteration, because it must
* outlast the chip's worst-case sector erase (IPC_XIP_GUARD_MAX_PARK_MS).
*******************************************************************************/
CY_SECTION(".cy_itcm") __attribute__((noinline, optimize("O1")))
static bool xip_guard_park(ipc_xip_guard_t *g, uint32_t token, uint32_t budget,
                           bool use_cyccnt)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    /* Acknowledge THIS request, by its token: a CM33 that gave up on an earlier
     * request and has since made a new one must not take this park, which is
     * about to end, as the answer to the new one. */
    g->parked = token;
    __DMB();

    uint32_t start = DWT->CYCCNT;
    while (g->request == token) {
        if (use_cyccnt) {
            if ((uint32_t)(DWT->CYCCNT - start) >= budget) break;
        } else {
            if (budget-- == 0u) break;
        }
    }

    bool timed_out = (g->request == token);
    g->parked = 0u;
    g->parks++;
    __DMB();

    __set_PRIMASK(primask);
    return timed_out;
}

/*******************************************************************************
* Called from the 33 ms UI tick. Cheap when nothing is happening: one read of a
* shared word.
*
* PARKS ON THE FIRST SIGHTING. A small file is written as many short program
* cycles, and CM33 sets and clears `request` for each; spending a tick on
* anything else (an overlay, a second look) means CM33 gives up waiting and the
* write is refused. Telling the user why the screen paused belongs to whoever
* started the write.
*
* The UI tick is an LVGL timer, so it never runs in the middle of a frame:
* LVGL waits for the GPU to finish before it leaves the refresh, and the
* VG-Lite GPU is idle here -- it is not fetching assets from the flash either.
*******************************************************************************/
void xip_guard_poll(void)
{
    if (s_guard.magic != IPC_XIP_GUARD_MAGIC) {
        return;
    }
    uint32_t token = s_guard.request;
    if ((token != 0u) && (token != s_abandoned_token)) {
        /* In cycles when CYCCNT runs: 400 MHz x 8000 ms = 3.2e9, which fits
         * in 32 bits, and CYCCNT wraps only after ~10.7 s at that clock.
         * Without CYCCNT the same number is used as an iteration count; every
         * iteration takes several cycles, so the park lasts at least as long,
         * which errs on the side of staying parked. */
        uint32_t budget = (SystemCoreClock / 1000u) * IPC_XIP_GUARD_MAX_PARK_MS;
        if (xip_guard_park(&s_guard, token, budget, s_cyccnt_ok)) {
            s_abandoned_token = token;
        }
    }
}

void xip_guard_init(void)
{
    /* Start the cycle counter the park uses as its clock. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    uint32_t c0 = DWT->CYCCNT;
    Cy_SysLib_DelayUs(10u);
    s_cyccnt_ok = (DWT->CYCCNT != c0);

    s_guard.request = 0u;
    s_guard.parked  = 0u;
    s_guard.parks   = 0u;
    __DMB();
    s_guard.magic   = IPC_XIP_GUARD_MAGIC;   /* published last */
    __DMB();
}

void xip_guard_init_headless(void)
{
    xip_guard_init();
    s_guard.parks = IPC_XIP_GUARD_PARKS_HEADLESS;
    __DMB();
}
