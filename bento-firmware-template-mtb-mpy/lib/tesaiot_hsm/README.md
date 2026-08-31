# libbento_hsm.a — tesaiot_hsm

OPTIGA Trust M enrolment: device keypair, CSR and the isolated Protected Update path. Prebuilt static archive.

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
BENTO_DIST := /abs/path/to/dist/tesaiot_hsm
INCLUDES += $(BENTO_DIST)/include
LDLIBS   += $(BENTO_DIST)/COMPONENT_CM33/COMPONENT_SOFTFP/TOOLCHAIN_GCC_ARM/libbento_hsm.a
```

`LDLIBS`, not `LDFLAGS` — an archive placed before the objects that need it
is scanned once, finds nothing undefined yet, and is dropped.


## Two different lists — do not confuse them

**`consumer_must_provide.txt`** — symbols the archive references and does
**not** define at all. Your link fails without them. This build has
71:

    _impure_ptr
    certificate_sync_success
    certificate_upload_success
    check_certificate_response_received
    check_certificate_response_semaphore
    ctime
    cy_mqtt_publish
    fflush
    free
    g_protected_update_just_completed
    ipc_hsm_touch_pause
    ipc_hsm_touch_pause_reason
    ipc_hsm_touch_resume
    malloc
    mbedtls_base64_decode
    mbedtls_base64_encode
    memcpy
    memset
    mqtt_connection
    mqtt_device_id
    optiga_check_certificate_validity
    optiga_crypt_create
    optiga_crypt_destroy
    optiga_crypt_random
    optiga_generate_csr_pem
    optiga_generate_device_keypair
    optiga_read_factory_uid
    optiga_slot_info
    optiga_util_close_application
    optiga_util_create
    optiga_util_destroy
    optiga_util_open_application
    optiga_util_protected_update_final
    optiga_util_protected_update_start
    optiga_util_read_data
    optiga_util_read_metadata
    platform_has_certificate
    printf
    publisher_task_q
    putchar
    puts
    pvPortMalloc
    rand
    scanf
    snprintf
    strcpy
    strlen
    strncpy
    sync_certificate_response_received
    sync_certificate_response_semaphore
    tesaiot_is_licensed
    tesaiot_mqtt_client_id
    tesaiot_mqtt_username
    tesaiot_read_data
    tesaiot_read_lcso
    tesaiot_read_metadata
    upload_certificate_response_received
    upload_certificate_response_semaphore
    vPortEnterCritical
    vPortExitCritical
    vPortFree
    vQueueDelete
    vTaskDelay
    xQueueCreateMutex
    xQueueGenericCreate
    xQueueGenericReset
    xQueueGenericSend
    xQueueReceive
    xQueueSemaphoreTake
    xTaskGetCurrentTaskHandle
    xTaskGetTickCount

**`overridable.txt`** — symbols the archive defines **weakly**. The archive
links and runs without you doing anything; the stubs simply do nothing useful.
Define your own and the linker prefers yours:

| Symbol |
|---|

This archive defines no weak symbols, so there is nothing to override.

## API

`api.txt` is the public exported set, read off the shipped binary rather than
maintained by hand. ~150 internal names are renamed to `bx_N` and hidden. A
handful of `bx_N` remain ELF-global because one object in the archive calls
another; they are excluded from `api.txt` and are not callable API.



## What this does not hide

`objdump -d` disassembles it, and 66 format strings survive in
`.rodata` as plain text. Obfuscation raises the cost of extraction; it does
not prevent it, and nothing in the firmware gates use behind it: the
OPTIGA-UID licence check is compiled into no core image — `tesaiot_license.c`
is `CY_IGNORE`d by both `proj_cm33_ns/Makefile:87` and
`proj_cm55/Makefile:179-184`, and `tesaiot_is_licensed` is in none of the three
Release ELFs (`arm-none-eabi-nm`, 2026-08-29). Use is restricted by the licence
agreement, which is a contractual boundary, not an enforced one.
