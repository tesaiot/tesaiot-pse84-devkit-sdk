/*******************************************************************************
 * File Name: nus_gatt_db.h
 *
 * Description: Nordic UART Service GATT database for the AIROC BTSTACK host.
 *              Exposes UUIDs and attribute handles consumed by
 *              wiced_bt_gatt_db_init() and the NUS write/notify paths.
 *
 *              Layout (mirrors the spec published by Nordic Semiconductor):
 *                GAP (0x1800):  Device Name, Appearance
 *                GATT (0x1801): Service Changed
 *                NUS (6E400001-...):
 *                   RX (6E400002, Write / Write-No-Response) — host -> device
 *                   TX (6E400003, Notify, CCCD)              — device -> host
 *
 *              Security: NONE. No attribute in this database carries an
 *              authentication or encryption requirement — no GATTDB_PERM_AUTH_*
 *              bit is set anywhere in nus_gatt_db.c. As declared there:
 *                 RX       GATTDB_PERM_WRITE_CMD | GATTDB_PERM_WRITE_REQ
 *                 TX       GATTDB_PERM_READABLE
 *                 TX CCCD  GATTDB_PERM_READABLE | GATTDB_PERM_WRITE_REQ
 *              An UNPAIRED peer can write RX and enable TX notifications.
 *              Insufficient Authentication is never returned. Advertising is
 *              open and the device accepts any central.
 *
 *              Pairing, if a central asks for it, is Just Works: the link is
 *              encrypted against passive sniffing but has NO MITM protection,
 *              and the bond is held in RAM only (lost on reboot).
 *
 *              Requiring LE Secure Connections on these characteristics is
 *              NOT YET IMPLEMENTED. The AUTH_* bits were removed deliberately;
 *              the reason is recorded in the comment above the RX declaration
 *              in nus_gatt_db.c.
 *
 *              What limits the exposure: none of this is built by default.
 *              The whole module is gated on ENABLE_PAGE_BENTO_BUDDY, which is
 *              0 in both shipped templates (proj_cm33_ns/Makefile, and set
 *              hard to 0 in bsps/TARGET_KIT_PSE84_AI/bsp_features.mk). The
 *              three prebuilt template images carry zero nus_/ble_nus_/
 *              wiced_bt_ symbols; the radio is not brought up at all. The
 *              statements above apply only once you turn the flag on.
 *
 *              Attribute handles are contiguous (0x01..0x14) because some
 *              AIROC stack versions reject gaps during GATT DB init.
 *
 *******************************************************************************/

#ifndef NUS_GATT_DB_H
#define NUS_GATT_DB_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 128-bit NUS UUIDs, little-endian byte order (BLE on-air format). */
extern const uint8_t NUS_UUID_SERVICE[16];
extern const uint8_t NUS_UUID_CHAR_RX[16];
extern const uint8_t NUS_UUID_CHAR_TX[16];

/*******************************************************************************
 * Attribute Handles — contiguous, 0x01..0x14.
 * Keep in lock-step with nus_gatt_database[] in nus_gatt_db.c.
 ******************************************************************************/
enum nus_attr_handle {
    /* GAP Service 0x1800 */
    HDLS_GAP                        = 0x01,
    HDLC_GAP_DEVICE_NAME            = 0x02,
    HDLC_GAP_DEVICE_NAME_VALUE      = 0x03,
    HDLC_GAP_APPEARANCE             = 0x04,
    HDLC_GAP_APPEARANCE_VALUE       = 0x05,

    /* GATT Service 0x1801 */
    HDLS_GATT                       = 0x06,
    HDLC_GATT_SERVICE_CHANGED       = 0x07,
    HDLC_GATT_SERVICE_CHANGED_VALUE = 0x08,
    HDLD_GATT_SERVICE_CHANGED_CCCD  = 0x09,

    /* Nordic UART Service */
    HDLS_NUS                        = 0x0A,
    HDLC_NUS_RX                     = 0x0B,   /* host -> device declaration */
    HDLC_NUS_RX_VALUE               = 0x0C,
    HDLC_NUS_TX                     = 0x0D,   /* device -> host declaration */
    HDLC_NUS_TX_VALUE               = 0x0E,
    HDLD_NUS_TX_CCCD                = 0x0F,   /* client config descriptor */

    HDLS_NUS_END                    = 0x0F,
};

/* BLE appearance code: 0x0540 = "Generic Remote Control" (closest match for
 * an approval companion). Replace when Anthropic publishes a buddy code. */
#define NUS_APPEARANCE  0x0540

/* Max NUS payload per frame: MTU (247) - ATT header (3). */
#define NUS_MAX_FRAME   244

/* Expose the byte-packed GATT database blob to wiced_bt_gatt_db_init(). */
extern const uint8_t  nus_gatt_database[];
extern const uint16_t nus_gatt_database_len;

#ifdef __cplusplus
}
#endif

#endif /* NUS_GATT_DB_H */
