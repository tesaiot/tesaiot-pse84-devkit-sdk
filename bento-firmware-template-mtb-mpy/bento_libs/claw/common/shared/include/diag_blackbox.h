/*******************************************************************************
 * diag_blackbox.h — cross-core hang/crash black box (CM55 writes, CM33 reads).
 *
 * Motive: the console froze on the bench (screen + touch dead, CM33 fine)
 * with no way to see WHICH CM55 task died or why — CM55 has no UART, the
 * Progtools openocd config exposes no CM55 debug target, and the wedge takes
 * the IPC pipe down with it, so nothing can be asked over IPC either.
 *
 * This block lives at a FIXED SOCMEM address both cores map identically
 * (same convention as imu_shared.h: fixed block in the lower half of
 * m33_m55_shared; the GCC-allocated CY_SECTION_SHAREDMEM window starts at
 * 0x261E0000 and the IMU block owns 0x261C0040, so 0x261C0200 is free).
 * CM55 tasks bump their beat counters; a CM33 beacon task samples the block
 * every 2 s and prints it over the debug UART ONLY when something is wrong —
 * a heartbeat that stopped advancing (tagged STALLED) or a fault record
 * (tagged FAULT).  A healthy board is silent, because these lines otherwise
 * land in the student's IDE terminal interleaved with their own output.
 * One baseline line is printed after each (re)boot so the log shows the
 * beacon is running.
 *
 * All fields are plain monotonic counters or write-once fault records — no
 * seqlock needed; a torn read is at worst one stale sample.
 *******************************************************************************/
#ifndef DIAG_BLACKBOX_H
#define DIAG_BLACKBOX_H

#include <stdint.h>

#define BBX_ADDR    (0x261C0000UL + 0x200UL)
#define BBX_MAGIC   (0xB1ACB0C5UL)

/* fault_kind values (mirror the LED blink codes in proj_cm55/main.c) */
#define BBX_FAULT_STACK   (1UL)
#define BBX_FAULT_MALLOC  (2UL)
#define BBX_FAULT_HARD    (3UL)

typedef struct __attribute__((packed, aligned(4))) {
    uint32_t magic;            /* BBX_MAGIC once CM55 watchman started */
    uint32_t watchman_beat;    /* prio MAX-1 watchman, 4 Hz */
    uint32_t gfx_beat;         /* LVGL/GFX main loop iterations */
    uint32_t app_beat;         /* app_task heartbeat (copied) */
    uint32_t ep2_busy;         /* CM55 IPC endpoint busy flag, sampled */
    uint32_t pipe_recoveries;  /* wifi_manager pipe self-heal count */
    uint32_t js_report_cnt;    /* HID reports received */
    uint32_t js_isr_count;     /* USB host ISR count */
    uint32_t usbh_running;     /* emUSB-Host stack running flag */
    uint32_t num_devices;      /* USB devices enumerated */
    uint32_t fault_flag;       /* 1 = a fault handler ran on CM55 */
    uint32_t fault_kind;       /* BBX_FAULT_* */
    uint32_t fault_pc;         /* stacked PC at HardFault */
    uint32_t fault_lr;         /* stacked LR at HardFault */
    uint32_t fault_cfsr;       /* SCB->CFSR */
    uint32_t fault_hfsr;       /* SCB->HFSR */
    uint32_t gfx_stage;        /* which call inside the GFX loop is running:
                                  1=lv_timer_handler 2=vg_lite_finish
                                  3=volume-poll 4=loop-tail. A frozen value
                                  names the call that never returned. */
} bbx_t;

#define BBX ((volatile bbx_t *)BBX_ADDR)

#endif /* DIAG_BLACKBOX_H */
