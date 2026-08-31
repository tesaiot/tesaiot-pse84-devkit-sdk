/*******************************************************************************
 * usb_hid_joystick.h — Layer 2: USB host driver public API for the
 * Logitech F310 joystick on CM55 + FreeRTOS via SEGGER emUSB-Host.
 *
 * This header re-exports the L1 raw report struct + constants from
 * hid_f310_report.h so existing BENTO consumers (UI pages, IPC handlers,
 * MicroPython bindings) keep working unchanged. New code that wants only
 * pure decoding logic should include hid_f310_parser.h directly without
 * touching the USB layer.
 *
 * Depends on: SEGGER emUSB-Host + FreeRTOS (linker side, not in this .h).
 ******************************************************************************/
#ifndef USB_HID_JOYSTICK_H
#define USB_HID_JOYSTICK_H

#include <stdint.h>
#include <stdbool.h>

/* L1: raw 8-byte report layout + button/hat constants + VID/PID. */
#include "hid_f310_report.h"

#ifdef __cplusplus
extern "C" {
#endif

/* USB init stages for debug tracking (visible via IPC info.joystick). */
#define USB_STAGE_NONE          0
#define USB_STAGE_USBH_INIT     1   /* USBH_Init() called */
#define USB_STAGE_USBH_DONE     2   /* USBH_Init() returned */
#define USB_STAGE_MAIN_TASK     3   /* USBH_Task created */
#define USB_STAGE_ISR_TASK      4   /* USBH_ISRTask created */
#define USB_STAGE_HID_INIT      5   /* USBH_HID_Init() done */
#define USB_STAGE_CALLBACKS     6   /* Callbacks registered */
#define USB_STAGE_COMPLETE      7   /* Fully initialized */

/* Joystick driver state — read-only from outside the USB-host task.
 * `volatile` fields are touched from the emUSB-Host callback context. */
typedef struct {
    volatile uint8_t  connected;        /* 0=disconnected, 1=connected */
    volatile uint8_t  sequence;         /* Increments on each new report */
    volatile uint8_t  usb_init_done;    /* 1 = USBH_Init completed OK */
    volatile uint8_t  init_stage;       /* USB_STAGE_* debug tracking */
    volatile uint16_t add_event_cnt;    /* Device add events from USBH */
    volatile uint16_t remove_event_cnt; /* Device remove events */
    volatile uint16_t report_cnt;       /* HID reports received */
    volatile uint32_t isr_count;        /* USB interrupt count */
    volatile uint32_t port_power_cnt;   /* Port power events */
    volatile uint8_t  usbh_running;     /* USBH_IsRunning() result */
    volatile uint8_t  num_devices;      /* USBH_GetNumDevicesConnected() */
    volatile uint8_t  root_conns;       /* USBH_GetNumRootPortConnections() */
    volatile uint8_t  usb_class;        /* USB interface class from enumeration */
    f310_report_t     report;           /* Latest 8-byte report */
    uint16_t          vid;
    uint16_t          pid;
    uint16_t          usb_vid;          /* VID from USB enumeration (not HID) */
    uint16_t          usb_pid;          /* PID from USB enumeration (not HID) */

    /* Multi-controller support: which decoder filled .report (set at connect
     * time from the device VID/PID; F310 copied as-is, NJ43 remapped). */
    volatile uint8_t  source;           /* JOY_SRC_* */

    /* Wire-level diagnostics (2026-07-17, TESAIoT Dev Kit joystick debug).
     * raw[] holds the last HID report exactly as received, BEFORE the size
     * guard and BEFORE any decode — so a wrong NJ43 byte map is provable
     * from a debugger (openocd mdw) or the Joystick page without guessing.
     * reject_cnt/last_reject_size account for reports the size guard drops:
     * report_cnt == sequence + reject_cnt (mod wrap) must always hold.
     * NOT marshalled over IPC (ipc_joystick_state_t is a separate struct). */
    volatile uint8_t  raw_len;          /* byte count captured into raw[] */
    volatile uint8_t  raw[F310_REPORT_SIZE]; /* last raw report, pre-decode */
    volatile uint16_t reject_cnt;       /* reports dropped by the size guard */
    volatile uint8_t  last_reject_size; /* ReportSize of last dropped report */
} joystick_state_t;

/* .source values — which controller/decoder is currently feeding .report */
#define JOY_SRC_NONE   0
#define JOY_SRC_F310   1   /* Logitech F310, USB HID (DirectInput) */
#define JOY_SRC_XBOX   2   /* Xbox Series X|S, USB GIP (not built here) */
#define JOY_SRC_NJ43   3   /* NUBWO NJ43, USB HID (DirectInput, DragonRise) */

/* Human-readable controller name for the active .source (for UI/REPL). */
static inline const char *joystick_source_name(uint8_t src) {
    switch (src) {
        case JOY_SRC_F310: return "F310";
        case JOY_SRC_XBOX: return "Xbox";
        case JOY_SRC_NJ43: return "NJ43";
        default:           return "none";
    }
}

/* Initialize USB Host HID joystick (creates USBH tasks). Idempotent. */
void usb_hid_joystick_init(void);

/* Request non-blocking USB Host initialization from a worker task. */
bool usb_hid_joystick_request_init(void);

/* Get pointer to current joystick state (thread-safe via volatile reads). */
const joystick_state_t* usb_hid_joystick_get_state(void);

/* Check if joystick is currently connected. */
bool usb_hid_joystick_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif /* USB_HID_JOYSTICK_H */
