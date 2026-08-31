/*******************************************************************************
 * File Name        : bento_cm55_stdio.c
 *
 * Description      : CM55 stdio policy — a WEAK, overridable no-op printf/puts.
 *
 * This file exists so that the shipped libbento_edge_ai.a does NOT have to.
 * Until 2026-08-30 the mute lived inside the archive as a STRONG definition of
 * printf and puts, which meant a customer linking Edge AI either lost every
 * printf on CM55 without being told, or — if they defined their own printf —
 * could not link at all. Both were measured; see the header of
 * secure_lib/api.edge_ai.extra for the numbers.
 *
 * WHY A MUTE IS NEEDED AT ALL
 * ---------------------------
 * CM55 has no console. proj_cm55/Makefile does `CY_IGNORE+=driver`, so
 * cy_retarget_io_init() is never called on this core, yet retarget-io's _write
 * is still pulled into the image by newlib's stdio. That _write takes
 * cy_retarget_io_mutex and calls abort() if it cannot get it — and the mutex is
 * never initialised. Measured in template/build/project_hex/proj_cm55.elf:
 *
 *     _write                 @0x6082ee94  -> cy_rtos_mutex_get -> abort()
 *     cy_retarget_io_mutex   @0x20022274  (.bss, never initialised)
 *     cy_retarget_io_init                 (absent — nothing calls it)
 *
 * The vendored ml-middleware, TFLite-Micro and the Ethos-U driver all call
 * printf on paths this project does not control. Left to reach newlib, the
 * first vendor diagnostic aborts the core. The mute is a defence, not a debug
 * convenience.
 *
 * WHAT YOU CAN DO WITH IT
 * -----------------------
 * Both definitions are WEAK, so you have three choices and need edit nothing
 * here for the first two:
 *
 *   1. Define your own printf()/puts() anywhere in your application. Yours is
 *      strong, so it wins the link; this file is ignored.
 *   2. Route CM55 diagnostics over IPC to the CM33_NS console, which owns the
 *      UART. That is what the BENTO firmware itself does.
 *   3. Take the real newlib printf: build with
 *          DEFINES+=BENTO_CM55_MUTE_STDIO=0
 *      and then you MUST also call cy_retarget_io_init() on CM55 and give this
 *      core a UART of its own, or the abort path above is what you will get.
 *
 * Deleting this file is equivalent to option 3 without the warning.
 *******************************************************************************/

#ifndef BENTO_CM55_MUTE_STDIO
#define BENTO_CM55_MUTE_STDIO 1
#endif

#if BENTO_CM55_MUTE_STDIO

/* Declared here rather than via <stdio.h>: the point is to define the symbol,
 * and stdio.h drags in the reentrancy machinery this file exists to avoid. */
__attribute__((weak)) int printf(const char *fmt, ...);
__attribute__((weak)) int printf(const char *fmt, ...)
{
    (void)fmt;
    return 0;
}

/* puts() is not optional cover for printf(): GCC rewrites printf("literal\n")
 * into puts("literal"), so a build that muted only printf would still reach
 * newlib — and _write — through every constant-string diagnostic in the
 * vendor middleware. That rewrite is also what made the old strong-symbol
 * arrangement fail to link: the reference that dragged the Edge AI archive's
 * printf into a customer's image was usually `puts`, not `printf`. */
__attribute__((weak)) int puts(const char *s);
__attribute__((weak)) int puts(const char *s)
{
    (void)s;
    return 0;
}

#endif /* BENTO_CM55_MUTE_STDIO */
