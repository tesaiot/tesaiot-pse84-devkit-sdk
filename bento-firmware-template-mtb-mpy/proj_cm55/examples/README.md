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

### `display` — 11 ไฟล์, 62 API

| ตัวอย่าง | สอนอะไร | API ที่เรียก | รันที่ไหน |
|---|---|---|---|
| `cm55/display/00_display_bringup` — Bring the display up (and prove it came up) | tesaiot_display_init() owns its own task -- never create one yourself -- and g_tesaiot_display_diag is how you tell a working panel from a silent one | `tesaiot_display_init`, `tesaiot_display_task`, `rtos_cm55_gfx_task_handle`, `g_tesaiot_display_diag` | จอ (แตะที่เมนู) |
| `cm55/display/01_bringup` — Bring the CM55 IPC peers up in the right order | the exact init sequence a GFX task owes the IPC library, and how to read back what is already running | `ipc_sensorhub_init`, `ipc_service_init`, `ipc_lcd_init`, `ipc_ui_init`, `ui_widget_mgr_init`, `ui_widget_mgr_get_parent`, `ui_widget_mgr_needs_container` | จอ (แตะที่เมนู) |
| `cm55/display/02_sensor_dashboard` — Put the CM33 sensor snapshot on the display | read every sensor CM33_NS publishes, tell live data from stale, and keep a panel refreshing without blocking the GFX task | `ipc_sensorhub_snapshot`, `ipc_sensorhub_wifi_connected`, `ipc_sensorhub_ble_connected`, `ipc_sensorhub_ntp_synced`, `ipc_sensorhub_get_time_str`, `ipc_ui_set_container`, `ui_widget_mgr_create`, `ui_widget_mgr_set_text`, `ui_widget_mgr_get_object` | จอ (แตะที่เมนู) |
| `cm55/display/03_widget_gallery` — Build a widget gallery on the display | create ten widget types through one struct, then move, resize, recolour, hide and delete them by handle | `ui_widget_mgr_needs_container`, `ui_widget_mgr_get_parent`, `ui_widget_mgr_set_parent`, `ui_widget_mgr_clear_all`, `ui_widget_mgr_set_screen`, `ui_widget_mgr_create`, `ui_widget_mgr_set_text`, `ui_widget_mgr_set_value`, `ui_widget_mgr_set_position`, `ui_widget_mgr_set_size`, `ui_widget_mgr_set_color`, `ui_widget_mgr_set_visible`, `ui_widget_mgr_set_dotmatrix`, `ui_widget_mgr_set_image`, `ui_widget_mgr_delete`, `ui_widget_mgr_count`, `ui_widget_mgr_get_object` | จอ (แตะที่เมนู) |
| `cm55/display/04_live_chart` — Stream three signals into a live chart | add series to a chart, push samples one at a time, widen the time window, and stop feeding when the chart dies | `ui_widget_mgr_create`, `ui_widget_mgr_chart_add_series`, `ui_widget_mgr_chart_set_next`, `ui_widget_mgr_set_prop`, `ui_widget_mgr_get_object`, `ui_widget_mgr_set_text`, `ipc_sensorhub_snapshot` | จอ (แตะที่เมนู) |
| `cm55/display/05_collection_events` — Fill a table and a list, then read the taps back | append rows to collection widgets one item at a time, subscribe to input events, and drain the event ring | `ui_widget_mgr_create`, `ui_widget_mgr_item_add`, `ui_widget_mgr_item_clear`, `ui_widget_mgr_set_prop`, `ui_widget_mgr_event_push`, `ui_widget_mgr_event_drain`, `ui_widget_mgr_get_object`, `ui_widget_mgr_get_value`, `ui_widget_mgr_set_text` | จอ (แตะที่เมนู) |
| `cm55/display/06_console_panel` — Show the MicroPython console over your page | bind the lcd.print() terminal to a container, flip between console and widgets, and badge output that arrived while it was hidden | `ipc_lcd_set_container`, `ipc_lcd_toggle_panel`, `ipc_lcd_is_panel_visible`, `ipc_lcd_has_unread`, `ipc_lcd_clear_unread`, `ipc_lcd_reset_auto_nav`, `ui_widget_mgr_set_all_visible`, `ui_widget_mgr_create`, `ipc_ui_set_container` | จอ (แตะที่เมนู) |
| `cm55/display/07_override_hooks` — Override the library's weak hooks with your own | which seven symbols libbento_ipc leaves for you, their exact signatures, and how to tell whose definition the linker chose | `cm55_controls_snapshot`, `game_sprite_create`, `game_sprite_set`, `game_sprite_lookup`, `ipc_ui_ext_clear_all`, `ipc_ui_ext_dispatch`, `ipc_ui_platform_diag` | จอ (แตะที่เมนู) |
| `cm55/display/08_sprites` — Animate an image sprite | build an lv_image_dsc_t with real transparency, create a sprite through the handle table, and swap frames without churning the object | `ui_widget_mgr_create_sprite`, `ui_widget_mgr_set_sprite_image`, `ui_widget_mgr_set_position`, `ui_widget_mgr_get_object`, `ipc_ui_set_container` | จอ (แตะที่เมนู) |
| `cm55/display/ref_core` — Reference: the three symbols with no header | the archive exports three symbols that no shipped header declares -- what they are, the correct extern for each, and when you would want them | `calculate_idle_percentage`, `tesaiot_display_ready`, `disp_touch_i2c_controller_context`, `g_tesaiot_display_diag`, `rtos_cm55_gfx_task_handle` | จอ (แตะที่เมนู) |
| `cm55/display/ref_display` — Reference: the ipc_core getters and probes | what each remaining read-only call answers, and when you would ask it | `ui_widget_mgr_needs_container`, `ui_widget_mgr_get_parent`, `ui_widget_mgr_count`, `ui_widget_mgr_list`, `ipc_ui_input_activity`, `ipc_sensorhub_weather` | จอ (แตะที่เมนู) |

### `sensors` — 4 ไฟล์, 14 API

| ตัวอย่าง | สอนอะไร | API ที่เรียก | รันที่ไหน |
|---|---|---|---|
| `cm55/sensors/01_feed_sensor_hub` — Publish a locally-read sensor into the hub | the Eva Kit path - CM55 reads the bus, feeds the hub, and every consumer sees it; plus who owns the sequence number | `ipc_sensorhub_feed_bmi270`, `ipc_sensorhub_feed_bmm350`, `ipc_sensorhub_feed_capsense`, `ipc_sensorhub_feed_pot`, `ipc_sensorhub_snapshot` | จอ (แตะที่เมนู) |
| `cm33/sensors/01_i2c_bus_scan` — Scan the sensor I2C bus | the bus has one mutex and several owners - take it, scan, give it | `sensor_i2c_is_init`, `sensor_i2c_lock`, `sensor_i2c_scan` | คอนโซล CM33_NS |
| `cm33/sensors/02_read_imu` — Read the BMI270 accelerometer and gyroscope | prove the wire before you trust the driver, the real init order, | `bmi270_read_chip_id`, `bmi270_read_accel`, `bmi270_read_gyro` | คอนโซล CM33_NS |
| `cm33/sensors/03_read_magnetometer` — Read the BMM350 magnetometer and calibrate the compass | a compass is useless until hard-iron calibration converges - how to | `bmm350_read_chip_id`, `bmm350_read_xyz`, `bmm350_read_heading` | คอนโซล CM33_NS |

### `io` — 3 ไฟล์, 5 API

| ตัวอย่าง | สอนอะไร | API ที่เรียก | รันที่ไหน |
|---|---|---|---|
| `cm33/io/01_read_buttons` — Read the CapSense buttons | capture the idle baseline with fingers OFF the pad, poll the two | `capsense_init`, `capsense_read` | คอนโซล CM33_NS |
| `cm33/io/02_capsense_slider` — Read the CapSense slider position | the slider is 0..100 with no "not touched" value, so you need a | `capsense_init`, `capsense_read_slider` | คอนโซล CM33_NS |
| `cm33/io/03_read_potentiometers` — Read the potentiometer three ways | raw counts, percent and volts off one SAR channel — and which | `potentiometer_init`, `potentiometer_read_raw` | คอนโซล CM33_NS |

### `edge_ai` — 6 ไฟล์, 28 API

| ตัวอย่าง | สอนอะไร | API ที่เรียก | รันที่ไหน |
|---|---|---|---|
| `cm55/edge_ai/01_first_inference` — Run your first inference | create the engine, activate one model, feed it at its training rate, read the verdict back | `ai_engine_init`, `ai_engine_model_count`, `ai_engine_model`, `ai_engine_set_sensor_rate`, `ai_engine_start`, `ai_engine_requested`, `ai_engine_active`, `ai_engine_snapshot`, `ai_engine_stop`, `ai_engine_resume_sensor` | จอ (แตะที่เมนู) |
| `cm55/edge_ai/02_model_registry` — Browse the model registry | enumerate every model the image carries, read its descriptor, and see how much run-time room is left | `ai_engine_model_count`, `ai_engine_model`, `ai_engine_dyn_count`, `ai_engine_dyn_capacity` | จอ (แตะที่เมนู) |
| `cm55/edge_ai/03_parallel_set_run` — Watch several models at once | start a parallel set, wait out the window fill, then read EACH member's verdict instead of the last one published | `ai_engine_start`, `ai_engine_set_name`, `ai_engine_set_members`, `ai_engine_mic_settling`, `ai_engine_mic_settle_pct`, `ai_engine_snapshot_model`, `ai_engine_active`, `ai_engine_stop`, `ai_engine_model` | จอ (แตะที่เมนู) |
| `cm55/edge_ai/04_set_membership` — Redefine what a set contains | read a set's compiled membership, replace it at run time, read the override back, and put the original back | `ai_engine_set_name`, `ai_engine_set_models`, `ai_engine_set_define`, `ai_engine_set_members_defined`, `ai_engine_model_count`, `ai_engine_model` | จอ (แตะที่เมนู) |
| `cm55/edge_ai/08_deepcraft_link` — Drive a model through the DEEPCRAFT link | go through the model link instead of poking the engine, so the READY/STOPPED events fire and CM33 raises the sensor rate -- and keep the watchdog ticking | `deepcraft_task_init`, `deepcraft_task_select`, `deepcraft_task_request`, `deepcraft_task_watchdog` | จอ (แตะที่เมนู) |
| `cm55/edge_ai/09_edge_ai_page` — Wire the Edge AI page into your page manager | the create/render/destroy trio is a page-manager callback set -- what each one owes the manager, and why calling them by hand corrupts the header | `page_edge_ai_create`, `page_edge_ai_render`, `page_edge_ai_destroy`, `ipc_sensorhub_snapshot`, `ai_engine_model_count` | จอ (แตะที่เมนู) |

### `security` — 7 ไฟล์, 13 API

| ตัวอย่าง | สอนอะไร | API ที่เรียก | รันที่ไหน |
|---|---|---|---|
| `cm55/security/01_hsm_screens` — Open the HSM enrol and protect screens | hand a seconds-long secure-element operation to a screen that polls, and always tear the previous overlay down first | `hsm_provision_ui_teardown`, `hsm_enrol_open`, `hsm_protect_open` | จอ (แตะที่เมนู) |
| `cm33/security/02_model_signature_hook` — Replace the staged-model signature check with your own | how to override a WEAK symbol the archive exports, and how to return | `optiga_verify_staged_model` | คอนโซล CM33_NS |
| `cm33/security/03_chip_ownership` — Take the secure element, use it, give it back | the init-before-anything rule, the three names for one re-entrant | `optiga_manager_init`, `optiga_manager_lock`, `optiga_manager_unlock` | คอนโซล CM33_NS |
| `cm33/security/04_touch_hold` — Keep the touch controller off the bus while the chip works | the counted hold/release pair, why it must wrap the WHOLE operation | `optiga_manager_touch_hold`, `optiga_manager_touch_hold_reason` | คอนโซล CM33_NS |
| `cm33/security/05_csr_enrolment` — Publish a CSR and track the request that follows it | the request bookkeeping — correlation id, target and anchor OIDs — | `publish_csr`, `trustm_update_state`, `trustm_reset_state` | คอนโซล CM33_NS |
| `cm33/security/06_protected_update` — Ask the platform for a Protected Update — and what it changes | the request, the anti-rollback counter, what a manifest lock does | `tesaiot_publish_protected_update` | คอนโซล CM33_NS |
| `cm33/security/ref_hsm` — Reference list — read the HSM's state without starting anything | which calls answer a question without a chip transaction, and the | `optiga_manager_lock`, `optiga_manager_unlock` | คอนโซล CM33_NS |

### `storage` — 3 ไฟล์, 15 API

| ตัวอย่าง | สอนอะไร | API ที่เรียก | รันที่ไหน |
|---|---|---|---|
| `cm55/storage/01_wifi_saved_crud` — Saved networks: add, find, load, update, list, erase | the full CRUD over the OPTIGA credential slots -- and why every one of these calls belongs on a worker task, never on the GFX task | `wifi_saved_count`, `wifi_saved_add`, `wifi_saved_find`, `wifi_saved_load`, `wifi_saved_store`, `wifi_saved_load_all`, `wifi_saved_erase` | จอ (แตะที่เมนู) |
| `cm55/storage/02_wifi_saved_async` — Read saved networks without blocking the screen | the start / ready / finish split -- how to drive OPTIGA from an lv_timer inside the GFX task and never stall a frame | `wifi_saved_probe_start`, `wifi_saved_probe_ready`, `wifi_saved_probe_finish`, `wifi_saved_read_start`, `wifi_saved_read_ready`, `wifi_saved_read_result` | จอ (แตะที่เมนู) |
| `cm33/storage/03_wifi_creds_lfs` — Read and write saved WiFi networks on LittleFS | the credential store's readiness contract, the checksum-migration | `lfs_wifi_creds_ready`, `lfs_wifi_creds_needs_resave` | คอนโซล CM33_NS |

### `sensors` — 4 ไฟล์, 14 API

| ตัวอย่าง | สอนอะไร | API ที่เรียก | รันที่ไหน |
|---|---|---|---|
| `cm55/sensors/01_feed_sensor_hub` — Publish a locally-read sensor into the hub | the Eva Kit path - CM55 reads the bus, feeds the hub, and every consumer sees it; plus who owns the sequence number | `ipc_sensorhub_feed_bmi270`, `ipc_sensorhub_feed_bmm350`, `ipc_sensorhub_feed_capsense`, `ipc_sensorhub_feed_pot`, `ipc_sensorhub_snapshot` | จอ (แตะที่เมนู) |
| `cm33/sensors/01_i2c_bus_scan` — Scan the sensor I2C bus | the bus has one mutex and several owners - take it, scan, give it | `sensor_i2c_is_init`, `sensor_i2c_lock`, `sensor_i2c_scan` | คอนโซล CM33_NS |
| `cm33/sensors/02_read_imu` — Read the BMI270 accelerometer and gyroscope | prove the wire before you trust the driver, the real init order, | `bmi270_read_chip_id`, `bmi270_read_accel`, `bmi270_read_gyro` | คอนโซล CM33_NS |
| `cm33/sensors/03_read_magnetometer` — Read the BMM350 magnetometer and calibrate the compass | a compass is useless until hard-iron calibration converges - how to | `bmm350_read_chip_id`, `bmm350_read_xyz`, `bmm350_read_heading` | คอนโซล CM33_NS |

### `io` — 3 ไฟล์, 5 API

| ตัวอย่าง | สอนอะไร | API ที่เรียก | รันที่ไหน |
|---|---|---|---|
| `cm33/io/01_read_buttons` — Read the CapSense buttons | capture the idle baseline with fingers OFF the pad, poll the two | `capsense_init`, `capsense_read` | คอนโซล CM33_NS |
| `cm33/io/02_capsense_slider` — Read the CapSense slider position | the slider is 0..100 with no "not touched" value, so you need a | `capsense_init`, `capsense_read_slider` | คอนโซล CM33_NS |
| `cm33/io/03_read_potentiometers` — Read the potentiometer three ways | raw counts, percent and volts off one SAR channel — and which | `potentiometer_init`, `potentiometer_read_raw` | คอนโซล CM33_NS |

### `connectivity` — 9 ไฟล์, 27 API

| ตัวอย่าง | สอนอะไร | API ที่เรียก | รันที่ไหน |
|---|---|---|---|
| `cm33/connectivity/01_link_registry` — Register and look up a transport link | how a backend joins the bento_link registry, and why registration | `bento_link_register`, `bento_link_get`, `bento_link_at` | คอนโซล CM33_NS |
| `cm33/connectivity/02_link_ipc_backend` — Bring up the IPC backend and pull data back from CM55 | the bidirectional QUERY — send a request, wait for CM55 to fill a | `bento_link_ipc_init`, `bento_link_ipc_query`, `bento_link_get` | คอนโซล CM33_NS |
| `cm33/connectivity/03_https_session` — One HTTPS session, end to end | connect, GET, POST, disconnect against the TESAIoT gateway — and | `claw_https_connect`, `claw_https_connected`, `claw_https_get` | คอนโซล CM33_NS |
| `cm33/connectivity/04_circuit_breaker` — Stop hammering a backend that is already failing | drive the breaker CLOSED -> OPEN -> HALFOPEN -> CLOSED and read the | `claw_cb_init`, `claw_cb_allow`, `claw_cb_failure`, `claw_cb_success` | คอนโซล CM33_NS |
| `cm33/connectivity/05_rate_limit` — Cap how often one tool may be called | check-then-record around every tool call, per-tool budgets, and the | `claw_rate_init`, `claw_rate_set`, `claw_rate_check`, `claw_rate_record` | คอนโซล CM33_NS |
| `cm33/connectivity/06_session_memory` — Keep a conversation, and remember facts across it | the RAM ring vs the persistent key-value store, how to build an LLM | `claw_session_init`, `claw_session_add`, `claw_session_count` | คอนโซล CM33_NS |
| `cm33/connectivity/07_trust_policy` — Let the transport decide which tools may run | set the trust level when a session opens, gate every tool on its own | `claw_trust_set`, `claw_trust_get`, `claw_trust_allows` | คอนโซล CM33_NS |
| `cm33/connectivity/08_tacp_host_protocol` — Pump the TACP host link and answer the IDE | the one-owner rule for the UART, the poll/drain loop, framed | `tacp_init`, `tacp_poll_uart`, `tacp_ring_buf_readable` | คอนโซล CM33_NS |
| `cm33/connectivity/ref_claw` — Reference list — every read-only call, and the two module objects | which mpy_secure calls are safe to make from any task at any time, | `claw_https_connected`, `claw_cb_state`, `claw_cb_cooldown_remaining` | คอนโซล CM33_NS |

### `security` — 7 ไฟล์, 13 API

| ตัวอย่าง | สอนอะไร | API ที่เรียก | รันที่ไหน |
|---|---|---|---|
| `cm55/security/01_hsm_screens` — Open the HSM enrol and protect screens | hand a seconds-long secure-element operation to a screen that polls, and always tear the previous overlay down first | `hsm_provision_ui_teardown`, `hsm_enrol_open`, `hsm_protect_open` | จอ (แตะที่เมนู) |
| `cm33/security/02_model_signature_hook` — Replace the staged-model signature check with your own | how to override a WEAK symbol the archive exports, and how to return | `optiga_verify_staged_model` | คอนโซล CM33_NS |
| `cm33/security/03_chip_ownership` — Take the secure element, use it, give it back | the init-before-anything rule, the three names for one re-entrant | `optiga_manager_init`, `optiga_manager_lock`, `optiga_manager_unlock` | คอนโซล CM33_NS |
| `cm33/security/04_touch_hold` — Keep the touch controller off the bus while the chip works | the counted hold/release pair, why it must wrap the WHOLE operation | `optiga_manager_touch_hold`, `optiga_manager_touch_hold_reason` | คอนโซล CM33_NS |
| `cm33/security/05_csr_enrolment` — Publish a CSR and track the request that follows it | the request bookkeeping — correlation id, target and anchor OIDs — | `publish_csr`, `trustm_update_state`, `trustm_reset_state` | คอนโซล CM33_NS |
| `cm33/security/06_protected_update` — Ask the platform for a Protected Update — and what it changes | the request, the anti-rollback counter, what a manifest lock does | `tesaiot_publish_protected_update` | คอนโซล CM33_NS |
| `cm33/security/ref_hsm` — Reference list — read the HSM's state without starting anything | which calls answer a question without a chip transaction, and the | `optiga_manager_lock`, `optiga_manager_unlock` | คอนโซล CM33_NS |

### `storage` — 3 ไฟล์, 15 API

| ตัวอย่าง | สอนอะไร | API ที่เรียก | รันที่ไหน |
|---|---|---|---|
| `cm55/storage/01_wifi_saved_crud` — Saved networks: add, find, load, update, list, erase | the full CRUD over the OPTIGA credential slots -- and why every one of these calls belongs on a worker task, never on the GFX task | `wifi_saved_count`, `wifi_saved_add`, `wifi_saved_find`, `wifi_saved_load`, `wifi_saved_store`, `wifi_saved_load_all`, `wifi_saved_erase` | จอ (แตะที่เมนู) |
| `cm55/storage/02_wifi_saved_async` — Read saved networks without blocking the screen | the start / ready / finish split -- how to drive OPTIGA from an lv_timer inside the GFX task and never stall a frame | `wifi_saved_probe_start`, `wifi_saved_probe_ready`, `wifi_saved_probe_finish`, `wifi_saved_read_start`, `wifi_saved_read_ready`, `wifi_saved_read_result` | จอ (แตะที่เมนู) |
| `cm33/storage/03_wifi_creds_lfs` — Read and write saved WiFi networks on LittleFS | the credential store's readiness contract, the checksum-migration | `lfs_wifi_creds_ready`, `lfs_wifi_creds_needs_resave` | คอนโซล CM33_NS |

### `ble` — 16 ไฟล์, 38 API

| ตัวอย่าง | สอนอะไร | API ที่เรียก | รันที่ไหน |
|---|---|---|---|
| `cm33/ble/01_advertise` — Bring up NUS and start advertising (STARTS THE BLE RADIO) | how to hand ble_nus a config, read back the advertised name and | `ble_nus_init`, `ble_nus_get_state`, `ble_nus_get_adv_name` | คอนโซล CM33_NS |
| `cm33/ble/01_nus_bring_up_and_talk` — Bring up NUS, pair, and exchange bytes (STARTS THE BLE RADIO) | the whole transport in one pass — advertise, read the link state, | `ble_nus_init`, `ble_nus_get_state`, `ble_nus_get_adv_name` | คอนโซล CM33_NS |
| `cm33/ble/02_host_protocol` — Speak the Bento Buddy wire protocol end to end | dispatch a command, emit its ack, queue the acks a human still | `nus_commands_dispatch`, `nus_commands_emit_ack` | คอนโซล CM33_NS |
| `cm33/ble/02_send_receive` — Move bytes over NUS and drive the newline framer | what ble_nus_send actually returns when there is no link, how to | `ble_nus_send`, `nus_on_rx_bytes`, `nus_protocol_init`, `nus_protocol_tick` | คอนโซล CM33_NS |
| `cm33/ble/03_commands` — Dispatch a desktop command and emit the ack | how to hand the dispatcher a parsed frame, the exact ack envelope | `nus_commands_dispatch`, `nus_commands_emit_ack` | คอนโซล CM33_NS |
| `cm33/ble/04_pending_acks` — Track notifications that still need a user acknowledgement | the four-slot pending-ack FIFO — dedup, oldest-first eviction, and | `nus_events_push_pending_ack`, `nus_events_drain_pending_ack` | คอนโซล CM33_NS |
| `cm33/ble/05_base64_stream` — Decode base64 that arrives split across BLE chunks | how to carry a half-finished 4-char quantum between calls, what the | `nus_b64_init`, `nus_b64_feed`, `nus_b64_flush` | คอนโซล CM33_NS |
| `cm33/ble/06_folder_push` — Receive a folder pushed from the desktop into LittleFS | the char_begin/file/chunk/file_end/char_end state machine, the | `nus_fp_char_begin`, `nus_fp_file`, `nus_fp_chunk`, `nus_fp_file_end` | คอนโซล CM33_NS |
| `cm33/ble/07_agent_stream` — Accumulate a streamed agent answer and hand it to the LCD | the ask/token/tool_call/done sequence, the id-matching rule that | `nus_agent_note_ask`, `nus_agent_handle_token`, `nus_agent_handle_done` | คอนโซล CM33_NS |
| `cm33/ble/08_devmode` — Unlock developer mode with a real HMAC challenge/response | the nonce/HMAC/unlock/lock cycle, the one-shot nonce, the five-fail | `bento_devmode_init`, `bento_devmode_nonce_issue`, `bento_devmode_unlock` | คอนโซล CM33_NS |
| `cm33/ble/09_firmware_update` — Report firmware identity and run the update physical-ack handshake | the boot-time SHA-256, what bento.fw.query answers, and why an | `fw_hash_compute_at_boot`, `fw_hash_hex`, `fw_hash_prefix8` | คอนโซล CM33_NS |
| `cm33/ble/10_radio_scheduler` — Arbitrate the single radio between BLE and WiFi | the mode state machine, the boot-mode persistence hooks you must | `radio_scheduler_init`, `radio_scheduler_get_mode` | คอนโซล CM33_NS |
| `cm33/ble/11_sensor_and_voice` — Stream a sensor to the desktop, and send a voice clip | starting and stopping the single sensor stream, reading the drop | `sensor_stream_init`, `sensor_stream_start`, `sensor_stream_stop` | คอนโซล CM33_NS |
| `cm33/ble/12_buddy_ipc_bridge` — Start the Buddy BLE stack on demand and bridge it to the CM55 LCD | the lazy start/stop pair the LCD button drives, and the two-way | `bento_buddy_request_start`, `bento_buddy_request_stop` | คอนโซล CM33_NS |
| `cm33/ble/13_weak_overrides` — Replace the archive's weak WiFi and credential stubs with your own | how the weak-symbol seam works, the five exact signatures, and why | `app_wifi_connect_direct`, `app_wifi_disconnect`, `app_wifi_get_ipv4` | คอนโซล CM33_NS |
| `cm33/ble/ref_ble` — Reference list — every read-only probe in the ble_nus module | what each inspector returns and when to reach for it | `ble_nus_get_state`, `ble_nus_get_adv_name`, `ble_nus_get_diagnostics` | คอนโซล CM33_NS |

<!-- END generated -->
