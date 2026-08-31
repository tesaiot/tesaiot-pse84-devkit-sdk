# nus_gatt_db.h

Nordic UART Service GATT database for the AIROC BTSTACK host. Exposes UUIDs and attribute handles consumed by wiced_bt_gatt_db_init() and the NUS write/notify paths. Layout (mirrors the spec published by Nordic Semiconductor): GAP (0x1800):  Device Name, Appearance GATT (0x1801): Service Changed NUS (6E400001-...): RX (6E400002, Write / Write-No-Response) — host -> device TX (6E400003, Notify, CCCD)              — device -> host Security: NONE. No attribute in this database carries an authentication or encryption requirement — no GATTDB_PERM_AUTH_* bit is set anywhere in nus_gatt_db.c. As declared there: RX       GATTDB_PERM_WRITE_CMD | GATTDB_PERM_WRITE_REQ TX       GATTDB_PERM_READABLE TX CCCD  GATTDB_PERM_READABLE | GATTDB_PERM_WRITE_REQ An UNPAIRED peer can write RX and enable TX notifications. Insufficient Authentication is never returned. Advertising is open and the device accepts any central. Pairing, if a central asks for it, is Just Works: the link is encrypted against passive sniffing but has NO MITM protection, and the bond is held in RAM only (lost on reboot). Requiring LE Secure Connections on these characteristics is NOT YET IMPLEMENTED. The AUTH_* bits were removed deliberately; the reason is recorded in the comment above the RX declaration in nus_gatt_db.c. What limits the exposure: none of this is built by default. The whole module is gated on ENABLE_PAGE_BENTO_BUDDY, which is 0 in both shipped templates (proj_cm33_ns/Makefile, and set hard to 0 in bsps/TARGET_KIT_PSE84_AI/bsp_features.mk). The three prebuilt template images carry zero nus_/ble_nus_/ wiced_bt_ symbols; the radio is not brought up at all. The statements above apply only once you turn the flag on. Attribute handles are contiguous (0x01..0x14) because some AIROC stack versions reject gaps during GATT DB init.

> Configuration header — constants and macros only, no functions.

## Constants

| Name | Value |
|---|---|
| `NUS_GATT_DB_H` | `#include` |
| `NUS_APPEARANCE` | `0x0540` |
| `NUS_MAX_FRAME` | `244` |
