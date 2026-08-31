# BENTO Firmware Stack, SDK reference

One section per shipped archive. A function appears only if nm
found it in the corresponding binary.

| Module | Core | Archive | Pages |
|---|---|---|---|
| [ble_nus](ble_nus/README.md) | cm33 | libbento_secure.a | 18 |
| [mpy_secure](mpy_secure/README.md) | cm33 | libbento_mpy.a | 8 |
| [ipc_core](ipc_core/README.md) | cm55 | libbento_ipc.a | 9 |
| [edge_ai](edge_ai/README.md) | cm55 | libbento_edge_ai.a | 4 |
| [tesaiot_hsm](tesaiot_hsm/README.md) | cm33 | libbento_hsm.a | 6 |
| [cm55_core](cm55_core/README.md) | cm55 | libbento_cm55.a | 6 |

Regenerate with ./bento-release.sh docs
