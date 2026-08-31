# ตัวอย่างการเรียกใช้ TESAIoT SDK

เอกสารนี้เป็นสารบัญของตัวอย่างภาษา C ที่ครอบคลุม **ทุกฟังก์ชันสาธารณะ** ของ SDK
ฉบับภาษาอังกฤษอยู่ที่ [`README.en.md`](README.en.md)

> ตารางรายการตัวอย่างด้านล่างถูกสร้างอัตโนมัติจากตัวไฟล์ตัวอย่างเอง
> ด้วย `tools/gen_examples_table.py` จึงไม่มีทางคลาดเคลื่อนจากโค้ดจริง

---

## 1. หลักการออกแบบ

ตัวอย่างในโฟลเดอร์นี้ยึดหลักสี่ข้อ

- **จัดกลุ่มตามงานจริง ไม่ใช่หนึ่งไฟล์ต่อหนึ่งฟังก์ชัน**
  แต่ละไฟล์คืองานที่นักพัฒนาทำจริงตั้งแต่ต้นจนจบ เช่น "อ่านค่าเซนเซอร์แล้วแสดงบนจอ"
  หรือ "ขอใบรับรองด้วย CSR แล้วเปิด mTLS" ครบทั้ง `#include` ลำดับการเริ่มต้น
  และการจัดการข้อผิดพลาดอย่างตรงไปตรงมา คัดลอกไฟล์เดียวไปใช้ในโครงงานของตนได้ทันที
- **ฟังก์ชันที่ไม่มีงานจริงรองรับ ถูกรวมไว้ในไฟล์อ้างอิงเดียวต่อหนึ่งไลบรารี**
  ได้แก่ getter, ตัวตรวจสถานะ และตัวอ่านค่าต่าง ๆ ไฟล์ `ref_<module>.c`
  ระบุชัดเจนว่าเป็นรายการอ้างอิง ไม่ใช่งานที่ทำงานได้จริง พร้อมคำอธิบายว่า
  ควรเรียกเมื่อใดและคืนค่าอะไร แนวทางนี้ทำให้ไฟล์งานจริงไม่ถูกเติมให้ยาวเกินจำเป็น
  เพียงเพื่อให้ตัวเลขความครอบคลุมสวยงาม
- **ตัวอย่างที่เกี่ยวกับการแสดงผล ต้องวาดลงจอจริง**
  ตระกูล `ui_widget_*` คือ 30 จาก 59 สัญลักษณ์ของ `ipc_core` และเป็นฟังก์ชันแสดงผล
  การสาธิตผ่าน UART เพียงอย่างเดียวไม่ได้สอนสิ่งใด
- **ปิดไว้เป็นค่าเริ่มต้น** เฟิร์มแวร์ที่ส่งมอบจริงไม่เปลี่ยนแปลงจนกว่าจะเปิดใช้เอง

---

## 2. วิธีเปิดใช้งาน

ธงเดียวควบคุมทั้งสองแกนประมวลผล

```sh
make build ENABLE_PAGE_EXAMPLES=1
```

- **บนจอ** เมนูหลักจะมีการ์ด **SDK Examples** เพิ่มขึ้นมา แตะเพื่อเปิดรายการ
  ตัวอย่างทั้งหมด แตะรายการใดรายการหนึ่งเพื่อดูว่าสอนอะไร เรียก API ใดบ้าง
  และกดปุ่ม **Run this example** เพื่อรันจริงพร้อมแสดงค่าที่ SDK คืนกลับมา
- **บนคอนโซล** CM33_NS จะพิมพ์รายการตัวอย่างทั้งหมดของฝั่งตนเมื่อบูต
  หากต้องการรันรายการใดให้ระบุรหัสของรายการนั้น

```sh
make build ENABLE_PAGE_EXAMPLES=1 SDK_EXAMPLE_CM33=tesaiot_hsm/01_acquire_chip
```

ค่าเริ่มต้นคือ `ENABLE_PAGE_EXAMPLES=0` เมื่อปิดไว้ ทั้ง `examples/` และ
`modules/page-components/examples/` จะถูก `CY_IGNORE` ทั้งไดเรกทอรี
จึงไม่มีโค้ดส่วนนี้หลงเข้าไปในภาพเฟิร์มแวร์แม้แต่ไบต์เดียว

---

## 3. ตัวอย่างอยู่ที่แกนประมวลผลใด และเพราะเหตุใด

ไลบรารีทั้งหกไม่ได้อยู่บนแกนเดียวกัน ข้อเท็จจริงนี้ตรวจสอบได้จากตัวไฟล์ archive เอง

```sh
arm-none-eabi-readelf -A lib/edge_ai/COMPONENT_CM55/.../libbento_edge_ai.a
    Tag_CPU_name: "cortex-m55"      Tag_ABI_VFP_args: VFP registers
arm-none-eabi-readelf -A lib/ble_nus/COMPONENT_CM33/.../libbento_secure.a
    Tag_CPU_arch: v8-M.mainline     (ไม่มี Tag_ABI_VFP_args)
```

| ไลบรารี | แกน | ตัวอย่างอยู่ที่ |
|---|---|---|
| `edge_ai`, `cm55_core`, `ipc_core` | CM55 (hard-float) | `proj_cm55/examples/` |
| `ble_nus`, `mpy_secure`, `tesaiot_hsm` | CM33_NS (soft-float) | `proj_cm33_ns/examples/` |

ทั้งสองแกนมี ABI ต่างกัน การนำ archive ของแกนหนึ่งไปเชื่อมกับภาพของอีกแกนหนึ่ง
จะล้มเหลวตั้งแต่ขั้นตอน link ซึ่งเป็นผลลัพธ์ที่พึงประสงค์

หน้าจอ **SDK Examples** แสดงรายการของ **ทั้งหก** ไลบรารี รายการฝั่ง CM33
จะมีเครื่องหมายกำกับและแสดงคำสั่งที่ใช้รันแทนปุ่มรัน เพราะคำถามที่ว่า
"เรียกฟังก์ชันใดได้บ้าง" เป็นคำถามเกี่ยวกับ SDK ทั้งชุด ไม่ใช่เฉพาะแกนที่ขับจออยู่

---

## 4. ข้อจำกัดที่ต้องทราบ

- **`mpy_secure` มีเฉพาะใน variant `mtb-mpy`** แพ็กเกจ `mtb-only` ไม่มีไลบรารีนี้
  (`variant_excludes()` ใน `bento-release.sh` ตัด `lib/mpy_secure` ออก)
  ตัวอย่างของไลบรารีนี้จึงกำกับด้วย `variant=mtb-mpy` และไม่ถูกคอมไพล์ในอีก variant หนึ่ง
- **`ble_nus` ยังไม่สามารถ link ได้ใน template ที่ส่งมอบ** ตรวจสอบแล้วว่า
  `libbento_secure.a` ไม่ปรากฏใน `LDLIBS` ของ makefile ใดเลย และไฟล์
  `bento_libs/lib.mk` ที่ `proj_cm33_ns/Makefile` เรียกใช้เมื่อเปิด
  `ENABLE_PAGE_BENTO_BUDDY=1` ไม่มีอยู่จริงในโครงสร้าง
  ตัวอย่างของ `ble_nus` จึง **คอมไพล์ผ่าน** แต่ยังรันบนอุปกรณ์ไม่ได้จนกว่าจะแก้ไขส่วนนี้
- **สถานะวงจรชีวิตของ OPTIGA เปลี่ยนทางเดียว** ไม่มีตัวอย่างใดเขียน metadata tag `C0`
  หรือเลื่อนสถานะ `LcsO` การเลื่อนสถานะไม่สามารถย้อนกลับได้และการแฟลชใหม่ไม่ช่วย

---

## 5. กฎที่ตัวอย่างทุกไฟล์ยึดถือ

| ฝั่ง CM55 | ฝั่ง CM33_NS |
|---|---|
| ถูกเรียกจาก GFX task ภายใน LVGL event callback | ถูกเรียกจาก task ที่ priority `tskIDLE_PRIORITY + 1` |
| เรียก LVGL ได้ และเรียกได้เฉพาะที่นี่ | ใช้ `printf` ได้ เพราะ CM33_NS เป็นเจ้าของ UART console |
| **ห้ามบล็อก** เพราะจะทำให้จอค้าง | ห้าม `printf` ใน IPC callback เด็ดขาด (บริบท ISR) |
| **ห้าม `printf`** เพราะ CM55 ไม่มีคอนโซล ใช้ `sdk_example_logf()` แทน | รายงานค่าที่ได้รับจริง ไม่สร้างผลลัพธ์ปลอม |

ทุกไฟล์คืนรหัสผลลัพธ์ตามจริง — `SDK_EX_OK`, `SDK_EX_UNAVAILABLE`, `SDK_EX_BUSY`,
`SDK_EX_REFUSED`, `SDK_EX_NO_DATA`, `SDK_EX_STARTED` — หากฮาร์ดแวร์ไม่พร้อม
ตัวอย่างจะแจ้งตามจริง ไม่แสร้งว่าสำเร็จ

---

## 6. การตรวจสอบอัตโนมัติ

```sh
tools/examples_check.sh          # คอมไพล์ทุกไฟล์ + ตรวจความครอบคลุม API
tools/examples_check.sh --compile-only
tools/gen_examples_table.py      # สร้างตารางเมนูและตารางในเอกสารนี้ใหม่
tools/gen_examples_table.py --check
```

`examples_check.sh` วัดความครอบคลุมจาก **ตารางสัญลักษณ์ของไฟล์ object จริง**
ไม่ใช่จากการค้นข้อความในซอร์ส

- API ที่ตัวอย่าง **เรียกหรืออ่าน** จะปรากฏเป็นสัญลักษณ์ undefined (`U`)
- API ที่ตัวอย่าง **override** (weak symbol) จะปรากฏเป็นสัญลักษณ์ defined

การกล่าวถึงชื่อฟังก์ชันในคอมเมนต์ไม่ก่อให้เกิดสัญลักษณ์ใด จึงไม่นับเป็นความครอบคลุม
เมื่อมี API สาธารณะใหม่เพิ่มเข้ามาโดยไม่มีตัวอย่างรองรับ การตรวจสอบนี้จะล้มเหลวทันที

---

## 7. รายการตัวอย่างทั้งหมด

<!-- BEGIN generated: tools/gen_examples_table.py -->

### `display` — 11 ไฟล์, 65 API

| ตัวอย่าง | สอนอะไร | API ที่เรียก | รันที่ไหน |
|---|---|---|---|
| `cm55/display/00_display_bringup` — Bring the display up (and prove it came up) | tesaiot_display_init() owns its own task -- never create one yourself -- and g_tesaiot_display_diag is how you tell a working panel from a silent one | `tesaiot_display_init`, `tesaiot_display_task`, `rtos_cm55_gfx_task_handle`, `g_tesaiot_display_diag` | จอ (แตะที่เมนู) |
| `cm55/display/01_bringup` — Bring the CM55 IPC peers up in the right order | the exact init sequence a GFX task owes the IPC library, and how to read back what is already running | `ipc_sensorhub_init`, `ipc_service_init`, `ipc_lcd_init`, `ipc_ui_init`, `ui_widget_mgr_init`, `ui_widget_mgr_get_parent`, `ui_widget_mgr_needs_container`, `cm55_ipc_pipe_isr`, `cm55_ipc_pipe_ep_busy`, `cm55_ipc_pipe_drain_release` | จอ (แตะที่เมนู) |
| `cm55/display/02_sensor_dashboard` — Put the CM33 sensor snapshot on the display | read every sensor CM33_NS publishes, tell live data from stale, and keep a panel refreshing without blocking the GFX task | `ipc_sensorhub_snapshot`, `ipc_sensorhub_wifi_connected`, `ipc_sensorhub_ble_connected`, `ipc_sensorhub_ntp_synced`, `ipc_sensorhub_get_time_str`, `ipc_ui_set_container`, `ui_widget_mgr_create`, `ui_widget_mgr_set_text`, `ui_widget_mgr_get_object` | จอ (แตะที่เมนู) |
| `cm55/display/03_widget_gallery` — Build a widget gallery on the display | create ten widget types through one struct, then move, resize, recolour, hide and delete them by handle | `ui_widget_mgr_needs_container`, `ui_widget_mgr_get_parent`, `ui_widget_mgr_set_parent`, `ui_widget_mgr_clear_all`, `ui_widget_mgr_set_screen`, `ui_widget_mgr_create`, `ui_widget_mgr_set_text`, `ui_widget_mgr_set_value`, `ui_widget_mgr_set_position`, `ui_widget_mgr_set_size`, `ui_widget_mgr_set_color`, `ui_widget_mgr_set_visible`, `ui_widget_mgr_set_dotmatrix`, `ui_widget_mgr_set_image`, `ui_widget_mgr_delete`, `ui_widget_mgr_count`, `ui_widget_mgr_get_object` | จอ (แตะที่เมนู) |
| `cm55/display/04_live_chart` — Stream three signals into a live chart | add series to a chart, push samples one at a time, widen the time window, and stop feeding when the chart dies | `ui_widget_mgr_create`, `ui_widget_mgr_chart_add_series`, `ui_widget_mgr_chart_set_next`, `ui_widget_mgr_set_prop`, `ui_widget_mgr_get_object`, `ui_widget_mgr_set_text`, `ipc_sensorhub_snapshot` | จอ (แตะที่เมนู) |
| `cm55/display/05_collection_events` — Fill a table and a list, then read the taps back | append rows to collection widgets one item at a time, subscribe to input events, and drain the event ring | `ui_widget_mgr_create`, `ui_widget_mgr_item_add`, `ui_widget_mgr_item_clear`, `ui_widget_mgr_set_prop`, `ui_widget_mgr_event_push`, `ui_widget_mgr_event_drain`, `ui_widget_mgr_get_object`, `ui_widget_mgr_get_value`, `ui_widget_mgr_set_text` | จอ (แตะที่เมนู) |
| `cm55/display/06_console_panel` — Show the MicroPython console over your page | bind the lcd.print() terminal to a container, flip between console and widgets, and badge output that arrived while it was hidden | `ipc_lcd_set_container`, `ipc_lcd_toggle_panel`, `ipc_lcd_is_panel_visible`, `ipc_lcd_has_unread`, `ipc_lcd_clear_unread`, `ipc_lcd_reset_auto_nav`, `ui_widget_mgr_set_all_visible`, `ui_widget_mgr_create`, `ipc_ui_set_container` | จอ (แตะที่เมนู) |
| `cm55/display/07_override_hooks` — Override the library's weak hooks with your own | which seven symbols libbento_ipc leaves for you, their exact signatures, and how to tell whose definition the linker chose | `cm55_controls_snapshot`, `game_sprite_create`, `game_sprite_set`, `game_sprite_lookup`, `ipc_ui_ext_clear_all`, `ipc_ui_ext_dispatch`, `ipc_ui_platform_diag` | จอ (แตะที่เมนู) |
| `cm55/display/08_sprites` — Animate an image sprite | build an lv_image_dsc_t with real transparency, create a sprite through the handle table, and swap frames without churning the object | `ui_widget_mgr_create_sprite`, `ui_widget_mgr_set_sprite_image`, `ui_widget_mgr_set_position`, `ui_widget_mgr_get_object`, `ipc_ui_set_container` | จอ (แตะที่เมนู) |
| `cm55/display/ref_core` — Reference: the three symbols with no header | the archive exports three symbols that no shipped header declares -- what they are, the correct extern for each, and when you would want them | `calculate_idle_percentage`, `tesaiot_display_ready`, `disp_touch_i2c_controller_context`, `g_tesaiot_display_diag`, `rtos_cm55_gfx_task_handle` | จอ (แตะที่เมนู) |
| `cm55/display/ref_display` — Reference: the ipc_core getters and probes | what each remaining read-only call answers, and when you would ask it | `ui_widget_mgr_needs_container`, `ui_widget_mgr_get_parent`, `ui_widget_mgr_count`, `ui_widget_mgr_list`, `ipc_ui_input_activity`, `ipc_sensorhub_weather` | จอ (แตะที่เมนู) |

### `sensors` — 8 ไฟล์, 54 API

| ตัวอย่าง | สอนอะไร | API ที่เรียก | รันที่ไหน |
|---|---|---|---|
| `cm55/sensors/01_feed_sensor_hub` — Publish a locally-read sensor into the hub | the Eva Kit path - CM55 reads the bus, feeds the hub, and every consumer sees it; plus who owns the sequence number | `ipc_sensorhub_feed_bmi270`, `ipc_sensorhub_feed_bmm350`, `ipc_sensorhub_feed_capsense`, `ipc_sensorhub_feed_pot`, `ipc_sensorhub_snapshot` | จอ (แตะที่เมนู) |
| `cm55/sensors/02_radar_presence` — Read range and presence from the BGT60TR13C radar | the range snapshot is a seqlock and bin 0 is dead; the threshold call has a magic zero that re-learns the room; and the two stats calls that tell a stalled sensor from a stalled task | `radar_dsp_snapshot`, `radar_dsp_set_threshold_x10`, `radar_dsp_process`, `tesaiot_radar_task`, `tesaiot_radar_loop_stats`, `tesaiot_radar_recover_stats` | จอ (แตะที่เมนู) |
| `cm33/sensors/01_i2c_bus_scan` — Scan the sensor I2C bus | the bus has one mutex and several owners - take it, scan, give it back, and quiesce the auto task first so you are not fighting it | `sensor_i2c_is_init`, `sensor_i2c_lock`, `sensor_i2c_scan`, `sensor_i2c_unlock`, `sensor_auto_is_running`, `sensor_auto_stop`, `sensor_auto_start` | คอนโซล CM33_NS |
| `cm33/sensors/02_read_imu` — Read the BMI270 accelerometer and gyroscope | prove the wire before you trust the driver, the real init order, and one lock per SAMPLE rather than one per register | `bmi270_read_chip_id`, `bmi270_read_accel`, `bmi270_read_gyro`, `bmi270_read_temperature`, `sensor_i2c_lock`, `sensor_i2c_unlock`, `sensor_auto_is_running` | คอนโซล CM33_NS |
| `cm33/sensors/03_read_magnetometer` — Read the BMM350 magnetometer and calibrate the compass | a compass is useless until hard-iron calibration converges - how to drive that, how to know when it has, and why heading_from_xy beats a second bus read | `bmm350_read_chip_id`, `bmm350_read_xyz`, `bmm350_read_heading`, `bmm350_cal_update`, `bmm350_cal_get_offsets`, `bmm350_heading_from_xy`, `bmm350_cal_reset`, `sensor_i2c_lock`, `sensor_i2c_unlock`, `bmm350_reinit`, `bmm350_diagnose`, `bmm350_debug_read` | คอนโซล CM33_NS |
| `cm33/sensors/04_read_environment` — Read pressure, air temperature and humidity (AI Kit) | read_both() is not a convenience wrapper - it halves the bus traffic; and the two parts report two different temperatures | `dps368_read_product_id`, `dps368_read_both`, `dps368_read_pressure`, `dps368_read_temperature`, `sht40_read_serial`, `sht40_read_both`, `sht40_read_temperature`, `sht40_read_humidity`, `sensor_i2c_lock`, `sensor_i2c_unlock` | คอนโซล CM33_NS |
| `cm33/sensors/05_auto_push_task` — Drive the background sensor-push task | retune the publisher that feeds the display and MicroPython - mask, rate, the 50 ms cliff, the free cache - and put it back | `sensor_auto_is_running`, `sensor_auto_get_mask`, `sensor_auto_set_mask`, `sensor_auto_enable`, `sensor_auto_disable`, `sensor_auto_get_rate`, `sensor_auto_set_rate`, `sensor_auto_get_push_count`, `sensor_auto_stop`, `sensor_auto_start`, `sensor_auto_get_bmi270` | คอนโซล CM33_NS |
| `cm33/sensors/06_raw_register_access` — Talk to any device on the sensor bus directly | the escape hatch - register reads, a safe read-modify-write, and the command-response form for parts that have no registers at all | `sensor_i2c_read_byte`, `sensor_i2c_write_byte`, `sensor_i2c_read_reg`, `sensor_i2c_write_reg`, `sensor_i2c_write_raw`, `sensor_i2c_read_raw`, `sensor_i2c_is_init`, `sensor_i2c_lock`, `sensor_i2c_unlock` | คอนโซล CM33_NS |

### `io` — 7 ไฟล์, 39 API

| ตัวอย่าง | สอนอะไร | API ที่เรียก | รันที่ไหน |
|---|---|---|---|
| `cm55/io/01_rgb_matrix` — Draw on the DFR0522 16x8 RGB matrix | the panel is on the DISPLAY I2C bus, so it belongs to the GFX task and to no other; the eight draw ops, what each one costs on the wire, and which of them cancel an animation | `dfr0522_clear`, `dfr0522_fill`, `dfr0522_pixel`, `dfr0522_blit`, `dfr0522_score`, `dfr0522_bar`, `dfr0522_scroll`, `dfr0522_effect` | จอ (แตะที่เมนู) |
| `cm55/io/02_pots_and_capsense` — Read the four knobs and the CapSense pad from CM55 | why these two live on CM55 and not on the core MicroPython runs on, the knob-to-channel table that is NOT the identity, and the two tick rates the driver deliberately keeps apart | `cm55_sensor_poll_init`, `cm55_sensor_poll_tick`, `cm55_capsense_tick`, `cm55_sensor_poll_status`, `cm55_pot_read_all`, `cm55_sensor_poll_diag`, `ipc_sensorhub_snapshot` | จอ (แตะที่เมนู) |
| `cm55/io/03_usb_host_devices` — Read a USB gamepad, and a smart card, from the host port | why init is a REQUEST and never a call from the GFX task, how to tell a controller that is absent from one that is present and silent, and the two decode layers | `usb_hid_joystick_init`, `usb_hid_joystick_request_init`, `usb_hid_joystick_is_connected`, `usb_hid_joystick_get_state`, `f310_parse`, `f310_deadzone`, `usb_ccid_smartcard_request_init`, `usb_ccid_smartcard_get_state`, `usb_ccid_smartcard_trigger_read` | จอ (แตะที่เมนู) |
| `cm33/io/01_read_buttons` — Read the CapSense buttons | capture the idle baseline with fingers OFF the pad, poll the two buttons, and know which of the two CapSense backends your build linked — they read two different I2C buses | `capsense_init`, `capsense_read` | คอนโซล CM33_NS |
| `cm33/io/02_capsense_slider` — Read the CapSense slider position | the slider is 0..100 with no "not touched" value, so you need a release test and a hysteresis band before you drive anything with it | `capsense_init`, `capsense_read_slider` | คอนโซล CM33_NS |
| `cm33/io/03_read_potentiometers` — Read the potentiometer three ways | raw counts, percent and volts off one SAR channel — and which channel that actually is, which is not the one most people assume | `potentiometer_init`, `potentiometer_read_raw`, `potentiometer_read_percent`, `potentiometer_read_voltage` | คอนโซล CM33_NS |
| `cm33/io/04_gpio_led_button` — Drive the board LEDs and read the user button | the PDL calls behind gpio.led()/gpio.button(), the drive modes that make them work, and how to debounce a button without blocking | `Cy_GPIO_Pin_FastInit`, `Cy_GPIO_Set`, `Cy_GPIO_Clr`, `Cy_GPIO_Inv`, `Cy_GPIO_Write`, `Cy_GPIO_Read`, `CYBSP_USER_LED1_PORT`, `CYBSP_USER_BTN1_PORT` | คอนโซล CM33_NS |

### `edge_ai` — 10 ไฟล์, 48 API

| ตัวอย่าง | สอนอะไร | API ที่เรียก | รันที่ไหน |
|---|---|---|---|
| `cm55/edge_ai/01_first_inference` — Run your first inference | create the engine, activate one model, feed it at its training rate, read the verdict back | `ai_engine_init`, `ai_engine_model_count`, `ai_engine_model`, `ai_engine_set_sensor_rate`, `ai_engine_start`, `ai_engine_requested`, `ai_engine_active`, `ai_engine_snapshot`, `ai_engine_stop`, `ai_engine_resume_sensor` | จอ (แตะที่เมนู) |
| `cm55/edge_ai/02_model_registry` — Browse the model registry | enumerate every model the image carries, read its descriptor, and see how much run-time room is left | `ai_engine_model_count`, `ai_engine_model`, `ai_engine_dyn_count`, `ai_engine_dyn_capacity` | จอ (แตะที่เมนู) |
| `cm55/edge_ai/03_parallel_set_run` — Watch several models at once | start a parallel set, wait out the window fill, then read EACH member's verdict instead of the last one published | `ai_engine_start`, `ai_engine_set_name`, `ai_engine_set_members`, `ai_engine_mic_settling`, `ai_engine_mic_settle_pct`, `ai_engine_snapshot_model`, `ai_engine_active`, `ai_engine_stop`, `ai_engine_model` | จอ (แตะที่เมนู) |
| `cm55/edge_ai/04_set_membership` — Redefine what a set contains | read a set's compiled membership, replace it at run time, read the override back, and put the original back | `ai_engine_set_name`, `ai_engine_set_models`, `ai_engine_set_define`, `ai_engine_set_members_defined`, `ai_engine_model_count`, `ai_engine_model` | จอ (แตะที่เมนู) |
| `cm55/edge_ai/05_register_model` — Register your own model, run it, release it | fill an ai_model_desc_t with your own entry points, join the registry at run time, then hand the resources back | `ai_engine_register`, `ai_engine_dyn_count`, `ai_engine_dyn_capacity`, `ai_engine_model_count`, `ai_engine_model`, `ai_engine_init`, `ai_engine_start`, `ai_engine_snapshot_model`, `ai_engine_active`, `ai_engine_stop`, `ai_engine_unload`, `ai_engine_unload_done`, `ai_engine_unload_refused` | จอ (แตะที่เมนู) |
| `cm55/edge_ai/06_staged_load` — Load a model staged by the host | check a staging manifest yourself before handing the address to the loader, then read the loader's verdict | `ai_model_staged_load`, `ai_model_staged_count`, `ai_model_staged_rejects`, `ai_model_staged_last_rc`, `ai_model_staged_heap_free`, `ai_engine_model`, `ai_engine_model_count` | จอ (แตะที่เมนู) |
| `cm55/edge_ai/07_engine_health` — Is the model actually running? | sample the pipeline counters twice and read the DELTAS, which is the only way a cumulative counter answers a question | `ai_engine_active`, `ai_engine_feeds`, `ai_engine_dq_calls`, `ai_engine_dq_ok`, `ai_engine_stale_drops`, `ai_engine_npu_cycles`, `ai_engine_stack_words`, `ai_engine_stack_free_words` | จอ (แตะที่เมนู) |
| `cm55/edge_ai/08_deepcraft_link` — Drive a model through the DEEPCRAFT link | go through the model link instead of poking the engine, so the READY/STOPPED events fire and CM33 raises the sensor rate -- and keep the watchdog ticking | `deepcraft_task_init`, `deepcraft_task_select`, `deepcraft_task_request`, `deepcraft_task_watchdog` | จอ (แตะที่เมนู) |
| `cm55/edge_ai/09_edge_ai_page` — Wire the Edge AI page into your page manager | the create/render/destroy trio is a page-manager callback set -- what each one owes the manager, and why calling them by hand corrupts the header | `page_edge_ai_create`, `page_edge_ai_render`, `page_edge_ai_destroy`, `ipc_sensorhub_snapshot`, `ai_engine_model_count` | จอ (แตะที่เมนู) |
| `cm55/edge_ai/10_model_load_diagnosis` — The model never produced a verdict — which of the four reasons is it? | init_calls, init_returns, inits and last_init_rc separate "the task never ran", "init hung", "init failed" and "init was never called" | `ai_engine_init_calls`, `ai_engine_init_returns`, `ai_engine_inits`, `ai_engine_last_init_rc` | จอ (แตะที่เมนู) |

### `security` — 7 ไฟล์, 22 API

| ตัวอย่าง | สอนอะไร | API ที่เรียก | รันที่ไหน |
|---|---|---|---|
| `cm55/security/01_hsm_screens` — Open the HSM enrol and protect screens | hand a seconds-long secure-element operation to a screen that polls, and always tear the previous overlay down first | `hsm_provision_ui_teardown`, `hsm_enrol_open`, `hsm_protect_open` | จอ (แตะที่เมนู) |
| `cm33/security/02_model_signature_hook` — Replace the staged-model signature check with your own | how to override a WEAK symbol the archive exports, and how to return a verdict that does not pretend to know more than it does | `optiga_verify_staged_model` | คอนโซล CM33_NS |
| `cm33/security/03_chip_ownership` — Take the secure element, use it, give it back | the init-before-anything rule, the three names for one re-entrant gate, and why optiga_chip_enter() is not an "is the chip up?" test | `optiga_manager_init`, `optiga_manager_lock`, `optiga_manager_unlock`, `optiga_manager_acquire`, `optiga_manager_release`, `optiga_chip_enter`, `optiga_chip_exit` | คอนโซล CM33_NS |
| `cm33/security/04_touch_hold` — Keep the touch controller off the bus while the chip works | the counted hold/release pair, why it must wrap the WHOLE operation and not just its setup, and what the user sees while it is held | `optiga_manager_touch_hold`, `optiga_manager_touch_hold_reason`, `optiga_manager_touch_release` | คอนโซล CM33_NS |
| `cm33/security/05_csr_enrolment` — Publish a CSR and track the request that follows it | the request bookkeeping — correlation id, target and anchor OIDs — and the buffer contract publish_csr() imposes on its caller | `publish_csr`, `trustm_update_state`, `trustm_reset_state`, `trustm_current_correlation_id`, `trustm_requested_target_oid`, `trustm_requested_anchor_oid` | คอนโซล CM33_NS |
| `cm33/security/06_protected_update` — Ask the platform for a Protected Update — and what it changes | the request, the anti-rollback counter, what a manifest lock does and undoes, and the one chip change that no reflash can undo | `tesaiot_publish_protected_update`, `tesaiot_run_protected_update_isolated_test` | คอนโซล CM33_NS |
| `cm33/security/ref_hsm` — Reference list — read the HSM's state without starting anything | which calls answer a question without a chip transaction, and the one probe that is balanced rather than free | `optiga_manager_lock`, `optiga_manager_unlock`, `trustm_requested_target_oid`, `trustm_requested_anchor_oid`, `trustm_current_correlation_id` | คอนโซล CM33_NS |

### `storage` — 4 ไฟล์, 24 API

| ตัวอย่าง | สอนอะไร | API ที่เรียก | รันที่ไหน |
|---|---|---|---|
| `cm55/storage/01_wifi_saved_crud` — Saved networks: add, find, load, update, list, erase | the full CRUD over the OPTIGA credential slots -- and why every one of these calls belongs on a worker task, never on the GFX task | `wifi_saved_count`, `wifi_saved_add`, `wifi_saved_find`, `wifi_saved_load`, `wifi_saved_store`, `wifi_saved_load_all`, `wifi_saved_erase` | จอ (แตะที่เมนู) |
| `cm55/storage/02_wifi_saved_async` — Read saved networks without blocking the screen | the start / ready / finish split -- how to drive OPTIGA from an lv_timer inside the GFX task and never stall a frame | `wifi_saved_probe_start`, `wifi_saved_probe_ready`, `wifi_saved_probe_finish`, `wifi_saved_read_start`, `wifi_saved_read_ready`, `wifi_saved_read_result` | จอ (แตะที่เมนู) |
| `cm33/storage/03_wifi_creds_lfs` — Read and write saved WiFi networks on LittleFS | the credential store's readiness contract, the checksum-migration flag, and why every call but two must run on the MicroPython task | `lfs_wifi_creds_ready`, `lfs_wifi_creds_needs_resave`, `lfs_wifi_creds_init`, `lfs_wifi_creds_read`, `lfs_wifi_creds_write`, `lfs_wifi_creds_deinit` | คอนโซล CM33_NS |
| `cm33/storage/10_littlefs_basics` — Mount LittleFS in C, write a file, read it back | the geometry contract the two variants share, why the C mount only exists under mtb-only, and what the API deliberately does NOT give you — no directory read, no unmount, no remove | `bento_storage_init`, `bento_storage_ready`, `bento_storage_read_file`, `bento_storage_write_file`, `bento_storage_format` | คอนโซล CM33_NS |

### `sensors` — 8 ไฟล์, 54 API

| ตัวอย่าง | สอนอะไร | API ที่เรียก | รันที่ไหน |
|---|---|---|---|
| `cm55/sensors/01_feed_sensor_hub` — Publish a locally-read sensor into the hub | the Eva Kit path - CM55 reads the bus, feeds the hub, and every consumer sees it; plus who owns the sequence number | `ipc_sensorhub_feed_bmi270`, `ipc_sensorhub_feed_bmm350`, `ipc_sensorhub_feed_capsense`, `ipc_sensorhub_feed_pot`, `ipc_sensorhub_snapshot` | จอ (แตะที่เมนู) |
| `cm55/sensors/02_radar_presence` — Read range and presence from the BGT60TR13C radar | the range snapshot is a seqlock and bin 0 is dead; the threshold call has a magic zero that re-learns the room; and the two stats calls that tell a stalled sensor from a stalled task | `radar_dsp_snapshot`, `radar_dsp_set_threshold_x10`, `radar_dsp_process`, `tesaiot_radar_task`, `tesaiot_radar_loop_stats`, `tesaiot_radar_recover_stats` | จอ (แตะที่เมนู) |
| `cm33/sensors/01_i2c_bus_scan` — Scan the sensor I2C bus | the bus has one mutex and several owners - take it, scan, give it back, and quiesce the auto task first so you are not fighting it | `sensor_i2c_is_init`, `sensor_i2c_lock`, `sensor_i2c_scan`, `sensor_i2c_unlock`, `sensor_auto_is_running`, `sensor_auto_stop`, `sensor_auto_start` | คอนโซล CM33_NS |
| `cm33/sensors/02_read_imu` — Read the BMI270 accelerometer and gyroscope | prove the wire before you trust the driver, the real init order, and one lock per SAMPLE rather than one per register | `bmi270_read_chip_id`, `bmi270_read_accel`, `bmi270_read_gyro`, `bmi270_read_temperature`, `sensor_i2c_lock`, `sensor_i2c_unlock`, `sensor_auto_is_running` | คอนโซล CM33_NS |
| `cm33/sensors/03_read_magnetometer` — Read the BMM350 magnetometer and calibrate the compass | a compass is useless until hard-iron calibration converges - how to drive that, how to know when it has, and why heading_from_xy beats a second bus read | `bmm350_read_chip_id`, `bmm350_read_xyz`, `bmm350_read_heading`, `bmm350_cal_update`, `bmm350_cal_get_offsets`, `bmm350_heading_from_xy`, `bmm350_cal_reset`, `sensor_i2c_lock`, `sensor_i2c_unlock`, `bmm350_reinit`, `bmm350_diagnose`, `bmm350_debug_read` | คอนโซล CM33_NS |
| `cm33/sensors/04_read_environment` — Read pressure, air temperature and humidity (AI Kit) | read_both() is not a convenience wrapper - it halves the bus traffic; and the two parts report two different temperatures | `dps368_read_product_id`, `dps368_read_both`, `dps368_read_pressure`, `dps368_read_temperature`, `sht40_read_serial`, `sht40_read_both`, `sht40_read_temperature`, `sht40_read_humidity`, `sensor_i2c_lock`, `sensor_i2c_unlock` | คอนโซล CM33_NS |
| `cm33/sensors/05_auto_push_task` — Drive the background sensor-push task | retune the publisher that feeds the display and MicroPython - mask, rate, the 50 ms cliff, the free cache - and put it back | `sensor_auto_is_running`, `sensor_auto_get_mask`, `sensor_auto_set_mask`, `sensor_auto_enable`, `sensor_auto_disable`, `sensor_auto_get_rate`, `sensor_auto_set_rate`, `sensor_auto_get_push_count`, `sensor_auto_stop`, `sensor_auto_start`, `sensor_auto_get_bmi270` | คอนโซล CM33_NS |
| `cm33/sensors/06_raw_register_access` — Talk to any device on the sensor bus directly | the escape hatch - register reads, a safe read-modify-write, and the command-response form for parts that have no registers at all | `sensor_i2c_read_byte`, `sensor_i2c_write_byte`, `sensor_i2c_read_reg`, `sensor_i2c_write_reg`, `sensor_i2c_write_raw`, `sensor_i2c_read_raw`, `sensor_i2c_is_init`, `sensor_i2c_lock`, `sensor_i2c_unlock` | คอนโซล CM33_NS |

### `io` — 7 ไฟล์, 39 API

| ตัวอย่าง | สอนอะไร | API ที่เรียก | รันที่ไหน |
|---|---|---|---|
| `cm55/io/01_rgb_matrix` — Draw on the DFR0522 16x8 RGB matrix | the panel is on the DISPLAY I2C bus, so it belongs to the GFX task and to no other; the eight draw ops, what each one costs on the wire, and which of them cancel an animation | `dfr0522_clear`, `dfr0522_fill`, `dfr0522_pixel`, `dfr0522_blit`, `dfr0522_score`, `dfr0522_bar`, `dfr0522_scroll`, `dfr0522_effect` | จอ (แตะที่เมนู) |
| `cm55/io/02_pots_and_capsense` — Read the four knobs and the CapSense pad from CM55 | why these two live on CM55 and not on the core MicroPython runs on, the knob-to-channel table that is NOT the identity, and the two tick rates the driver deliberately keeps apart | `cm55_sensor_poll_init`, `cm55_sensor_poll_tick`, `cm55_capsense_tick`, `cm55_sensor_poll_status`, `cm55_pot_read_all`, `cm55_sensor_poll_diag`, `ipc_sensorhub_snapshot` | จอ (แตะที่เมนู) |
| `cm55/io/03_usb_host_devices` — Read a USB gamepad, and a smart card, from the host port | why init is a REQUEST and never a call from the GFX task, how to tell a controller that is absent from one that is present and silent, and the two decode layers | `usb_hid_joystick_init`, `usb_hid_joystick_request_init`, `usb_hid_joystick_is_connected`, `usb_hid_joystick_get_state`, `f310_parse`, `f310_deadzone`, `usb_ccid_smartcard_request_init`, `usb_ccid_smartcard_get_state`, `usb_ccid_smartcard_trigger_read` | จอ (แตะที่เมนู) |
| `cm33/io/01_read_buttons` — Read the CapSense buttons | capture the idle baseline with fingers OFF the pad, poll the two buttons, and know which of the two CapSense backends your build linked — they read two different I2C buses | `capsense_init`, `capsense_read` | คอนโซล CM33_NS |
| `cm33/io/02_capsense_slider` — Read the CapSense slider position | the slider is 0..100 with no "not touched" value, so you need a release test and a hysteresis band before you drive anything with it | `capsense_init`, `capsense_read_slider` | คอนโซล CM33_NS |
| `cm33/io/03_read_potentiometers` — Read the potentiometer three ways | raw counts, percent and volts off one SAR channel — and which channel that actually is, which is not the one most people assume | `potentiometer_init`, `potentiometer_read_raw`, `potentiometer_read_percent`, `potentiometer_read_voltage` | คอนโซล CM33_NS |
| `cm33/io/04_gpio_led_button` — Drive the board LEDs and read the user button | the PDL calls behind gpio.led()/gpio.button(), the drive modes that make them work, and how to debounce a button without blocking | `Cy_GPIO_Pin_FastInit`, `Cy_GPIO_Set`, `Cy_GPIO_Clr`, `Cy_GPIO_Inv`, `Cy_GPIO_Write`, `Cy_GPIO_Read`, `CYBSP_USER_LED1_PORT`, `CYBSP_USER_BTN1_PORT` | คอนโซล CM33_NS |

### `connectivity` — 10 ไฟล์, 53 API

| ตัวอย่าง | สอนอะไร | API ที่เรียก | รันที่ไหน |
|---|---|---|---|
| `cm33/connectivity/01_link_registry` — Register and look up a transport link | how a backend joins the bento_link registry, and why registration is one-way — look up before you register, never register twice | `bento_link_register`, `bento_link_get`, `bento_link_at` | คอนโซล CM33_NS |
| `cm33/connectivity/02_link_ipc_backend` — Bring up the IPC backend and pull data back from CM55 | the bidirectional QUERY — send a request, wait for CM55 to fill a shared-memory response, and why that response must be SHAREDMEM | `bento_link_ipc_init`, `bento_link_ipc_query`, `bento_link_get` | คอนโซล CM33_NS |
| `cm33/connectivity/03_https_session` — One HTTPS session, end to end | connect, GET, POST, disconnect against the TESAIoT gateway — and when the plaintext door is the right one and when it is a mistake | `claw_https_connect`, `claw_https_connected`, `claw_https_get`, `claw_https_post`, `claw_https_disconnect`, `claw_http_connect_insecure` | คอนโซล CM33_NS |
| `cm33/connectivity/04_circuit_breaker` — Stop hammering a backend that is already failing | drive the breaker CLOSED -> OPEN -> HALFOPEN -> CLOSED and read the cooldown, so a dead backend costs three attempts, not thousands | `claw_cb_init`, `claw_cb_allow`, `claw_cb_failure`, `claw_cb_success`, `claw_cb_state`, `claw_cb_cooldown_remaining` | คอนโซล CM33_NS |
| `cm33/connectivity/05_rate_limit` — Cap how often one tool may be called | check-then-record around every tool call, per-tool budgets, and the two ways the limiter lets calls through that you must design for | `claw_rate_init`, `claw_rate_set`, `claw_rate_check`, `claw_rate_record` | คอนโซล CM33_NS |
| `cm33/connectivity/06_session_memory` — Keep a conversation, and remember facts across it | the RAM ring vs the persistent key-value store, how to build an LLM context out of both, and the one call that must run on the MPY task | `claw_session_init`, `claw_session_add`, `claw_session_count`, `claw_session_dirty`, `claw_session_clear`, `claw_session_build_context`, `claw_session_flush`, `claw_memory_set`, `claw_memory_get` | คอนโซล CM33_NS |
| `cm33/connectivity/07_trust_policy` — Let the transport decide which tools may run | set the trust level when a session opens, gate every tool on its own risk class, and put the level back when the session closes | `claw_trust_set`, `claw_trust_get`, `claw_trust_allows` | คอนโซล CM33_NS |
| `cm33/connectivity/08_tacp_host_protocol` — Pump the TACP host link and answer the IDE | the one-owner rule for the UART, the poll/drain loop, framed responses, and what "_from_isr" buys you and what it costs | `tacp_init`, `tacp_poll_uart`, `tacp_ring_buf_readable`, `tacp_ring_buf_read`, `tacp_claw_respond`, `tacp_request_delete_main_from_isr` | คอนโซล CM33_NS |
| `cm33/connectivity/10_wifi_join` — Join a Wi-Fi network | the whole join on CM33_NS — bring the radio up, take credentials from a store instead of a #define, connect, read the IP and the associated AP back, and put the link down again | `app_wifi_is_ready`, `app_wifi_init`, `app_wifi_connect_direct`, `app_wifi_get_ipv4`, `app_wifi_disconnect`, `cy_wcm_is_connected_to_ap`, `cy_wcm_get_ip_addr`, `cy_wcm_get_associated_ap_info`, `lfs_load_wifi_creds`, `tesaiot_config_get` | คอนโซล CM33_NS |
| `cm33/connectivity/ref_claw` — Reference list — every read-only call, and the two module objects | which mpy_secure calls are safe to make from any task at any time, and how the MicroPython module objects are referenced | `claw_https_connected`, `claw_cb_state`, `claw_cb_cooldown_remaining`, `claw_trust_get`, `claw_rate_check`, `claw_session_count`, `claw_session_dirty`, `claw_session_build_context`, `claw_memory_get`, `lfs_wifi_creds_ready`, `lfs_wifi_creds_needs_resave`, `tacp_ring_buf_readable`, `bento_link_at`, `bento_link_get`, `mp_module_edge_ai`, `mp_module_optiga` | คอนโซล CM33_NS |

### `security` — 7 ไฟล์, 22 API

| ตัวอย่าง | สอนอะไร | API ที่เรียก | รันที่ไหน |
|---|---|---|---|
| `cm55/security/01_hsm_screens` — Open the HSM enrol and protect screens | hand a seconds-long secure-element operation to a screen that polls, and always tear the previous overlay down first | `hsm_provision_ui_teardown`, `hsm_enrol_open`, `hsm_protect_open` | จอ (แตะที่เมนู) |
| `cm33/security/02_model_signature_hook` — Replace the staged-model signature check with your own | how to override a WEAK symbol the archive exports, and how to return a verdict that does not pretend to know more than it does | `optiga_verify_staged_model` | คอนโซล CM33_NS |
| `cm33/security/03_chip_ownership` — Take the secure element, use it, give it back | the init-before-anything rule, the three names for one re-entrant gate, and why optiga_chip_enter() is not an "is the chip up?" test | `optiga_manager_init`, `optiga_manager_lock`, `optiga_manager_unlock`, `optiga_manager_acquire`, `optiga_manager_release`, `optiga_chip_enter`, `optiga_chip_exit` | คอนโซล CM33_NS |
| `cm33/security/04_touch_hold` — Keep the touch controller off the bus while the chip works | the counted hold/release pair, why it must wrap the WHOLE operation and not just its setup, and what the user sees while it is held | `optiga_manager_touch_hold`, `optiga_manager_touch_hold_reason`, `optiga_manager_touch_release` | คอนโซล CM33_NS |
| `cm33/security/05_csr_enrolment` — Publish a CSR and track the request that follows it | the request bookkeeping — correlation id, target and anchor OIDs — and the buffer contract publish_csr() imposes on its caller | `publish_csr`, `trustm_update_state`, `trustm_reset_state`, `trustm_current_correlation_id`, `trustm_requested_target_oid`, `trustm_requested_anchor_oid` | คอนโซล CM33_NS |
| `cm33/security/06_protected_update` — Ask the platform for a Protected Update — and what it changes | the request, the anti-rollback counter, what a manifest lock does and undoes, and the one chip change that no reflash can undo | `tesaiot_publish_protected_update`, `tesaiot_run_protected_update_isolated_test` | คอนโซล CM33_NS |
| `cm33/security/ref_hsm` — Reference list — read the HSM's state without starting anything | which calls answer a question without a chip transaction, and the one probe that is balanced rather than free | `optiga_manager_lock`, `optiga_manager_unlock`, `trustm_requested_target_oid`, `trustm_requested_anchor_oid`, `trustm_current_correlation_id` | คอนโซล CM33_NS |

### `storage` — 4 ไฟล์, 24 API

| ตัวอย่าง | สอนอะไร | API ที่เรียก | รันที่ไหน |
|---|---|---|---|
| `cm55/storage/01_wifi_saved_crud` — Saved networks: add, find, load, update, list, erase | the full CRUD over the OPTIGA credential slots -- and why every one of these calls belongs on a worker task, never on the GFX task | `wifi_saved_count`, `wifi_saved_add`, `wifi_saved_find`, `wifi_saved_load`, `wifi_saved_store`, `wifi_saved_load_all`, `wifi_saved_erase` | จอ (แตะที่เมนู) |
| `cm55/storage/02_wifi_saved_async` — Read saved networks without blocking the screen | the start / ready / finish split -- how to drive OPTIGA from an lv_timer inside the GFX task and never stall a frame | `wifi_saved_probe_start`, `wifi_saved_probe_ready`, `wifi_saved_probe_finish`, `wifi_saved_read_start`, `wifi_saved_read_ready`, `wifi_saved_read_result` | จอ (แตะที่เมนู) |
| `cm33/storage/03_wifi_creds_lfs` — Read and write saved WiFi networks on LittleFS | the credential store's readiness contract, the checksum-migration flag, and why every call but two must run on the MicroPython task | `lfs_wifi_creds_ready`, `lfs_wifi_creds_needs_resave`, `lfs_wifi_creds_init`, `lfs_wifi_creds_read`, `lfs_wifi_creds_write`, `lfs_wifi_creds_deinit` | คอนโซล CM33_NS |
| `cm33/storage/10_littlefs_basics` — Mount LittleFS in C, write a file, read it back | the geometry contract the two variants share, why the C mount only exists under mtb-only, and what the API deliberately does NOT give you — no directory read, no unmount, no remove | `bento_storage_init`, `bento_storage_ready`, `bento_storage_read_file`, `bento_storage_write_file`, `bento_storage_format` | คอนโซล CM33_NS |

### `ble` — 3 ไฟล์, 87 API

| ตัวอย่าง | สอนอะไร | API ที่เรียก | รันที่ไหน |
|---|---|---|---|
| `cm33/ble/01_nus_bring_up_and_talk` — Bring up NUS, pair, and exchange bytes (STARTS THE BLE RADIO) | the whole transport in one pass — advertise, read the link state, feed the newline framer, send a frame, answer a permission prompt, and stop cleanly | `ble_nus_init`, `ble_nus_get_state`, `ble_nus_get_adv_name`, `ble_nus_send`, `ble_nus_rearm_advertising`, `ble_nus_deinit`, `ble_nus_passkey_cb`, `nus_gatt_database`, `nus_gatt_database_len`, `NUS_UUID_SERVICE`, `NUS_UUID_CHAR_RX`, `NUS_UUID_CHAR_TX`, `nus_protocol_init`, `nus_on_rx_bytes`, `nus_protocol_tick`, `nus_protocol_set_link_encrypted`, `nus_protocol_get_link_encrypted`, `nus_protocol_send_permission` | คอนโซล CM33_NS |
| `cm33/ble/02_host_protocol` — Speak the Bento Buddy wire protocol end to end | dispatch a command, emit its ack, queue the acks a human still owes, decode chunked base64, receive a pushed folder, and accumulate a streamed agent answer | `nus_commands_dispatch`, `nus_commands_emit_ack`, `nus_commands_handle_time_sync`, `nus_emit_event`, `nus_events_push_pending_ack`, `nus_events_drain_pending_ack`, `nus_events_pending_ack_count`, `nus_b64_init`, `nus_b64_feed`, `nus_b64_flush`, `nus_fp_char_begin`, `nus_fp_file`, `nus_fp_chunk`, `nus_fp_file_end`, `nus_fp_char_end`, `nus_fp_is_active`, `nus_fp_device_write_bytes`, `nus_agent_note_ask`, `nus_agent_handle_token`, `nus_agent_handle_tool_call`, `nus_agent_handle_done`, `nus_agent_buffer_len`, `nus_agent_reset` | คอนโซล CM33_NS |
| `cm33/ble/ref_ble` — Reference list — everything else in the ble_nus module | what each remaining call is for, when to reach for it, and what it gives back — devmode, firmware identity, OTA consent, the radio scheduler, sensor and voice streams, the CM55 bridge, and the five weak stubs you are expected to replace | `ble_nus_get_diagnostics`, `fw_hash_compute_at_boot`, `fw_hash_hex`, `fw_hash_prefix8`, `fw_hash_get_diagnostics`, `bento_fw_handle_query`, `bento_fw_handle_update_begin`, `bento_fw_on_user_decision`, `bento_fw_emit_boot_complete`, `bento_devmode_init`, `bento_devmode_secret_fp_hex`, `bento_devmode_secret_hex`, `bento_devmode_nonce_issue`, `bento_devmode_unlock`, `bento_devmode_is_unlocked`, `bento_devmode_lock`, `bento_devmode_emit_provision`, `radio_scheduler_init`, `radio_scheduler_get_mode`, `radio_scheduler_get_status`, `radio_scheduler_get_boot_mode`, `radio_scheduler_set_boot_mode`, `radio_scheduler_request_mode`, `radio_scheduler_set_on_state`, `radio_scheduler_set_wifi_creds`, `radio_mode_str`, `nus_radio_emit_state_event`, `sensor_stream_init`, `sensor_stream_start`, `sensor_stream_stop`, `sensor_stream_stop_all`, `sensor_stream_is_active`, `sensor_stream_dropped_count`, `voice_capture_start`, `voice_capture_stop`, `voice_capture_is_running`, `ipc_bento_buddy_rx_init`, `ipc_bento_buddy_send`, `bento_buddy_request_start`, `bento_buddy_request_stop`, `bento_buddy_auto_start_install`, `app_wifi_connect_direct`, `app_wifi_disconnect`, `app_wifi_get_ipv4`, `lfs_save_wifi_creds`, `lfs_load_wifi_creds` | คอนโซล CM33_NS |

<!-- END generated -->
