# TESAIoT firmware template
<!-- //! [doc-drift-fix] — see docs/template_local_deltas.list; a sync that reverts this file must be refused -->

> English version: [README.en.md](README.en.md)

เฟิร์มแวร์ที่ทำงานได้จริงและครบทั้งชุด สำหรับ **TESAIoT Dev Kit** (PSoC Edge
PSE846GPS2DBZC4A) ตั้งใจให้คัดลอกไปแล้วพัฒนาต่อเป็นสินค้าของคุณเอง

บูตขึ้นมาเป็นหน้า Home แบบสัมผัสที่มีเมนู 18 รายการ รัน MicroPython อ่านเซนเซอร์ทุกตัวบนบอร์ด
และคุยกับคลาวด์ของ TESAIoT ทั้งหมดนี้อยู่ในรูปซอร์สที่คุณอ่านและแก้ได้ ยกเว้นหกส่วนที่ส่งมาเป็น
ไลบรารีสำเร็จรูป — ว่าหกส่วนไหนและเพราะอะไร อธิบายไว้ในหัวข้อที่ 6

```bash
./bento.sh            # เริ่มที่นี่
```

---

## 1. สิ่งที่คุณกำลังดูอยู่

PSoC Edge มีหน่วยประมวลผลสามตัว และเฟิร์มแวร์นี้ใช้ครบทั้งสามตัว
เรื่องนี้สำคัญเพราะมันเป็นตัวกำหนดว่าโค้ดของคุณควรไปอยู่ที่ไหน

| คอร์ | รันอะไร | งานที่มักทำ |
|---|---|---|
| **CM33_S** | secure boot | คุณจะไม่ได้แตะส่วนนี้ |
| **CM33_NS** | MicroPython, WiFi, เซนเซอร์, คลาวด์ | Python API, ไดรเวอร์, การเชื่อมต่อ |
| **CM55** | จอ LVGL และหน้า UI ทั้งหมด | หน้าจอ, เมนู, กราฟิก |

คอร์ที่รันแอปพลิเคชันสองตัวคุยกันผ่าน IPC mailbox เซนเซอร์ถูกอ่านบนคอร์หนึ่งแล้วไปวาดบนอีกคอร์หนึ่ง
การเพิ่มหน้าเซนเซอร์ใหม่จึงมักหมายถึงการแก้เล็กน้อยทั้งสองฝั่ง

## 2. การรันครั้งแรก

```bash
./bento.sh doctor     # ของครบหรือยัง

# dependency ถูกดึงแยก "ต่อโปรเจกต์" ไม่มี getlibs ที่ระดับบนสุด
# แต่ละโปรเจกต์ประกาศ deps/*.mtb ของตัวเอง — ถ้ารันเฉพาะใน proj_cm33_ns
# จะได้มาแค่ 33 จาก 41 asset แล้วการ build จะหยุดใน ninja
# ตรงไฟล์ของ optiga-trust-m ที่หายไป
for p in proj_cm33_s proj_cm33_ns proj_cm55; do (cd $p && make getlibs); done

./bento.sh build      # ประมาณ 10 นาที สำหรับ clean build ครบทั้งสามคอร์
./bento.sh flash      # โปรแกรมลงบอร์ดผ่าน KitProg
```

**จากนั้นให้ถอดไฟแล้วเสียบใหม่** การรีเซ็ตผ่าน debugger ไม่พอ — ไฟหน้าจอต้องการขอบสัญญาณ
0→1 แบบเย็น ถ้าไม่ทำจอจะดับอยู่อย่างนั้น ซึ่งดูเหมือนการ flash ล้มเหลวทุกประการ

**`getlibs` ไม่ใช่ทางเลือก และลำดับมีผล** ซอร์สบางส่วนที่เฟิร์มแวร์นี้คอมไพล์ไม่ได้อยู่ในแพ็กเกจนี้เลย
— มันถูกดึงมา แล้วการ build คัดลอกออกมาจาก asset ที่ดึงได้ ดังนั้น `make getlibs` ใน `proj_cm55`
ต้องรัน **ก่อน** การ build ครั้งแรก มิฉะนั้นการ build จะหยุดและบอกคุณ ดูรายละเอียดที่หัวข้อ 7.1

**dependency ที่ถูกแพตช์** เฟิร์มแวร์นี้ต้องการการแก้ไขเฉพาะที่กับ asset ห้าตัวใต้ `mtb_shared`
รวม 11 ไฟล์ ซึ่ง `getlibs` ไม่ได้ให้มา หนึ่งในนั้นทำให้ build หยุด ที่เหลือล้มเหลวแบบเงียบ ๆ
รวมถึงตัวที่ผูกกุญแจ OPTIGA เข้ากับ TLS session การ build จะไม่ยอมเริ่มถ้าขาดไฟล์เหล่านี้
และจะบอกว่าขาดอะไร ส่วน diff เดินทางมาพร้อมแพ็กเกจนี้แล้ว อยู่ใน `third_party_patches/`

`doctor` ตรวจ toolchain และทรีสองชุดที่ template นี้ตั้งใจไม่พกมาด้วย (ดูหัวข้อ 7)
แก้ทุกอย่างที่มันรายงานก่อนเริ่ม build เพราะ error ที่จะได้ถ้าไม่แก้นั้นยาวและไม่ช่วยอะไร

## 3. คำสั่ง CLI

`bento.sh` เรียกเปล่า ๆ จะเปิดเมนู ทุกการกระทำเรียกเป็นคำสั่งย่อยได้ด้วย

| คำสั่ง | ทำอะไร |
|---|---|
| `doctor` | toolchain, archive และทรีที่คุณต้องเตรียมเอง |
| `menus` | เมนูทั้ง 18 รายการ ว่าเปิดอยู่ไหม และกินพื้นที่เท่าไร |
| `enable <menu>` | เปิดเมนู |
| `disable <menu>` | ปิดเมนู — โค้ดยังอยู่ แค่ไม่ถูก build |
| `remove <menu>` | ลบซอร์สของเมนู หลังจากบอกคุณว่าต้องแก้อะไรอีกบ้าง |
| `build` / `flash` / `clean` | วงรอบการ build |
| `verify` | ตรวจไลบรารีสำเร็จรูปเทียบกับลายเซ็น |

`menus` ถาม `make` ทีละ flag แทนที่จะอ่าน Makefile ตรง ๆ เพราะมีหลาย flag ที่ถูกกำหนดค่าสองครั้ง
— ครั้งหนึ่งในเงื่อนไขของบอร์ด อีกครั้งใน else — การอ่านตัวหนังสือจึงให้คำตอบผิด

## 4. การเพิ่มหน้าจอของคุณเอง

เมนูอยู่แยกโฟลเดอร์ละหนึ่งเมนู ใต้ `proj_cm55/modules/page-components/`

```
_core         animation     bentoclaw     edge_ai       environ       examples
gpio_rgb      hsm           joystick      motion        smart_watch   wifi_connect
```

ให้คัดลอก `edge_ai` — เป็นหน้าเดียวที่ต่อสายไว้ครบทุกจุดตามตารางข้างล่าง จึงเป็นตัวที่ปลอดภัย
ที่จะเลียนแบบ (`environ` และ `motion` เล็กกว่า แต่ลงทะเบียนไว้โดยไม่มีการ์ดบนหน้า Home
ซึ่งคือสภาพที่ผิดตามที่อธิบายใต้ตาราง อย่าคัดลอกสองตัวนั้น) เปลี่ยนชื่อ แล้วต่อสายที่จุดเหล่านี้

| ไฟล์ | เพิ่มอะไร |
|---|---|
| `proj_cm55/modules/page-components/_core/page_manager.h` | `PAGE_ID_…` ของคุณใน enum — ระบุค่าตรง ๆ ต่อท้าย ใช้เลขว่างถัดไป และห้ามเรียงเลขใหม่ (ค่าเหล่านี้เป็น ABI ดูคอมเมนต์บนหัว enum) |
| `proj_cm55/modules/page-components/_core/sensorhub_ui.c` | `#include "page_<name>.h"` (วางรวมกับตัวอื่น ประมาณบรรทัด 34-97) และการเรียก `pm_register(...)` |
| `proj_cm55/modules/page-components/_core/page_home.c` | รายการใน `s_card_defs[]` |
| `proj_cm55/Makefile` — ค่าเริ่มต้นของ flag | `ENABLE_PAGE_<NAME> ?= 1` วางรวมกับตัวอื่น (บรรทัด 23-57) |
| `proj_cm55/Makefile` — ตัวกันอัตโนมัติ + `CY_IGNORE` | บรรทัด `$(wildcard …)` ที่บังคับ flag เป็น 0 เมื่อโฟลเดอร์หายไป (บรรทัด 66-81) และบล็อก `CY_IGNORE+=modules/page-components/<name>` (บรรทัด 636-700) |
| `proj_cm55/Makefile` — `INCLUDES+=` และ `DEFINES+=` | `INCLUDES+=modules/page-components/<name>` (บรรทัด 737-798) และ `DEFINES+=ENABLE_PAGE_<NAME>=$(ENABLE_PAGE_<NAME>)` (ตั้งแต่บรรทัด 897) |

ไม่มีคำสั่ง `./bento.sh add` — การต่อสายทำด้วยมือ และ `./bento.sh menus` รายงาน flag
เฉพาะโฟลเดอร์ที่มีอยู่แล้วเท่านั้น

**ต้องครบทุกจุด หรือไม่ทำเลย** หน้าที่ลงทะเบียนไว้แต่ไม่มีการ์ดจะมีตัวตนแต่ไปถึงไม่ได้
ส่วนการ์ดที่ไม่มีการลงทะเบียนจะถูกเมินเงียบ ๆ เมื่อกด — `pm_navigate()` จะ return ทันที
เมื่อหน้านั้นไม่มี `create_cb` — อาการจึงเป็นการ์ดที่กดแล้วไม่เกิดอะไร ไม่ใช่เครื่องค้าง
ส่วนการลืม `INCLUDES+=` จะทำให้ build ล้มด้วย `fatal error: page_<name>.h: No such file or directory`
นี่คือความผิดพลาดที่พบบ่อยที่สุดเวลาเพิ่มหน้าจอ

`_core` ไม่ใช่เมนู — มันถือหน้า Home และ page manager และไม่มีอะไรทำงานได้ถ้าขาดมัน

### การลบเมนู

`./bento.sh disable <menu>` คือทางที่ปลอดภัย: flag เป็น 0 โค้ดไม่ถูกคอมไพล์ และไม่ต้องแก้อะไรอีก
ใช้ `remove` ต่อเมื่อคุณต้องการให้ซอร์สหายไปจริง ๆ และเตรียมใจว่าจะต้องแก้ไฟล์สองไฟล์ที่มันบอกหลังจากนั้น

## 5. เขียนด้วย Python แทน

บอร์ดรัน MicroPython บน CM33_NS ผ่าน USB serial console

```python
import sensors
sensors.scan()          # ที่อยู่ I2C ที่ตอบกลับมา
sensors.read_all()      # เซนเซอร์ทุกตัว ในรูป dict

import ui
ui.Button(...)          # วาดบนจอ CM55 จาก Python ผ่าน IPC

import wifi, tesaiot
wifi.connect("ssid", "password")
tesaiot.config()        # ตั้งค่าคลาวด์
```

ถ้าต้องการให้สคริปต์รันตอนบูต ให้เขียนลงระบบไฟล์ของบอร์ดเป็น `/main.py` จาก REPL
หรือใช้เครื่องมือของ TESAIoT ถ้ามี — template นี้ไม่ได้แนบตัวอัปโหลดมาด้วย

## 6. อะไรที่เป็นไลบรารีสำเร็จรูป และเพราะอะไร

หกส่วนส่งมาเป็น static library ใน `lib/` แทนที่จะเป็นซอร์ส ซอร์สของมันไม่ได้อยู่ในทรีนี้เลย
— นั่นคือประเด็น และการที่ template นี้ build ได้โดยไม่มีมัน คือข้อพิสูจน์

| ส่วน | ไลบรารี | คอร์ |
|---|---|---|
| Bento Buddy BLE agent | `libbento_secure.a` | CM33_NS, ปิดโดยค่าเริ่มต้น |
| หน้า HSM, หน้า Edge AI, การเริ่มจอ | `libbento_cm55.a` | CM55 |
| การอนุมาน Edge AI และเอนจิน parallel-feed | `libbento_edge_ai.a` | CM55 |
| IPC หลัก: service, LCD, UI, sensor hub | `libbento_ipc.a` | CM55 |
| โปรโตคอล TACP, ที่เก็บรหัส WiFi | `libbento_mpy.a` | CM33_NS |
| การลงทะเบียน OPTIGA: CSR และ Protected Update | `libbento_hsm.a` | CM33_NS |

แต่ละตัวอยู่ใน `lib/<area>/` พร้อม `include/` ของตัวเอง, `api.txt` ที่ระบุทุก symbol ที่มันเปิดออกมา,
`consumer_must_provide.txt` ที่ระบุสิ่งที่มันคาดหวังจากคุณ และ `PROVENANCE.txt` ที่บันทึกว่า
มันถูก build เทียบกับโปรเจกต์ไหน

```bash
./bento.sh verify     # ลายเซ็น ECDSA และ SHA-256 ของทุกไฟล์ที่ส่งมา
```

อ่าน `lib/ipc_core/PROVENANCE.txt` ก่อนนำตัวนั้นไปใช้ซ้ำในโปรเจกต์อื่น เพราะมันมีค่าคงที่ของลำดับหน้า
ที่คอมไพล์ติดมาตอน build โปรเจกต์ที่เรียงเมนูต่างออกไปจะลิงก์ผ่านแต่ทำงานผิด

**เอนจิน Edge AI เป็นของเรา แต่โมเดล Edge AI ไม่ใช่**
`libbento_edge_ai.a` คือ registry, ตัวจัดเส้นทางป้อนข้อมูลเซนเซอร์ และตัวโหลดตอนรันไทม์
นั่นคือผลงานของ TESAIoT ส่วนโมเดลที่มันรันนั้นไม่ใช่
`proj_cm55/modules/ai_models/model_motion.{c,h}`, `model_audio.{c,h}` และ `model_radar.{c,h}`
เป็นผลลัพธ์ที่ export จาก **DEEPCRAFT™ Studio** ซึ่งเป็นเครื่องมือ Edge AI ของ Infineon
และเป็นลิขสิทธิ์ของ **Imagimob AB บริษัทในเครือ Infineon Technologies** —
บรรทัดในแต่ละไฟล์เขียนว่า *"Copyright © 2023- Imagimob AB, All Rights Reserved."*
นั่นคือการสงวนสิทธิ์โดยไม่มีการให้สิทธิ์ใด ๆ เขียนไว้ในซอร์ส template นี้จึงให้เครดิตเขา
ไม่ใช่ส่งต่อสิทธิ์ ไม่มีใครที่นี่เทรนมันหรือเป็นเจ้าของมัน สิทธิ์ Apache-2.0 บนโค้ดของ template นี้
ไม่ครอบคลุมเข้าไปข้างในโมเดล และโมเดลถูกใช้ที่นี่เพื่อการวิจัยและการเรียนการสอน ไม่ใช่การใช้งานเชิงพาณิชย์
หากคุณตั้งใจจะออกสินค้าที่มีโมเดลเหล่านี้ หรือสิ่งใดที่ได้มาจากโมเดลหรือจาก DEEPCRAFT™ Studio
ให้ตกลงกับ Infineon และ Imagimob ก่อน เริ่มที่
https://www.infineon.com/design-resources/embedded-software/deepcraft-edge-ai-solutions/deepcraft-studio
รายละเอียดเต็มอยู่ใน `THIRD_PARTY.md` และ `THIRD_PARTY_NOTICES.md` §2.2, §2.4 และ §4.3
ส่วน `proj_cm55/modules/ai_models/README.md` ย้ำเรื่องเดียวกันไว้ตรงที่ไฟล์อยู่

**สิ่งนี้ให้อะไรและไม่ให้อะไร** ไลบรารีซ่อนวิธีการทำงานและชื่อ symbol ภายในไว้
แต่ไม่ได้ซ่อนโปรโตคอล — UUID, คำสั่ง JSON และ format string อ่านได้จากไบนารีใด ๆ
และ disassembler อ่านรหัสเครื่องได้อยู่ดี

และไม่มีกลไกทางเทคนิคใดรองรับอยู่เบื้องหลัง การตรวจสิทธิ์ด้วย OPTIGA UID ที่อธิบายในหัวข้อ 8
ไม่ได้ถูกคอมไพล์เข้าไปในคอร์ใดเลยทั้งสามคอร์: ทั้ง `proj_cm33_ns/Makefile:87` และ
`proj_cm55/Makefile:179-184` ต่างก็ `CY_IGNORE` โฟลเดอร์ที่เก็บ `tesaiot_license.c`,
ไม่มี `tesaiot_license.o` อยู่ในทรี build ใด และ `tesaiot_is_licensed` ไม่ปรากฏใน Release ELF
ทั้งสามตัว (ตรวจด้วย `arm-none-eabi-nm` เมื่อ 2026-08-29)
สิ่งที่จำกัดการใช้งานคือสัญญาอนุญาต — เป็นขอบเขตทางสัญญา ไม่ใช่ขอบเขตที่บังคับด้วยกลไก
การทำให้อ่านยากช่วยเพิ่มต้นทุนของการลอกเลียน แต่ไม่ได้หยุดมัน

## 7. สิ่งที่คุณต้องเตรียมเอง

template พกแอปพลิเคชันและ board support package มาให้ แต่ไม่ได้พกแพลตฟอร์มมาด้วย
เพราะมันใหญ่เกินกว่าจะใส่ใน template — เฉพาะ MicroPython port อย่างเดียวก็ 154 MB แล้ว

```
<workspace ของคุณ>/
  micropython-psoc-edge-psoc-edge-main/   154 MB   MicroPython port
  mtb_shared/                             1.9 GB   make getlibs
  bento-firmware-template/                         โฟลเดอร์นี้
```

โดยค่าเริ่มต้น template จะหา workspace ที่ระดับเหนือขึ้นไปหนึ่งชั้น (ภายใน repository ของ release
มันอยู่ลึกลงไปอีกหนึ่งชั้น และมันตรวจพบเอง) ถ้าจะชี้ไปที่อื่นใช้

```bash
make build BENTO_WORKSPACE=/path/to/workspace
```

ตัวแปรนั้นถูกอ่านโดย `common.mk`, `proj_cm33_ns/Makefile.micropython` และ `bento.sh`

### 7.1 ซอร์สที่ถูกดึงมา แทนที่จะส่งมาด้วย

ไฟล์บางส่วนที่เฟิร์มแวร์นี้คอมไพล์ **ไม่ได้** อยู่ในแพ็กเกจนี้โดยตั้งใจ เพราะเป็นของบุคคลที่สาม
ที่สัญญาอนุญาตอนุญาตให้เราส่งไบนารีได้แต่ส่งซอร์สไม่ได้ การ build จึงดึงมาจาก asset ที่ผู้ผลิต
เผยแพร่เอง แล้วใช้จากที่นั่น ไม่มีอะไรสูญหาย — asset คือต้นฉบับที่เชื่อถือได้ และห่างออกไปแค่ `getlibs` เดียว

| อะไร | ต้นทาง | ตรึงไว้โดย |
|---|---|---|
| การตั้งค่า emUSB-Host — `usbh_config.c`, `usbh_config_io.c` | [github.com/Infineon/emusb-host](https://github.com/Infineon/emusb-host) `release-v2.2.0` ไฟล์อยู่ที่ `export/Config/` | `proj_cm55/deps/emusb-host.mtb` |

emUSB-Host เป็น USB stack ของ SEGGER ซึ่ง SEGGER ให้สิทธิ์ Cypress เฉพาะการแจกจ่ายในรูป object code
ซอร์สของมันจึงเดินทางมากับแพ็กเกจนี้ไม่ได้ Infineon ใส่ไฟล์ตั้งค่าสองตัวไว้ใน asset ให้ผู้นำไปใช้คัดลอกออกมา
— `.cyignore` ของ asset กัน `export/Config` ไว้ก็เพื่อไม่ให้ ModusToolbox คอมไพล์มันตรงที่มันอยู่ —
และ `materialize_emusb_config.sh` ทำการคัดลอกนั้นทุกครั้งที่เริ่ม build `proj_cm55`
แล้วใส่การเปลี่ยนแปลงสามอย่างที่เป็นของเรา (ลดลำดับความสำคัญของ ISR และตัวนับสองตัวที่ไดรเวอร์ joystick
อ่านเพื่อแยก "ไม่มีอุปกรณ์" ออกจาก "มีอุปกรณ์แต่เงียบ") สคริปต์นั้นคือบันทึกที่อ่านได้ว่าเราเปลี่ยนอะไรและทำไม

**ผลต่อคุณ:** รัน `make getlibs` ใน `proj_cm55` ก่อน build ครั้งแรก ถ้าไม่ทำ การ build จะหยุด
พร้อมข้อความบอกชื่อ asset ที่ขาด แทนที่จะเป็น undefined reference เต็มหน้าจอ
การรันซ้ำไม่มีผลเสีย เพราะการคัดลอกเป็น idempotent และเกิดขึ้นทุกครั้งที่ build
asset ที่ดึงมากับทรีของคุณจึงไม่มีทางแยกจากกัน

หากคุณกำลังแพ็ก template นี้ใหม่ ให้ทราบว่าไฟล์เหล่านี้จะปรากฏบนดิสก์หลังการ build ใด ๆ
มันถูกกันออกจากแพ็กเกจโดย `bento-release.sh` และผลถูกตรวจที่นั่น — อย่า commit และอย่าส่งมันไป

คุณยังต้องใช้ **ModusToolbox 3.6 เท่านั้น** และ ARM GCC ที่มากับมัน ใหม่กว่าไม่ได้ดีกว่าในกรณีนี้:
Configurator รุ่นหลังจะสร้างการตั้งค่า BSP ใหม่จาก `design.modus` แล้วออก notice ที่ `-Werror=cpp`
เปลี่ยนเป็น error ในไฟล์ที่คุณไม่เคยแตะ ใช้ `BENTO_MTB_VERSION` เพื่อข้ามการตรึงนี้
ถ้าคุณตั้งใจจะตรวจสอบใหม่เอง

## 8. การขอสิทธิ์ใช้งานสำหรับบอร์ดของคุณ

`bento_libs/claw/kit-pse84-ai/tesaiot/include/tesaiot_license_config.h` มีค่าตัวอย่างวางไว้
วิธีได้ค่าจริง

```python
import optiga
print(optiga.uid())     # เลขฐานสิบหก 54 ตัว ไม่ซ้ำกับบอร์ดอื่น
```

ส่ง UID นั้นให้ TESAIoT แล้วคุณจะได้ลายเซ็นกลับมา ใส่ทั้งสองค่าลงใน header แล้ว build ใหม่

**ต้องเข้าใจให้ตรงกันว่าวันนี้สิ่งนี้ทำอะไร** การใส่ค่าลงใน header ไม่เปลี่ยนพฤติกรรมของเฟิร์มแวร์เลย
เพราะโค้ดที่จะอ่านค่านั้นไม่ได้ถูก build `tesaiot_license.c` อยู่ในโฟลเดอร์ที่ Makefile
ของทั้งสองคอร์ `CY_IGNORE` (`proj_cm33_ns/Makefile:87`, `proj_cm55/Makefile:179-184`),
ไลบรารีสำเร็จรูป `tesaiot/lib/libtesaiot_license.a` ไม่ถูกลิงก์โดยอะไรเลย และ
`tesaiot_is_licensed` ไม่มีอยู่ใน Release ELF ทั้งสามตัว (ตรวจด้วย `arm-none-eabi-nm` เมื่อ 2026-08-29)
ส่วน `tesaiot.license_verify()` มีอยู่ใน MicroPython แต่ตัวจัดการของมันถูกคอมไพล์ออกไปด้วย
`ENABLE_OPTIGA=0` (`proj_cm55/Makefile:109`) และตอบกลับว่า "ไม่พร้อมใช้งาน"

ถึงอย่างนั้นก็ให้ลงทะเบียน UID ไว้ เพราะเป็นวิธีที่บอร์ดของคุณถูกบันทึกว่าได้รับสิทธิ์
และเป็นค่าที่ตัวตรวจจะอ่านเมื่อมันถูกต่อสายเข้ามาในอนาคต
เพียงแต่อย่าเข้าใจว่ามันหยุดเฟิร์มแวร์ที่ไม่มีสิทธิ์ไม่ให้ทำงานได้ในตอนนี้

## 9. เมื่อมีอะไรผิดพลาด

| อาการ | สาเหตุ |
|---|---|
| linker หาฟังก์ชันจาก `lib/` ไม่เจอ | `LDLIBS` ถูกกำหนดหลัง `include start.mk` — ModusToolbox อ่านค่านั้นระหว่าง include ไฟล์นั้น อะไรที่มาทีหลังจะไปไม่ถึง linker |
| เมนูหายไปจากหน้า Home | flag เป็น 0 หรือ `s_card_defs[]` ไม่มีรายการ ใช้ `./bento.sh menus` ดูว่าเป็นอันไหน |
| กดการ์ดแล้วไม่เกิดอะไร | การ์ดมีอยู่แต่ไม่มี `pm_register` — `pm_navigate()` เมินหน้าที่ไม่มี `create_cb` ดูหัวข้อ 4 |
| `undefined reference` ถึงสิ่งที่มีอยู่ในซอร์ส | object file ค้าง ใช้ `./bento.sh clean` แล้ว build ใหม่ |
| จอดำ เซนเซอร์คืน `[]` หลัง flash ซ้ำหลายรอบ | บัส I2C ที่ใช้ร่วมกันค้าง ให้ถอด USB ออกจนสุด รอสิบวินาที แล้วเสียบใหม่ — ปุ่มรีเซ็ตไม่พอ |
| MicroPython ทำงานแปลก ๆ หลังลบซอร์สของไลบรารี | qstr pool ขยับ ดู `bento_archived_qstrs.c` — มันมีอยู่เพื่อกันเรื่องนี้โดยเฉพาะ |

## 10. โครงสร้างไฟล์

```
bento.sh                   ตัว CLI
Makefile common.mk         จุดเข้าของการ build
bsps/                      board support package
configs/                   การตั้งค่าการเซ็นและการบูต
proj_cm33_s/               คอร์ secure boot
proj_cm33_ns/              MicroPython, WiFi, เซนเซอร์, คลาวด์
proj_cm55/
  modules/
    page-components/
      _core/               หน้า Home, page manager, page_id_t  (ขาดไม่ได้)
      <menu>/              หนึ่งโฟลเดอร์ต่อหนึ่งเมนู  <- หน้าจอของคุณอยู่ที่นี่
    lvgl_display/          ไดรเวอร์จอและ LVGL port
    ai_models/             โมเดลจาก DEEPCRAFT(TM) Studio, (c) Imagimob AB —
                           ดู README.md ของมัน ไม่ใช่ผลงานของ TESAIoT
    deepcraft_task/ ...
bento_libs/                ไลบรารี BENTO ที่ใช้ร่วมกัน ในรูปซอร์ส
lib/                       หกส่วนสำเร็จรูป พร้อม header และลายเซ็น
```

---

build และตรวจสอบแล้วบน TESAIoT Dev Kit: สามคอร์, `app_combined.hex`
15,939,088 ไบต์, เซนเซอร์ตอบกลับที่ I2C `0x18`, `0x68`, `0x77`
