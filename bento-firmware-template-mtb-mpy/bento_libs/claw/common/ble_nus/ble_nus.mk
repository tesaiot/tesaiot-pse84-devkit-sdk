# =============================================================================
# ble_nus.mk — Makefile inclusion for the BENTO BLE Nordic-UART-Service agent
# (GATT DB, JSON wire protocol, folder push, voice capture, FW hash/OTA,
# radio scheduler).
#
# Usage (directly, or via the repo-root lib.mk module expander):
#     include $(BENTO_COMMON)/ble_nus/ble_nus.mk
#
# The consuming project remains responsible for the AIROC BT stack itself
# (btstack / btstack-integration include paths, COMPONENTS+=WICED_BLE,
# getlibs guards) — that wiring is board/stack policy, not this module's.
# =============================================================================

BLE_NUS_DIR := $(BENTO_COMMON)/ble_nus

SOURCES+=$(BLE_NUS_DIR)/ble_nus.c
SOURCES+=$(BLE_NUS_DIR)/nus_gatt_db.c
SOURCES+=$(BLE_NUS_DIR)/nus_protocol.c
SOURCES+=$(BLE_NUS_DIR)/nus_commands.c
SOURCES+=$(BLE_NUS_DIR)/nus_events.c
SOURCES+=$(BLE_NUS_DIR)/nus_agent.c
SOURCES+=$(BLE_NUS_DIR)/sensor_stream.c
SOURCES+=$(BLE_NUS_DIR)/bento_devmode.c
SOURCES+=$(BLE_NUS_DIR)/ipc_bento_buddy_bridge.c
SOURCES+=$(BLE_NUS_DIR)/nus_base64.c
SOURCES+=$(BLE_NUS_DIR)/nus_folder_push.c
SOURCES+=$(BLE_NUS_DIR)/ble_nus_lazy.c
SOURCES+=$(BLE_NUS_DIR)/fw_hash.c
SOURCES+=$(BLE_NUS_DIR)/bento_fw.c
SOURCES+=$(BLE_NUS_DIR)/voice_capture.c
SOURCES+=$(BLE_NUS_DIR)/radio_scheduler.c
INCLUDES+=$(BLE_NUS_DIR)
