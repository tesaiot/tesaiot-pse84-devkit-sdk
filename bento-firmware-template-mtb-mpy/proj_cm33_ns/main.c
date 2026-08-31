/*******************************************************************************
 * File Name: main.c
 *
 * Description: CM33 Non-Secure FreeRTOS entry point.
 *              Creates MicroPython REPL task, boots CM55, starts scheduler.
 *
 *              Boot sequence:
 *                1. cybsp_init() + retarget-io (UART)
 *                2. Create MicroPython task
 *                3. Boot CM55 (display + camera + AI)
 *                4. Start FreeRTOS scheduler (never returns)
 *
 *              WiFi SDIO init is lazy — happens inside modwifi.c on first
 *              wifi.scan() or wifi.connect() call from Python.
 *
 * Author: TESAIoT (Asst.Prof.Santi Nuratch, Ph.D)
 * Version: 3.0 (FreeRTOS migration)
 *
 ******************************************************************************/

#include "cybsp.h"
#include "cy_sysclk.h"
#if BSP_HAS_POTENTIOMETER
#include "cy_autanalog.h"
#include "cycfg_peripherals.h"
#endif
#include "FreeRTOS.h"
#include "task.h"
#include "retarget_io_init.h"
#include "sensor_auto_task.h"
#include "ipc_hsm_handler.h"
#include "ipc_tesaiot_handler.h"
#include "psa/crypto.h"
#include "optiga_psa_se.h"
#include <malloc.h>   /* mallinfo() — heap reporting in the malloc-failed hook */
#if ENABLE_PAGE_BENTO_BUDDY
#include "radio_scheduler.h"
#include "cycfg_pins.h"   /* CYBSP_WIFI_WL_REG_ON_PORT / _PIN */
#include "FreeRTOS.h"
#include "task.h"

/* On the CYW55513 / Murata LBEE5HY2FY module the radio die is held in
 * reset by default — the WL_REG_ON GPIO controls its power rail. AIROC's
 * BTM_ENABLED handshake fails unless the radio is up first. DualBand
 * variant gets this for free because WHD always runs concurrently (and
 * WHD asserts WL_REG_ON during cy_wcm_init); Playground BLE-only never
 * runs WHD, so the pin stayed low and the BLE controller never woke.
 *
 * Minimal fix: assert WL_REG_ON via raw PDL before BLE host init. This
 * uses zero FreeRTOS heap (initial app_wifi_init() attempt failed BLE
 * init with `Malloc failed` — WHD + AIROC together don't fit the 90 KB
 * heap on a single-radio build, exactly the trade-off Memory agent
 * flagged in PLAN.md §6). The BT controller transport is HCI-UART
 * (separate from SDIO), so we don't need any of the WHD bring-up. */
//! [ble_chip_power_then_ble_task]
static void chip_power_then_ble_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(1500));

    printf("[boot] Asserting WL_REG_ON (P%u.%u) for CYW55513...\r\n",
           CYBSP_WIFI_WL_REG_ON_PORT_NUM, (unsigned)CYBSP_WIFI_WL_REG_ON_PIN);
    Cy_GPIO_Write(CYBSP_WIFI_WL_REG_ON_PORT, CYBSP_WIFI_WL_REG_ON_PIN, 1U);
    /* Murata 2FY datasheet: WL_REG_ON to module-ready ≈ 5 ms. Give 50 ms
     * for the HCI controller firmware to finish its internal power-up. */
    vTaskDelay(pdMS_TO_TICKS(50));
    printf("[boot] WL_REG_ON asserted — bringing up BLE NUS stack\r\n");

    extern int bento_buddy_request_start(void);
    int rc = bento_buddy_request_start();
    printf("[boot] bento_buddy_request_start rc=%d\r\n", rc);

    vTaskDelete(NULL);
}
//! [ble_chip_power_then_ble_task]

static void install_chip_power_then_ble(void)
{
    static StackType_t  task_stack[1536];
    static StaticTask_t task_tcb;
    xTaskCreateStatic(chip_power_then_ble_task,
                      "bento_chip_pwr",
                      (uint16_t)(sizeof(task_stack) / sizeof(task_stack[0])),
                      NULL, tskIDLE_PRIORITY + 1,
                      task_stack, &task_tcb);
}
#endif

#ifndef BENTO_HAS_MPY
#define BENTO_HAS_MPY 1
#endif

#if BENTO_HAS_MPY
/*******************************************************************************
 * MicroPython task entry point (defined in mpy_main.c, linked from
 * libmicropython.a)
 ******************************************************************************/
extern void mpy_task_entry(void *arg);
#else
/* mtb-only: the C storage layer replaces the VM's VFS mount and config load. */
#include "bento_storage.h"
extern void cm33_ipc_communication_setup(void);
extern bool tesaiot_config_init(void);
//! [j8_heartbeat_task]
static void bento_heartbeat_task(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        printf("[HB] t=%lus tasks=%u\r\n",
               (unsigned long)(xTaskGetTickCount() / configTICK_RATE_HZ),
               (unsigned)uxTaskGetNumberOfTasks());
    }
}
//! [j8_heartbeat_task]
#endif

/*******************************************************************************
 * Task Configuration
 ******************************************************************************/
#define MPY_TASK_STACK_SIZE  (8 * 1024)   /* 8KB stack for deep REPL call chains */
#define MPY_TASK_PRIORITY    (3)          /* Mid priority: below WHD(5), above idle(0) */

/*******************************************************************************
 * CM55 Boot Configuration
 ******************************************************************************/
#define CM55_BOOT_WAIT_TIME_USEC    (10U)
#define CM55_APP_BOOT_ADDR          (CYMEM_CM33_0_m55_nvm_START + \
                                        CYBSP_MCUBOOT_HEADER_SIZE)

#if BSP_HAS_POTENTIOMETER
/*******************************************************************************
 * Potentiometer ADC Init (AutAnalog SAR — QWA309 VR1-4 on P15.4-7)
 *
 * BSP design.modus provides the AutAnalog config struct
 * (autonomous_analog_init: SAR GPIO ch 0 + ch 4-7) but
 * init_cycfg_peripherals() does NOT call the init API.
 * CM33_NS must initialise + start the SAR ADC before CM55's
 * cm55_sensor_poll can read VR1-4 via Cy_AutAnalog_SAR_ReadResult().
 * Ported from the Eva Kit CM33_NS main.c (single-pot P15[1] variant).
 ******************************************************************************/
static bool init_potentiometer_adc(void)
{
    if (CY_AUTANALOG_SUCCESS != Cy_AutAnalog_Init(&autonomous_analog_init)) {
        return false;
    }
    Cy_AutAnalog_SetInterruptMask(CY_AUTANALOG_INT_SAR0_RESULT);
    Cy_AutAnalog_StartAutonomousControl();
    return true;
}
#endif /* BSP_HAS_POTENTIOMETER */

/*******************************************************************************
 * HSM / OPTIGA Trust M Security Init
 *
 * Registers IPC callback on CM33_NS to handle security data requests
 * from CM55's HSM Security page (certificate chain, public key, UID,
 * metadata).  When CM55 sends IPC_CMD_HSM_*, this handler reads the
 * OPTIGA Trust M chip over I2C and replies via IPC.
 ******************************************************************************/
static void init_hsm_optiga_security(void)
{
    ipc_hsm_handler_init();
}

 //! [j1_gfxss_clock_init]
/*******************************************************************************
 * GFXSS Clock Init (GPU + Display Controller + MIPI-DSI)
 *
 * Our BSP design.modus does NOT include GFXSS, so init_cycfg_peripherals()
 * never enables these peripheral clocks. CM33_NS must enable them before
 * CM55 can initialise the display pipeline (DCNano + VGLite + MIPI-DSI).
 ******************************************************************************/
static void init_gfxss_clocks(void)
{
    Cy_SysClk_PeriGroupSlaveInit(CY_MMIO_GFXSS_GPU_PERI_NR,
                                 CY_MMIO_GFXSS_GPU_GROUP_NR,
                                 CY_MMIO_GFXSS_GPU_SLAVE_NR,
                                 CY_MMIO_GFXSS_GPU_CLK_HF_NR);
    Cy_SysClk_PeriGroupSlaveInit(CY_MMIO_GFXSS_DC_PERI_NR,
                                 CY_MMIO_GFXSS_DC_GROUP_NR,
                                 CY_MMIO_GFXSS_DC_SLAVE_NR,
                                 CY_MMIO_GFXSS_DC_CLK_HF_NR);
    Cy_SysClk_PeriGroupSlaveInit(CY_MMIO_GFXSS_MIPIDSI_PERI_NR,
                                 CY_MMIO_GFXSS_MIPIDSI_GROUP_NR,
                                 CY_MMIO_GFXSS_MIPIDSI_SLAVE_NR,
                                 CY_MMIO_GFXSS_MIPIDSI_CLK_HF_NR);
}
//! [j1_gfxss_clock_init]

/*******************************************************************************
 * CM55 Core Boot (Display + Sensors + AI + Radar)
 *
 * Starts the CM55 core at its MCUBoot-signed application address.
 * CM55 runs: LVGL display, IPC sensorhub, cm55_sensor_poll (BMI270),
 * USB Host HID joystick, radar presence detection (BGT60TR13C).
 * GFXSS clocks must be enabled before calling this.
 ******************************************************************************/
//! [j1_cm55_release]
static void init_cm55_boot(void)
{
    Cy_SysEnableCM55(MXCM55, CM55_APP_BOOT_ADDR, CM55_BOOT_WAIT_TIME_USEC);
}
//! [j1_cm55_release]

/*******************************************************************************
 * Function Name: main
 ******************************************************************************/
int main(void)
{
    cy_rslt_t result;

    /* Initialize the device and board peripherals */
    result = cybsp_init();
    if (CY_RSLT_SUCCESS != result) {
        CY_ASSERT(0);
    }

    /* Enable global interrupts */
    __enable_irq();

#if BSP_HAS_POTENTIOMETER
    /* Potentiometer SAR ADC — required before CM55 cm55_sensor_poll can read it */
    init_potentiometer_adc();
#endif

    /* Initialize retarget-io for UART serial (MicroPython REPL) */
    init_retarget_io();

    //! [j1_psa_se_register_first]
    /* ...context: inside main(), before any TLS use ... */
    /* Phase G: PSA Crypto + OPTIGA SE driver init.
     * MUST be called BEFORE any TLS operations (WiFi, MQTT, HTTPS).
     * Order: register SE driver FIRST, then init PSA crypto subsystem.
     * psa_crypto_init() makes PSA hash functions available for x509 cert parsing
     * (required when MBEDTLS_USE_PSA_CRYPTO is enabled). */
    {
        psa_status_t psa_ret = optiga_psa_register();
        if (psa_ret != PSA_SUCCESS) {
            printf("[BOOT] optiga_psa_register failed: %d\n", (int)psa_ret);
        }
        psa_ret = psa_crypto_init();
        if (psa_ret != PSA_SUCCESS) {
            printf("[BOOT] psa_crypto_init failed: %d\n", (int)psa_ret);
        }
    }
    //! [j1_psa_se_register_first]

#ifdef BOOT_VERBOSE
    printf("\r\n=====================================================\r\n");
    printf("PSoC Edge AI MicroPython + WiFi (CM33_NS) by TESAIoT\r\n");
    printf("FreeRTOS %s\r\n", tskKERNEL_VERSION_NUMBER);
    printf("=====================================================\r\n");
#endif

//! [j1_variant_fork_mpy_vs_c]
/* ...context: inside main() ... */
#if BENTO_HAS_MPY
    /* Create MicroPython REPL task */
    BaseType_t xResult = xTaskCreate(
        mpy_task_entry,
        "MicroPython",
        MPY_TASK_STACK_SIZE / sizeof(StackType_t),
        NULL,
        MPY_TASK_PRIORITY,
        NULL
    );
    if (pdPASS != xResult) {
        printf("ERROR: Failed to create MicroPython task\r\n");
        CY_ASSERT(0);
    }
#else
    /* mtb-only. Three jobs the MicroPython task performed at boot have to
     * happen here, or they happen nowhere — and all three fail silently:
     *
     *   mount /              vfs_mount_script          -> bento_storage_init()
     *   load config          mpy_main was the only caller of
     *                        tesaiot_config_init()
     *   configure IPC pipe   sensor_auto_task_create() was the ONLY boot-path
     *                        caller of cm33_ipc_communication_setup(); the two
     *                        handler inits below RegisterCallback on that pipe
     *                        and do not set it up themselves. Without this the
     *                        link is clean, the boot is quiet, and every CM55
     *                        page that talks to CM33 is dead.
     *
     * (Boot WiFi credentials are read inside sensor_auto_task's C path.)
     *
     * A failed mount is reported and NOT formatted — the mpy variant formats
     * on any mount error, but wiping /main.py and the config to recover a
     * transient SMIF fault is the wrong trade on a board people work on. */
    //! [j8_heartbeat_rationale]
    /* ...context: inside main(), mtb-only branch ... */
    /* Liveness heartbeat over UART, every 10 s.
     *
     * This variant has no REPL, so with the boot prints muted the only sign of
     * life used to be the debugger — and attaching the debugger to a running
     * board parks CM33 in a boot-ROM loop (measured 2026-08-28; the mpy
     * variant tolerates the same attach, cause not established). One line
     * every ten seconds is what let that be discovered at all: a heartbeat
     * that kept beating for six minutes detached, after an hour of postmortems
     * that all blamed the firmware. Keep it until the variant has an
     * instrument that is not also the murder weapon. */
    xTaskCreate(bento_heartbeat_task, "HB", 256, NULL, 1, NULL);
    //! [j8_heartbeat_rationale]

    if (!bento_storage_init()) {
        printf("ERROR: storage unavailable — config and WiFi credentials "
               "will use defaults\r\n");
    }
    if (!tesaiot_config_init()) {
        printf("ERROR: tesaiot_config_init failed\r\n");
    }
    cm33_ipc_communication_setup();
#endif
//! [j1_variant_fork_mpy_vs_c]

    /* Sensor auto-push background task (BMM350, DPS368, SHT40 → IPC to CM55 LVGL) */
    sensor_auto_task_create();

    /* HSM Security: OPTIGA Trust M IPC handler for CM55 security dashboard */
    init_hsm_optiga_security();

    /* TESAIoT Connectivity: config IPC handler for CM55 TESAIoT page */
    ipc_tesaiot_handler_init();

#ifdef BOOT_VERBOSE
    printf("[BOOT] HSM OPTIGA handler OK\r\n");
#endif

    /* GFXSS peripheral clocks — must be enabled before CM55 display init */
    init_gfxss_clocks();

    /* Boot CM55 core */
#ifdef BOOT_VERBOSE
    printf("CM33_NS: Booting CM55...\r\n");
#endif
    init_cm55_boot();
#ifdef BOOT_VERBOSE
    printf("CM33_NS: CM55 boot initiated\r\n");
#endif

//! [ble_radio_scheduler_bringup]
#if ENABLE_PAGE_BENTO_BUDDY
    /* Two-layer BLE bring-up:
     *
     *   1. bento_buddy_auto_start_install — the legacy 3-s-delayed task
     *      that brings up the AIROC BLE host stack. Proven path: kept as
     *      the boot-time owner of ble_nus_init. Smoke tests showed that
     *      calling ble_nus_init from any other context (notably the
     *      radio_scheduler worker) fails with state=ERROR even with the
     *      same 3-s delay — the original task's stack/priority is what
     *      the AIROC HCI bring-up actually needs.
     *
     *   2. radio_scheduler — runtime arbiter for BLE↔Wi-Fi mode switches.
     *      Initialised AFTER auto_start so it doesn't race the AIROC init.
     *      Phase 1 only services the verbs (radio.status / radio.switch /
     *      wifi.set_creds) and persists creds; the actual swap-radio path
     *      is exercised by user action, not at boot. Saved-creds auto-Wi-Fi
     *      moves to Phase 2 once the persistence hooks (LFS boot_mode +
     *      LCD long-press) are wired. */
    {
        /* Replace bento_buddy_auto_start_install with our chip-power-then-BLE
         * variant — the legacy task skipped the WL_REG_ON / WCM init step,
         * which CYW55513 needs for BLE controller bring-up. */
        install_chip_power_then_ble();

        extern void nus_radio_emit_state_event(const struct radio_status_s *st);
        radio_scheduler_config_t cfg = {
            .persist_boot_mode = NULL,
            .load_boot_mode    = NULL,
        };
        radio_scheduler_set_on_state(nus_radio_emit_state_event);
        (void)radio_scheduler_init(&cfg);
    }
#endif
//! [ble_radio_scheduler_bringup]

    /* Start FreeRTOS scheduler — never returns */
    vTaskStartScheduler();

    /* Should never reach here */
    CY_ASSERT(0);
    for (;;) {}
}

/*******************************************************************************
 * FreeRTOS Hook Functions
 ******************************************************************************/

void vApplicationStackOverflowHook(TaskHandle_t pxTask, char *pcTaskName)
{
    (void)pxTask;
    printf("FATAL: Stack overflow in task '%s'\r\n", pcTaskName);
    CY_ASSERT(0);
}

static volatile uint32_t s_malloc_fail_count = 0;

void vApplicationMallocFailedHook(void)
{
    s_malloc_fail_count++;
    /* Log but do NOT halt — let the caller handle NULL gracefully.
     * CY_ASSERT(0) was killing the board on transient allocation spikes
     * (e.g. WiFi reconnect, OPTIGA init) making USB reconnect impossible.
     *
     * Report the arena too. "Malloc failed" on its own says nothing about
     * whether the heap is exhausted or merely fragmented, and the difference
     * decides whether the fix is to free memory or to allocate earlier. heap_3
     * puts pvPortMalloc straight onto newlib's malloc, so mallinfo describes
     * the same arena FreeRTOS just failed in. */
    struct mallinfo mi = mallinfo();
    printf("WARN: Malloc failed (#%lu) arena=%d used=%d free=%d largest_free=%d\r\n",
           (unsigned long)s_malloc_fail_count,
           mi.arena, mi.uordblks, mi.fordblks, mi.keepcost);
}
