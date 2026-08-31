# libbento_mpy.a — mpy_secure

TACP wire protocol and WiFi credential management, as used by the MicroPython layer. Prebuilt static archive.

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
BENTO_DIST := /abs/path/to/dist/mpy_secure
INCLUDES += $(BENTO_DIST)/include
LDLIBS   += $(BENTO_DIST)/COMPONENT_CM33/COMPONENT_SOFTFP/TOOLCHAIN_GCC_ARM/libbento_mpy.a
```

`LDLIBS`, not `LDFLAGS` — an archive placed before the objects that need it
is scanned once, finds nothing undefined yet, and is dropped.


## Two different lists — do not confuse them

**`consumer_must_provide.txt`** — symbols the archive references and does
**not** define at all. Your link fails without them. This build has
139:

    Cy_IPC_Pipe_RegisterCallback
    Cy_IPC_Pipe_SendMessage
    Cy_SCB_Write
    Cy_SysLib_DelayUs
    DEBUG_UART_hal_obj
    bentoclaw_tacp_dispatch
    cy_http_client_connect
    cy_http_client_create
    cy_http_client_delete
    cy_http_client_disconnect
    cy_http_client_init
    cy_http_client_send
    cy_http_client_write_header
    g_is_safe_boot
    gc_collect
    m_free
    m_malloc
    m_malloc_maybe
    memcpy
    memset
    mp_arg_parse_all
    mp_call_function_0
    mp_call_function_n_kw
    mp_compile
    mp_const_empty_dict_obj
    mp_get_buffer
    mp_hal_delay_ms
    mp_hal_ticks_ms
    mp_interrupt_char
    mp_lexer_new_from_str_len
    mp_load_global
    mp_obj_dict_store
    mp_obj_get_array
    mp_obj_get_int
    mp_obj_get_type_str
    mp_obj_is_callable
    mp_obj_is_true
    mp_obj_list_append
    mp_obj_new_bytes
    mp_obj_new_dict
    mp_obj_new_exception
    mp_obj_new_float
    mp_obj_new_int
    mp_obj_new_int_from_uint
    mp_obj_new_int_from_ull
    mp_obj_new_list
    mp_obj_new_str
    mp_obj_new_tuple
    mp_obj_print_exception
    mp_obj_str_get_str
    mp_parse
    mp_plat_print
    mp_printf
    mp_raise_TypeError
    mp_raise_ValueError
    mp_raise_msg
    mp_raise_msg_varg
    mp_sched_exception
    mp_sched_keyboard_interrupt
    mp_sched_schedule
    mp_state_ctx
    mp_store_global
    mp_stream_close
    mp_stream_rw
    mp_type_IndexError
    mp_type_OSError
    mp_type_SystemExit
    mp_type_ValueError
    mp_type_dict
    mp_type_float
    mp_type_fun_builtin_0
    mp_type_fun_builtin_1
    mp_type_fun_builtin_2
    mp_type_fun_builtin_var
    mp_type_module
    mp_vfs_open
    mp_vfs_remove
    mp_vfs_rename
    mpy_request_delete_main_py
    mpy_request_safe_boot_once
    mtb_hal_system_delay_ms
    mtb_hal_uart_readable
    nlr_jump
    nlr_pop
    nlr_push
    optiga_crypt_create
    optiga_crypt_destroy
    optiga_crypt_ecc_generate_keypair
    optiga_crypt_ecdh
    optiga_crypt_ecdsa_sign
    optiga_crypt_hash
    optiga_crypt_hkdf
    optiga_crypt_hmac
    optiga_crypt_random
    optiga_crypt_symmetric_decrypt
    optiga_crypt_symmetric_encrypt
    optiga_crypt_symmetric_generate_key
    optiga_generate_csr_pem
    optiga_generate_device_keypair
    optiga_manager_init
    optiga_manager_lock
    optiga_manager_touch_hold_reason
    optiga_manager_touch_release
    optiga_manager_unlock
    optiga_slot_info_raw
    optiga_trust_close_application
    optiga_trust_open_application
    optiga_util_callback
    optiga_util_create
    optiga_util_destroy
    optiga_util_read_data
    optiga_util_read_metadata
    optiga_util_update_count
    optiga_util_write_data
    optiga_util_write_metadata
    optiga_verify_cert_key_pair
    printf
    puts
    sensor_auto_is_delete_pending
    sensor_auto_is_restart_pending
    snprintf
    sprintf
    strcmp
    strlen
    strncpy
    tesaiot_config_get
    tesaiot_run_protected_update_isolated_test
    ui_notify_ide_connected
    ui_show_deploy_screen
    vQueueDelete
    vTaskDelay
    vTaskSuspendAll
    wifi_creds_flush_if_dirty
    xQueueGenericCreate
    xQueueGenericSendFromISR
    xQueueReceive
    xTaskCreate
    xTaskGetTickCount
    xTaskResumeAll

**`overridable.txt`** — symbols the archive defines **weakly**. The archive
links and runs without you doing anything; the stubs simply do nothing useful.
Define your own and the linker prefers yours:

| Symbol |
|---|
| `optiga_verify_staged_model` |

Leaving a weak stub in place is legal and silent — the call succeeds and does nothing. That is the failure mode to watch for.

## API

`api.txt` is the public exported set, read off the shipped binary rather than
maintained by hand. ~150 internal names are renamed to `bx_N` and hidden. A
handful of `bx_N` remain ELF-global because one object in the archive calls
another; they are excluded from `api.txt` and are not callable API.

Some exported symbols are declared in no header at all — each caller in the BENTO tree wrote its own `extern`. `include/bento_secure_undeclared.h` carries the real declarations, recovered from that calling code.

## What this does not hide

`objdump -d` disassembles it, and 16 format strings survive in
`.rodata` as plain text. Obfuscation raises the cost of extraction; it does
not prevent it, and nothing in the firmware gates use behind it: the
OPTIGA-UID licence check is compiled into no core image — `tesaiot_license.c`
is `CY_IGNORE`d by both `proj_cm33_ns/Makefile:87` and
`proj_cm55/Makefile:179-184`, and `tesaiot_is_licensed` is in none of the three
Release ELFs (`arm-none-eabi-nm`, 2026-08-29). Use is restricted by the licence
agreement, which is a contractual boundary, not an enforced one.
