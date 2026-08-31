# TESAIoT SDK — C examples

A catalogue of C examples covering **every public function** in the SDK.
Thai edition: [`README.md`](README.md)

> The example table below is generated from the example files themselves by
> `tools/gen_examples_table.py`, so it cannot drift from the code.

---

## 1. Design

Four rules hold across this tree.

- **Grouped by real task, not one file per function.**
  Each file is a complete job a developer would actually do — "read a sensor and
  put it on the screen", "enrol a certificate with the HSM then open mTLS" —
  with every include, the full init order, and honest error handling. Copy one
  file into your own project and it works.
- **Functions no realistic task exercises live in one reference file per module.**
  Getters, status probes and readers go into `ref_<module>.c`, each call carrying
  a short comment saying when to use it and what it returns. That file is clearly
  headed as a reference list rather than a working job — which is what keeps the
  task files honest instead of padded to hit a coverage number.
- **Examples that draw, draw on the real display.**
  The `ui_widget_*` family is 30 of the 59 `ipc_core` symbols and they are display
  functions. A UART-only demonstration of them teaches nothing.
- **Off by default.** The shipped product firmware is unchanged unless a
  developer opts in.

---

## 2. Turning them on

One flag covers both cores.

```sh
make build ENABLE_PAGE_EXAMPLES=1
```

- **On screen**, the Home grid gains an **SDK Examples** card. Tap it for the
  full list; tap a row to see what it teaches and which APIs it calls, then press
  **Run this example** to run it and see what the SDK actually returned.
- **On the console**, CM33_NS prints its half of the catalogue at boot. Name one
  to run it:

```sh
make build ENABLE_PAGE_EXAMPLES=1 SDK_EXAMPLE_CM33=tesaiot_hsm/01_acquire_chip
```

The default is `ENABLE_PAGE_EXAMPLES=0`. With the flag off, both `examples/` and
`modules/page-components/examples/` are `CY_IGNORE`d wholesale, so not one byte
of this tree reaches the firmware image.

---

## 3. Which core an example lives on, and why

The six libraries are not all on the same core. This is checkable from the
archives themselves:

```sh
arm-none-eabi-readelf -A lib/edge_ai/COMPONENT_CM55/.../libbento_edge_ai.a
    Tag_CPU_name: "cortex-m55"      Tag_ABI_VFP_args: VFP registers
arm-none-eabi-readelf -A lib/ble_nus/COMPONENT_CM33/.../libbento_secure.a
    Tag_CPU_arch: v8-M.mainline     (no Tag_ABI_VFP_args)
```

| Library | Core | Examples live in |
|---|---|---|
| `edge_ai`, `cm55_core`, `ipc_core` | CM55 (hard-float) | `proj_cm55/examples/` |
| `ble_nus`, `mpy_secure`, `tesaiot_hsm` | CM33_NS (soft-float) | `proj_cm33_ns/examples/` |

The two cores are not ABI-compatible; linking one core's archive into the other's
image fails at the link step, which is the good outcome.

The **SDK Examples** page lists all **six** libraries. CM33 rows are marked and
show the command that runs them instead of a Run button, because "which functions
can I call" is a question about the whole SDK, not about whichever core happens
to be driving the screen.

---

## 4. Limits you should know about

- **`mpy_secure` ships only in the `mtb-mpy` variant.** The `mtb-only` package
  does not carry it — `variant_excludes()` in `bento-release.sh` drops
  `lib/mpy_secure`. Its examples are tagged `variant=mtb-mpy` and are not
  compiled into the other variant.
- **`ble_nus` cannot be linked in the template as shipped.** Verified:
  `libbento_secure.a` appears in no makefile's `LDLIBS`, and
  `bento_libs/lib.mk` — which `proj_cm33_ns/Makefile` includes when
  `ENABLE_PAGE_BENTO_BUDDY=1` — does not exist anywhere in the template. The
  `ble_nus` examples therefore **compile** against the shipped headers but cannot
  run on a device until that build wiring is completed.
- **OPTIGA life-cycle state moves one way only.** No example writes metadata tag
  `C0` or advances `LcsO`. Advancing it is irreversible on that chip and no
  reflash recovers it.

---

## 5. Rules every example follows

| CM55 side | CM33_NS side |
|---|---|
| Called from the GFX task inside an LVGL event callback | Called from a task at `tskIDLE_PRIORITY + 1` |
| LVGL calls are legal, and legal only here | `printf` is fine — CM33_NS owns the UART console |
| **Must not block** — a busy-wait freezes the display | Never `printf` from an IPC callback (ISR context) |
| **Never `printf`** — CM55 has no console; use `sdk_example_logf()` | Report the values actually returned; never fake a result |

Every file returns an honest result code — `SDK_EX_OK`, `SDK_EX_UNAVAILABLE`,
`SDK_EX_BUSY`, `SDK_EX_REFUSED`, `SDK_EX_NO_DATA`, `SDK_EX_STARTED`. If the
hardware is absent the example says so rather than pretending to succeed.

---

## 6. The gates

```sh
tools/examples_check.sh          # compile every example + score API coverage
tools/examples_check.sh --compile-only
tools/gen_examples_table.py      # regenerate the menu table and this catalogue
tools/gen_examples_table.py --check
```

`examples_check.sh` measures coverage from the **symbol table of a real object
file**, not by grepping the source:

- an API the example **calls or reads** appears as an undefined symbol (`U`);
- an API the example **overrides** (a weak symbol) appears as a defined symbol.

A mention in a comment produces no symbol and therefore no coverage. When a new
public API appears with no example, this gate fails.

---

## 7. The catalogue

<!-- BEGIN generated: tools/gen_examples_table.py -->

### `display` — 11 files, 62 API

| Example | What it teaches | APIs it exercises | Runs on |
|---|---|---|---|
| `cm55/display/00_display_bringup` — Bring the display up (and prove it came up) | tesaiot_display_init() owns its own task -- never create one yourself -- and g_tesaiot_display_diag is how you tell a working panel from a silent one | `tesaiot_display_init`, `tesaiot_display_task`, `rtos_cm55_gfx_task_handle`, `g_tesaiot_display_diag` | on-screen (tap the row) |
| `cm55/display/01_bringup` — Bring the CM55 IPC peers up in the right order | the exact init sequence a GFX task owes the IPC library, and how to read back what is already running | `ipc_sensorhub_init`, `ipc_service_init`, `ipc_lcd_init`, `ipc_ui_init`, `ui_widget_mgr_init`, `ui_widget_mgr_get_parent`, `ui_widget_mgr_needs_container` | on-screen (tap the row) |
| `cm55/display/02_sensor_dashboard` — Put the CM33 sensor snapshot on the display | read every sensor CM33_NS publishes, tell live data from stale, and keep a panel refreshing without blocking the GFX task | `ipc_sensorhub_snapshot`, `ipc_sensorhub_wifi_connected`, `ipc_sensorhub_ble_connected`, `ipc_sensorhub_ntp_synced`, `ipc_sensorhub_get_time_str`, `ipc_ui_set_container`, `ui_widget_mgr_create`, `ui_widget_mgr_set_text`, `ui_widget_mgr_get_object` | on-screen (tap the row) |
| `cm55/display/03_widget_gallery` — Build a widget gallery on the display | create ten widget types through one struct, then move, resize, recolour, hide and delete them by handle | `ui_widget_mgr_needs_container`, `ui_widget_mgr_get_parent`, `ui_widget_mgr_set_parent`, `ui_widget_mgr_clear_all`, `ui_widget_mgr_set_screen`, `ui_widget_mgr_create`, `ui_widget_mgr_set_text`, `ui_widget_mgr_set_value`, `ui_widget_mgr_set_position`, `ui_widget_mgr_set_size`, `ui_widget_mgr_set_color`, `ui_widget_mgr_set_visible`, `ui_widget_mgr_set_dotmatrix`, `ui_widget_mgr_set_image`, `ui_widget_mgr_delete`, `ui_widget_mgr_count`, `ui_widget_mgr_get_object` | on-screen (tap the row) |
| `cm55/display/04_live_chart` — Stream three signals into a live chart | add series to a chart, push samples one at a time, widen the time window, and stop feeding when the chart dies | `ui_widget_mgr_create`, `ui_widget_mgr_chart_add_series`, `ui_widget_mgr_chart_set_next`, `ui_widget_mgr_set_prop`, `ui_widget_mgr_get_object`, `ui_widget_mgr_set_text`, `ipc_sensorhub_snapshot` | on-screen (tap the row) |
| `cm55/display/05_collection_events` — Fill a table and a list, then read the taps back | append rows to collection widgets one item at a time, subscribe to input events, and drain the event ring | `ui_widget_mgr_create`, `ui_widget_mgr_item_add`, `ui_widget_mgr_item_clear`, `ui_widget_mgr_set_prop`, `ui_widget_mgr_event_push`, `ui_widget_mgr_event_drain`, `ui_widget_mgr_get_object`, `ui_widget_mgr_get_value`, `ui_widget_mgr_set_text` | on-screen (tap the row) |
| `cm55/display/06_console_panel` — Show the MicroPython console over your page | bind the lcd.print() terminal to a container, flip between console and widgets, and badge output that arrived while it was hidden | `ipc_lcd_set_container`, `ipc_lcd_toggle_panel`, `ipc_lcd_is_panel_visible`, `ipc_lcd_has_unread`, `ipc_lcd_clear_unread`, `ipc_lcd_reset_auto_nav`, `ui_widget_mgr_set_all_visible`, `ui_widget_mgr_create`, `ipc_ui_set_container` | on-screen (tap the row) |
| `cm55/display/07_override_hooks` — Override the library's weak hooks with your own | which seven symbols libbento_ipc leaves for you, their exact signatures, and how to tell whose definition the linker chose | `cm55_controls_snapshot`, `game_sprite_create`, `game_sprite_set`, `game_sprite_lookup`, `ipc_ui_ext_clear_all`, `ipc_ui_ext_dispatch`, `ipc_ui_platform_diag` | on-screen (tap the row) |
| `cm55/display/08_sprites` — Animate an image sprite | build an lv_image_dsc_t with real transparency, create a sprite through the handle table, and swap frames without churning the object | `ui_widget_mgr_create_sprite`, `ui_widget_mgr_set_sprite_image`, `ui_widget_mgr_set_position`, `ui_widget_mgr_get_object`, `ipc_ui_set_container` | on-screen (tap the row) |
| `cm55/display/ref_core` — Reference: the three symbols with no header | the archive exports three symbols that no shipped header declares -- what they are, the correct extern for each, and when you would want them | `calculate_idle_percentage`, `tesaiot_display_ready`, `disp_touch_i2c_controller_context`, `g_tesaiot_display_diag`, `rtos_cm55_gfx_task_handle` | on-screen (tap the row) |
| `cm55/display/ref_display` — Reference: the ipc_core getters and probes | what each remaining read-only call answers, and when you would ask it | `ui_widget_mgr_needs_container`, `ui_widget_mgr_get_parent`, `ui_widget_mgr_count`, `ui_widget_mgr_list`, `ipc_ui_input_activity`, `ipc_sensorhub_weather` | on-screen (tap the row) |

### `sensors` — 4 files, 14 API

| Example | What it teaches | APIs it exercises | Runs on |
|---|---|---|---|
| `cm55/sensors/01_feed_sensor_hub` — Publish a locally-read sensor into the hub | the Eva Kit path - CM55 reads the bus, feeds the hub, and every consumer sees it; plus who owns the sequence number | `ipc_sensorhub_feed_bmi270`, `ipc_sensorhub_feed_bmm350`, `ipc_sensorhub_feed_capsense`, `ipc_sensorhub_feed_pot`, `ipc_sensorhub_snapshot` | on-screen (tap the row) |
| `cm33/sensors/01_i2c_bus_scan` — Scan the sensor I2C bus | the bus has one mutex and several owners - take it, scan, give it | `sensor_i2c_is_init`, `sensor_i2c_lock`, `sensor_i2c_scan` | CM33_NS console |
| `cm33/sensors/02_read_imu` — Read the BMI270 accelerometer and gyroscope | prove the wire before you trust the driver, the real init order, | `bmi270_read_chip_id`, `bmi270_read_accel`, `bmi270_read_gyro` | CM33_NS console |
| `cm33/sensors/03_read_magnetometer` — Read the BMM350 magnetometer and calibrate the compass | a compass is useless until hard-iron calibration converges - how to | `bmm350_read_chip_id`, `bmm350_read_xyz`, `bmm350_read_heading` | CM33_NS console |

### `io` — 3 files, 5 API

| Example | What it teaches | APIs it exercises | Runs on |
|---|---|---|---|
| `cm33/io/01_read_buttons` — Read the CapSense buttons | capture the idle baseline with fingers OFF the pad, poll the two | `capsense_init`, `capsense_read` | CM33_NS console |
| `cm33/io/02_capsense_slider` — Read the CapSense slider position | the slider is 0..100 with no "not touched" value, so you need a | `capsense_init`, `capsense_read_slider` | CM33_NS console |
| `cm33/io/03_read_potentiometers` — Read the potentiometer three ways | raw counts, percent and volts off one SAR channel — and which | `potentiometer_init`, `potentiometer_read_raw` | CM33_NS console |

### `edge_ai` — 6 files, 28 API

| Example | What it teaches | APIs it exercises | Runs on |
|---|---|---|---|
| `cm55/edge_ai/01_first_inference` — Run your first inference | create the engine, activate one model, feed it at its training rate, read the verdict back | `ai_engine_init`, `ai_engine_model_count`, `ai_engine_model`, `ai_engine_set_sensor_rate`, `ai_engine_start`, `ai_engine_requested`, `ai_engine_active`, `ai_engine_snapshot`, `ai_engine_stop`, `ai_engine_resume_sensor` | on-screen (tap the row) |
| `cm55/edge_ai/02_model_registry` — Browse the model registry | enumerate every model the image carries, read its descriptor, and see how much run-time room is left | `ai_engine_model_count`, `ai_engine_model`, `ai_engine_dyn_count`, `ai_engine_dyn_capacity` | on-screen (tap the row) |
| `cm55/edge_ai/03_parallel_set_run` — Watch several models at once | start a parallel set, wait out the window fill, then read EACH member's verdict instead of the last one published | `ai_engine_start`, `ai_engine_set_name`, `ai_engine_set_members`, `ai_engine_mic_settling`, `ai_engine_mic_settle_pct`, `ai_engine_snapshot_model`, `ai_engine_active`, `ai_engine_stop`, `ai_engine_model` | on-screen (tap the row) |
| `cm55/edge_ai/04_set_membership` — Redefine what a set contains | read a set's compiled membership, replace it at run time, read the override back, and put the original back | `ai_engine_set_name`, `ai_engine_set_models`, `ai_engine_set_define`, `ai_engine_set_members_defined`, `ai_engine_model_count`, `ai_engine_model` | on-screen (tap the row) |
| `cm55/edge_ai/08_deepcraft_link` — Drive a model through the DEEPCRAFT link | go through the model link instead of poking the engine, so the READY/STOPPED events fire and CM33 raises the sensor rate -- and keep the watchdog ticking | `deepcraft_task_init`, `deepcraft_task_select`, `deepcraft_task_request`, `deepcraft_task_watchdog` | on-screen (tap the row) |
| `cm55/edge_ai/09_edge_ai_page` — Wire the Edge AI page into your page manager | the create/render/destroy trio is a page-manager callback set -- what each one owes the manager, and why calling them by hand corrupts the header | `page_edge_ai_create`, `page_edge_ai_render`, `page_edge_ai_destroy`, `ipc_sensorhub_snapshot`, `ai_engine_model_count` | on-screen (tap the row) |

### `security` — 7 files, 13 API

| Example | What it teaches | APIs it exercises | Runs on |
|---|---|---|---|
| `cm55/security/01_hsm_screens` — Open the HSM enrol and protect screens | hand a seconds-long secure-element operation to a screen that polls, and always tear the previous overlay down first | `hsm_provision_ui_teardown`, `hsm_enrol_open`, `hsm_protect_open` | on-screen (tap the row) |
| `cm33/security/02_model_signature_hook` — Replace the staged-model signature check with your own | how to override a WEAK symbol the archive exports, and how to return | `optiga_verify_staged_model` | CM33_NS console |
| `cm33/security/03_chip_ownership` — Take the secure element, use it, give it back | the init-before-anything rule, the three names for one re-entrant | `optiga_manager_init`, `optiga_manager_lock`, `optiga_manager_unlock` | CM33_NS console |
| `cm33/security/04_touch_hold` — Keep the touch controller off the bus while the chip works | the counted hold/release pair, why it must wrap the WHOLE operation | `optiga_manager_touch_hold`, `optiga_manager_touch_hold_reason` | CM33_NS console |
| `cm33/security/05_csr_enrolment` — Publish a CSR and track the request that follows it | the request bookkeeping — correlation id, target and anchor OIDs — | `publish_csr`, `trustm_update_state`, `trustm_reset_state` | CM33_NS console |
| `cm33/security/06_protected_update` — Ask the platform for a Protected Update — and what it changes | the request, the anti-rollback counter, what a manifest lock does | `tesaiot_publish_protected_update` | CM33_NS console |
| `cm33/security/ref_hsm` — Reference list — read the HSM's state without starting anything | which calls answer a question without a chip transaction, and the | `optiga_manager_lock`, `optiga_manager_unlock` | CM33_NS console |

### `storage` — 3 files, 15 API

| Example | What it teaches | APIs it exercises | Runs on |
|---|---|---|---|
| `cm55/storage/01_wifi_saved_crud` — Saved networks: add, find, load, update, list, erase | the full CRUD over the OPTIGA credential slots -- and why every one of these calls belongs on a worker task, never on the GFX task | `wifi_saved_count`, `wifi_saved_add`, `wifi_saved_find`, `wifi_saved_load`, `wifi_saved_store`, `wifi_saved_load_all`, `wifi_saved_erase` | on-screen (tap the row) |
| `cm55/storage/02_wifi_saved_async` — Read saved networks without blocking the screen | the start / ready / finish split -- how to drive OPTIGA from an lv_timer inside the GFX task and never stall a frame | `wifi_saved_probe_start`, `wifi_saved_probe_ready`, `wifi_saved_probe_finish`, `wifi_saved_read_start`, `wifi_saved_read_ready`, `wifi_saved_read_result` | on-screen (tap the row) |
| `cm33/storage/03_wifi_creds_lfs` — Read and write saved WiFi networks on LittleFS | the credential store's readiness contract, the checksum-migration | `lfs_wifi_creds_ready`, `lfs_wifi_creds_needs_resave` | CM33_NS console |

### `sensors` — 4 files, 14 API

| Example | What it teaches | APIs it exercises | Runs on |
|---|---|---|---|
| `cm55/sensors/01_feed_sensor_hub` — Publish a locally-read sensor into the hub | the Eva Kit path - CM55 reads the bus, feeds the hub, and every consumer sees it; plus who owns the sequence number | `ipc_sensorhub_feed_bmi270`, `ipc_sensorhub_feed_bmm350`, `ipc_sensorhub_feed_capsense`, `ipc_sensorhub_feed_pot`, `ipc_sensorhub_snapshot` | on-screen (tap the row) |
| `cm33/sensors/01_i2c_bus_scan` — Scan the sensor I2C bus | the bus has one mutex and several owners - take it, scan, give it | `sensor_i2c_is_init`, `sensor_i2c_lock`, `sensor_i2c_scan` | CM33_NS console |
| `cm33/sensors/02_read_imu` — Read the BMI270 accelerometer and gyroscope | prove the wire before you trust the driver, the real init order, | `bmi270_read_chip_id`, `bmi270_read_accel`, `bmi270_read_gyro` | CM33_NS console |
| `cm33/sensors/03_read_magnetometer` — Read the BMM350 magnetometer and calibrate the compass | a compass is useless until hard-iron calibration converges - how to | `bmm350_read_chip_id`, `bmm350_read_xyz`, `bmm350_read_heading` | CM33_NS console |

### `io` — 3 files, 5 API

| Example | What it teaches | APIs it exercises | Runs on |
|---|---|---|---|
| `cm33/io/01_read_buttons` — Read the CapSense buttons | capture the idle baseline with fingers OFF the pad, poll the two | `capsense_init`, `capsense_read` | CM33_NS console |
| `cm33/io/02_capsense_slider` — Read the CapSense slider position | the slider is 0..100 with no "not touched" value, so you need a | `capsense_init`, `capsense_read_slider` | CM33_NS console |
| `cm33/io/03_read_potentiometers` — Read the potentiometer three ways | raw counts, percent and volts off one SAR channel — and which | `potentiometer_init`, `potentiometer_read_raw` | CM33_NS console |

### `connectivity` — 9 files, 27 API

| Example | What it teaches | APIs it exercises | Runs on |
|---|---|---|---|
| `cm33/connectivity/01_link_registry` — Register and look up a transport link | how a backend joins the bento_link registry, and why registration | `bento_link_register`, `bento_link_get`, `bento_link_at` | CM33_NS console |
| `cm33/connectivity/02_link_ipc_backend` — Bring up the IPC backend and pull data back from CM55 | the bidirectional QUERY — send a request, wait for CM55 to fill a | `bento_link_ipc_init`, `bento_link_ipc_query`, `bento_link_get` | CM33_NS console |
| `cm33/connectivity/03_https_session` — One HTTPS session, end to end | connect, GET, POST, disconnect against the TESAIoT gateway — and | `claw_https_connect`, `claw_https_connected`, `claw_https_get` | CM33_NS console |
| `cm33/connectivity/04_circuit_breaker` — Stop hammering a backend that is already failing | drive the breaker CLOSED -> OPEN -> HALFOPEN -> CLOSED and read the | `claw_cb_init`, `claw_cb_allow`, `claw_cb_failure`, `claw_cb_success` | CM33_NS console |
| `cm33/connectivity/05_rate_limit` — Cap how often one tool may be called | check-then-record around every tool call, per-tool budgets, and the | `claw_rate_init`, `claw_rate_set`, `claw_rate_check`, `claw_rate_record` | CM33_NS console |
| `cm33/connectivity/06_session_memory` — Keep a conversation, and remember facts across it | the RAM ring vs the persistent key-value store, how to build an LLM | `claw_session_init`, `claw_session_add`, `claw_session_count` | CM33_NS console |
| `cm33/connectivity/07_trust_policy` — Let the transport decide which tools may run | set the trust level when a session opens, gate every tool on its own | `claw_trust_set`, `claw_trust_get`, `claw_trust_allows` | CM33_NS console |
| `cm33/connectivity/08_tacp_host_protocol` — Pump the TACP host link and answer the IDE | the one-owner rule for the UART, the poll/drain loop, framed | `tacp_init`, `tacp_poll_uart`, `tacp_ring_buf_readable` | CM33_NS console |
| `cm33/connectivity/ref_claw` — Reference list — every read-only call, and the two module objects | which mpy_secure calls are safe to make from any task at any time, | `claw_https_connected`, `claw_cb_state`, `claw_cb_cooldown_remaining` | CM33_NS console |

### `security` — 7 files, 13 API

| Example | What it teaches | APIs it exercises | Runs on |
|---|---|---|---|
| `cm55/security/01_hsm_screens` — Open the HSM enrol and protect screens | hand a seconds-long secure-element operation to a screen that polls, and always tear the previous overlay down first | `hsm_provision_ui_teardown`, `hsm_enrol_open`, `hsm_protect_open` | on-screen (tap the row) |
| `cm33/security/02_model_signature_hook` — Replace the staged-model signature check with your own | how to override a WEAK symbol the archive exports, and how to return | `optiga_verify_staged_model` | CM33_NS console |
| `cm33/security/03_chip_ownership` — Take the secure element, use it, give it back | the init-before-anything rule, the three names for one re-entrant | `optiga_manager_init`, `optiga_manager_lock`, `optiga_manager_unlock` | CM33_NS console |
| `cm33/security/04_touch_hold` — Keep the touch controller off the bus while the chip works | the counted hold/release pair, why it must wrap the WHOLE operation | `optiga_manager_touch_hold`, `optiga_manager_touch_hold_reason` | CM33_NS console |
| `cm33/security/05_csr_enrolment` — Publish a CSR and track the request that follows it | the request bookkeeping — correlation id, target and anchor OIDs — | `publish_csr`, `trustm_update_state`, `trustm_reset_state` | CM33_NS console |
| `cm33/security/06_protected_update` — Ask the platform for a Protected Update — and what it changes | the request, the anti-rollback counter, what a manifest lock does | `tesaiot_publish_protected_update` | CM33_NS console |
| `cm33/security/ref_hsm` — Reference list — read the HSM's state without starting anything | which calls answer a question without a chip transaction, and the | `optiga_manager_lock`, `optiga_manager_unlock` | CM33_NS console |

### `storage` — 3 files, 15 API

| Example | What it teaches | APIs it exercises | Runs on |
|---|---|---|---|
| `cm55/storage/01_wifi_saved_crud` — Saved networks: add, find, load, update, list, erase | the full CRUD over the OPTIGA credential slots -- and why every one of these calls belongs on a worker task, never on the GFX task | `wifi_saved_count`, `wifi_saved_add`, `wifi_saved_find`, `wifi_saved_load`, `wifi_saved_store`, `wifi_saved_load_all`, `wifi_saved_erase` | on-screen (tap the row) |
| `cm55/storage/02_wifi_saved_async` — Read saved networks without blocking the screen | the start / ready / finish split -- how to drive OPTIGA from an lv_timer inside the GFX task and never stall a frame | `wifi_saved_probe_start`, `wifi_saved_probe_ready`, `wifi_saved_probe_finish`, `wifi_saved_read_start`, `wifi_saved_read_ready`, `wifi_saved_read_result` | on-screen (tap the row) |
| `cm33/storage/03_wifi_creds_lfs` — Read and write saved WiFi networks on LittleFS | the credential store's readiness contract, the checksum-migration | `lfs_wifi_creds_ready`, `lfs_wifi_creds_needs_resave` | CM33_NS console |

### `ble` — 16 files, 38 API

| Example | What it teaches | APIs it exercises | Runs on |
|---|---|---|---|
| `cm33/ble/01_advertise` — Bring up NUS and start advertising (STARTS THE BLE RADIO) | how to hand ble_nus a config, read back the advertised name and | `ble_nus_init`, `ble_nus_get_state`, `ble_nus_get_adv_name` | CM33_NS console |
| `cm33/ble/01_nus_bring_up_and_talk` — Bring up NUS, pair, and exchange bytes (STARTS THE BLE RADIO) | the whole transport in one pass — advertise, read the link state, | `ble_nus_init`, `ble_nus_get_state`, `ble_nus_get_adv_name` | CM33_NS console |
| `cm33/ble/02_host_protocol` — Speak the Bento Buddy wire protocol end to end | dispatch a command, emit its ack, queue the acks a human still | `nus_commands_dispatch`, `nus_commands_emit_ack` | CM33_NS console |
| `cm33/ble/02_send_receive` — Move bytes over NUS and drive the newline framer | what ble_nus_send actually returns when there is no link, how to | `ble_nus_send`, `nus_on_rx_bytes`, `nus_protocol_init`, `nus_protocol_tick` | CM33_NS console |
| `cm33/ble/03_commands` — Dispatch a desktop command and emit the ack | how to hand the dispatcher a parsed frame, the exact ack envelope | `nus_commands_dispatch`, `nus_commands_emit_ack` | CM33_NS console |
| `cm33/ble/04_pending_acks` — Track notifications that still need a user acknowledgement | the four-slot pending-ack FIFO — dedup, oldest-first eviction, and | `nus_events_push_pending_ack`, `nus_events_drain_pending_ack` | CM33_NS console |
| `cm33/ble/05_base64_stream` — Decode base64 that arrives split across BLE chunks | how to carry a half-finished 4-char quantum between calls, what the | `nus_b64_init`, `nus_b64_feed`, `nus_b64_flush` | CM33_NS console |
| `cm33/ble/06_folder_push` — Receive a folder pushed from the desktop into LittleFS | the char_begin/file/chunk/file_end/char_end state machine, the | `nus_fp_char_begin`, `nus_fp_file`, `nus_fp_chunk`, `nus_fp_file_end` | CM33_NS console |
| `cm33/ble/07_agent_stream` — Accumulate a streamed agent answer and hand it to the LCD | the ask/token/tool_call/done sequence, the id-matching rule that | `nus_agent_note_ask`, `nus_agent_handle_token`, `nus_agent_handle_done` | CM33_NS console |
| `cm33/ble/08_devmode` — Unlock developer mode with a real HMAC challenge/response | the nonce/HMAC/unlock/lock cycle, the one-shot nonce, the five-fail | `bento_devmode_init`, `bento_devmode_nonce_issue`, `bento_devmode_unlock` | CM33_NS console |
| `cm33/ble/09_firmware_update` — Report firmware identity and run the update physical-ack handshake | the boot-time SHA-256, what bento.fw.query answers, and why an | `fw_hash_compute_at_boot`, `fw_hash_hex`, `fw_hash_prefix8` | CM33_NS console |
| `cm33/ble/10_radio_scheduler` — Arbitrate the single radio between BLE and WiFi | the mode state machine, the boot-mode persistence hooks you must | `radio_scheduler_init`, `radio_scheduler_get_mode` | CM33_NS console |
| `cm33/ble/11_sensor_and_voice` — Stream a sensor to the desktop, and send a voice clip | starting and stopping the single sensor stream, reading the drop | `sensor_stream_init`, `sensor_stream_start`, `sensor_stream_stop` | CM33_NS console |
| `cm33/ble/12_buddy_ipc_bridge` — Start the Buddy BLE stack on demand and bridge it to the CM55 LCD | the lazy start/stop pair the LCD button drives, and the two-way | `bento_buddy_request_start`, `bento_buddy_request_stop` | CM33_NS console |
| `cm33/ble/13_weak_overrides` — Replace the archive's weak WiFi and credential stubs with your own | how the weak-symbol seam works, the five exact signatures, and why | `app_wifi_connect_direct`, `app_wifi_disconnect`, `app_wifi_get_ipv4` | CM33_NS console |
| `cm33/ble/ref_ble` — Reference list — every read-only probe in the ble_nus module | what each inspector returns and when to reach for it | `ble_nus_get_state`, `ble_nus_get_adv_name`, `ble_nus_get_diagnostics` | CM33_NS console |

<!-- END generated -->
