# ตัวอย่าง SDK ฝั่ง CM33 Non-secure

สารบัญฉบับเต็มของตัวอย่างทั้งหมด รวมทั้งฝั่ง CM55 อยู่ที่
[`../../proj_cm55/examples/README.md`](../../proj_cm55/examples/README.md)
(ฉบับภาษาอังกฤษ: [`README.en.md`](../../proj_cm55/examples/README.en.md))

---

## เหตุใดตัวอย่างเหล่านี้จึงอยู่ที่นี่

ไลบรารีสามชุด — `ble_nus`, `mpy_secure`, `tesaiot_hsm` — เป็น archive ของ
Cortex-M33 แบบ soft-float ตรวจสอบได้จากตัวไฟล์เอง

```sh
arm-none-eabi-readelf -A lib/ble_nus/COMPONENT_CM33/.../libbento_secure.a
    Tag_CPU_arch: v8-M.mainline        (ไม่มี Tag_ABI_VFP_args)
```

ขณะที่ `edge_ai`, `cm55_core`, `ipc_core` เป็น `cortex-m55` แบบ hard-float
ทั้งสองแกนมี ABI ไม่ตรงกัน การนำ archive ข้ามแกนไปเชื่อมจะล้มเหลวตั้งแต่ขั้น link
ตัวอย่างของสามไลบรารีนี้จึงอยู่ที่ `proj_cm33_ns/` และเรียกจากหน้าจอบน CM55 ไม่ได้

หน้าจอ **SDK Examples** ยังคงแสดงรายการเหล่านี้ พร้อมคำสั่งที่ใช้รันจริง
เพราะคำถามที่ว่า "เรียกฟังก์ชันใดได้บ้าง" เป็นคำถามเกี่ยวกับ SDK ทั้งชุด

---

## วิธีรัน

CM33_NS เป็นเจ้าของ UART console ตัวอย่างจึงรายงานผลผ่าน `printf`

```sh
# แสดงรายการทั้งหมดบนคอนโซลตอนบูต แต่ไม่รันสิ่งใด
make build ENABLE_PAGE_EXAMPLES=1

# รันรายการที่ระบุ
make build ENABLE_PAGE_EXAMPLES=1 SDK_EXAMPLE_CM33=tesaiot_hsm/01_acquire_chip
```

ตัวรันทำงานที่ priority `tskIDLE_PRIORITY + 1` ซึ่งต่ำกว่าทุก task ที่ใช้ `printf`
ในระบบ จึงเป็นได้เพียงฝ่ายรอ mutex ของ UART ไม่ใช่ฝ่ายที่ถือ mutex แล้วขวาง
task ที่สำคัญกว่า — เป็นเหตุผลเดียวกับที่ heartbeat task ที่ priority 1 ปลอดภัย

---

## ข้อควรทราบ

- **`ble_nus` ยังเชื่อม (link) ไม่ได้ใน template ที่ส่งมอบ**
  `libbento_secure.a` ไม่ปรากฏใน `LDLIBS` ของ makefile ใดเลย และ
  `bento_libs/lib.mk` ที่ `proj_cm33_ns/Makefile` เรียกใช้เมื่อเปิด
  `ENABLE_PAGE_BENTO_BUDDY=1` ไม่มีอยู่จริง ตัวอย่างชุดนี้จึงถูกกำกับด้วย
  `ifeq ($(ENABLE_PAGE_BENTO_BUDDY),1)` ใน Makefile และในตารางที่สร้างอัตโนมัติ
  สถานะปัจจุบันคือ **ผ่านการคอมไพล์และตรวจความครอบคลุมแล้ว แต่ยังไม่ได้เชื่อมเข้าภาพเฟิร์มแวร์**
- **`mpy_secure` มีเฉพาะ variant `mtb-mpy`** จึงกำกับด้วย `ifeq ($(BENTO_HAS_MPY),1)`
- **ห้ามเรียก `printf` ใน IPC callback** เพราะเป็นบริบท ISR
- **สถานะวงจรชีวิต OPTIGA (`LcsO`) เปลี่ยนทางเดียวและย้อนกลับไม่ได้**
  ไม่มีตัวอย่างใดเขียน metadata tag `C0` หรือเลื่อนสถานะ
