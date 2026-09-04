# libbento_ipc.a — ipc_core

Core IPC management of internal services: sensor hub, LCD, UI and service dispatch. Prebuilt static archive.

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
BENTO_DIST := /abs/path/to/dist/ipc_core
INCLUDES += $(BENTO_DIST)/include
LDLIBS   += $(BENTO_DIST)/COMPONENT_CM55/COMPONENT_HARDFP/TOOLCHAIN_GCC_ARM/libbento_ipc.a
```

`LDLIBS`, not `LDFLAGS` — an archive placed before the objects that need it
is scanned once, finds nothing undefined yet, and is dropped.


## Two different lists — do not confuse them

**`consumer_must_provide.txt`** — symbols the archive references and does
**not** define at all. Your link fails without them. This build has
302:

    Cy_IPC_Pipe_RegisterCallback
    Cy_SysLib_Delay
    _ctype_
    cm55_ipc_communication_setup
    cm55_sensor_poll_diag
    cosf
    cy_wcm_start_scan
    cy_wcm_stop_scan
    emoji_u1f534
    emoji_u1f7e1
    emoji_u1f7e2
    lv_arc_create
    lv_arc_get_value
    lv_arc_set_range
    lv_arc_set_value
    lv_bar_create
    lv_bar_get_value
    lv_bar_set_range
    lv_bar_set_value
    lv_button_create
    lv_buttonmatrix_create
    lv_buttonmatrix_get_button_text
    lv_buttonmatrix_get_selected_button
    lv_buttonmatrix_set_button_ctrl
    lv_buttonmatrix_set_map
    lv_calendar_add_header_arrow
    lv_calendar_add_header_dropdown
    lv_calendar_create
    lv_calendar_get_pressed_date
    lv_calendar_set_month_shown
    lv_calendar_set_today_date
    lv_canvas_create
    lv_canvas_set_draw_buf
    lv_chart_add_series
    lv_chart_create
    lv_chart_get_series_next
    lv_chart_set_axis_range
    lv_chart_set_next_value
    lv_chart_set_point_count
    lv_chart_set_type
    lv_checkbox_create
    lv_checkbox_set_text
    lv_color_hex
    lv_draw_buf_clear
    lv_draw_buf_create
    lv_draw_buf_destroy
    lv_dropdown_class
    lv_dropdown_create
    lv_dropdown_get_list
    lv_dropdown_get_options
    lv_dropdown_get_selected
    lv_dropdown_get_selected_str
    lv_dropdown_set_options
    lv_dropdown_set_selected
    lv_event_get_code
    lv_event_get_target
    lv_event_get_target_obj
    lv_event_get_user_data
    lv_event_stop_processing
    lv_font_montserrat_14
    lv_font_montserrat_16
    lv_font_montserrat_20
    lv_font_montserrat_24
    lv_font_montserrat_28
    lv_font_montserrat_36
    lv_font_noto_thai_14
    lv_font_noto_thai_16
    lv_font_noto_thai_20
    lv_font_noto_thai_24
    lv_font_noto_thai_28
    lv_font_unscii_8
    lv_free
    lv_image_create
    lv_image_get_transformed_height
    lv_image_get_transformed_width
    lv_image_set_inner_align
    lv_image_set_pivot
    lv_image_set_rotation
    lv_image_set_scale
    lv_image_set_src
    lv_imgfont_create
    lv_indev_active
    lv_indev_get_gesture_dir
    lv_keyboard_create
    lv_keyboard_def_event_cb
    lv_keyboard_get_mode
    lv_keyboard_get_textarea
    lv_keyboard_set_map
    lv_keyboard_set_mode
    lv_keyboard_set_textarea
    lv_label_class
    lv_label_create
    lv_label_get_text
    lv_label_set_text
    lv_led_create
    lv_led_off
    lv_led_on
    lv_led_set_brightness
    lv_led_set_color
    lv_line_create
    lv_line_set_points
    lv_list_add_button
    lv_list_add_text
    lv_list_create
    lv_malloc
    lv_menu_cont_create
    lv_menu_create
    lv_menu_get_cur_main_page
    lv_menu_get_main_header_back_button
    lv_menu_get_sidebar_header_back_button
    lv_menu_page_create
    lv_menu_section_create
    lv_menu_separator_create
    lv_menu_set_load_page_event
    lv_menu_set_mode_root_back_button
    lv_menu_set_page
    lv_menu_set_page_title
    lv_menu_set_sidebar_page
    lv_msgbox_add_close_button
    lv_msgbox_add_footer_button
    lv_msgbox_add_text
    lv_msgbox_add_title
    lv_msgbox_create
    lv_msgbox_get_content
    lv_msgbox_get_title
    lv_obj_add_event_cb
    lv_obj_add_flag
    lv_obj_add_state
    lv_obj_align
    lv_obj_center
    lv_obj_check_type
    lv_obj_clean
    lv_obj_create
    lv_obj_delete
    lv_obj_get_child
    lv_obj_get_child_count
    lv_obj_get_content_width
    lv_obj_get_height
    lv_obj_get_width
    lv_obj_has_flag
    lv_obj_has_state
    lv_obj_invalidate
    lv_obj_remove_event_cb
    lv_obj_remove_flag
    lv_obj_remove_state
    lv_obj_scroll_to_y
    lv_obj_send_event
    lv_obj_set_align
    lv_obj_set_height
    lv_obj_set_pos
    lv_obj_set_scroll_dir
    lv_obj_set_scrollbar_mode
    lv_obj_set_size
    lv_obj_set_style_anim_duration
    lv_obj_set_style_arc_color
    lv_obj_set_style_bg_color
    lv_obj_set_style_bg_opa
    lv_obj_set_style_border_color
    lv_obj_set_style_border_side
    lv_obj_set_style_border_width
    lv_obj_set_style_line_color
    lv_obj_set_style_line_rounded
    lv_obj_set_style_line_width
    lv_obj_set_style_opa
    lv_obj_set_style_pad_bottom
    lv_obj_set_style_pad_left
    lv_obj_set_style_pad_right
    lv_obj_set_style_pad_top
    lv_obj_set_style_radius
    lv_obj_set_style_text_align
    lv_obj_set_style_text_color
    lv_obj_set_style_text_font
    lv_obj_set_style_text_line_space
    lv_obj_set_width
    lv_obj_update_layout
    lv_pct
    lv_port_indev_disable_touch
    lv_port_indev_request_reinit
    lv_roller_class
    lv_roller_create
    lv_roller_get_options
    lv_roller_get_selected
    lv_roller_get_selected_str
    lv_roller_set_options
    lv_roller_set_selected
    lv_roller_set_visible_row_count
    lv_scale_create
    lv_scale_set_label_show
    lv_scale_set_line_needle_value
    lv_scale_set_major_tick_every
    lv_scale_set_mode
    lv_scale_set_range
    lv_scale_set_total_tick_count
    lv_screen_active
    lv_slider_create
    lv_slider_get_value
    lv_slider_set_range
    lv_slider_set_value
    lv_span_get_style
    lv_span_set_text
    lv_spangroup_add_span
    lv_spangroup_create
    lv_spangroup_delete_span
    lv_spangroup_get_child
    lv_spangroup_get_expand_height
    lv_spangroup_get_span_count
    lv_spangroup_refresh
    lv_spangroup_set_align
    lv_spangroup_set_mode
    lv_spangroup_set_overflow
    lv_spinbox_create
    lv_spinbox_get_value
    lv_spinbox_set_digit_format
    lv_spinbox_set_range
    lv_spinbox_set_value
    lv_spinner_create
    lv_strcmp
    lv_style_get_prop
    lv_style_register_prop
    lv_style_set_prop
    lv_style_set_text_color
    lv_style_set_text_decor
    lv_style_set_text_font
    lv_style_set_text_letter_space
    lv_switch_create
    lv_table_create
    lv_table_set_cell_value
    lv_table_set_column_count
    lv_table_set_column_width
    lv_table_set_row_count
    lv_tabview_add_tab
    lv_tabview_create
    lv_tabview_get_tab_active
    lv_tabview_set_active
    lv_tabview_set_tab_bar_size
    lv_textarea_add_char
    lv_textarea_class
    lv_textarea_create
    lv_textarea_delete_char
    lv_textarea_get_one_line
    lv_textarea_get_text
    lv_textarea_set_one_line
    lv_textarea_set_password_mode
    lv_textarea_set_text
    lv_tick_elaps
    lv_tick_get
    lv_tileview_add_tile
    lv_tileview_create
    lv_timer_create
    lv_timer_set_period
    lv_win_add_title
    lv_win_create
    lv_win_get_content
    memcpy
    memmove
    memset
    pm_current
    pm_get_instance
    radar_dsp_set_threshold_x10
    radar_dsp_snapshot
    sensorhub_ui_set_ide_connected
    sensorhub_ui_switch_to_uxui
    sinf
    snprintf
    strcmp
    strlen
    strncpy
    strnlen
    strstr
    strtoul
    tesaiot_radar_current_energy
    tesaiot_radar_initialized
    tesaiot_radar_presence_detected
    thai_to_pua
    ui_busy_modal_request
    ui_busy_modal_reset
    usb_ccid_smartcard_get_state
    usb_ccid_smartcard_request_init
    usb_ccid_smartcard_trigger_read
    usb_hid_joystick_get_state
    usb_hid_joystick_request_init
    uxQueueMessagesWaiting
    uxQueueMessagesWaitingFromISR
    vPortEnterCritical
    vPortExitCritical
    vQueueDelete
    vTaskDelay
    wifi_manager_connect
    wifi_manager_disconnect
    wifi_manager_get_ip
    wifi_manager_get_status
    wifi_manager_init
    wifi_manager_is_connected
    wifi_manager_last_error
    wifi_manager_start_softap
    xQueueGenericCreate
    xQueueGenericSend
    xQueueGenericSendFromISR
    xQueueReceive
    xQueueReceiveFromISR
    xQueueSemaphoreTake
    xTaskCreate

**`overridable.txt`** — symbols the archive defines **weakly**. The archive
links and runs without you doing anything; the stubs simply do nothing useful.
Define your own and the linker prefers yours:

| Symbol |
|---|
| `cm55_controls_snapshot` |
| `cm55_env_snapshot` |
| `game_sprite_create` |
| `game_sprite_lookup` |
| `game_sprite_set` |
| `ipc_ui_ext_clear_all` |
| `ipc_ui_ext_dispatch` |
| `ipc_ui_platform_diag` |

Leaving a weak stub in place is legal and silent — the call succeeds and does nothing. That is the failure mode to watch for.

## API

`api.txt` is the public exported set, read off the shipped binary rather than
maintained by hand. ~150 internal names are renamed to `bx_N` and hidden. A
handful of `bx_N` remain ELF-global because one object in the archive calls
another; they are excluded from `api.txt` and are not callable API.

Some exported symbols are declared in no header at all — each caller in the BENTO tree wrote its own `extern`. `include/bento_secure_undeclared.h` carries the real declarations, recovered from that calling code.

## What this does not hide

`objdump -d` disassembles it, and 2 format strings survive in
`.rodata` as plain text. Obfuscation raises the cost of extraction; it does
not prevent it, and nothing in the firmware gates use behind it: the
OPTIGA-UID licence check is compiled into no core image — `tesaiot_license.c`
is `CY_IGNORE`d by both `proj_cm33_ns/Makefile:87` and
`proj_cm55/Makefile:179-184`, and `tesaiot_is_licensed` is in none of the three
Release ELFs (`arm-none-eabi-nm`, 2026-08-29). Use is restricted by the licence
agreement, which is a contractual boundary, not an enforced one.
