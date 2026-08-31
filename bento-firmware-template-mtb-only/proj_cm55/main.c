/******************************************************************************
* File Name:   main.c
*
* Description: CM55 application with FreeRTOS + LVGL.
*              LVGL display on Waveshare 4.3" DSI LCD (800x480)
*              Uses TESAIoT display library for GFXSS/VGLite/LVGL init.
*              GFX task handles all LVGL + IPC initialization.
*
*              NOTE: CM55 must NOT use printf/retarget-io.
*              CM33_NS owns the UART for MicroPython REPL.
*
******************************************************************************/

#include "cybsp.h"
#include "FreeRTOS.h"
#include "task.h"
#include "tesaiot_display.h"
#include "bsp_feature_flags.h"
#if ENABLE_USB_HOST
#include "usb_hid_joystick.h"
#include "usb_ccid_smartcard.h"
#endif
#if TESAIOT_ENABLE_FACE_RUNTIME
#include "face_mode_runtime.h"
#endif
#if BSP_HAS_RADAR
#include "radar_task.h"
#endif

/*******************************************************************************
* Macros
*******************************************************************************/
#define APP_TASK_STACK_SIZE     (configMINIMAL_STACK_SIZE * 8)
#define APP_TASK_PRIORITY       (configMAX_PRIORITIES - 2)
/* Temporary diagnosis toggles for hang isolation. */
#define TESAIOT_DIAG_DISABLE_RADAR_TASK   0
#define TESAIOT_DIAG_DISABLE_APP_TASK     0
/* Safety guard: keep default boot on SensorHub until native Face runtime
 * boot-path is fully stabilized in this merged firmware image. */
#define TESAIOT_ENABLE_FACE_RUNTIME_BOOT  0

/*******************************************************************************
* Function Prototypes
*******************************************************************************/
#if !TESAIOT_DIAG_DISABLE_APP_TASK
static void app_task(void *arg);
#endif

/*******************************************************************************
* LVGL Tick Hook — called every 1ms from FreeRTOS tick ISR
* Required for LVGL timing (animations, input debounce, etc.)
*******************************************************************************/
void vApplicationTickHook(void)
{
#if TESAIOT_ENABLE_FACE_RUNTIME
    if (!face_mode_runtime_active()) {
        tesaiot_display_tick();
    }
#else
    tesaiot_display_tick();
#endif
}

/*******************************************************************************
 * Radar Presence Detection Init (BGT60TR13C, SPI, 200 Hz)
 *
 * Creates the radar FreeRTOS task that continuously reads BGT60TR13C
 * frame data over SPI, runs presence/absence detection, and pushes
 * results to the LVGL radar page via shared state.
 * AI Kit only (BSP_HAS_RADAR=1). Eva Kit has no radar hardware.
 ******************************************************************************/
#if BSP_HAS_RADAR && !TESAIOT_DIAG_DISABLE_RADAR_TASK
static BaseType_t init_radar_presence_detection(void)
{
    return xTaskCreate(tesaiot_radar_task, RADAR_TASK_NAME,
                       RADAR_TASK_STACK_SIZE, NULL,
                       RADAR_TASK_PRIORITY, NULL);
}
#endif

/*******************************************************************************
 * Joystick USB Host Init (SEGGER emUSB-Host + HID driver)
 *
 * Spawns a one-shot task that calls USBH_Init(), registers HID
 * callbacks, power-cycles the USB root port (forces re-enumeration
 * of devices already plugged in), and creates USBH_Main/ISR tasks.
 * Must be called post-scheduler (emUSB-Host uses OS primitives).
 ******************************************************************************/
#if ENABLE_USB_HOST
/* USB CCID (smart card) tasks share priorities with USBH_Main/USBH_ISR and
 * starve the HID interrupt-IN pipe — joystick enumerates but reports never
 * flow (same incident class as the AI Kit Tiny variant). Keep CCID compiled
 * (ipc_service links its symbols) but do not start it unless opted in. */
#ifndef ENABLE_USB_CCID
#define ENABLE_USB_CCID 0
#endif

/* Boot-race diagnostics surfaced on the Joystick page: app_task heartbeat
 * proves the task is scheduled; retry-fail count proves whether the USBH
 * init task creation is failing (FreeRTOS heap exhaustion). */
volatile uint32_t app_task_beat = 0;
volatile uint16_t usb_init_retry_fails = 0;

static void init_joystick_usb_host(void)
{
    /* Run the real USB-host bring-up DIRECTLY in app_task context (prio
     * MAX-2). The library's dedicated USBH_Init task sits at the lowest
     * priority (MAX-5) and on random boots never gets scheduled at all —
     * init_stage stayed 0 with zero xTaskCreate failures (starvation, not
     * heap). Calling the init function here guarantees it executes. */
    usb_hid_joystick_init();

    /* Then spawn the library's init task anyway: it sees s_initialized,
     * skips the bring-up, and serves as the persistent re-enumeration
     * watchdog (unplug/replug recovery). If it starves we only lose
     * hot-replug detection, not boot-time detection. */
    while (!usb_hid_joystick_request_init()) {
        usb_init_retry_fails++;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
#if ENABLE_USB_CCID
    usb_ccid_smartcard_request_init();
#endif
}
#endif

#if !TESAIOT_DIAG_DISABLE_APP_TASK
/*******************************************************************************
* Function Name: app_task
* Summary: Waits for display + IPC init, inits USB joystick, then idles.
*******************************************************************************/
//! [cm55_tesaiot_display_ready_wait]
static void app_task(void *arg)
{
    CY_UNUSED_PARAMETER(arg);

    /* Wait for display + IPC initialization to complete
     * tesaiot_display_ready: 0=pending, 1=display+IPC, 2=IPC-only (headless) */
    extern volatile uint8_t tesaiot_display_ready;
    uint32_t wait_count = 0;
    while (tesaiot_display_ready == 0)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
        wait_count++;
        if (wait_count > 150) {
            break;
        }
    }
    //! [cm55_tesaiot_display_ready_wait]

    /* Joystick — early init so F310 plugged at boot is detected */
#if ENABLE_USB_HOST
    init_joystick_usb_host();
#endif

    /* Idle — LED1 left under MicroPython gpio.led(0) control.
     * Previous heartbeat blink removed so users can control LED1
     * from Python without conflict. */
    for (;;)
    {
        app_task_beat++;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
#endif

/*******************************************************************************
* Function Name: main
*******************************************************************************/
//! [cm55_tesaiot_display_init_boot]
int main(void)
{
    cy_rslt_t result;

#if TESAIOT_ENABLE_FACE_RUNTIME && TESAIOT_ENABLE_FACE_RUNTIME_BOOT
    /* Alternate boot mode: launch native Face-ID runtime. */
    if (face_mode_runtime_requested()) {
        face_mode_launch_runtime();
    }
#endif

    /* Initialize the device and board peripherals */
    result = cybsp_init();
    if (CY_RSLT_SUCCESS != result)
    {
        for (;;) {
            Cy_GPIO_Inv(CYBSP_USER_LED1_PORT, CYBSP_USER_LED1_PIN);
            Cy_SysLib_Delay(50);
        }
    }

    /* Enable global interrupts */
    __enable_irq();

    /* GFX task: GFXSS/LVGL init + IPC + sensorhub UI */
    BaseType_t xResult = tesaiot_display_init();
    if (pdPASS != xResult) {
        for (;;) {
            Cy_GPIO_Inv(CYBSP_USER_LED2_PORT, CYBSP_USER_LED2_PIN);
            Cy_SysLib_Delay(50);
        }
    }
    //! [cm55_tesaiot_display_init_boot]

    /* USB Host joystick init deferred to app_task (post-scheduler) */

#if BSP_HAS_RADAR && !TESAIOT_DIAG_DISABLE_RADAR_TASK
    /* Radar presence detection (BGT60TR13C, SPI, 200 Hz) — AI Kit only */
    xResult = init_radar_presence_detection();
    if (pdPASS != xResult) {
        for (;;) {
            Cy_GPIO_Inv(CYBSP_USER_LED2_PORT, CYBSP_USER_LED2_PIN);
            Cy_SysLib_Delay(100);
        }
    }
#endif

#if !TESAIOT_DIAG_DISABLE_APP_TASK
    /* Optional app task (heartbeat only). Disabled in recovery mode to
     * reduce heap pressure during early boot. */
    xResult = xTaskCreate(app_task, "CM55_App",
                          APP_TASK_STACK_SIZE, NULL,
                          APP_TASK_PRIORITY, NULL);
    if (pdPASS != xResult) {
        for (;;) {
            Cy_GPIO_Inv(CYBSP_USER_LED1_PORT, CYBSP_USER_LED1_PIN);
            Cy_GPIO_Inv(CYBSP_USER_LED2_PORT, CYBSP_USER_LED2_PIN);
            Cy_SysLib_Delay(100);
        }
    }
#endif

    /* Start the RTOS Scheduler */
    vTaskStartScheduler();

    /* Should never reach here */
    for (;;) {
        Cy_GPIO_Inv(CYBSP_USER_LED1_PORT, CYBSP_USER_LED1_PIN);
        Cy_GPIO_Inv(CYBSP_USER_LED2_PORT, CYBSP_USER_LED2_PIN);
        Cy_SysLib_Delay(500);
    }
    return -1;
}

/*******************************************************************************
* FreeRTOS Hook Functions
*******************************************************************************/
/* Fault breadcrumb in fixed SRAM — survives until power cycle.
 * Read via J-Link: mem32 0x28000000 1 (or check LED pattern). */
#define FAULT_MARKER_ADDR  ((volatile uint32_t *)0x28000000)
#define FAULT_MARKER_STACK  0xDEAD0001
#define FAULT_MARKER_MALLOC 0xDEAD0002
#define FAULT_MARKER_HARD   0xDEAD0003

/* Blink LED N times, pause, repeat — each fault has a distinct count:
 *   1 blink  = Stack Overflow  (LED1)
 *   2 blinks = Malloc Failed   (LED2)
 *   3 blinks = HardFault       (LED1+LED2) */
static void fault_blink_pattern(uint8_t count, bool led1, bool led2)
{
    for (;;) {
        for (uint8_t i = 0; i < count; i++) {
            if (led1) Cy_GPIO_Clr(CYBSP_USER_LED1_PORT, CYBSP_USER_LED1_PIN);
            if (led2) Cy_GPIO_Clr(CYBSP_USER_LED2_PORT, CYBSP_USER_LED2_PIN);
            for (volatile uint32_t d = 0; d < 300000; d++) {}
            if (led1) Cy_GPIO_Set(CYBSP_USER_LED1_PORT, CYBSP_USER_LED1_PIN);
            if (led2) Cy_GPIO_Set(CYBSP_USER_LED2_PORT, CYBSP_USER_LED2_PIN);
            for (volatile uint32_t d = 0; d < 300000; d++) {}
        }
        for (volatile uint32_t d = 0; d < 1500000; d++) {}  /* Long pause */
    }
}

void vApplicationStackOverflowHook(TaskHandle_t pxTask, char *pcTaskName)
{
    CY_UNUSED_PARAMETER(pxTask);
    CY_UNUSED_PARAMETER(pcTaskName);
    *FAULT_MARKER_ADDR = FAULT_MARKER_STACK;
    __disable_irq();
    fault_blink_pattern(1, true, false);  /* 1 blink LED1 */
}

void vApplicationMallocFailedHook(void)
{
    *FAULT_MARKER_ADDR = FAULT_MARKER_MALLOC;
    __disable_irq();
    fault_blink_pattern(2, false, true);  /* 2 blinks LED2 */
}

void HardFault_Handler(void)
{
    *FAULT_MARKER_ADDR = FAULT_MARKER_HARD;
    __disable_irq();
    fault_blink_pattern(3, true, true);   /* 3 blinks LED1+LED2 */
}
