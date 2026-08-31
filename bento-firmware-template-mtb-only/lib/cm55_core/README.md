# libbento_cm55.a — cm55_core

CM55 display bring-up, the model-link control plane and the HSM provisioning flow as operated. Prebuilt static archive.

## Verify before you use it

```bash
./verify.sh
```

## ABI — cm55 only

Built v8.1-M.mainline, FPv5-D16, **hardfp**, MVE, GCC_ARM. The two cores are not ABI-compatible; linking this into
the other core's image fails at the link step rather than at run time, which is
the good outcome.

## Add it to a ModusToolbox project

In `proj_cm55/Makefile`:

```make
BENTO_DIST := /abs/path/to/dist/cm55_core
INCLUDES += $(BENTO_DIST)/include
LDLIBS   += $(BENTO_DIST)/COMPONENT_CM55/COMPONENT_HARDFP/TOOLCHAIN_GCC_ARM/libbento_cm55.a
```

`LDLIBS`, not `LDFLAGS` — an archive placed before the objects that need it
is scanned once, finds nothing undefined yet, and is dropped.


## Two different lists — do not confuse them

**`consumer_must_provide.txt`** — symbols the archive references and does
**not** define at all. Your link fails without them. This build has
182:

    CYBSP_I2C_CAM_CONTROLLER_config
    Cy_GFXSS_Clear_DC_Interrupt
    Cy_GFXSS_Clear_GPU_Interrupt
    Cy_GFXSS_Init
    Cy_IPC_Pipe_RegisterCallback
    Cy_IPC_Pipe_SendMessage
    Cy_RTC_GetDateAndTime
    Cy_SCB_I2C_Init
    Cy_SCB_I2C_Interrupt
    Cy_SysInt_Init
    Cy_SysLib_DelayUs
    GFXSS_config
    ai_engine_active
    ai_engine_dq_ok
    ai_engine_feeds
    ai_engine_init
    ai_engine_init_calls
    ai_engine_init_returns
    ai_engine_inits
    ai_engine_last_init_rc
    ai_engine_mic_settle_pct
    ai_engine_mic_settling
    ai_engine_model
    ai_engine_model_count
    ai_engine_npu_cycles
    ai_engine_requested
    ai_engine_set_define
    ai_engine_set_members
    ai_engine_set_members_defined
    ai_engine_set_models
    ai_engine_set_name
    ai_engine_set_sensor_rate
    ai_engine_snapshot
    ai_engine_snapshot_model
    ai_engine_stack_free_words
    ai_engine_stack_words
    ai_engine_stale_drops
    ai_engine_start
    ai_engine_stop
    ai_engine_unload
    ai_engine_unload_done
    ai_engine_unload_refused
    ai_model_staged_count
    ai_model_staged_heap_free
    ai_model_staged_last_rc
    ai_model_staged_load
    ai_model_staged_rejects
    bento_audio_codec_init
    bento_audio_init
    bento_audio_set_source
    bento_audio_start
    bento_i2c_semaphore
    bento_sfx_init
    cm55_capsense_tick
    cm55_ipc_communication_setup
    cm55_sensor_poll_init
    cm55_sensor_poll_status
    cm55_sensor_poll_tick
    eTaskGetState
    frame_buffer1
    gfx_context
    ipc_bentoclaw_init
    ipc_lcd_init
    ipc_sensorhub_init
    ipc_service_init
    ipc_ui_init
    lv_async_call
    lv_bar_create
    lv_bar_set_range
    lv_bar_set_value
    lv_button_create
    lv_color_hex
    lv_dropdown_create
    lv_dropdown_get_list
    lv_dropdown_get_selected
    lv_dropdown_set_options
    lv_dropdown_set_selected
    lv_event_get_user_data
    lv_font_montserrat_14
    lv_font_montserrat_16
    lv_font_montserrat_20
    lv_font_montserrat_24
    lv_font_montserrat_28
    lv_init
    lv_label_create
    lv_label_get_text
    lv_label_set_long_mode
    lv_label_set_text
    lv_label_set_text_fmt
    lv_label_set_text_static
    lv_obj_add_event_cb
    lv_obj_add_flag
    lv_obj_add_state
    lv_obj_center
    lv_obj_clean
    lv_obj_create
    lv_obj_delete
    lv_obj_get_parent
    lv_obj_get_user_data
    lv_obj_remove_flag
    lv_obj_remove_state
    lv_obj_remove_style_all
    lv_obj_set_flex_align
    lv_obj_set_flex_flow
    lv_obj_set_flex_grow
    lv_obj_set_height
    lv_obj_set_pos
    lv_obj_set_size
    lv_obj_set_style_bg_color
    lv_obj_set_style_bg_opa
    lv_obj_set_style_border_color
    lv_obj_set_style_border_width
    lv_obj_set_style_opa
    lv_obj_set_style_pad_bottom
    lv_obj_set_style_pad_left
    lv_obj_set_style_pad_right
    lv_obj_set_style_pad_row
    lv_obj_set_style_pad_top
    lv_obj_set_style_radius
    lv_obj_set_style_shadow_width
    lv_obj_set_style_text_align
    lv_obj_set_style_text_color
    lv_obj_set_style_text_font
    lv_obj_set_style_text_line_space
    lv_obj_set_user_data
    lv_obj_set_width
    lv_port_disp_check_flush_timeout
    lv_port_disp_flush_ready
    lv_port_disp_get_flush_ready_count
    lv_port_disp_get_flush_start_count
    lv_port_disp_get_flush_timeout_count
    lv_port_disp_init
    lv_port_indev_init
    lv_screen_active
    lv_snprintf
    lv_strcmp
    lv_strncpy
    lv_timer_create
    lv_timer_delete
    lv_timer_handler
    memcpy
    memset
    mktime
    mtb_disp_waveshare_4p3_dsi_config
    mtb_disp_waveshare_4p3_init
    mtb_ml_get_init_state
    pdm_frames_dropped
    pm_create_page_with_header
    pm_get_instance
    sensorhub_ui_init
    snprintf
    strcmp
    strlen
    strncat
    strncmp
    strncpy
    tesaiot_col_create
    tesaiot_radar_loop_stats
    tesaiot_radar_recover_stats
    tesaiot_row_create
    ui_busy_modal_service
    ulTaskGenericNotifyTake
    uxTaskGetStackHighWaterMark
    vQueueDelete
    vTaskDelay
    vg_lite_IRQHandler
    vg_lite_close
    vg_lite_finish
    vg_lite_init
    vg_lite_init_mem
    ws_panel_power_up
    xQueueGenericCreate
    xQueueGenericSend
    xQueueGenericSendFromISR
    xQueueReceive
    xQueueSemaphoreTake
    xTaskAbortDelay
    xTaskCreate
    xTaskGenericNotifyFromISR
    xTaskGetTickCount
    xTimerCreate
    xTimerGenericCommand

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

Some exported symbols are declared in no header at all — each caller in the BENTO tree wrote its own `extern`. `include/bento_secure_undeclared.h` carries the real declarations, recovered from that calling code.

## What this does not hide

`objdump -d` disassembles it, and 9 format strings survive in
`.rodata` as plain text. Obfuscation raises the cost of extraction; it does
not prevent it, and nothing in the firmware gates use behind it: the
OPTIGA-UID licence check is compiled into no core image — `tesaiot_license.c`
is `CY_IGNORE`d by both `proj_cm33_ns/Makefile:87` and
`proj_cm55/Makefile:179-184`, and `tesaiot_is_licensed` is in none of the three
Release ELFs (`arm-none-eabi-nm`, 2026-08-29). Use is restricted by the licence
agreement, which is a contractual boundary, not an enforced one.
