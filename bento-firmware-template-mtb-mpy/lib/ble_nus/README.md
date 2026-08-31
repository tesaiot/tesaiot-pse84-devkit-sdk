# libbento_secure.a — ble_nus

BLE NUS agent carrying the Bento Buddy protocol. Prebuilt static archive.

## Verify before you use it

```bash
./verify.sh
```

## ABI — cm33 only

Built v8-M.mainline, FPv5-SP-D16, **softfp**, GCC_ARM. The two cores are not ABI-compatible; linking this into
the other core's image fails at the link step rather than at run time, which is
the good outcome.

## Add it to a ModusToolbox project

In `proj_cm33_ns/Makefile`:

```make
BENTO_DIST := /abs/path/to/dist/ble_nus
INCLUDES += $(BENTO_DIST)/include
LDLIBS   += $(BENTO_DIST)/COMPONENT_CM33/COMPONENT_SOFTFP/TOOLCHAIN_GCC_ARM/libbento_secure.a
```

`LDLIBS`, not `LDFLAGS` — an archive placed before the objects that need it
is scanned once, finds nothing undefined yet, and is dropped.

The archive needs the AIROC BT host stack. In the same Makefile, **before** any
`CY_IGNORE` block that tests it:

```make
ENABLE_PAGE_BENTO_BUDDY := 1
COMPONENTS += WICED_BLE RTOS_AWARE
```

Assign it with no trailing spaces. `FOO := 1   # note` gives the value `"1   "`
and every `ifeq ($(FOO),1)` downstream is silently false.
## Two different lists — do not confuse them

**`consumer_must_provide.txt`** — symbols the archive references and does
**not** define at all. Your link fails without them. This build has
61:

    Cy_IPC_Pipe_RegisterCallback
    Cy_IPC_Pipe_SendMessage
    atoll
    ble_nus_is_connected
    exec_python_capture
    exec_python_str_public
    mbedtls_md_hmac
    mbedtls_md_info_from_type
    mbedtls_sha256
    mbedtls_sha256_finish
    mbedtls_sha256_free
    mbedtls_sha256_init
    mbedtls_sha256_starts
    mbedtls_sha256_update
    memcmp
    memcpy
    memmove
    memset
    printf
    puts
    pvPortMalloc
    sensor_auto_push_ble_state
    snprintf
    strchr
    strcmp
    strcpy
    strlen
    strncmp
    strncpy
    strnlen
    strstr
    strtol
    strtoll
    vPortFree
    vTaskDelay
    vTaskDelete
    wiced_bt_ble_set_raw_advertisement_data
    wiced_bt_ble_set_raw_scan_response_data
    wiced_bt_dev_read_local_addr
    wiced_bt_gatt_db_init
    wiced_bt_gatt_disconnect
    wiced_bt_gatt_register
    wiced_bt_gatt_server_send_error_rsp
    wiced_bt_gatt_server_send_mtu_rsp
    wiced_bt_gatt_server_send_notification
    wiced_bt_gatt_server_send_read_by_type_rsp
    wiced_bt_gatt_server_send_read_handle_rsp
    wiced_bt_gatt_server_send_write_rsp
    wiced_bt_gattdb_getAttrValue
    wiced_bt_gattdb_local_read_data_by_type
    wiced_bt_stack_init
    wiced_bt_start_advertisements
    xQueueCreateMutex
    xQueueCreateMutexStatic
    xQueueGenericCreateStatic
    xQueueGenericSend
    xQueueReceive
    xQueueSemaphoreTake
    xTaskCreate
    xTaskCreateStatic
    xTaskGetTickCount

**`overridable.txt`** — symbols the archive defines **weakly**. The archive
links and runs without you doing anything; the stubs simply do nothing useful.
Define your own and the linker prefers yours:

| Symbol |
|---|
| `app_wifi_connect_direct` |
| `app_wifi_disconnect` |
| `app_wifi_get_ipv4` |
| `lfs_load_wifi_creds` |
| `lfs_save_wifi_creds` |

Leaving a weak stub in place is legal and silent — the call succeeds and does nothing. That is the failure mode to watch for.

## API

`api.txt` is the public exported set, read off the shipped binary rather than
maintained by hand. ~150 internal names are renamed to `bx_N` and hidden. A
handful of `bx_N` remain ELF-global because one object in the archive calls
another; they are excluded from `api.txt` and are not callable API.

Some exported symbols are declared in no header at all — each caller in the BENTO tree wrote its own `extern`. `include/bento_secure_undeclared.h` carries the real declarations, recovered from that calling code.

## What this does not hide

`objdump -d` disassembles it, and 56 format strings survive in
`.rodata` as plain text. Obfuscation raises the cost of extraction; it does
not prevent it, and nothing in the firmware gates use behind it: the
OPTIGA-UID licence check is compiled into no core image — `tesaiot_license.c`
is `CY_IGNORE`d by both `proj_cm33_ns/Makefile:87` and
`proj_cm55/Makefile:179-184`, and `tesaiot_is_licensed` is in none of the three
Release ELFs (`arm-none-eabi-nm`, 2026-08-29). Use is restricted by the licence
agreement, which is a contractual boundary, not an enforced one.
