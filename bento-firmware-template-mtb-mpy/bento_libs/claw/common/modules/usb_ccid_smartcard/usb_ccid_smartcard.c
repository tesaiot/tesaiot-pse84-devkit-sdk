/*******************************************************************************
 * File Name: usb_ccid_smartcard.c
 *
 * Description: USB CCID Smart Card driver for ACS ACR39U on CM55.
 *              Uses emUSB-Host CCID class driver to read Thai National ID Card.
 *              Co-exists with USB HID (Joystick F310) — both class drivers
 *              registered at init, only one physical device at a time.
 *
 *              Architecture mirrors usb_hid_joystick.c:
 *              - Static smartcard_state_t updated from USB callbacks
 *              - page_smart_card.c reads state directly (no IPC)
 *              - MicroPython accesses via IPC from CM33_NS
 *
 *******************************************************************************/

#include "usb_ccid_smartcard.h"
#include "USBH.h"
#include "USBH_CCID.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include <stdio.h>

/*******************************************************************************
 * Module State
 *******************************************************************************/
static smartcard_state_t s_sc_state;
static USBH_CCID_HANDLE s_ccid_handle = USBH_CCID_INVALID_HANDLE;
static USBH_NOTIFICATION_HOOK s_ccid_hook;
static volatile bool s_init_requested = false;

static volatile bool s_read_requested = false;

/*******************************************************************************
 * TIS-620 to UTF-8 Converter
 *
 * TIS-620 byte 0xA1-0xFB maps to Unicode U+0E01-U+0E5B (Thai characters).
 * Thai Unicode range U+0E00-U+0EFF encodes to 3-byte UTF-8:
 *   0xE0, 0xB8|0xB9, 0x80-0xBF
 *******************************************************************************/
static size_t tis620_to_utf8(const uint8_t *tis, size_t tis_len,
                              char *utf8, size_t utf8_size)
{
    size_t out = 0;
    for (size_t i = 0; i < tis_len && tis[i] != 0x00; i++) {
        uint8_t c = tis[i];

        if (c < 0x80) {
            /* ASCII pass-through */
            if (out + 1 >= utf8_size) break;
            utf8[out++] = (char)c;
        } else if (c >= 0xA1 && c <= 0xFB) {
            /* TIS-620 Thai character → Unicode U+0E01 + (c - 0xA1) */
            uint16_t unicode = 0x0E01 + (c - 0xA1);
            /* Unicode U+0800-U+FFFF → 3-byte UTF-8 */
            if (out + 3 >= utf8_size) break;
            utf8[out++] = (char)(0xE0 | ((unicode >> 12) & 0x0F));
            utf8[out++] = (char)(0x80 | ((unicode >> 6) & 0x3F));
            utf8[out++] = (char)(0x80 | (unicode & 0x3F));
        }
        /* else: skip invalid bytes (0x80-0xA0, 0xFC-0xFF) */
    }
    if (out < utf8_size) utf8[out] = '\0';
    return out;
}

/* Trim trailing spaces/nulls from ASCII field */
static void trim_ascii(char *s, size_t maxlen)
{
    size_t len = strnlen(s, maxlen);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '#')) {
        s[--len] = '\0';
    }
}

/* Strip '#' padding from card data.
 * Thai ID card raw APDU uses '#' (0x23) as field delimiter/padding:
 *   "นาย#วิรุฬห์##ศรีบริรักษ์" → "นาย วิรุฬห์ ศรีบริรักษ์"
 *   "Mr.#Wiroon##Sriborrirux"  → "Mr. Wiroon Sriborrirux"
 * Safe for UTF-8 strings (0x23 never appears in multi-byte sequences). */
static void strip_hash_padding(char *s)
{
    /* Replace '#' with space */
    for (char *p = s; *p; p++) {
        if (*p == '#') *p = ' ';
    }
    /* Collapse multiple consecutive spaces into one */
    char *dst = s;
    const char *src = s;
    bool prev_space = false;
    while (*src) {
        if (*src == ' ') {
            if (!prev_space) { *dst++ = ' '; prev_space = true; }
            src++;
        } else {
            *dst++ = *src++;
            prev_space = false;
        }
    }
    *dst = '\0';
    /* Trim trailing space */
    size_t len = strlen(s);
    while (len > 0 && s[len - 1] == ' ') s[--len] = '\0';
}

/*******************************************************************************
 * APDU Helpers
 *******************************************************************************/

/* Build READ BINARY command: 80 B0 P1 P2 02 00 Le */
static void build_read_cmd(uint8_t *cmd, uint16_t offset, uint8_t length)
{
    cmd[0] = 0x80;                 /* CLA */
    cmd[1] = 0xB0;                 /* INS: READ BINARY */
    cmd[2] = (offset >> 8) & 0xFF; /* P1 */
    cmd[3] = offset & 0xFF;        /* P2 */
    cmd[4] = 0x02;                 /* Lc */
    cmd[5] = 0x00;                 /* Data byte 1 */
    cmd[6] = length;               /* Le */
}

/* Send APDU and check SW 90 00 */
static bool send_apdu(uint8_t *cmd, uint32_t cmd_len,
                      uint8_t *resp, uint32_t resp_size, uint32_t *resp_len)
{
    if (s_ccid_handle == USBH_CCID_INVALID_HANDLE) return false;

    *resp_len = resp_size;
    USBH_STATUS status = USBH_CCID_APDU(s_ccid_handle, 0,
                                          cmd_len, cmd,
                                          resp_len, resp);
    if (status != USBH_STATUS_SUCCESS) return false;

    /* Check status word (last 2 bytes) */
    if (*resp_len < 2) return false;
    uint8_t sw1 = resp[*resp_len - 2];
    uint8_t sw2 = resp[*resp_len - 1];

    if (sw1 == 0x90 && sw2 == 0x00) {
        *resp_len -= 2;  /* strip SW */
        return true;
    }

    /* Handle "more data" (61 xx) — ATR-based P2 for GET RESPONSE */
    if (sw1 == 0x61) {
        uint8_t p2_gr = 0x00;
        if (s_sc_state.atr_len >= 2 &&
            s_sc_state.atr[0] == 0x3B && s_sc_state.atr[1] == 0x67) {
            p2_gr = 0x01;
        }
        uint8_t get_resp[5] = { 0x00, 0xC0, 0x00, p2_gr, sw2 };
        uint32_t extra_len = resp_size;
        status = USBH_CCID_APDU(s_ccid_handle, 0,
                                  5, get_resp,
                                  &extra_len, resp);
        if (status == USBH_STATUS_SUCCESS && extra_len >= 2) {
            if (resp[extra_len - 2] == 0x90 && resp[extra_len - 1] == 0x00) {
                *resp_len = extra_len - 2;
                return true;
            }
        }
    }

    /* Handle "wrong Le" (6C xx) — retry with correct Le */
    if (sw1 == 0x6C) {
        cmd[cmd_len - 1] = sw2;  /* replace Le */
        *resp_len = resp_size;
        status = USBH_CCID_APDU(s_ccid_handle, 0,
                                  cmd_len, cmd,
                                  resp_len, resp);
        if (status == USBH_STATUS_SUCCESS && *resp_len >= 2) {
            if (resp[*resp_len - 2] == 0x90 && resp[*resp_len - 1] == 0x00) {
                *resp_len -= 2;
                return true;
            }
        }
    }

    return false;
}

/*******************************************************************************
 * Read Thai ID Card Data
 *******************************************************************************/
static bool read_thai_id(void)
{
    uint8_t cmd[16];
    uint8_t resp[300];
    uint32_t resp_len;
    thai_id_data_t *d = &s_sc_state.card_data;

    memset(d, 0, sizeof(*d));

    /* Step 1: Power on card + get ATR */
    uint32_t atr_len = sizeof(s_sc_state.atr);
    USBH_STATUS st = USBH_CCID_PowerOnATR(s_ccid_handle, 0,
                                            &atr_len, s_sc_state.atr);
    if (st != USBH_STATUS_SUCCESS) {
        snprintf(s_sc_state.error_msg, sizeof(s_sc_state.error_msg),
                 "PowerOn failed: 0x%04X", (unsigned)st);
        return false;
    }
    s_sc_state.atr_len = (uint8_t)atr_len;

    /* Step 2: SELECT Thai ID applet */
    uint8_t select_cmd[] = {
        0x00, 0xA4, 0x04, 0x00, 0x08,
        0xA0, 0x00, 0x00, 0x00, 0x54, 0x48, 0x00, 0x01
    };
    resp_len = sizeof(resp);
    if (!send_apdu(select_cmd, sizeof(select_cmd), resp, sizeof(resp), &resp_len)) {
        snprintf(s_sc_state.error_msg, sizeof(s_sc_state.error_msg),
                 "SELECT applet failed");
        USBH_CCID_PowerOff(s_ccid_handle, 0);
        return false;
    }

    /* Step 3: Read each field */
    bool ok = true;

    /* CID (13 bytes ASCII) */
    build_read_cmd(cmd, THAI_ID_CID_OFFSET, THAI_ID_CID_LEN);
    if (send_apdu(cmd, 7, resp, sizeof(resp), &resp_len) && resp_len >= THAI_ID_CID_LEN) {
        memcpy(d->citizen_id, resp, 13);
        d->citizen_id[13] = '\0';
        trim_ascii(d->citizen_id, 13);
    } else {
        ok = false;
    }

    /* Name (Thai, TIS-620 → UTF-8) */
    build_read_cmd(cmd, THAI_ID_NAME_TH_OFFSET, THAI_ID_NAME_TH_LEN);
    if (send_apdu(cmd, 7, resp, sizeof(resp), &resp_len)) {
        tis620_to_utf8(resp, resp_len, d->name_th, sizeof(d->name_th));
        strip_hash_padding(d->name_th);
    } else {
        ok = false;
    }

    /* Name (English, ASCII) */
    build_read_cmd(cmd, THAI_ID_NAME_EN_OFFSET, THAI_ID_NAME_EN_LEN);
    if (send_apdu(cmd, 7, resp, sizeof(resp), &resp_len)) {
        size_t copy = (resp_len < 100) ? resp_len : 100;
        memcpy(d->name_en, resp, copy);
        d->name_en[copy] = '\0';
        trim_ascii(d->name_en, 100);
        strip_hash_padding(d->name_en);
    } else {
        ok = false;
    }

    /* Birthdate (8 bytes ASCII YYYYMMDD) */
    build_read_cmd(cmd, THAI_ID_BIRTH_OFFSET, THAI_ID_BIRTH_LEN);
    if (send_apdu(cmd, 7, resp, sizeof(resp), &resp_len) && resp_len >= 8) {
        memcpy(d->birthdate, resp, 8);
        d->birthdate[8] = '\0';
    }

    /* Issue date */
    build_read_cmd(cmd, THAI_ID_ISSUE_OFFSET, THAI_ID_ISSUE_LEN);
    if (send_apdu(cmd, 7, resp, sizeof(resp), &resp_len) && resp_len >= 8) {
        memcpy(d->issue_date, resp, 8);
        d->issue_date[8] = '\0';
    }

    /* Expire date */
    build_read_cmd(cmd, THAI_ID_EXPIRE_OFFSET, THAI_ID_EXPIRE_LEN);
    if (send_apdu(cmd, 7, resp, sizeof(resp), &resp_len) && resp_len >= 8) {
        memcpy(d->expire_date, resp, 8);
        d->expire_date[8] = '\0';
    }

    /* Address (Thai, TIS-620 → UTF-8) */
    build_read_cmd(cmd, THAI_ID_ADDR_OFFSET, THAI_ID_ADDR_LEN);
    if (send_apdu(cmd, 7, resp, sizeof(resp), &resp_len)) {
        tis620_to_utf8(resp, resp_len, d->address_th, sizeof(d->address_th));
        strip_hash_padding(d->address_th);
    }

    d->valid = ok;
    return ok;
}

/*******************************************************************************
 * Raw Bulk APDU — bypasses emUSB-Host CCID protocol handler entirely
 *
 * Both USBH_CCID_APDU() and USBH_CCID_Cmd() hang permanently on photo
 * reads because emUSB-Host doesn't handle CCID Time Extension responses
 * (bmCommandStatus=0x02). When the card chip needs processing time for
 * binary data, the ACR39U reader sends a Time Extension message, but
 * emUSB-Host's internal bulk transfer loop doesn't handle it — it blocks
 * forever waiting for a "real" response.
 *
 * Solution: Use USBH_CCID_Write() / USBH_CCID_Read() for direct bulk
 * endpoint access. We build the CCID PC_to_RDR_XfrBlock message manually,
 * send it, then loop on bulk IN with explicit timeout to handle Time
 * Extension responses (re-read until we get success or failure).
 *
 * CCID message format (USB CCID 1.1 spec):
 *   PC_to_RDR_XfrBlock (0x6F): 10-byte header + APDU payload
 *   RDR_to_PC_DataBlock (0x80): 10-byte header + response data
 *   bStatus byte 7:6 = bmCommandStatus: 00=ok, 01=fail, 10=time extension
 *******************************************************************************/
/* CCID sequence counter — independent of SEGGER's internal bSeq used by
 * USBH_CCID_APDU(). This works because ACR39U (TPDU reader, single slot)
 * doesn't validate strict sequence continuity. bSeq is primarily used
 * for multi-slot multiplexing, which doesn't apply here. */
static uint8_t s_ccid_seq = 0;

static bool send_apdu_bulk(uint8_t *apdu, uint32_t apdu_len,
                            uint8_t *resp, uint32_t resp_size, uint32_t *resp_len,
                            uint32_t timeout_ms)
{
    if (s_ccid_handle == USBH_CCID_INVALID_HANDLE) return false;
    if (apdu_len > 261) return false;  /* Max APDU = 5 + 256 */

    /* Build PC_to_RDR_XfrBlock (10-byte header + APDU data) */
    uint8_t msg[271];  /* 10 + 261 max */
    msg[0] = 0x6F;                          /* bMessageType */
    msg[1] = (apdu_len)       & 0xFF;       /* dwLength (LE) */
    msg[2] = (apdu_len >> 8)  & 0xFF;
    msg[3] = 0;
    msg[4] = 0;
    msg[5] = 0;                             /* bSlot = 0 */
    uint8_t sent_seq = s_ccid_seq++;
    msg[6] = sent_seq;                      /* bSeq */
    msg[7] = 0;                             /* bBWI (0 = default) */
    msg[8] = 0;                             /* wLevelParameter lo */
    msg[9] = 0;                             /* wLevelParameter hi */
    memcpy(msg + 10, apdu, apdu_len);

    /* Send via bulk OUT */
    USBH_STATUS st = USBH_CCID_Write(s_ccid_handle, msg, 10 + apdu_len, timeout_ms);
    if (st != USBH_STATUS_SUCCESS) {
        *resp_len = 0;
        return false;
    }

    /* Read response — loop handles Time Extension (bmCommandStatus=0x02) */
    uint8_t rmsg[300 + 10];  /* 10-byte header + max 300 data */
    int wtx_count = 0;
    const int max_wtx = 60;  /* Max 60 time extensions (~60s total) */

    while (wtx_count < max_wtx) {
        uint32_t bytes_read = 0;
        st = USBH_CCID_Read(s_ccid_handle, rmsg, sizeof(rmsg),
                             &bytes_read, timeout_ms);

        if (st != USBH_STATUS_SUCCESS || bytes_read < 10) {
            *resp_len = 0;
            return false;
        }

        /* Parse CCID response header */
        uint32_t data_len = rmsg[1] | ((uint32_t)rmsg[2] << 8)
                          | ((uint32_t)rmsg[3] << 16) | ((uint32_t)rmsg[4] << 24);
        uint8_t resp_seq = rmsg[6];
        uint8_t bStatus = rmsg[7];

        /* Validate bSeq — discard stale responses from prior USBH_CCID_APDU calls */
        if (resp_seq != sent_seq) {
            /* bSeq mismatch — stale response, discard and retry */
            wtx_count++;
            continue;  /* Discard and re-read */
        }

        /* bmCommandStatus in bits 7:6 */
        uint8_t cmd_status = (bStatus >> 6) & 0x03;

        if (cmd_status == 0x02) {
            /* Time Extension — card needs more processing time */
            wtx_count++;
            continue;  /* Re-read bulk IN */
        }

        if (cmd_status == 0x01) {
            /* Command failed */
            *resp_len = 0;
            return false;
        }

        /* Success (cmd_status == 0x00) */
        if (data_len > resp_size) data_len = resp_size;
        if (data_len > bytes_read - 10) data_len = bytes_read - 10;
        memcpy(resp, rmsg + 10, data_len);
        *resp_len = data_len;
        return true;
    }

    /* Too many time extensions */
    *resp_len = 0;
    return false;
}

/* Send APDU via raw bulk and check SW 90 00, handle 61xx / 6Cxx */
static bool send_apdu_raw(uint8_t *cmd, uint32_t cmd_len,
                           uint8_t *resp, uint32_t resp_size, uint32_t *resp_len)
{
    /* Use 10-second timeout for photo reads (card chip processing time) */
    if (!send_apdu_bulk(cmd, cmd_len, resp, resp_size, resp_len, 10000))
        return false;

    if (*resp_len < 2) { *resp_len = 0; return false; }

    uint8_t sw1 = resp[*resp_len - 2];
    uint8_t sw2 = resp[*resp_len - 1];

    /* Success */
    if (sw1 == 0x90 && sw2 == 0x00) {
        *resp_len -= 2;
        return true;
    }

    /* More data available (61 xx) — GET RESPONSE
     * Some Thai ID cards (ATR starts 3B 67) require P2=0x01 for GET RESPONSE.
     * Reference: ninyawee/pythaiidcard library ATR-based detection */
    if (sw1 == 0x61) {
        uint8_t p2_gr = 0x00;
        if (s_sc_state.atr_len >= 2 &&
            s_sc_state.atr[0] == 0x3B && s_sc_state.atr[1] == 0x67) {
            p2_gr = 0x01;
        }
        uint8_t get_resp_cmd[5] = { 0x00, 0xC0, 0x00, p2_gr, sw2 };
        if (!send_apdu_bulk(get_resp_cmd, 5, resp, resp_size, resp_len, 10000))
            return false;
        if (*resp_len >= 2) {
            sw1 = resp[*resp_len - 2];
            sw2 = resp[*resp_len - 1];
            if (sw1 == 0x90 && sw2 == 0x00) {
                *resp_len -= 2;
                return true;
            }
        }
        *resp_len = 0;
        return false;
    }

    /* Wrong Le (6C xx) — retry with correct Le */
    if (sw1 == 0x6C) {
        cmd[cmd_len - 1] = sw2;
        if (!send_apdu_bulk(cmd, cmd_len, resp, resp_size, resp_len, 10000))
            return false;
        if (*resp_len >= 2) {
            sw1 = resp[*resp_len - 2];
            sw2 = resp[*resp_len - 1];
            if (sw1 == 0x90 && sw2 == 0x00) {
                *resp_len -= 2;
                return true;
            }
        }
        *resp_len = 0;
        return false;
    }

    *resp_len = 0;
    return false;
}

/*******************************************************************************
 * Read Photo (JPEG) from Thai ID Card — 20 Fixed Segments
 *
 * Thai ID card stores photo as 20 segments with pre-defined P1/P2 offsets:
 *   P1 increments 0x01→0x14, P2 decrements 0x7B→0x68, Le=0xFF (255 bytes)
 * This is NOT a linear offset — it's a card-specific addressing scheme
 * for the MOI (Ministry of Interior) applet.
 *
 * Each segment uses two-step T=0 protocol:
 *   1. READ BINARY (80 B0 P1 P2 02 00 FF) → card returns 61 FF
 *   2. GET RESPONSE (00 C0 00 00 FF) → card returns 255 data bytes + 90 00
 * The send_apdu_raw() handler manages the 61xx→GET RESPONSE automatically.
 *
 * Uses USBH_CCID_Cmd (low-level) to avoid SEGGER T=0 state machine hang.
 *
 * Reference: https://github.com/chakphanu/ThaiNationalIDCard/blob/master/APDU.md
 *******************************************************************************/

static bool read_thai_id_photo(void)
{
    uint8_t cmd[7];
    uint8_t resp[300];
    uint32_t resp_len;
    thai_id_photo_t *p = &s_sc_state.card_photo;

    memset(p, 0, sizeof(*p));

    /* Thai ID photo is a JPEG stored in an EF starting at byte offset 0x017B.
     * Read it in 255-byte chunks at increasing offsets until the JPEG EOI
     * marker (FF D9) appears, the card returns a short/!90-00 reply, or the
     * 16 KB cap is hit. A previous fixed 20-chunk (5100-byte) table truncated
     * larger photos, so jd_decomp() failed with JDR_INP (input exhausted).
     *
     * Each chunk is a T=0 two-step that must be driven explicitly:
     *   1. READ BINARY  (80 B0 Phi Plo 02 00 FF) -> card replies SW 61 xx
     *   2. GET RESPONSE (00 C0 00 00 xx)          -> bytes + 90 00
     * USBH_CCID_APDU returns the raw card reply incl. the trailing 2-byte SW. */
    USBH_CCID_SetTimeout(s_ccid_handle, 30000);  /* 30s per chunk */

    uint32_t total   = 0;
    uint32_t offset  = 0x017B;       /* photo EF data start */
    bool     got_eoi = false;
    int      chunk   = 0;

    while (total + 256 < THAI_ID_PHOTO_MAX_SIZE) {
        /* Step 1: READ BINARY at the current offset */
        cmd[0] = 0x80; cmd[1] = 0xB0;
        cmd[2] = (uint8_t)(offset >> 8);
        cmd[3] = (uint8_t)(offset & 0xFF);
        cmd[4] = 0x02; cmd[5] = 0x00; cmd[6] = 0xFF;
        resp_len = sizeof(resp);
        if (USBH_CCID_APDU(s_ccid_handle, 0, 7, cmd, &resp_len, resp)
                != USBH_STATUS_SUCCESS || resp_len < 2) {
            if (chunk == 0) {
                snprintf(s_sc_state.error_msg, sizeof(s_sc_state.error_msg),
                         "Photo: READ fail off=%04lX", (unsigned long)offset);
                return false;
            }
            break;
        }

        uint8_t sw1 = resp[resp_len - 2];
        uint8_t sw2 = resp[resp_len - 1];

        if (sw1 == 0x90 && sw2 == 0x00) {
            resp_len -= 2;                    /* data returned inline, strip SW */
        } else if (sw1 == 0x61) {
            /* Step 2: GET RESPONSE for the announced byte count (sw2) */
            uint8_t gr[5] = { 0x00, 0xC0, 0x00, 0x00, sw2 };
            resp_len = sizeof(resp);
            if (USBH_CCID_APDU(s_ccid_handle, 0, 5, gr, &resp_len, resp)
                    != USBH_STATUS_SUCCESS || resp_len < 2
                    || resp[resp_len - 2] != 0x90 || resp[resp_len - 1] != 0x00) {
                if (chunk == 0) {
                    snprintf(s_sc_state.error_msg, sizeof(s_sc_state.error_msg),
                             "Photo: GET RESP fail off=%04lX", (unsigned long)offset);
                    return false;
                }
                break;
            }
            resp_len -= 2;                    /* strip SW, keep data */
        } else {
            if (chunk == 0) {
                snprintf(s_sc_state.error_msg, sizeof(s_sc_state.error_msg),
                         "Photo: SW %02X%02X off=%04lX", sw1, sw2, (unsigned long)offset);
                return false;
            }
            break;  /* offset past end of EF */
        }

        if (resp_len == 0) break;

        /* Validate JPEG header on the very first chunk */
        if (total == 0 &&
            (resp_len < 3 || resp[0] != 0xFF || resp[1] != 0xD8 || resp[2] != 0xFF)) {
            snprintf(s_sc_state.error_msg, sizeof(s_sc_state.error_msg),
                     "Photo: not JPEG (%02X%02X%02X)",
                     resp[0], resp_len > 1 ? resp[1] : 0, resp_len > 2 ? resp[2] : 0);
            return false;
        }

        /* Append (clamped to buffer) */
        uint32_t old_total = total;
        uint32_t space = THAI_ID_PHOTO_MAX_SIZE - total;
        uint32_t copy  = resp_len < space ? resp_len : space;
        memcpy(p->data + total, resp, copy);
        total  += copy;
        offset += copy;
        chunk++;

        /* Scan for JPEG EOI (FF D9), including across the chunk boundary */
        for (uint32_t i = (old_total >= 1 ? old_total - 1 : 1); i < total; i++) {
            if (p->data[i - 1] == 0xFF && p->data[i] == 0xD9) {
                total = i + 1;
                got_eoi = true;
                break;
            }
        }
        if (got_eoi) break;
        if (copy < 255) break;            /* short read -> end of EF */

        vTaskDelay(pdMS_TO_TICKS(5));
    }

    USBH_CCID_SetTimeout(s_ccid_handle, 5000);

    if (total > 0) {
        p->size = total;
        p->valid = true;
        return true;
    }

    return false;
}

/*******************************************************************************
 * USB CCID Notification Callback (device add/remove)
 *
 * CRITICAL: This runs in emUSB-Host task context. Must be MINIMAL:
 * - No printf (CM55 has no UART — causes cross-core deadlock)
 * - No USBH_CCID_GetCSDesc (nested USB request = deadlock)
 * - No USBH_CCID_SetTimeout (deferred to poll task)
 * Pattern matches usb_hid_joystick.c _cbOnAddRemove: Open + GetDeviceInfo only.
 *******************************************************************************/
static volatile bool s_device_setup_needed = false;

static void _ccid_on_add_remove(void *pContext, U8 DevIndex,
                                 USBH_DEVICE_EVENT Event)
{
    (void)pContext;

    if (Event == USBH_DEVICE_EVENT_ADD) {
        USBH_CCID_HANDLE h = USBH_CCID_Open(DevIndex);
        if (h == USBH_CCID_INVALID_HANDLE) return;

        USBH_CCID_DEVICE_INFO info;
        if (USBH_CCID_GetDeviceInfo(h, &info) == USBH_STATUS_SUCCESS) {
            s_sc_state.vid = info.VendorId;
            s_sc_state.pid = info.ProductId;
            s_sc_state.connected = 1;
            s_ccid_handle = h;
            s_device_setup_needed = true;  /* Deferred to poll task */
            __DMB();
        } else {
            USBH_CCID_Close(h);
        }

    } else if (Event == USBH_DEVICE_EVENT_REMOVE) {
        s_sc_state.connected = 0;
        s_sc_state.card_present = 0;
        s_sc_state.card_read_ok = 0;
        s_sc_state.reading = 0;
        s_device_setup_needed = false;
        if (s_ccid_handle != USBH_CCID_INVALID_HANDLE) {
            USBH_CCID_Close(s_ccid_handle);
            s_ccid_handle = USBH_CCID_INVALID_HANDLE;
        }
        __DMB();
    }
}

/*******************************************************************************
 * Slot Change Callback (card insert/remove)
 *******************************************************************************/
static void _ccid_slot_change(void *pContext, U32 SlotState)
{
    (void)pContext;
    bool present = (SlotState & 0x01) != 0;  /* Slot 0 */
    s_sc_state.card_present = present ? 1 : 0;

    if (!present) {
        s_sc_state.card_read_ok = 0;
        memset(&s_sc_state.card_data, 0, sizeof(s_sc_state.card_data));
        memset(&s_sc_state.card_photo, 0, sizeof(s_sc_state.card_photo));
        s_sc_state.error_msg[0] = '\0';
    }
}

/*******************************************************************************
 * Smart Card Polling Task
 *******************************************************************************/
static void _smartcard_poll_task(void *arg)
{
    (void)arg;

    for (;;) {
        /* Deferred device setup — runs in task context (safe for USB requests) */
        if (s_device_setup_needed && s_ccid_handle != USBH_CCID_INVALID_HANDLE) {
            s_device_setup_needed = false;

            /* Set timeout (safe in task context, not in notification callback) */
            USBH_CCID_SetTimeout(s_ccid_handle, 5000);

            /* Register slot change callback */
            USBH_CCID_SetOnSlotChange(s_ccid_handle, _ccid_slot_change, NULL);

            /* Read CCID class-specific descriptor for diagnostics.
             * Store dwFeatures in error_msg for UI display (no printf on CM55). */
            {
                uint8_t cs_desc[54];
                unsigned desc_len = sizeof(cs_desc);
                if (USBH_CCID_GetCSDesc(s_ccid_handle, cs_desc, &desc_len)
                    == USBH_STATUS_SUCCESS && desc_len >= 48) {
                    uint32_t dwFeatures = cs_desc[40] | ((uint32_t)cs_desc[41] << 8)
                                        | ((uint32_t)cs_desc[42] << 16)
                                        | ((uint32_t)cs_desc[43] << 24);
                    /* Store for debug via UI (not printf) */
                    snprintf(s_sc_state.error_msg, sizeof(s_sc_state.error_msg),
                             "dwFeat:0x%08lX", (unsigned long)dwFeatures);
                }
            }
        }

        if (s_sc_state.connected && s_ccid_handle != USBH_CCID_INVALID_HANDLE) {
            /* Poll slot status */
            U16 slot_status = 0;
            USBH_STATUS st = USBH_CCID_GetSlotStatus(s_ccid_handle, 0, &slot_status);
            if (st == USBH_STATUS_SUCCESS) {
                /* ICC status in bits [1:0]: 0=present+active, 1=present+inactive, 2=not present */
                uint8_t icc = slot_status & 0x03;
                bool was_present = s_sc_state.card_present;
                s_sc_state.card_present = (icc <= 1) ? 1 : 0;

                /* Card removed — clear all data immediately */
                if (was_present && !s_sc_state.card_present) {
                    s_sc_state.card_read_ok = 0;
                    memset(&s_sc_state.card_data, 0, sizeof(s_sc_state.card_data));
                    memset(&s_sc_state.card_photo, 0, sizeof(s_sc_state.card_photo));
                    s_sc_state.error_msg[0] = '\0';
                }

                /* Auto-read on first card detection or manual trigger */
                if (s_sc_state.card_present && (!s_sc_state.card_read_ok || s_read_requested)) {
                    s_sc_state.reading = 1;
                    s_read_requested = false;
                    __DMB();

                    if (read_thai_id()) {
                        /* Text data available — show immediately */
                        s_sc_state.card_read_ok = 1;
                        s_sc_state.read_count++;
                        s_sc_state.error_msg[0] = '\0';
                        __DMB();

                        /* Photo read via raw bulk (bypasses emUSB CCID handler) */
                        if (!read_thai_id_photo()) {
                            /* Photo failed — non-fatal, text data already OK */
                        }
                    } else {
                        s_sc_state.card_read_ok = 0;
                        s_sc_state.error_count++;
                    }

                    /* Power off card after reading */
                    USBH_CCID_PowerOff(s_ccid_handle, 0);
                    s_sc_state.reading = 0;
                    __DMB();
                }
            }

            /* Slot change callback registered in deferred setup above */
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/*******************************************************************************
 * Init Task (one-shot, self-deleting)
 *******************************************************************************/
static void _ccid_init_task(void *arg)
{
    (void)arg;

    s_sc_state.init_stage = CCID_STAGE_CCID_INIT;

    /* CCID class driver init (HID already initialized by joystick) */
    USBH_STATUS st = USBH_CCID_Init();
    if (st != USBH_STATUS_SUCCESS && st != USBH_STATUS_ALREADY_ADDED) {
        s_sc_state.error_count++;
        snprintf(s_sc_state.error_msg, sizeof(s_sc_state.error_msg),
                 "CCID_Init fail: 0x%04X", (unsigned)st);
        vTaskDelete(NULL);
        return;
    }

    s_sc_state.init_stage = CCID_STAGE_NOTIF_REG;

    /* Register device notification */
    USBH_CCID_AddNotification(&s_ccid_hook, _ccid_on_add_remove, NULL);

    s_sc_state.init_stage = CCID_STAGE_RUNNING;
    s_sc_state.usb_init_done = 1;

    /* Create polling task */
    /* Stack needs 8KB+ for photo read (300-byte APDU buffer per iteration) */
    xTaskCreate(_smartcard_poll_task, "SC_Poll", 8192, NULL,
                configMAX_PRIORITIES - 4, NULL);

    s_sc_state.init_stage = CCID_STAGE_COMPLETE;

    /* Self-delete init task */
    vTaskDelete(NULL);
}

/*******************************************************************************
 * Public API
 *******************************************************************************/

void usb_ccid_smartcard_request_init(void)
{
    if (s_init_requested) return;
    s_init_requested = true;

    memset(&s_sc_state, 0, sizeof(s_sc_state));
    s_sc_state.init_stage = CCID_STAGE_NONE;

    xTaskCreate(_ccid_init_task, "CCID_Init",
                configMINIMAL_STACK_SIZE * 4, NULL,
                configMAX_PRIORITIES - 5, NULL);
}

const smartcard_state_t *usb_ccid_smartcard_get_state(void)
{
    return &s_sc_state;
}

void usb_ccid_smartcard_trigger_read(void)
{
    s_read_requested = true;
}
