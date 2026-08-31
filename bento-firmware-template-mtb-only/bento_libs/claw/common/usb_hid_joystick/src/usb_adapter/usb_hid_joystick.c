/*******************************************************************************
 * File Name: usb_hid_joystick.c
 *
 * Description: USB Host HID Joystick driver for Logitech F310 (DirectInput).
 *              Uses SEGGER emUSB-Host USBH_HID API on CM55 with FreeRTOS.
 *              Stores latest 8-byte report for IPC access from CM33.
 *
 *******************************************************************************/

#include "usb_hid_joystick.h"
#include "cybsp.h"
#include "FreeRTOS.h"
#include "task.h"
#include "USBH.h"
#include "USBH_HID.h"
/* NOTE: CM55 must NOT use printf/retarget-io (CM33_NS owns UART for REPL) */
#include <string.h>

/* ISR counters from usbh_config.c — used for debug via IPC */
extern volatile uint32_t usbh_isr_count;
extern volatile uint32_t usbh_port_power_count;

/*******************************************************************************
 * Configuration
 *******************************************************************************/
#define JOYSTICK_USBH_MAIN_STACK    (8192)
#define JOYSTICK_USBH_ISR_STACK     (8192)
#define JOYSTICK_USBH_INIT_STACK    (configMINIMAL_STACK_SIZE * 4)
#define JOYSTICK_USBH_MAIN_PRIO     (configMAX_PRIORITIES - 4)
#define JOYSTICK_USBH_ISR_PRIO      (configMAX_PRIORITIES - 4)
#define JOYSTICK_USBH_INIT_PRIO     (configMAX_PRIORITIES - 5)

/*******************************************************************************
 * Static Data
 *******************************************************************************/
static joystick_state_t s_joystick_state;
static USBH_NOTIFICATION_HOOK s_hid_hook;
static TaskHandle_t s_usbh_main_handle;
static TaskHandle_t s_usbh_isr_handle;
static TaskHandle_t s_usbh_init_handle;
static volatile bool s_initialized = false;
static volatile bool s_init_requested = false;

/*******************************************************************************
 * NUBWO NJ43 -> F310 report translation (DragonRise / PC TWIN SHOCK layout)
 *
 * The NJ43 is a DirectInput HID joystick but its 8-byte report is laid out
 * differently from the F310 (right stick at byte3/4, hat+buttons packed at
 * byte5/6). We repack it into f310_report_t so all downstream code — the IPC
 * struct and the MicroPython `joystick` module — stays controller-agnostic.
 * See hid_f310_report.h for the byte map + the (verify-on-HW) button order.
 *******************************************************************************/
static void nj43_decode_to_f310(const uint8_t *r)
{
    f310_report_t out;

    out.left_x  = r[NJ43_OFF_LEFT_X];
    out.left_y  = r[NJ43_OFF_LEFT_Y];
    out.right_x = r[NJ43_OFF_RIGHT_X];
    out.right_y = r[NJ43_OFF_RIGHT_Y];

    /* The D-pad arrives on the X/Y axes, not as a hat nibble.
     *
     * This byte5 low nibble was read as a hat and it never is one: measured on
     * the wire it reads 0x0F whatever the pad is doing, so every direction
     * clamped to neutral and the D-pad did nothing at all. The buttons in the
     * high nibble worked, which is why only movement was affected -- and why
     * the fault stayed hidden until a page needed the D-pad specifically.
     *
     * Measured, centre 0x7F, extremes 0x00 and 0xFF:
     *     neutral  7F 7F      up    7F 00      down  7F FF
     *     left     00 7F      right FF 7F
     * The thresholds are wide because the axes are analogue and a worn stick
     * does not reach the rail. */
    bool dp_l = out.left_x < 0x40, dp_r = out.left_x > 0xC0;
    bool dp_u = out.left_y < 0x40, dp_d = out.left_y > 0xC0;
    uint8_t hat;
    if      (dp_u && dp_r) hat = F310_HAT_UP_RIGHT;
    else if (dp_d && dp_r) hat = F310_HAT_DOWN_RIGHT;
    else if (dp_d && dp_l) hat = F310_HAT_DOWN_LEFT;
    else if (dp_u && dp_l) hat = F310_HAT_UP_LEFT;
    else if (dp_u)         hat = F310_HAT_UP;
    else if (dp_r)         hat = F310_HAT_RIGHT;
    else if (dp_d)         hat = F310_HAT_DOWN;
    else if (dp_l)         hat = F310_HAT_LEFT;
    else                   hat = F310_HAT_NEUTRAL;

    /* Pack the 12 HID buttons into one field so a press of button N maps to
     * bit (N-1): btn 1..4 = byte5 high nibble, btn 5..12 = byte6. Using a single
     * 0-based shift avoids any negative shift count. */
    uint16_t btns = (uint16_t)((r[NJ43_OFF_HAT_BTN14] >> 4) & 0x0Fu)
                  | (uint16_t)((uint16_t)r[NJ43_OFF_BTN512] << 4);
    #define NJ43_PRESSED(idx) (btns & (1u << ((idx) - 1)))

    uint8_t b1 = (uint8_t)(hat & F310_HAT_MASK);
    if (NJ43_PRESSED(NJ43_BTN_X)) b1 |= F310_BTN_X;
    if (NJ43_PRESSED(NJ43_BTN_A)) b1 |= F310_BTN_A;
    if (NJ43_PRESSED(NJ43_BTN_B)) b1 |= F310_BTN_B;
    if (NJ43_PRESSED(NJ43_BTN_Y)) b1 |= F310_BTN_Y;

    uint8_t b2 = 0;
    if (NJ43_PRESSED(NJ43_BTN_LB))    b2 |= F310_BTN_LB;
    if (NJ43_PRESSED(NJ43_BTN_RB))    b2 |= F310_BTN_RB;
    if (NJ43_PRESSED(NJ43_BTN_LT))    b2 |= F310_BTN_LT;
    if (NJ43_PRESSED(NJ43_BTN_RT))    b2 |= F310_BTN_RT;
    if (NJ43_PRESSED(NJ43_BTN_BACK))  b2 |= F310_BTN_BACK;
    if (NJ43_PRESSED(NJ43_BTN_START)) b2 |= F310_BTN_START;
    if (NJ43_PRESSED(NJ43_BTN_L3))    b2 |= F310_BTN_L3;
    if (NJ43_PRESSED(NJ43_BTN_R3))    b2 |= F310_BTN_R3;
    #undef NJ43_PRESSED

    out.buttons1 = b1;
    out.buttons2 = b2;
    out.mode     = 0;
    out.status   = 0;

    memcpy((void *)&s_joystick_state.report, &out, sizeof(f310_report_t));
}

/*******************************************************************************
 * USB HID Report Callback
 *
 * Called by emUSB-Host for every HID report received. The decoder is selected
 * by s_joystick_state.source (set at connect time from the device VID/PID):
 * the F310 report is copied as-is; the NJ43 report is remapped first.
 *******************************************************************************/
static void _cbOnReport(USBH_INTERFACE_ID InterfaceID, const U8 *pReport,
                        unsigned ReportSize, int Handled)
{
    (void)InterfaceID;
    (void)Handled;

    s_joystick_state.report_cnt++;

    if (pReport == NULL) {
        s_joystick_state.reject_cnt++;
        s_joystick_state.last_reject_size = 0;
        return;
    }

    /* Capture the raw wire bytes BEFORE the guard and BEFORE any decode, so
     * the true device byte layout is always inspectable (debugger / UI diag).
     * Invariant: report_cnt == sequence + reject_cnt (mod counter wrap). */
    {
        unsigned n = (ReportSize < (unsigned)sizeof(s_joystick_state.raw))
                   ? ReportSize : (unsigned)sizeof(s_joystick_state.raw);
        for (unsigned i = 0; i < n; i++) {
            s_joystick_state.raw[i] = pReport[i];
        }
        s_joystick_state.raw_len = (uint8_t)n;
    }

    if (ReportSize < F310_REPORT_SIZE) {
        s_joystick_state.reject_cnt++;
        s_joystick_state.last_reject_size =
            (ReportSize > 0xFFu) ? 0xFFu : (uint8_t)ReportSize;
        return;
    }

    if (s_joystick_state.source == JOY_SRC_NJ43) {
        nj43_decode_to_f310(pReport);          /* remap DragonRise layout */
    } else {
        /* F310 (and other Logitech) — already in f310_report_t order */
        memcpy((void *)&s_joystick_state.report, pReport, F310_REPORT_SIZE);
    }
    s_joystick_state.sequence++;
    __DMB();  /* Ensure memory barrier for cross-core visibility */
}

/* The HID handle currently held open, and one handed to the watchdog task to
 * close.
 *
 * The handle has to stay open while the pad is in use -- emUSB-Host delivers
 * reports through it. But it was opened on every ADD into a local and then
 * dropped, and never closed on REMOVE, so each attach leaked one handle out of
 * a 32 KB pool. Enough unplug/replug cycles and enumeration stops working
 * permanently, which is the "gamepad dead from boot" case.
 *
 * The close is deferred rather than done in the callback. USBH_HID_Close takes
 * USBH_LockDeviceList -- the same infinite-wait mutex that froze the panel when
 * the render path touched it -- and _cbOnAddRemove runs inside the stack, which
 * may already hold it. The callback therefore only parks the handle; the
 * watchdog task, which is allowed to block, performs the close. */
static volatile USBH_HID_HANDLE s_hid_open     = USBH_HID_INVALID_HANDLE;
static volatile USBH_HID_HANDLE s_hid_to_close = USBH_HID_INVALID_HANDLE;

static void _usbh_close_pending_hid(void)
{
    USBH_HID_HANDLE h = s_hid_to_close;
    if (h != USBH_HID_INVALID_HANDLE) {
        s_hid_to_close = USBH_HID_INVALID_HANDLE;
        USBH_HID_Close(h);
    }
}


/*******************************************************************************
 * USB HID Device Connect/Disconnect Callback
 *******************************************************************************/
static void _cbOnAddRemove(void *pContext, U8 DevIndex,
                           USBH_DEVICE_EVENT Event)
{
    (void)pContext;

    switch (Event) {
    case USBH_DEVICE_EVENT_ADD: {
        s_joystick_state.add_event_cnt++;

        USBH_HID_DEVICE_INFO devInfo;
        USBH_HID_HANDLE hDevice;

        /* A re-ADD without an intervening REMOVE (a bouncing link) would
         * otherwise strand the previous handle. */
        if (s_hid_open != USBH_HID_INVALID_HANDLE) {
            s_hid_to_close = s_hid_open;
            s_hid_open = USBH_HID_INVALID_HANDLE;
        }
        hDevice = USBH_HID_Open(DevIndex);
        if (hDevice != USBH_HID_INVALID_HANDLE) {
            s_hid_open = hDevice;
            USBH_STATUS status = USBH_HID_GetDeviceInfo(hDevice, &devInfo);
            if (status == USBH_STATUS_SUCCESS) {
                /* Store VID/PID regardless of match for debug */
                s_joystick_state.vid = devInfo.VendorId;
                s_joystick_state.pid = devInfo.ProductId;

                /* Auto-detect the controller from its USB VID/PID and pick the
                 * matching decoder (set in s_joystick_state.source, read back in
                 * _cbOnReport). Add more pads here with the same pattern. */
                uint8_t matched = 1;
                if (devInfo.VendorId == F310_VID) {
                    /* Logitech gamepads (F310, F710, RumblePad, ...) — VID-only */
                    s_joystick_state.source = JOY_SRC_F310;
                } else if (devInfo.VendorId == NJ43_VID &&
                           devInfo.ProductId == NJ43_PID) {
                    /* NUBWO NJ43 (DragonRise clone) — gate on VID+PID since 0x0079
                     * is shared by many unrelated clone pads */
                    s_joystick_state.source = JOY_SRC_NJ43;
                } else {
                    matched = 0;   /* unknown device — leave disconnected */
                }

                if (matched) {
                    s_joystick_state.connected = 1;
                    /* Clear report to neutral position */
                    memset((void *)&s_joystick_state.report, 0,
                           sizeof(f310_report_t));
                    s_joystick_state.report.left_x = 0x80;
                    s_joystick_state.report.left_y = 0x7F;
                    s_joystick_state.report.right_x = 0x80;
                    s_joystick_state.report.right_y = 0x7F;
                }
            }
            /* Left open on purpose: emUSB-Host delivers reports through it.
             * It is released on REMOVE, by the watchdog task. */
        }
        break;
    }

    case USBH_DEVICE_EVENT_REMOVE:
        s_joystick_state.remove_event_cnt++;
        (void)DevIndex;
        if (s_hid_open != USBH_HID_INVALID_HANDLE) {
            s_hid_to_close = s_hid_open;
            s_hid_open = USBH_HID_INVALID_HANDLE;
        }
        s_joystick_state.connected = 0;
        s_joystick_state.source = JOY_SRC_NONE;
        memset((void *)&s_joystick_state.report, 0, sizeof(f310_report_t));
        s_joystick_state.report.left_x = 0x80;
        s_joystick_state.report.left_y = 0x7F;
        s_joystick_state.report.right_x = 0x80;
        s_joystick_state.report.right_y = 0x7F;
        __DMB();
        break;

    default:
        break;
    }
}

/*******************************************************************************
 * USBH Task Wrappers (required by emUSB-Host)
 *******************************************************************************/
static void _usbh_main_task(void *arg)
{
    (void)arg;
    USBH_Task();    /* Never returns */
}

static void _usbh_isr_task(void *arg)
{
    (void)arg;
    USBH_ISRTask(); /* Never returns */
}

/* Everything in here talks to the emUSB-Host stack, and the stack guards its
 * device list with a mutex it takes with an INFINITE timeout (USBH_OS_Lock ->
 * cy_rtos_mutex_get(CY_RTOS_NEVER_TIMEOUT)). That is acceptable for this task,
 * whose whole job is waiting on USB -- and fatal for the GFX task, which used
 * to make these same calls from usb_hid_joystick_get_state() every 33 ms while
 * rendering the Home page. When the stack wedged mid-recovery holding that
 * mutex -- seen on the bench with a DragonRise NJ43 whose interrupt-IN stream
 * stalls -- the GFX task blocked forever and took touch, rendering and the
 * clock down with it, two to three minutes after boot, without ever entering
 * a game. The GFX task now only reads cached fields; this task is the only
 * caller allowed to block on the stack. */
static void _usbh_refresh_stack_diag(void)
{
    if (!s_initialized) {
        return;
    }
    s_joystick_state.usbh_running = (uint8_t)USBH_IsRunning();
    s_joystick_state.num_devices  = (uint8_t)USBH_GetNumDevicesConnected(0);
    s_joystick_state.root_conns   = (uint8_t)USBH_GetNumRootPortConnections(0);

    /* USB-level identity (VID/PID/Class), once per attach. The interface-list
     * walk takes the same device-list lock, which is exactly why it lives here
     * and not in the 33 ms read path. */
    if (s_joystick_state.num_devices > 0 && s_joystick_state.usb_vid == 0) {
        USBH_INTERFACE_MASK mask;
        memset(&mask, 0, sizeof(mask));
        unsigned int count = 0;
        USBH_INTERFACE_LIST_HANDLE hList = USBH_CreateInterfaceList(&mask, &count);
        if (hList && count > 0) {
            USBH_INTERFACE_ID id = USBH_GetInterfaceId(hList, 0);
            USBH_INTERFACE_INFO info;
            if (USBH_GetInterfaceInfo(id, &info) == USBH_STATUS_SUCCESS) {
                s_joystick_state.usb_vid   = info.VendorId;
                s_joystick_state.usb_pid   = info.ProductId;
                s_joystick_state.usb_class = info.Class;
            }
        }
        if (hList) {
            USBH_DestroyInterfaceList(hList);
        }
    }
}

static void _usbh_init_task(void *arg)
{
    (void)arg;
    usb_hid_joystick_init();

    /* Persistent enumeration watchdog — so the user never has to unplug/replug.
     * The bounded power-cycle in usb_hid_joystick_init() catches the common case at
     * boot; this loop covers the rest: a pad that wasn't ready during those cycles,
     * one plugged in later, or one unplugged then plugged back in. While nothing is
     * connected, regenerate a fresh connect edge by re-powering the root port every
     * ~5s; once a controller is connected, idle quietly (never cut power in use). */
    for (;;) {
        _usbh_close_pending_hid();
        _usbh_refresh_stack_diag();
        if (!s_joystick_state.connected) {
            USBH_SetRootPortPower(0, 1, USBH_POWER_OFF);
            vTaskDelay(pdMS_TO_TICKS(400));            /* power down + bus reset */
            USBH_SetRootPortPower(0, 1, USBH_NORMAL_POWER);
            for (int i = 0; i < 34 && !s_joystick_state.connected; i++) {
                vTaskDelay(pdMS_TO_TICKS(150));        /* ~5s for enumeration to finish */
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(1000));           /* connected — light idle poll */
        }
    }
}

/*******************************************************************************
 * Public API
 *******************************************************************************/

void usb_hid_joystick_init(void)
{
    if (s_initialized) {
        return;
    }

    /* Clear state */
    memset(&s_joystick_state, 0, sizeof(joystick_state_t));
    s_joystick_state.report.left_x = 0x80;
    s_joystick_state.report.left_y = 0x7F;
    s_joystick_state.report.right_x = 0x80;
    s_joystick_state.report.right_y = 0x7F;

    /* Initialize USB Host stack (calls USBH_X_Config internally).
     * MUST be called from a task context (post-scheduler) because
     * emUSB-Host uses OS primitives (semaphores/mutexes) internally. */
    s_joystick_state.init_stage = USB_STAGE_USBH_INIT;
    USBH_Init();
    s_joystick_state.init_stage = USB_STAGE_USBH_DONE;

    /* Register HID class driver BEFORE creating tasks.
     * SEGGER recommended sequence: Init → class drivers → tasks.
     * If tasks start before HID is registered, devices connected at boot
     * may be enumerated but not claimed by the HID class driver. */
    USBH_HID_Init();
    s_joystick_state.init_stage = USB_STAGE_HID_INIT;

    /* Register raw report callback (receives ALL HID reports) */
    USBH_HID_SetOnReport(_cbOnReport);

    /* Register device add/remove notification */
    USBH_HID_AddNotification(&s_hid_hook, _cbOnAddRemove, NULL);
    s_joystick_state.init_stage = USB_STAGE_CALLBACKS;

    /* Power-cycle the root port to force device re-detection.
     * This ensures a fresh connect sequence even if the device was
     * already plugged in before USB Host init. */
    USBH_SetRootPortPower(0, 1, USBH_POWER_OFF);
    vTaskDelay(pdMS_TO_TICKS(200));
    USBH_SetRootPortPower(0, 1, USBH_NORMAL_POWER);
    vTaskDelay(pdMS_TO_TICKS(100));

    /* Create emUSB-Host required tasks (after class driver registration) */
    BaseType_t res;
    res = xTaskCreate(_usbh_main_task, "USBH_Main",
                      JOYSTICK_USBH_MAIN_STACK, NULL,
                      JOYSTICK_USBH_MAIN_PRIO, &s_usbh_main_handle);
    if (res != pdPASS) {
        return;  /* Heap exhausted — leave usb_init_done=0 */
    }
    s_joystick_state.init_stage = USB_STAGE_MAIN_TASK;

    res = xTaskCreate(_usbh_isr_task, "USBH_ISR",
                      JOYSTICK_USBH_ISR_STACK, NULL,
                      JOYSTICK_USBH_ISR_PRIO, &s_usbh_isr_handle);
    if (res != pdPASS) {
        vTaskDelete(s_usbh_main_handle);
        return;  /* Heap exhausted — leave usb_init_done=0 */
    }
    s_joystick_state.init_stage = USB_STAGE_ISR_TASK;

    s_joystick_state.usb_init_done = 1;
    s_joystick_state.init_stage = USB_STAGE_COMPLETE;
    s_initialized = true;
}

bool usb_hid_joystick_request_init(void)
{
    if (s_initialized || s_init_requested) {
        return true;
    }

    if (xTaskCreate(_usbh_init_task, "USBH_Init",
                    JOYSTICK_USBH_INIT_STACK, NULL,
                    JOYSTICK_USBH_INIT_PRIO, &s_usbh_init_handle) != pdPASS) {
        s_usbh_init_handle = NULL;
        return false;
    }

    s_init_requested = true;
    return true;
}

const joystick_state_t* usb_hid_joystick_get_state(void)
{
    /* Snapshot ISR counters from usbh_config.c into state for IPC query */
    s_joystick_state.isr_count = usbh_isr_count;
    s_joystick_state.port_power_cnt = usbh_port_power_count;

    /* usbh_running / num_devices / root_conns / VID / PID are refreshed by
     * _usbh_refresh_stack_diag() in the init task. This function is called
     * from the GFX task at frame rate, so it must never talk to the stack:
     * every one of those queries takes the device-list mutex with an infinite
     * timeout, and a wedged stack held it once -- the whole panel froze. The
     * gamepad report itself does not come through here at all; the HID report
     * callback fills it in independently. */

    return &s_joystick_state;
}

bool usb_hid_joystick_is_connected(void)
{
    return (s_joystick_state.connected != 0);
}
