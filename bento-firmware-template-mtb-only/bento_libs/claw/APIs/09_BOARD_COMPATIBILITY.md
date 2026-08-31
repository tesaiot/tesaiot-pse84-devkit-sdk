# Board Compatibility Matrix

**BENTO PSoC Edge E84 — Cross-Board API Reference**

---

## 1. Board Overview

| Property | AI Kit | Eva Kit | Game Console |
|----------|--------|---------|--------------|
| **Project** | `KIT_PSE84_AI-MicroPython-AI-Core` | `KIT_PSE84_EVAL_EPC2-MicroPython-AI-Core` | `KIT_PSE84_AI-MicroPython-AI-Game` |
| **TARGET** | `APP_KIT_PSE84_AI` | `APP_KIT_PSE84_EVAL_EPC2` | `APP_KIT_PSE84_AI` |
| **SoC** | PSoC Edge E84 (CYW55513) | PSoC Edge E84 (CYW55513) | PSoC Edge E84 (CYW55513) |
| **Cores** | CM33_S + CM33_NS + CM55 | CM33_S + CM33_NS + CM55 | CM33_S + CM33_NS + CM55 |
| **Display** | 4" 800x480 TFT | 4" 800x480 TFT | 4" 800x480 TFT |
| **WiFi** | CYW55513 SoftAP + STA | CYW55513 SoftAP + STA | CYW55513 SoftAP + STA |
| **USB Host** | USB-HS (CM55) | USB-HS (CM55) | USB-HS (CM55) |
| **QSPI Flash** | 64 MB | 64 MB | 64 MB |
| **GC Heap** | 112 KB | 112 KB | 112 KB |

---

## 2. BSP Feature Flags

| Flag | AI Kit | Eva Kit | Game Console | Header |
|------|--------|---------|--------------|--------|
| `BSP_HAS_BMI270` | 1 | 1 | 1 | `bsp_feature_flags.h` |
| `BSP_HAS_BMM350` | 1 | 1 | 0 | `bsp_feature_flags.h` |
| `BSP_HAS_DPS368` | 1 | 0 | 0 | `bsp_feature_flags.h` |
| `BSP_HAS_SHT40` | 1 | 0 | 0 | `bsp_feature_flags.h` |
| `BSP_HAS_CAPSENSE` | 0 | 1 | 0 | `bsp_feature_flags.h` |
| `BSP_HAS_POTENTIOMETER` | 0 | 1 | 0 | `bsp_feature_flags.h` |
| `BSP_HAS_RADAR` | 1 | 0 | 0 | `bsp_feature_flags.h` |
| `BSP_HAS_USB_HOST` | 1 | 1 | 1 | `bsp_feature_flags.h` |
| `BSP_HAS_PDM_MIC` | 1 | 1 | 1 | `bsp_feature_flags.h` |

---

## 3. Sensor API Compatibility

| Sensor | C API Header | AI Kit | Eva Kit | Game Console | I2C/Bus | Address |
|--------|-------------|--------|---------|--------------|---------|---------|
| **BMI270** (Accel+Gyro) | `sensor_bmi270.h` | Y | Y | Y | SCB0 I2C | 0x68 |
| **DPS368** (Barometer) | `sensor_dps368.h` | Y | — | — | SCB0 I2C | 0x77 |
| **SHT40** (Climate) | `sensor_sht40.h` | Y | — | — | SCB0 I2C | 0x44 |
| **BMM350** (Magnetometer) | `sensor_bmm350.h` | Y | Y | — | I3C | 0x15 |
| **CapSense** (Touch) | `sensor_capsense.h` | — | Y | — | SCB0 I2C | 0x08 |
| **Potentiometer** (ADC) | `sensor_potentiometer.h` | — | Y | — | SAR ADC | P15[1] |
| **Radar** (60GHz) | `radar_task.h` | Y (CM55) | — | — | SPI | — |
| **Sensor I2C Bus** | `sensor_i2c.h` | Y | Y | Y | SCB0 | — |
| **Sensor Auto Task** | `sensor_auto_task.h` | Y | Y | Y | — | — |

---

## 4. Hardware API Compatibility

| Hardware | API Doc | AI Kit | Eva Kit | Game Console | Notes |
|----------|---------|--------|---------|--------------|-------|
| **GPIO LED** | `02_HARDWARE` | 5 LEDs | 2 LEDs | Y | LED count differs |
| **GPIO Button** | `02_HARDWARE` | 1 (SW1) | 1 (SW1) | 1 (SW1) | Active-low |
| **Machine Pin** | `02_HARDWARE` | Y | Y | Y | Standard MicroPython |
| **Machine RTC** | `02_HARDWARE` | Y | Y | Y | BREG 28-byte backup |
| **Machine PDM_PCM** | `02_HARDWARE` | Y | Y | Y | Depends on PDM peripheral |
| **I2C Target** | `02_HARDWARE` | Y | Y | Y | SCB-based |
| **Camera DVP** (OV7675) | `02_HARDWARE` | Y | — | — | DVP parallel interface |
| **Camera UVC** (USB) | `02_HARDWARE` | Y | Y | Y | USB Video Class |
| **USB Joystick** | `02_HARDWARE` | Y | Y | Y | F310 via IPC to CM55 |

---

## 5. UI API Compatibility

| Feature | AI Kit | Eva Kit | Game Console | Notes |
|---------|--------|---------|--------------|-------|
| **16 Widget Types** | Y | Y | Y | All via IPC |
| **LCD Terminal** | Y | Y | Y | Markup support |
| **Built-in Icons** (16) | Y | Y | Y | 24x24 mono bitmaps |
| **Max Widgets** | 32 | 32 | 32 | — |
| **LVGL Fonts** | 14,16,20,24,28,36,40 | 12,14,16,18,20,22,24,28 | 14,16,20,24,28 | Vary by project |
| **Page Manager** | Full (12 pages) | Full (12 pages) | Reduced (6 pages) | Game pages differ |

---

## 6. Connectivity API Compatibility

| Feature | AI Kit | Eva Kit | Game Console | Notes |
|---------|--------|---------|--------------|-------|
| **WiFi STA** | Y | Y | Y | CYW55513 |
| **WiFi SoftAP** | Y | Y | Y | 192.168.4.1:554 |
| **WiFi Scan** | Y | Y | Y | — |
| **WiFi Cred Storage** | Y | Y | Y | LittleFS + CRC32 |
| **MQTT Client** | Y | Y | Y | cy_mqtt |
| **RTSP Server** | Y | — | — | Camera required |

---

## 7. Security API Compatibility

| Feature | AI Kit | Eva Kit | Game Console | Notes |
|---------|--------|---------|--------------|-------|
| **OPTIGA Trust M** | Y | Y | — | I2C SCB0, touch pause/resume |
| **TESAIoT Crypto** | Y | Y | — | `libtesaiot_security.a` |
| **HSM IPC** | Y | Y | — | 8 commands (0xB5-0xBC) |
| **Device Licensing** | Y | Y | — | `libtesaiot_license.a` |

---

## 8. Edge Computing API Compatibility

| Feature | AI Kit | Eva Kit | Game Console | Notes |
|---------|--------|---------|--------------|-------|
| **DSP Filters** (6 types) | Y | Y | Y | Pure Python float math |
| **IMU Fusion** (Madgwick) | Y | Y | Y | Requires BMI270 |
| **Pedometer** | Y | Y | Y | Requires BMI270 |
| **Stateless Functions** | Y | Y | Y | tilt, compass, altitude, etc. |

---

## 9. Service API Compatibility

| Feature | AI Kit | Eva Kit | Game Console | Notes |
|---------|--------|---------|--------------|-------|
| **TACP File Transfer** | Y | Y | Y | IDE ↔ Board |
| **MicroPython REPL** | Y | Y | Y | Raw + Friendly |
| **Boot System** | Y | Y | Y | Safe boot, main.py |
| **NTP Sync** | Y | Y | Y | Requires WiFi |
| **LittleFS Filesystem** | Y | Y | Y | 64 MB QSPI |

---

## 10. IPC Command Compatibility

All IPC commands are available on all boards. Command behavior may vary based on BSP feature flags:

| Category | Commands | AI Kit | Eva Kit | Game Console |
|----------|----------|--------|---------|--------------|
| **UI** (0x50-0x62) | 19 | Y | Y | Y |
| **GPIO/LED** (0x80-0x81) | 2 | Y | Y | Y |
| **Sensor** (0x91-0x99) | 9 | All sensors | BMI270+BMM350+CapSense+Pot | BMI270 only |
| **TESAIoT** (0xA0-0xAF) | varies | Y | Y | — |
| **Radar** (0xB0) | 1 | Y | — | — |
| **HSM** (0xB5-0xBC) | 8 | Y | Y | — |
| **Joystick** (0xC0-0xC1) | 2 | Y | Y | Y |
| **WiFi/Time** (0xD0-0xD9) | 10 | Y | Y | Y |
| **LCD** (0xE0-0xE2) | 3 | Y | Y | Y |
| **MQTT** (0xF0-0xF6) | 7 | Y | Y | Y |

---

## 11. Memory Layout (CM33_NS)

| Resource | AI Kit | Eva Kit | Game Console |
|----------|--------|---------|--------------|
| **Total RAM** | 256 KB | 256 KB | 256 KB |
| **.bss** | ~186 KB | ~186 KB | ~170 KB |
| **.heap** (FreeRTOS) | ~63 KB | ~63 KB | ~63 KB |
| **.data** | ~2 KB | ~2 KB | ~2 KB |
| **Free** | ~4 KB | ~4 KB | ~20 KB |
| **GC Heap** (MicroPython) | 112 KB | 112 KB | 112 KB |
| **QSPI Flash** | 64 MB | 64 MB | 64 MB |
| **MicroPython Stack** | 8 KB | 8 KB | 8 KB |

---

## 12. Static Library Mapping

| Library | Contents | AI Kit | Eva Kit | Game Console |
|---------|----------|--------|---------|--------------|
| `libmicropython.a` | MicroPython core (VM, parser, GC, extmod) | 994 KB | 994 KB | 994 KB |
| `libtesaiot_sensors.a` | sensor_i2c + sensor_bmi270/dps368/sht40/bmm350/capsense/pot | To build | To build | To build |
| `libtesaiot_ipc_cm33.a` | cm33_ipc_communication.c | To build | To build | To build |
| `libtesaiot_wifi_creds.a` | lfs_wifi_creds + qspi_wifi_creds | To build | To build | To build |
| `libtesaiot_security.a` | OPTIGA + TESAIoT crypto | Pre-built | Pre-built | — |
| `libtesaiot_license.a` | Device licensing | Pre-built | Pre-built | — |

---

## 13. Per-Project Unique Files

These files exist independently per project and are NOT shared:

| File | Purpose | Differs Between Projects? |
|------|---------|--------------------------|
| `sensorhub_ui.c` | Page registration (`pm_register`) | Yes — different pages per board |
| `page_home.c` | Home card grid (`s_card_defs[]`) | Yes — different cards per board |
| `page_manager.h` | `page_id_t` enum | Yes — Game has game pages |
| `proj_cm33_ns/Makefile` | Build defines, WHD_PRINT_DISABLE | Yes — BSP flags differ |
| `mpconfigboard.h` | Board name, branding | Yes — different board names |
| `bsp_feature_flags.h` | Sensor availability | Yes — core differentiator |
