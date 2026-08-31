# Course core 70% — frozen so students just `import bentogame`.
# Scope to bentogame only; vfs_lfs2 stays unfrozen (VFS mounts via C as today).
# NOTE: use $(MPY_DIR) not $(PORT_DIR): in the out-of-tree BentoClaw build the
# manifest's PORT_DIR is set to $(shell pwd) (=proj_cm33_ns), not the port dir.
freeze("$(MPY_DIR)/ports/psoc-edge/freeze", "bentogame.py")

# mic.py - หน้าบ้านของไมโครโฟน ให้ import mic แล้วใช้ได้เลย
# บอร์ดนี้เดินสาย P8_5/P8_6 ไปช่อง PDM 3 และตั้ง sampledelay = 5 เหมือน Eva Kit
# (cycfg_routing.h, cycfg_peripherals.c) ค่าเริ่มต้นใน mic.py จึงใช้ได้ทั้งสองใบ
# machine.PDM_PCM ยังอยู่ครบสำหรับคนที่อยากคุมเอง
freeze("$(MPY_DIR)/ports/psoc-edge/freeze", "mic.py")

# net.py - หน้าบ้านของ WiFi และ MQTT ให้ import net แล้วต่อได้ในบรรทัดเดียว
# ปลอดภัยบนบอร์ดนี้เพราะเรียกเฉพาะฟังก์ชันที่ modwifi.c และ modmqtt.c ลงทะเบียน
# ไว้แบบไม่มี ifdef เลย ทั้งสองไฟล์ถูกคอมไพล์เข้าทุกโปรเจกต์ที่ใช้ manifest นี้
# (MOD_SRC_C += mpy/modwifi.c และ mpy/modmqtt.c ใน Makefile.micropython ไม่มี
# เงื่อนไข BSP ห่ออยู่) จึงไม่มีทางที่ import แล้วเจอฟังก์ชันหาย
# โมดูล wifi และ mqtt ยังอยู่ครบสำหรับคนที่อยากคุมเอง
freeze("$(MPY_DIR)/ports/psoc-edge/freeze", "net.py")

# sense.py - หน้าบ้านของเซ็นเซอร์ ให้ import sense แล้ววัดได้ในบรรทัดเดียว
# ปลอดภัยบนบอร์ดนี้เพราะไม่ได้ผูกกับ BSP ตัวใดตายตัว มันดูจาก hasattr ว่า
# โมดูลลูกตัวไหนถูกลงทะเบียนไว้จริงในไบนารีที่กำลังรัน แล้วเรียกเฉพาะตัวที่มี
# บอร์ดนี้ไม่มี sensors.snapshot จึงเดินทาง sensors.init() ซึ่งเป็นทางที่ถูก
# ของบอร์ดที่ CM33 เป็นเจ้าของบัส I2C เอง
# โมดูล sensors ยังอยู่ครบสำหรับคนที่อยากคุมเอง
freeze("$(MPY_DIR)/ports/psoc-edge/freeze", "sense.py")
