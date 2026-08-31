/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2025 Infineon Technologies AG
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// std includes
#include "stdbool.h"   // because of missing include in shared/timeutils/timeutils.h
#include "stdio.h"

// MTB includes
#include "retarget_io_init.h"

// micropython includes
#include "mpconfigport.h"
#include "mphalport.h"

#include "py/runtime.h"
#include "shared/timeutils/timeutils.h"
#include "tacp.h"

extern mtb_hal_uart_t DEBUG_UART_hal_obj;
extern mtb_hal_timer_t psoc_edge_timer;
extern uint64_t modtime_ticks_us64(void);   // strictly-monotonic 64-bit us (modtime.c)

#ifdef CY_RTOS_AWARE
#include "FreeRTOS.h"
#include "task.h"
#endif

void mp_hal_delay_ms(mp_uint_t ms) {
    /* Service TACP once before the sleep loop, not only inside it.
     *
     * A frame-paced game loop sleeps for whatever is LEFT of its frame period
     * (bentogame.run() does time.sleep_ms(max(0, delay - elapsed))).  Any game
     * that overruns its budget therefore calls sleep_ms(0) every frame, the
     * loop below runs zero iterations, and tacp_poll_uart() never gets called
     * at all: no Ctrl-C, no PROGRAM_MODE, no HARD_RESET.  The board became
     * completely unreachable from the IDE and only a power cycle recovered it.
     * One unconditional poll keeps a running script interruptible no matter
     * how little time it leaves to sleep.
     *
     * Use the _FAST hook, never the plain one: MICROPY_EVENT_POLL_HOOK ends in
     * mp_hal_rtos_yield(), which is vTaskDelay(1) — a full FreeRTOS tick. Doing
     * that here charged every sleep_ms() call, sleep_ms(0) included, ~1 ms:
     * measured 988 us of pure overhead per call, which is most of a 50 fps
     * frame budget spent on nothing. _FAST still runs mp_handle_pending(), so a
     * scheduled KeyboardInterrupt or SystemExit is raised exactly as before —
     * the yield was never what made a script interruptible. */
    tacp_poll_uart();
    MICROPY_EVENT_POLL_HOOK_FAST;

    #ifdef CY_RTOS_AWARE
    /* Sleep in 1-tick chunks. tacp_poll_uart() reads UART bytes and detects
     * Ctrl-C / TACP commands during sleep. MICROPY_EVENT_POLL_HOOK then
     * raises any scheduled exception in the same iteration.               */
    mp_uint_t start = mp_hal_ticks_ms();
    while (mp_hal_ticks_ms() - start < ms) {
        tacp_poll_uart();
        MICROPY_EVENT_POLL_HOOK;
        vTaskDelay(1);
    }
    #else
    mp_uint_t start = mp_hal_ticks_ms();
    while (mp_hal_ticks_ms() - start < ms) {
        tacp_poll_uart();
        MICROPY_EVENT_POLL_HOOK;
        mtb_hal_system_delay_ms(1);
    }
    #endif
}

#ifdef CY_RTOS_AWARE
void mp_hal_rtos_yield(void) {
    tacp_poll_uart();
    vTaskDelay(1);
}
#endif

void mp_hal_delay_us(mp_uint_t us) {
    mtb_hal_system_delay_us(us);
}

uint64_t mp_hal_time_ns(void) {
    // TODO: This is not fully functional until rtc machine module is implemented.
    cy_stc_rtc_config_t current_date_time = {0};
    Cy_RTC_GetDateAndTime(&current_date_time);

    uint64_t s = timeutils_seconds_since_epoch(current_date_time.year, current_date_time.month, current_date_time.date,
        current_date_time.hour, current_date_time.min, current_date_time.sec);

    // add sub-second ticks to make sure time is strictly monotonic
    return s * 1000000000ULL + (modtime_ticks_us64() % 1000000ULL) * 1000ULL;
}

mp_uint_t mp_hal_ticks_ms(void) {
    return (mp_uint_t)(modtime_ticks_us64() / 1000ULL);
}

mp_uint_t mp_hal_ticks_us(void) {
    return (mp_uint_t)modtime_ticks_us64();
}

mp_uint_t mp_hal_ticks_cpu(void) {
    return (mp_uint_t)modtime_ticks_us64();
}

/*******************************************************************************
 * STDIO — UART-based stdin/stdout for REPL
 ******************************************************************************/
#include "py/stream.h"
#include "shared/runtime/interrupt_char.h"

uintptr_t mp_hal_stdio_poll(uintptr_t poll_flags) {
    uintptr_t ret = 0;
    if ((poll_flags & MP_STREAM_POLL_RD) &&
        (tacp_ring_buf_readable() || mtb_hal_uart_readable(&DEBUG_UART_hal_obj))) {
        ret |= MP_STREAM_POLL_RD;
    }
    if (poll_flags & MP_STREAM_POLL_WR) {
        ret |= MP_STREAM_POLL_WR;
    }
    return ret;
}

void mp_hal_stdout_tx_strn(const char *str, mp_uint_t len) {
    int r = write(STDOUT_FILENO, str, len);
    (void)r;
}

/* Deferred WiFi credential flush — called from stdin polling loop.
 * Defined in mpy_main.c, runs lfs_wifi_creds_write() if dirty flag set.
 * Throttled here to avoid calling on every 1ms poll iteration. */
extern void wifi_creds_flush_if_dirty(void);

int mp_hal_stdin_rx_chr(void) {
    uint32_t flush_counter = 0;
    for (;;) {
        /* Drain bytes queued by TACP sniffer (from sleep, IPC waits,
         * or the tacp_poll_uart() call below). */
        int buffered = tacp_ring_buf_read();
        if (buffered >= 0) {
            return buffered;
        }

        /* Route ALL UART reads through the TACP sniffer so that TACP
         * commands (0xAA 0x55 CMD) are detected even while the REPL is
         * waiting for input.  Data bytes (including Ctrl-C) are placed
         * into the ring buffer and consumed on the next iteration. */
        tacp_poll_uart();

        /* When an NLR handler exists (e.g. inside input() or running
         * script), the full event poll hook can safely raise pending
         * exceptions like KeyboardInterrupt.
         * During REPL readline, NO NLR handler is on the stack — calling
         * nlr_raise() would hit nlr_jump_fail.  In that case, clear
         * pending state without raising; the Ctrl-C byte is returned
         * from the ring buffer so readline handles it as a character. */
        if (MP_STATE_THREAD(nlr_top) != NULL) {
            MICROPY_EVENT_POLL_HOOK
        } else {
            mp_handle_pending(false);
            #ifdef CY_RTOS_AWARE
            mp_hal_rtos_yield();
            #endif
        }

        /* Deferred WiFi credential flush — throttled to ~1s intervals.
         * mp_hal_rtos_yield() sleeps 1ms, so ~1000 iterations ≈ 1 second. */
        if (++flush_counter >= 1000) {
            flush_counter = 0;
            wifi_creds_flush_if_dirty();
        }
    }
}

extern uint32_t get_drive_mode(uint8_t mode, uint8_t pull);

void mp_hal_pin_config(mp_hal_pin_obj_t pin, uint32_t mode, uint32_t pull, uint32_t value) {
    uint32_t drive_mode = get_drive_mode(mode, pull);
    Cy_GPIO_Pin_FastInit(Cy_GPIO_PortToAddr(pin->port), pin->pin, drive_mode, value, HSIOM_SEL_GPIO);
}

extern uint8_t pin_get_mode(const machine_pin_obj_t *self);

uint32_t mp_hal_pin_read(mp_hal_pin_obj_t pin) {
    uint8_t mode = pin_get_mode(pin);
    if (mode == GPIO_MODE_OUT ||
        mode == GPIO_MODE_OPEN_DRAIN) {
        return Cy_GPIO_ReadOut(Cy_GPIO_PortToAddr(pin->port), pin->pin);
    }
    return Cy_GPIO_Read(Cy_GPIO_PortToAddr(pin->port), pin->pin);
}

uint32_t mp_hal_pin_get_drive(mp_hal_pin_obj_t pin) {
    return Cy_GPIO_GetDriveSel(Cy_GPIO_PortToAddr(pin->port), pin->pin);
}

void mp_hal_pin_set_drive(mp_hal_pin_obj_t pin, uint32_t drive) {
    Cy_GPIO_SetDriveSel(Cy_GPIO_PortToAddr(pin->port), pin->pin, drive);
}

void mp_hal_pin_write(mp_hal_pin_obj_t pin, uint8_t polarity) {
    Cy_GPIO_Write(Cy_GPIO_PortToAddr(pin->port), pin->pin, polarity);
}

mp_hal_pin_af_obj_t mp_hal_pin_af_find(mp_hal_pin_obj_t pin, uint32_t af_signal) {
    for (uint8_t i = 0; i < pin->af_num; i++) {
        const machine_pin_af_obj_t *af = &pin->af[i];
        if (af->signal == af_signal) {
            return af;
        }
    }

    mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("Pin '%q' does not support '%s'."), pin->name, machine_pin_af_signal_str[af_signal]);
}

mp_hal_af_periph_t mp_hal_periph_pins_af_config(const mp_hal_pin_af_config_t *periph_pins_config, uint8_t num_pins) {
    /**
     * If there is more than one pin,
     * validate if all pins share
     * the same AF peripheral pointer and unit
     */
    if (num_pins > 1) {
        const mp_hal_pin_af_config_t *first_pin_cfg = &periph_pins_config[0];
        mp_hal_af_periph_t periph_ptr = first_pin_cfg->af->periph;
        uint32_t unit = first_pin_cfg->af->unit;
        for (uint8_t i = 1; i < num_pins; i++) {
            const mp_hal_pin_af_config_t *pin_cfg = &periph_pins_config[i];
            if (pin_cfg->af->periph != periph_ptr) {
                mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("All pins must belong to the same peripheral."));
            }
            if (pin_cfg->af->unit != unit) {
                mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("All pins must belong to the same unit."));
            }
        }
    }

    for (uint8_t i = 0; i < num_pins; i++) {
        const mp_hal_pin_af_config_t *pin_cfg = &periph_pins_config[i];
        Cy_GPIO_Pin_FastInit(Cy_GPIO_PortToAddr(pin_cfg->pin->port), pin_cfg->pin->pin, pin_cfg->cy_drive_mode, pin_cfg->init_value, pin_cfg->af->idx);
    }

    return periph_pins_config[0].af->periph;
}
