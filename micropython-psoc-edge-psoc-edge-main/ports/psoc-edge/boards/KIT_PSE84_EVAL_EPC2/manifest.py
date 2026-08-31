# Course core 70% — frozen so students just `import bentogame`.
# Scope to bentogame only; vfs_lfs2 stays unfrozen (VFS mounts via C as today).
# NOTE: use $(MPY_DIR) not $(PORT_DIR): in the out-of-tree BentoClaw build the
# manifest's PORT_DIR is set to $(shell pwd) (=proj_cm33_ns), not the port dir.
freeze("$(MPY_DIR)/ports/psoc-edge/freeze", "bentogame.py")

# mic.py - หน้าบ้านของไมโครโฟน ให้ import mic แล้วใช้ได้เลย
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
# ปลอดภัยบนบอร์ดนี้ และจำเป็นบนบอร์ดนี้เป็นพิเศษ เพราะ Eva Kit ห้ามเรียก
# sensors.init() เด็ดขาด (CM55 เป็นเจ้าของ SCB0 อยู่ ถ้า CM33 เปิดซ้ำบอร์ดค้าง
# จนต้องถอดไฟ) sense.py ตรวจด้วย hasattr(sensors, "snapshot") ซึ่งมีเฉพาะเมื่อ
# คอมไพล์ด้วย USE_KIT_PSE84_EVAL_EPC2 แล้วเลี่ยง init() ให้เอง ผู้เรียนจึงเขียน
# โปรแกรมเดียวกันได้ทั้งสองบอร์ดโดยไม่ต้องรู้เรื่องบัส
# โมดูล sensors ยังอยู่ครบสำหรับคนที่อยากคุมเอง
freeze("$(MPY_DIR)/ports/psoc-edge/freeze", "sense.py")
