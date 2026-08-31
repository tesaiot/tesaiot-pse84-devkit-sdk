/*******************************************************************************
 * File Name: usb_ccid_smartcard.h
 *
 * Description: USB CCID Smart Card driver for ACS ACR39U.
 *              Runs on CM55 via emUSB-Host CCID class driver.
 *              Reads Thai National ID Card (ISO 7816-4 APDU).
 *              Co-exists with USB HID (Joystick) — both class drivers
 *              registered, only one physical device at a time.
 *
 *******************************************************************************/

#ifndef USB_CCID_SMARTCARD_H
#define USB_CCID_SMARTCARD_H

#include <stdint.h>
#include <stdbool.h>

/*******************************************************************************
 * ACR39U Device Constants
 *******************************************************************************/
#define ACR39U_VID              0x072Fu  /* Advanced Card Systems Ltd. */
#define ACR39U_PID              0xB100u  /* ACR39U ICC Reader */

/*******************************************************************************
 * Thai National ID Card Constants
 *******************************************************************************/
#define THAI_ID_AID             "\xA0\x00\x00\x00\x54\x48\x00\x01"
#define THAI_ID_AID_LEN         8

/* APDU field offsets and lengths
 * Reference: https://github.com/chakphanu/ThaiNationalIDCard/blob/master/APDU.md */
#define THAI_ID_CID_OFFSET      0x0004
#define THAI_ID_CID_LEN         13
#define THAI_ID_NAME_TH_OFFSET  0x0011
#define THAI_ID_NAME_TH_LEN     100
#define THAI_ID_NAME_EN_OFFSET  0x0075
#define THAI_ID_NAME_EN_LEN     100
#define THAI_ID_BIRTH_OFFSET    0x00D9
#define THAI_ID_BIRTH_LEN       8
#define THAI_ID_GENDER_OFFSET   0x00E1
#define THAI_ID_GENDER_LEN      1
#define THAI_ID_ISSUE_OFFSET    0x0167
#define THAI_ID_ISSUE_LEN       8
#define THAI_ID_EXPIRE_OFFSET   0x016F
#define THAI_ID_EXPIRE_LEN      8
#define THAI_ID_ADDR_OFFSET     0x1579
#define THAI_ID_ADDR_LEN        100     /* 100 bytes sufficient — 200 includes laser code garbage */

/* Photo field — JPEG image (~4-15 KB, read in 255-byte chunks) */
#define THAI_ID_PHOTO_OFFSET    0x017B
#define THAI_ID_PHOTO_CHUNK     0x40        /* 64 bytes — small chunks avoid CCID T=0 chaining hang */
#define THAI_ID_PHOTO_MAX_SIZE  (16 * 1024) /* 16 KB safety limit */

/*******************************************************************************
 * Thai ID Data (all strings stored as UTF-8)
 *******************************************************************************/
typedef struct {
    char citizen_id[14];      /* 13 ASCII digits + null */
    char name_th[256];        /* Thai name (UTF-8 from TIS-620, ~3x expansion) */
    char name_en[101];        /* English name (ASCII) + null */
    char birthdate[9];        /* YYYYMMDD + null */
    char issue_date[9];       /* YYYYMMDD + null */
    char expire_date[9];      /* YYYYMMDD + null */
    char address_th[512];     /* Thai address (UTF-8 from TIS-620) */
    bool valid;               /* true if all critical fields read OK */
} thai_id_data_t;

/*******************************************************************************
 * Thai ID Photo (JPEG from card chip, ~100x120 pixels)
 *******************************************************************************/
typedef struct {
    uint8_t  data[THAI_ID_PHOTO_MAX_SIZE]; /* raw JPEG bytes */
    uint32_t size;                          /* actual JPEG size in bytes */
    bool     valid;                         /* true if JPEG header verified */
} thai_id_photo_t;

/*******************************************************************************
 * Init Stage Tracking (debug)
 *******************************************************************************/
enum {
    CCID_STAGE_NONE = 0,
    CCID_STAGE_USBH_INIT,
    CCID_STAGE_CCID_INIT,
    CCID_STAGE_NOTIF_REG,
    CCID_STAGE_PORT_POWER,
    CCID_STAGE_TASKS_CREATED,
    CCID_STAGE_RUNNING,
    CCID_STAGE_COMPLETE,
};

/*******************************************************************************
 * Smart Card Reader State (volatile — updated from USB callbacks)
 *******************************************************************************/
typedef struct {
    volatile uint8_t  connected;        /* ACR39U detected on USB */
    volatile uint8_t  card_present;     /* Card inserted in slot */
    volatile uint8_t  card_read_ok;     /* Data read successfully */
    volatile uint8_t  reading;          /* Currently reading card */
    volatile uint8_t  usb_init_done;    /* USBH_Init() completed */
    volatile uint8_t  init_stage;       /* CCID_STAGE_* debug */
    volatile uint16_t vid;
    volatile uint16_t pid;
    thai_id_data_t    card_data;        /* Latest card data (UTF-8) */
    thai_id_photo_t   card_photo;       /* Latest card photo (JPEG) */
    uint8_t           atr[33];          /* Answer-to-Reset */
    uint8_t           atr_len;
    volatile uint16_t read_count;       /* Successful reads */
    volatile uint16_t error_count;      /* Failed reads */
    char              error_msg[64];    /* Last error description */
} smartcard_state_t;

/*******************************************************************************
 * Public API (called from CM55 context)
 *******************************************************************************/

/**
 * Request USB CCID initialization (creates one-shot init task).
 * Safe to call multiple times — second call is a no-op.
 * Must be called from FreeRTOS task context (post-scheduler).
 */
void usb_ccid_smartcard_request_init(void);

/**
 * Get pointer to current smart card state (read-only).
 * State is updated from USB callbacks — use volatile fields.
 * Safe to call from any CM55 task (LVGL render, IPC handler).
 */
const smartcard_state_t *usb_ccid_smartcard_get_state(void);

/**
 * Trigger a card read. Non-blocking — sets reading=1,
 * results appear in card_data when card_read_ok becomes 1.
 */
void usb_ccid_smartcard_trigger_read(void);

#endif /* USB_CCID_SMARTCARD_H */
