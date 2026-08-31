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
#include "ipc_scb5_lock.h"
#include "ipc_xip_guard.h"
#include "xip_guard.h"
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
 * Display I2C bus clear (SCB5: SCL P17.0, SDA P17.1), before the display task.
 *
 * A warm reset -- the reset after programming is one -- restarts the PSoC but
 * not the targets on this bus: the panel MCU (0x45), the touch controller, the
 * mikroBUS OPTIGA and the other 3V3 peripherals keep their power. If the reset
 * lands while a target is driving SDA low, SDA stays low, every later transfer
 * fails, the panel init gives up, and the screen stays black until power is
 * removed. Nine SCL pulses and a STOP make any target finish its byte and let
 * go. The same routine runs inside the display task in the MicroPython Dev Kit
 * firmware (2.4.1 and later; commit f444e2a), where it took warm-reset failures
 * on the bench from 3 of 6 to 0 of 27.
 *
 * Runs with the pins as GPIO and puts the SCB routing back afterwards, so the
 * display task finds the pins exactly as cybsp_init() left them.
 *
 * s_disp_bus_clear: bit0 = SDA was held low, bit1 = SDA high afterwards.
 * Static on purpose: the display library built from the Dev Kit sources defines
 * a global bus-clear flag of its own for the clear it does inside the task.
 *******************************************************************************/
static volatile uint8_t s_disp_bus_clear = 0u;

static void disp_i2c_bus_clear(void)
{
    GPIO_PRT_Type *scl_prt = CYBSP_I2C_SCL_3V3_PORT;
    GPIO_PRT_Type *sda_prt = CYBSP_I2C_SDA_3V3_PORT;
    const uint32_t scl = CYBSP_I2C_SCL_3V3_PIN;
    const uint32_t sda = CYBSP_I2C_SDA_3V3_PIN;
    en_hsiom_sel_t hs_scl = Cy_GPIO_GetHSIOM(scl_prt, scl);
    en_hsiom_sel_t hs_sda = Cy_GPIO_GetHSIOM(sda_prt, sda);
    uint32_t dm_scl = Cy_GPIO_GetDrivemode(scl_prt, scl);
    uint32_t dm_sda = Cy_GPIO_GetDrivemode(sda_prt, sda);

    Cy_GPIO_Write(scl_prt, scl, 1u);
    Cy_GPIO_Write(sda_prt, sda, 1u);
    Cy_GPIO_SetDrivemode(scl_prt, scl, CY_GPIO_DM_OD_DRIVESLOW);
    Cy_GPIO_SetDrivemode(sda_prt, sda, CY_GPIO_DM_OD_DRIVESLOW);
    Cy_GPIO_SetHSIOM(scl_prt, scl, HSIOM_SEL_GPIO);
    Cy_GPIO_SetHSIOM(sda_prt, sda, HSIOM_SEL_GPIO);
    Cy_SysLib_DelayUs(10u);

    bool held = (0u == Cy_GPIO_Read(sda_prt, sda));
    for (uint32_t i = 0u; (i < 9u) && (0u == Cy_GPIO_Read(sda_prt, sda)); ++i) {
        Cy_GPIO_Write(scl_prt, scl, 0u); Cy_SysLib_DelayUs(5u);
        Cy_GPIO_Write(scl_prt, scl, 1u); Cy_SysLib_DelayUs(5u);
    }
    /* STOP: SDA rises while SCL is high. */
    Cy_GPIO_Write(scl_prt, scl, 0u); Cy_SysLib_DelayUs(5u);
    Cy_GPIO_Write(sda_prt, sda, 0u); Cy_SysLib_DelayUs(5u);
    Cy_GPIO_Write(scl_prt, scl, 1u); Cy_SysLib_DelayUs(5u);
    Cy_GPIO_Write(sda_prt, sda, 1u); Cy_SysLib_DelayUs(10u);
    bool freed = (0u != Cy_GPIO_Read(sda_prt, sda));

    Cy_GPIO_SetHSIOM(scl_prt, scl, hs_scl);
    Cy_GPIO_SetHSIOM(sda_prt, sda, hs_sda);
    Cy_GPIO_SetDrivemode(scl_prt, scl, dm_scl);
    Cy_GPIO_SetDrivemode(sda_prt, sda, dm_sda);

    s_disp_bus_clear = (uint8_t)((held ? 1u : 0u) | (freed ? 2u : 0u));
}

/*******************************************************************************
 * SCB5 boot hold (ipc_scb5_lock.h).
 *
 * The display task brings up the panel MCU and the touch controller on SCB5,
 * which CM33 also uses for the OPTIGA. main() takes the cross-core lock before
 * that task exists; this task gives it back once the display reports ready,
 * or after SCB5_BOOT_HOLD_MAX_MS if it never does (a display task that gave
 * up is no longer using the bus, and CM33 must not wait for ever).
 *******************************************************************************/
#define SCB5_BOOT_HOLD_MAX_MS   (15000U)   /* pal_i2c.c waits 16 s: keep it longer */
#define SCB5_BOOT_LOCK_WAIT_US  (1000000UL)
static bool s_scb5_boot_held;

/* How long after the display reports ready the UI must have published the
 * XIP guard block (sensorhub_ui_init() does it before its first tick). */
#define XIP_GUARD_UI_GRACE_MS   (2000U)
#define XIP_GUARD_HEADLESS_POLL_MS (10U)

static void scb5_boot_release_task(void *arg)
{
    CY_UNUSED_PARAMETER(arg);
    extern volatile uint8_t tesaiot_display_ready;
    for (uint32_t t = 0U; (tesaiot_display_ready == 0U) && (t < SCB5_BOOT_HOLD_MAX_MS); t += 50U) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (s_scb5_boot_held) {
        ipc_scb5_quiesce(CYBSP_I2C_CAM_CONTROLLER_HW);
        ipc_scb5_unlock();
        s_scb5_boot_held = false;
    }

    /* The XIP guard normally rides the 33 ms UI tick. When the panel did not
     * come up, the display task skips the UI and nothing would ever answer
     * CM33 -- every flash write (saved WiFi, config) would then be refused for
     * the whole boot. In that case this task becomes the guard's poller.
     * Without the UI there is no LVGL drawing and no GPU work to interrupt. */
    vTaskDelay(pdMS_TO_TICKS(XIP_GUARD_UI_GRACE_MS));
    if (ipc_xip_guard_get()->magic != IPC_XIP_GUARD_MAGIC) {
        xip_guard_init_headless();
        for (;;) {
            xip_guard_poll();
            vTaskDelay(pdMS_TO_TICKS(XIP_GUARD_HEADLESS_POLL_MS));
        }
    }
    vTaskDelete(NULL);
}

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

    /* Hold SCB5 for the bus clear and the display bring-up (released by
     * scb5_boot_release_task). The IPC semaphores are already attached:
     * cybsp_init() calls Cy_IPC_Sema_Init(IPC0_SEMA_CH_NUM, 0, NULL) on this
     * core, and CM33 set up the array before it started CM55. CM33 does not use
     * the bus this early, so the wait is only a bound, not an expected delay. */
    s_scb5_boot_held = ipc_scb5_lock_wait_us(SCB5_BOOT_LOCK_WAIT_US);
    if (pdPASS != xTaskCreate(scb5_boot_release_task, "scb5_boot",
                              configMINIMAL_STACK_SIZE * 2U, NULL,
                              tskIDLE_PRIORITY + 1U, NULL)) {
        /* Nothing would ever give the bus back: do not hold it. */
        if (s_scb5_boot_held) {
            ipc_scb5_unlock();
            s_scb5_boot_held = false;
        }
    }

    /* Release a display bus that a target is still holding from before a warm
     * reset. Must run before the display task initialises SCB5 on these pins,
     * and only while this core holds the bus -- toggling the pins under a CM33
     * transfer would corrupt it. */
    if (s_scb5_boot_held) {
        disp_i2c_bus_clear();
    }

    /* GFX task: GFXSS/LVGL init + IPC + sensorhub UI */
    BaseType_t xResult = tesaiot_display_init();
    if (pdPASS != xResult) {
        for (;;) {
            Cy_GPIO_Inv(CYBSP_USER_LED2_PORT, CYBSP_USER_LED2_PIN);
            Cy_SysLib_Delay(50);
        }
    }
    //! [cm55_tesaiot_display_init_boot]

    /* SDK examples need no hook here. They are reached from the Examples page
     * (PAGE_ID_EXAMPLES), which sensorhub_ui.c registers when
     * ENABLE_PAGE_EXAMPLES=1 — nothing runs until a developer taps a row. */

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
