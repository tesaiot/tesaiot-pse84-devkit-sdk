/*******************************************************************************
 * Upstream provenance and licence — MicroPython
 *
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * It is a refactor of the psoc-edge port entry point, upstream file
 * `ports/psoc-edge/main.c` of the `micropython-psoc-edge` tree. MicroPython is
 * distributed under the MIT licence, which requires its copyright notice and
 * permission notice to be retained in all copies and substantial portions of
 * the software. Both are reproduced below. The Infineon and TESAIoT lines in
 * the header that follows record the port and this refactor; they stand
 * alongside the notice below and do not replace it.
 *
 * ---------------------------------------------------------------------------
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2025 Damien P. George and contributors
 * Copyright (c) 2022-2025 Infineon Technologies AG (psoc-edge port)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 * ---------------------------------------------------------------------------
 *
 * Upstream's own LICENSE file ships beside this one as LICENSE-MICROPYTHON.txt.
 ******************************************************************************/
/******************************************************************************
* File Name:   mpy_main.c
*
* Description: MicroPython task entry point for FreeRTOS on CM33_NS.
*              Called from main.c as a FreeRTOS task.
*
*              v3.0: Refactored from bare-metal main() to FreeRTOS task.
*              - BSP init, retarget-io, CM55 boot moved to main.c
*              - GC heap is a static array (not linker __HeapBase/__HeapLimit)
*              - Stack control uses FreeRTOS task stack
*
* Copyright (c) 2022-2025 Infineon Technologies AG (MicroPython port)
* Modifications (c) 2026 TESAIoT - FreeRTOS task integration
*
******************************************************************************/

// std includes
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

// MTB includes (for GPIO in check_boot_mode)
#include "cybsp.h"
#include "cy_pdl.h"

// micropython includes
#include "py/builtin.h"
#include "py/compile.h"
#include "py/gc.h"
#include "py/lexer.h"
#include "py/mperrno.h"
#include "py/nlr.h"
#include "py/parse.h"
#include "py/stackctrl.h"
#include "py/runtime.h"
#include "py/misc.h"
#include "py/obj.h"
#include "shared/runtime/gchelper.h"
#include "shared/runtime/pyexec.h"
#include "shared/readline/readline.h"

// port-specific includes
#include "mplogger.h"
#include "tacp.h"
#include "lfs_wifi_creds.h"
#include "sensor_auto_task.h"  /* wifi_creds_lock / wifi_creds_unlock */
#include "wifi_creds_types.h"


/*******************************************************************************
* MicroPython GC Heap — Static array (FreeRTOS-compatible)
*
* Under bare-metal, GC heap used linker symbols __HeapBase/__HeapLimit.
* Under FreeRTOS with heap_3 (malloc wrapper), the C heap is shared between
* FreeRTOS and other malloc users. Using a static array avoids contention
* and provides a dedicated, non-fragmented GC region.
*******************************************************************************/
#ifndef MPY_GC_HEAP_SIZE
#define MPY_GC_HEAP_SIZE  (112 * 1024)  /* 112KB default; frees 16KB RAM for new modules */
#endif

/* MPY_GC_HEAP_IN_SOCMEM — where the GC heap lives. Off by default.
 *
 *   0 (default)  m33_data, alongside .bss. Fastest, and what every build has
 *                always used.
 *   1            the shared SOCMEM window (.cy_shared_socmem, 256 KB and
 *                otherwise unused by CM33_NS).
 *
 * THIS IS A SPEED TRADE, NOT A FREE WIN. SOCMEM is further from the CM33 than
 * m33_data, so every GC allocation, every collection sweep and every Python
 * object access pays for it. Turn it on only when the project needs the RAM
 * back more than it needs the speed, and say so in the project Makefile.
 *
 * Why it exists: .heap in the CM33_NS linker script is a fill-to-end section,
 * so m33_data is fully committed and each byte of .bss costs a byte of C heap
 * one for one. The GC heap is the single largest .bss object, so moving it
 * hands ~64-112 KB straight to the C heap. On the TESAIoT Dev Kit the mTLS
 * handshake could not complete without it — measured 6,308 bytes free with the
 * heap in m33_data, against a reference design that has 180 KB.
 *
 * The size does NOT change either way. This moves the heap; it never shrinks it.
 *
 * Safe with respect to the NOLOAD section not being zero-initialised:
 * MicroPython clears its own bookkeeping in gc_setup_area() (py/gc.c:175,178)
 * and zeroes each block as it is handed out (:899,:906), so it never relies on
 * the memory arriving zeroed. */
#ifndef MPY_GC_HEAP_IN_SOCMEM
#define MPY_GC_HEAP_IN_SOCMEM  0
#endif

#if MPY_GC_HEAP_IN_SOCMEM
#define MPY_GC_HEAP_ATTR  __attribute__((section(".cy_shared_socmem"), aligned(4)))
#define MPY_GC_HEAP_WHERE "SOCMEM (slower — MPY_GC_HEAP_IN_SOCMEM=1)"
#else
#define MPY_GC_HEAP_ATTR  __attribute__((aligned(4)))
#define MPY_GC_HEAP_WHERE "SRAM"
#endif

static uint8_t mpy_gc_heap[MPY_GC_HEAP_SIZE] MPY_GC_HEAP_ATTR;

/* Must match main.c MPY_TASK_STACK_SIZE */
#define MPY_TASK_STACK_SIZE  (8 * 1024)

/*******************************************************************************
* Boot Mode
*******************************************************************************/
typedef enum {
    BOOT_MODE_NORMAL,
    BOOT_MODE_SAFE,
    BOOT_MODE_SAFE_DEPLOY   /* Software-requested safe boot (TACP PROGRAM_MODE) */
} boot_mode_t;

extern void time_init(void);
extern void machine_pin_irq_deinit_all(void);

/*******************************************************************************
* Deferred WiFi credential flush — MicroPython task context ONLY.
*
* WiFi IPC worker (separate FreeRTOS task) stages credentials in
* g_boot_wifi_creds[] and sets g_boot_wifi_creds_dirty = true after a
* successful IPC_CMD_WIFI_CONNECT.  We flush to QSPI LittleFS here
* because lfs_wifi_creds_write() uses exec_python_str() which requires
* the MicroPython runtime (GC heap, globals dict, lexer/parser/compiler).
*******************************************************************************/
extern qspi_wifi_entry_t g_boot_wifi_creds[];
extern volatile int      g_boot_wifi_creds_count;
extern volatile bool     g_boot_wifi_creds_dirty;

//! [mpy_lfs_wifi_creds_flush_if_dirty]
void wifi_creds_flush_if_dirty(void) {
    if (!g_boot_wifi_creds_dirty) return;
    if (!lfs_wifi_creds_ready())  return;

    /* Snapshot count + entries under the lock so a concurrent BLE
     * worker / WiFi-IPC writer can't mutate the array while we hand
     * it to lfs_wifi_creds_write. The flush itself runs under the
     * lock — torn-write protection extends across the LFS write. */
    wifi_creds_lock();
    int count = g_boot_wifi_creds_count;
    if (count <= 0 || count > QSPI_WIFI_CREDS_MAX) {
        wifi_creds_unlock();
        return;
    }

    if (lfs_wifi_creds_write(g_boot_wifi_creds, count)) {
        g_boot_wifi_creds_dirty = false;
    } else {
        /* Silently retry on next REPL iteration */
    }
    wifi_creds_unlock();
}
//! [mpy_lfs_wifi_creds_flush_if_dirty]

/* One-shot safe boot request marker.
 * Survives a MicroPython soft reset (goto soft_reset — SRAM untouched), which
 * is how TACP_CMD_PROGRAM_MODE arms it.  It does NOT survive
 * NVIC_SystemReset(): the bootloader wipes .noinit, so a hard reset always
 * comes up in NORMAL boot and runs /main.py.  That asymmetry is deliberate and
 * load-bearing — it is exactly how the IDE gets the board back out of
 * safe-boot deploy mode after programming (TACP_CMD_HARD_RESET, 0x06).
 * Do not "fix" this comment to say the flag is retained across a hard reset;
 * an earlier version claimed that and it caused a real design error. */
#define MPY_SAFE_BOOT_MAGIC (0x53424654u) /* 'SBFT' */
__attribute__((section(".noinit"))) static uint32_t s_safe_boot_once_flag;

/* One-shot delete /main.py request.
 * NOT .noinit: soft reset (goto soft_reset) keeps SRAM intact.
 * NVIC_SystemReset() wipes .noinit via bootloader, so we use soft reset instead. */
#define MPY_DELETE_MAIN_MAGIC (0x444D5059u) /* 'DMPY' */
static volatile uint32_t s_delete_main_flag = 0;

/* Global flag: true when current boot is safe-boot (no main.py).
 * Re-evaluated on every soft-reboot via check_boot_mode(). */
bool g_is_safe_boot = false;

void mpy_request_safe_boot_once(void)
{
    s_safe_boot_once_flag = MPY_SAFE_BOOT_MAGIC;
}

void mpy_request_delete_main_py(void)
{
    s_delete_main_flag = MPY_DELETE_MAIN_MAGIC;
}

/*******************************************************************************
* Execute a Python source string (replaces frozen module dependency)
*******************************************************************************/
static void exec_python_str(const char *src) {
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_str_len(
            MP_QSTR__lt_stdin_gt_, src, strlen(src), 0);
        mp_parse_tree_t pt = mp_parse(lex, MP_PARSE_FILE_INPUT);
        mp_obj_t module_fun = mp_compile(&pt, MP_QSTR__lt_stdin_gt_, false);
        mp_call_function_0(module_fun);
        nlr_pop();
    } else {
        mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);
    }
}

#if defined(BENTO_HAS_BLE_NUS) && (BENTO_HAS_BLE_NUS == 1)
int exec_python_str_public(const char *src) {
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_str_len(
            MP_QSTR__lt_stdin_gt_, src, strlen(src), 0);
        mp_parse_tree_t pt = mp_parse(lex, MP_PARSE_FILE_INPUT);
        mp_obj_t module_fun = mp_compile(&pt, MP_QSTR__lt_stdin_gt_, false);
        mp_call_function_0(module_fun);
        nlr_pop();
        return 0;
    } else {
        mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);
        return -1;
    }
}

/*******************************************************************************
 * exec_python_capture — execute a Python source string and capture stdout.
 *
 * Strategy
 * --------
 * MicroPython's `mp_hal_stdout_tx_strn` is a weak symbol used process-wide for
 * all print output; overriding it for a single exec is not reentrant-safe. The
 * approach taken here instead wraps the user source in a short Python shim:
 *
 *   import sys as __bsys, io as __bio
 *   __bcap = __bio.StringIO()
 *   __bold = __bsys.stdout
 *   __bsys.stdout = __bcap
 *   try:
 *       <user source>
 *   except BaseException as __be:
 *       __bsys.stdout = __bold
 *       raise
 *   __bsys.stdout = __bold
 *   __bento_capture_result = __bcap.getvalue()
 *
 * After execution we retrieve `__bento_capture_result` from the module
 * globals dict and copy into the caller-provided buffer. On exception,
 * `mp_obj_print_exception` writes the traceback via the (restored) real
 * stdout — we surface only the exception's `str()` into the caller buffer
 * so the wire protocol can return a single-line error.
 *
 * Thread safety
 * -------------
 * MicroPython runtime is single-threaded. This helper is invoked only from
 * the NUS RX callback, which runs on the BLE task context. A FreeRTOS mutex
 * (`s_capture_mutex`) serialises concurrent callers — desktop pipelines
 * occasionally send two BLE frames close together and the second could
 * otherwise corrupt globals mid-execution.
 *
 * Tradeoffs
 * ---------
 *  - Roughly 400 B of GC heap allocated per call (StringIO buffer + temp
 *    vars). Caller should not exceed out_buf_sz of 2 KB for v1 exec verbs.
 *  - If the user source itself writes to sys.stderr, that output is NOT
 *    captured (only sys.stdout is redirected). This matches the SPEC §5.3.9
 *    contract which only mandates stdout capture for bento.exec.
 *  - If GC heap is exhausted mid-shim, the exception path still restores
 *    sys.stdout via the outer except clause; no persistent redirection leak.
 ******************************************************************************/
#include "FreeRTOS.h"
#include "semphr.h"

static SemaphoreHandle_t s_capture_mutex = NULL;

static void ensure_capture_mutex(void) {
    if (s_capture_mutex == NULL) {
        s_capture_mutex = xSemaphoreCreateMutex();
    }
}

/* Wrap the user source inside the capture shim.
 * We copy the user source into the wrapper with each line indented 4 spaces
 * so it becomes the body of a try block. Indentation is done by the caller
 * via a small snprintf loop. */
static int build_capture_wrapper(const char *user_src,
                                 char *out_wrap, size_t out_wrap_sz)
{
    /* Header + footer lengths are bounded; estimate conservatively.
     *
     * The wrapper snapshots __bento_capture_result inside the finally block
     * so partial stdout is still visible when the user code raises. The
     * exception is re-raised after the snapshot so the C caller's NLR
     * handler still sees it — and can surface the message while the
     * snapshotted partial stdout sits in the module globals. */
    /* This MPy port builds with MICROPY_PY_SYS_STDFILES=0 (sys.stdout
     * isn't exposed). The previous wrapper imported sys + redirected
     * sys.stdout into a StringIO; that fails with
     * `AttributeError: 'module' object has no attribute 'stdout'` on
     * every call. Instead we ask the user code to write its result
     * into the global `__bento_capture_result` variable directly.
     *
     * For built-in verbs (`bento.sensor.*`, `bento.gpio.*`, …) we
     * control the expression and emit `__bento_capture_result = str(…)`.
     * For `bento.exec` (user-supplied), `print(…)` calls still go to
     * the UART REPL — surface the captured exception, not stdout.
     * The first line installs a shim `print` that *also* mirrors into
     * the result buffer, so simple user scripts that say `print(x)`
     * still get their output captured. */
    /* The MPy port builds with MICROPY_PY_SYS_STDFILES=0 (no
     * `sys.stdout`) and `builtins` isn't importable as a module here
     * either. Shadow `print` in module globals instead — Python's name
     * resolution order (locals → enclosing → globals → builtins) makes
     * our local-globals binding take precedence over the built-in
     * `print`, so user-code that says `print(X)` hits our shim and
     * appends the formatted output to `__bento_capture_result`. */
    static const char header[] =
        "__bento_capture_result = ''\n"
        "def print(*args, sep=' ', end='\\n', **kw):\n"
        "    global __bento_capture_result\n"
        "    __bento_capture_result += sep.join(str(a) for a in args) + end\n"
        "try:\n";
    static const char footer[] =
        "\nfinally:\n"
        "    pass\n";

    size_t hlen = sizeof(header) - 1;
    size_t flen = sizeof(footer) - 1;
    size_t ulen = strlen(user_src);
    /* Each newline in user_src becomes \n + 4 space indent — worst case
     * the body doubles in size. Plus one leading 4-space indent. */
    size_t need = hlen + 4 + ulen * 5 + flen + 1;
    if (need >= out_wrap_sz) return -1;

    size_t p = 0;
    memcpy(out_wrap + p, header, hlen); p += hlen;
    /* Leading indent for first user line. */
    out_wrap[p++] = ' '; out_wrap[p++] = ' ';
    out_wrap[p++] = ' '; out_wrap[p++] = ' ';
    for (size_t i = 0; i < ulen; i++) {
        char c = user_src[i];
        out_wrap[p++] = c;
        if (c == '\n' && i + 1 < ulen) {
            /* Indent next line. */
            out_wrap[p++] = ' '; out_wrap[p++] = ' ';
            out_wrap[p++] = ' '; out_wrap[p++] = ' ';
        }
        if (p + flen + 1 >= out_wrap_sz) return -1;
    }
    memcpy(out_wrap + p, footer, flen); p += flen;
    out_wrap[p] = '\0';
    return 0;
}

int exec_python_capture(const char *src,
                        char *out_buf, size_t out_buf_sz,
                        size_t *out_written)
{
    if (out_written) *out_written = 0;
    if (out_buf && out_buf_sz) out_buf[0] = '\0';
    if (src == NULL) return -1;

    ensure_capture_mutex();
    if (s_capture_mutex != NULL) {
        /* 500 ms is generous — MPy calls for sensor reads finish in <50 ms. */
        if (xSemaphoreTake(s_capture_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
            if (out_buf && out_buf_sz) {
                snprintf(out_buf, out_buf_sz, "capture_busy");
                if (out_written) *out_written = strlen(out_buf);
            }
            return -1;
        }
    }

    /* 2 KB wrapper is sufficient for the v1 bridge verbs (the longest is
     * `print(sensors.bmi270.acceleration())` at ~40 B user code). bento.exec
     * with dev-mode passes longer code; caller must chunk if > 1.5 KB. */
    static char s_wrap_buf[2048];
    int rc = -1;
    if (build_capture_wrapper(src, s_wrap_buf, sizeof(s_wrap_buf)) != 0) {
        if (out_buf && out_buf_sz) {
            snprintf(out_buf, out_buf_sz, "source_too_large");
            if (out_written) *out_written = strlen(out_buf);
        }
        goto unlock;
    }

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_str_len(
            MP_QSTR__lt_stdin_gt_, s_wrap_buf, strlen(s_wrap_buf), 0);
        mp_parse_tree_t pt = mp_parse(lex, MP_PARSE_FILE_INPUT);
        mp_obj_t module_fun = mp_compile(&pt, MP_QSTR__lt_stdin_gt_, false);
        mp_call_function_0(module_fun);
        nlr_pop();

        /* Retrieve __bento_capture_result from globals. Use a dynamic
         * QSTR so we don't need a qstrdefs.h entry for this name. */
        qstr q_result = qstr_from_str("__bento_capture_result");
        mp_obj_dict_t *g = mp_globals_get();
        mp_map_elem_t *e = mp_map_lookup(
            &g->map,
            MP_OBJ_NEW_QSTR(q_result),
            MP_MAP_LOOKUP);
        if (e != NULL && e->value != MP_OBJ_NULL) {
            size_t slen = 0;
            const char *s = mp_obj_str_get_data(e->value, &slen);
            if (out_buf && out_buf_sz) {
                size_t copy = slen < out_buf_sz - 1 ? slen : out_buf_sz - 1;
                memcpy(out_buf, s, copy);
                out_buf[copy] = '\0';
                if (out_written) *out_written = copy;
            }
        }
        rc = 0;
    } else {
        /* Exception — format its str() into the out buffer so the wire
         * protocol returns a single-line error. The full traceback still
         * prints to the real stdout (UART) for REPL debugging. */
        mp_obj_t exc = (mp_obj_t)nlr.ret_val;
        if (out_buf && out_buf_sz) {
            vstr_t vstr;
            vstr_init(&vstr, 64);
            mp_print_t pr = { .data = &vstr, .print_strn = (void *)vstr_add_strn };
            mp_obj_print_helper(&pr, exc, PRINT_EXC);
            size_t slen = vstr.len < out_buf_sz - 1 ? vstr.len : out_buf_sz - 1;
            memcpy(out_buf, vstr.buf, slen);
            out_buf[slen] = '\0';
            if (out_written) *out_written = slen;
            vstr_clear(&vstr);
        }
        /* Also print to UART for developer visibility. */
        mp_obj_print_exception(&mp_plat_print, exc);
        rc = -1;
    }

unlock:
    if (s_capture_mutex != NULL) {
        xSemaphoreGive(s_capture_mutex);
    }
    return rc;
}
#endif

/*******************************************************************************
* VFS LFS2 mount script (equivalent to freeze/vfs_lfs2.py)
* Mounts external QSPI flash as LittleFS2 filesystem at "/"
*******************************************************************************/
static const char vfs_mount_script[] =
    "import os, psoc_edge\n"
    "bdev = psoc_edge.QSPI_Flash()\n"
    "try:\n"
    "    vfs = os.VfsLfs2(bdev, progsize=0x200, readsize=0x200)\n"
    "    os.mount(vfs, '/')\n"
    "except:\n"
    "    os.VfsLfs2.mkfs(bdev, progsize=0x200, readsize=0x200)\n"
    "    vfs = os.VfsLfs2(bdev, progsize=0x200, readsize=0x200)\n"
    "    os.mount(vfs, '/')\n"
;

/* Deploy screen: shown when TACP PROGRAM_MODE triggers safe boot.
 * Native LVGL overlay on CM55 (via IPC_CMD_UI_DEPLOY_SCREEN).
 * Cleared on next soft reset by ui_auto_clear_on_reset(). */
extern void ui_show_deploy_screen(void);

/*******************************************************************************
* Function Name: check_boot_mode
* Check if user button is pressed at boot for safe mode
*******************************************************************************/
boot_mode_t check_boot_mode(void) {
    boot_mode_t boot_mode;

    if (s_safe_boot_once_flag == MPY_SAFE_BOOT_MAGIC) {
        s_safe_boot_once_flag = 0;
        g_is_safe_boot = true;
        mp_printf(&mp_plat_print, "- DEVICE IS IN SAFE BOOT MODE (software request) -\n");
        return BOOT_MODE_SAFE_DEPLOY;
    }
    g_is_safe_boot = false;

    // Initialize user LED
    Cy_GPIO_Pin_FastInit(CYBSP_USER_LED_PORT, CYBSP_USER_LED_NUM, CY_GPIO_DM_STRONG, 0, HSIOM_SEL_GPIO);

    // Initialize user button
    Cy_GPIO_Pin_FastInit(CYBSP_USER_BTN1_PORT, CYBSP_USER_BTN1_NUM, CY_GPIO_DM_PULLUP, 1, HSIOM_SEL_GPIO);

    // Allow bypass capacitor connected to the user button to charge
    Cy_SysLib_Delay(5);

    if (Cy_GPIO_Read(CYBSP_USER_BTN1_PORT, CYBSP_USER_BTN1_NUM) == CYBSP_BTN_PRESSED) {
        // Blink LED twice to indicate safe boot mode
        for (int i = 0; i < 4; i++) {
            Cy_GPIO_Inv(CYBSP_USER_LED_PORT, CYBSP_USER_LED_NUM);
            Cy_SysLib_Delay(500);
        }
        boot_mode = BOOT_MODE_SAFE;
        mp_printf(&mp_plat_print, "- DEVICE IS IN SAFE BOOT MODE -\n");
    } else {
        boot_mode = BOOT_MODE_NORMAL;
    }

    // Turn off LED after boot mode check
    Cy_GPIO_Clr(CYBSP_USER_LED_PORT, CYBSP_USER_LED_NUM);

    return boot_mode;
}

/*******************************************************************************
* Function Name: mpy_task_entry
* FreeRTOS task: MicroPython runtime + REPL
*******************************************************************************/
void mpy_task_entry(void *arg) {
    (void)arg;

    /* Initialize MicroPython GC with static heap */
    #if MICROPY_ENABLE_GC
    /* Say where the heap is, every boot. A build with the heap in SOCMEM runs
     * measurably slower, and someone who inherited that flag without knowing
     * would otherwise have no way to tell it from a performance regression. */
    printf("[MPY] GC heap %u KB @ %p in %s\r\n",
           (unsigned)(sizeof(mpy_gc_heap) / 1024U),
           (void *)mpy_gc_heap, MPY_GC_HEAP_WHERE);
    gc_init(mpy_gc_heap, mpy_gc_heap + sizeof(mpy_gc_heap));
    #endif

    /* Stack control: use FreeRTOS task stack bounds.
     * Stack grows downward on ARM. Current SP is near top of task stack.
     * Leave 512 bytes margin for FreeRTOS overhead. */
    volatile uint8_t stack_marker;
    mp_cstack_init_with_top((void *)&stack_marker, MPY_TASK_STACK_SIZE - 512);

    /* Initialize time subsystem */
    time_init();

//! [mpy_tacp_init_soft_reset]
/* ...context: inside the MicroPython task, at the soft_reset label ... */
soft_reset:
    /* Clear TACP ring buffer, state machine, and UART FIFO on every
     * soft reset so stale bytes don't cause double-resets. */
    tacp_init();

    /* Peek at safe-boot flag early so g_is_safe_boot is correct before
     * check_boot_mode() runs later. Defensive: if any code path calls
     * tacp_poll_uart() during init, the TACP handler sees the right value.
     * The flag is NOT consumed here — check_boot_mode() handles that. */
    g_is_safe_boot = (s_safe_boot_once_flag == MPY_SAFE_BOOT_MAGIC);
    //! [mpy_tacp_init_soft_reset]

    /* Ensure friendly REPL mode even if a previous raw REPL paste
     * operation left pyexec_mode_kind in RAW_REPL state. */
    pyexec_mode_kind = PYEXEC_MODE_FRIENDLY_REPL;

    mp_init();

    readline_init0();

    #if MICROPY_VFS
    mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(MP_QSTR__slash_));
    mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(MP_QSTR__slash_lib));

    #if MICROPY_VFS_LFS2 && MICROPY_ENABLE_EXT_QSPI_FLASH
    /* Mount QSPI flash filesystem (replaces frozen vfs_lfs2.py) */
    exec_python_str(vfs_mount_script);
    /* Reclaim VFS mount temporaries before config I/O */
    gc_collect();

    /* Init TESAIoT config store (load from LittleFS or create defaults) */
    {
        extern bool tesaiot_config_init(void);
        tesaiot_config_init();
        extern void ipc_tesaiot_refresh_status(void);
        ipc_tesaiot_refresh_status();
    }

//! [ble_ipc_bento_buddy_rx_init_boot]
/* ...context: inside the MicroPython task boot sequence ... */
#if defined(BENTO_HAS_BLE_NUS) && (BENTO_HAS_BLE_NUS == 1)
    /* Bento Desktop Buddy variant: BLE stack init is DEFERRED.
     * Boot path stays byte-identical to flag=0 so WiFi + sensors + REPL
     * come up unconditionally. The stack is brought up on-demand via
     * bento_buddy_request_start() — invoked from the CM55 UI tap, from
     * MicroPython bento_buddy.start(), or from the debug REPL. See
     * ble_nus_lazy.c.
     *
     * HOWEVER: the CM33_NS-side IPC receiver that actually translates
     * the CM55 Start/Stop button taps into bento_buddy_request_start()
     * must be armed BEFORE the first tap, otherwise the tap falls on
     * deaf ears (chicken-and-egg). Register it here at boot so the LCD
     * button works from the moment the Bento Buddy page is visible. */
    {
        extern int ipc_bento_buddy_rx_init(void);
        int rc = ipc_bento_buddy_rx_init();
        printf("[MPY] bento_buddy IPC RX init: %s\r\n",
               rc == 0 ? "OK" : "FAIL");
    }
#endif
//! [ble_ipc_bento_buddy_rx_init_boot]

    //! [mpy_lfs_wifi_creds_boot_read]
    /* ...context: inside the MicroPython task boot sequence, after VFS mount ... */
    /* Reclaim config init temporaries before WiFi credential read.
     * Both config and WiFi cred I/O use Python open() which allocates
     * ~16KB from GC heap. gc_collect() between ensures heap is clean. */
    gc_collect();
    lfs_wifi_creds_init();
    if (lfs_wifi_creds_ready()) {
        extern qspi_wifi_entry_t g_boot_wifi_creds[];
        extern volatile int g_boot_wifi_creds_count;
        /* Boot-time read runs before WiFi/BLE workers exist, so the
         * mutex is single-claimer here — but take it anyway so the
         * pattern is uniform across all writers and any future code
         * that grows a concurrent boot path doesn't have to remember
         * a special case. wifi_creds_lock is a no-op pre-scheduler. */
        wifi_creds_lock();
        int n = lfs_wifi_creds_read(g_boot_wifi_creds, QSPI_WIFI_CREDS_MAX);
        g_boot_wifi_creds_count = n;
        /* printf("[MPY] Loaded %d WiFi cred(s) from QSPI\r\n", n); */
        /* CRC32 migration: re-save credentials in new checksum format if needed */
        if (lfs_wifi_creds_needs_resave() && n > 0) {
            lfs_wifi_creds_write(g_boot_wifi_creds, n);
            /* printf("[MPY] WiFi credentials migrated to CRC32 checksum\r\n"); */
        }
        wifi_creds_unlock();
    } else {
        printf("[MPY] WiFi creds store not ready\r\n");
    }
    //! [mpy_lfs_wifi_creds_boot_read]
    #endif
    #endif

    /* One-shot delete /main.py if requested by CM55 Delete button */
    if (s_delete_main_flag == MPY_DELETE_MAIN_MAGIC) {
        s_delete_main_flag = 0;
        exec_python_str(
            "import os\n"
            "try:\n"
            "    os.remove('/main.py')\n"
            "    print('main.py deleted by user request')\n"
            "except: pass\n"
        );
    }

    boot_mode_t boot_mode = check_boot_mode();

    if (boot_mode == BOOT_MODE_NORMAL) {
        /* Clear deploy screen overlay + leftover widgets from previous session.
         * Called HERE (not earlier) because IPC pipe is reliably ready at this
         * point — same stage where ui_show_deploy_screen() works in SAFE_DEPLOY. */
        {
            extern void ui_auto_clear_on_reset(void);
            ui_auto_clear_on_reset();
        }

        // Execute user scripts
        int ret = pyexec_file_if_exists("/boot.py");

        if (ret & PYEXEC_FORCED_EXIT) {
            goto soft_reset;
        }

        if (pyexec_mode_kind == PYEXEC_MODE_FRIENDLY_REPL) {
            ret = pyexec_file_if_exists("/main.py");

            if (ret & PYEXEC_FORCED_EXIT) {
                goto soft_reset;
            }
        }
    } else if (boot_mode == BOOT_MODE_SAFE_DEPLOY) {
        /* TACP PROGRAM_MODE: show native deploy overlay on LCD
         * while IDE uploads new code. Cleared on next soft reset. */
        ui_show_deploy_screen();
    }

    /* BentoClaw: send READY frame to IDE so it knows FW is up.
     * Must be AFTER tacp_init(), mp_init(), VFS mount, and boot/main.py —
     * the board is truly ready for commands at this point. */
    {
        extern void bentoclaw_send_ready(void);
        bentoclaw_send_ready();
    }

    /* Enter REPL loop */
    for (;;) {
        /* Flush any WiFi credentials staged by WiFi IPC worker.
         * Safe here: MicroPython context, VFS mounted, between REPL commands. */
        wifi_creds_flush_if_dirty();

        if (pyexec_mode_kind == PYEXEC_MODE_RAW_REPL) {
            if (pyexec_raw_repl() != 0) {
                break;
            }
        } else {
            if (pyexec_friendly_repl() != 0) {
                break;
            }
        }
    }

    mp_printf(&mp_plat_print, "MPY: soft reboot\n");

    //! [mpy_lfs_wifi_creds_deinit_order]
    /* ...context: MicroPython task teardown, before gc_sweep_all() ... */
    /* Flush pending WiFi credentials to QSPI before VFS is torn down.
     * Must happen BEFORE gc_sweep_all() which destroys VFS Python objects. */
    wifi_creds_flush_if_dirty();
    lfs_wifi_creds_deinit();
    //! [mpy_lfs_wifi_creds_deinit_order]

    machine_pin_irq_deinit_all();

    #if MICROPY_ENABLE_GC
    gc_sweep_all();
    #endif
    mp_deinit();

    goto soft_reset;
}

/*******************************************************************************
* GC Collection
*******************************************************************************/
#if MICROPY_ENABLE_GC
void gc_collect(void) {
    gc_collect_start();
    gc_helper_collect_regs_and_stack();
    gc_collect_end();
}
#endif

/*******************************************************************************
* Error Handlers
*******************************************************************************/
void nlr_jump_fail(void *val) {
    (void)val;
    printf("FATAL: nlr_jump_fail\r\n");
    for (;;) {
    }
}
