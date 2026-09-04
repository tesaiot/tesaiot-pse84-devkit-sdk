# BENTO Secure Library — SDK reference

Generated from the shipped headers and cross-checked against the shipped
binary: a function appears here only if `nm` found it in the archive.
Regenerate with `./bento-release.sh docs`.

| Header | Archive API | Open-source fns | What it is |
|---|---|---|---|
| [bento_devmode.h](bento_devmode.md) | 8 | 0 | Developer-mode unlock state for the Bento Desktop Buddy bridge. Gates bento.exec and other privi... |
| [bento_fw.h](bento_fw.md) | 4 | 0 | Bento Desktop Buddy firmware-auto-update handlers (SPEC §5.6). Owns the state machine for: bento... |
| [bento_kit.h](bento_kit.md) | 0 | 0 | configuration / constants |
| [bento_time.h](bento_time.md) | 0 | 2 | Wall-clock for the BentoClaw firmware. The PSE84 has no battery-backed RTC on the AI Kit / Eva K... |
| [ble_nus.h](ble_nus.md) | 8 | 2 | Nordic UART Service (NUS) transport for BENTO Bento Desktop Buddy. Runs on CM33_NS over AIROC CY... |
| [ble_nus_lazy.h](ble_nus_lazy.md) | 3 | 0 | Public interface for the deferred Bento Buddy BLE bring-up. See ble_nus_lazy.c for behavioural c... |
| [cycfg_connectivity_bt.h](cycfg_connectivity_bt.md) | 0 | 0 | Minimal stub of the Bluetooth Configurator output expected by cybsp_bt_config.c. Defines low-pow... |
| [fw_hash.h](fw_hash.md) | 4 | 0 | Boot-time SHA-256 of the active firmware image. Used by the Bento Desktop Buddy firmware-update ... |
| [nus_agent.h](nus_agent.md) | 6 | 0 | Desktop -> Device agent stream handlers for the LLM-side loop that runs inside Bento Desktop Bud... |
| [nus_base64.h](nus_base64.md) | 2 | 0 | Stateful base64 decoder for the Bento Desktop Buddy folder push protocol (Bento forked this prot... |
| [nus_commands.h](nus_commands.md) | 4 | 0 | Command dispatcher + ack/status emitter for NUS wire protocol. Separated from nus_protocol.c so ... |
| [nus_events.h](nus_events.md) | 4 | 0 | Device -> Desktop NUS event emitter. Separates device-originated frames (media / system commands... |
| [nus_folder_push.h](nus_folder_push.md) | 6 | 0 | Folder-push state machine for the Bento Desktop Buddy (Bento forked this protocol under its own ... |
| [nus_gatt_db.h](nus_gatt_db.md) | 0 | 0 | Nordic UART Service GATT database for the AIROC BTSTACK host. Exposes UUIDs and attribute handle... |
| [nus_protocol.h](nus_protocol.md) | 5 | 0 | Public API of the Bento Desktop Buddy NUS wire protocol layer. See TESAIoT_PLAN/2026-4/Claude_De... |
| [radio_scheduler.h](radio_scheduler.md) | 9 | 3 |  |
| [sensor_stream.h](sensor_stream.md) | 6 | 0 | Periodic sensor sampler for the Bento Desktop Buddy bridge. Emits `bento.sensor.data` NUS events... |
| [voice_capture.h](voice_capture.md) | 3 | 0 | Microphone capture → `bento.voice.chunk` NUS event emitter. STUB. The real I²S mic capture path ... |

**72 archive API functions across 18 headers**, plus 7 functions declared here whose implementation ships as open source in this package (yours to read and change). "Archive API" means `nm` found the symbol exported by the shipped binary; `api.txt` lists 87 exported symbols in total — the difference is data symbols and functions whose only declaration is in `bento_secure_undeclared.h`.
