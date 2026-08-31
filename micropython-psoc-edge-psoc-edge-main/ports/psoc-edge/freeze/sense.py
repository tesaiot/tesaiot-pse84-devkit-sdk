# sense.py - หน้าบ้านของเซ็นเซอร์บนบอร์ด แบบที่บรรทัดแรกไม่ต้องรู้ว่าถือบอร์ดใบไหน
#
# ทำไมต้องมี: บรรทัดแรกที่ถูกต้องบนสองบอร์ดนี้ไม่เหมือนกัน และความต่างนั้น
# ไม่ใช่สิ่งที่ผู้เรียนตัดสินใจ มันเป็นเรื่องของสายบนแผ่นวงจร
#
#   บน AI Kit และ Dev Kit   ต้อง sensors.init() ก่อน ไม่งั้นการอ่านจะล้มเหลว
#                           เพราะบัส I2C ยังไม่ได้เปิด (sensor_i2c_read_reg
#                           คืน false ตั้งแต่บรรทัดแรกของ bmi270_read_accel)
#   บน Eva Kit              ห้าม sensors.init() เด็ดขาด เพราะแกน CM55 เป็น
#                           เจ้าของ SCB0 อยู่ ถ้า CM33 ไปเปิดซ้ำจะเป็นมาสเตอร์
#                           ตัวที่สองบนบัสเดียวกัน แล้วบอร์ดค้างจนต้องถอดไฟ
#                           เฟิร์มแวร์จึงปฏิเสธ sensors.init() ด้วย OSError
#
# ผู้เรียนคาบแรกไม่ควรต้องรู้เรื่องนี้เพื่อจะรู้ว่าบอร์ดเอียงกี่องศา โมดูลนี้
# ดูเองว่าอยู่บนบอร์ดใบไหน แล้วเดินทางที่ถูกให้
#
#   ชั้นต้น    sense.tilt() sense.shake() sense.knob() sense.touch()
#   ชั้นกลาง   sense.accel() sense.gyro() sense.motion() sense.temperature()
#              sense.humidity() sense.pressure() sense.heading() sense.slide()
#              sense.ready() sense.available() sense.readings()
#   ชั้นสูง    โมดูล sensors ยังอยู่ครบทุกฟังก์ชัน ไม่ถูกแตะต้อง
#
# เรื่องที่ต้องพูดตรง ๆ ไม่กลบ
#
# 1) บน Eva Kit ค่าที่อ่านได้ไม่ได้มาจากบัสโดยตรง แต่มาจากภาพนิ่งที่แกนจอ
#    เก็บไว้ให้ทุก 200 มิลลิวินาที ค่าจึงเก่าได้ถึงสองในสิบวินาที นั่นคือ
#    ราคาของการไม่แย่งบัสกับจอ ซึ่งถูกกว่าบอร์ดค้างมาก
#
# 2) การอ่านครั้งแรกหลังรีเซ็ตบน Eva Kit ใช้เวลาได้ถึงประมาณ 16 วินาที
#    วัดได้จริงเมื่อ 2026-08-13 ว่าแกนจอเพิ่งเริ่มรับคำขอเซ็นเซอร์ที่ราว
#    วินาทีที่ 13 โมดูลนี้ไม่ได้ทำให้เร็วขึ้น เพราะทำไม่ได้ สิ่งที่ทำได้คือ
#    บอกล่วงหน้าและมี sense.ready() ให้เรียกรอตอนเปิดโปรแกรม ไม่ให้ผู้เรียน
#    เข้าใจว่าบอร์ดพัง
#
# 3) โมดูลนี้ยังไม่ได้ทดสอบบนบอร์ดจริง เขียนขึ้นจากการอ่านตาราง globals ของ
#    modsensors.c และโมดูลลูกทุกไฟล์ ทุกฟังก์ชันที่เรียกมีอยู่จริงในนั้น
#    แต่ยังไม่มีใครรันจริง ถ้าพบพฤติกรรมต่างจากที่เขียนไว้ ให้เชื่อบอร์ดก่อน

import sensors

# ดูว่าบอร์ดใบไหนจากสิ่งที่เฟิร์มแวร์ประกาศไว้ ไม่ใช่จากชื่อรุ่น
#
# sensors.snapshot มีอยู่เฉพาะเมื่อคอมไพล์ด้วย USE_KIT_PSE84_EVAL_EPC2
# (modsensors.c ห่อบรรทัดลงทะเบียนไว้ด้วย ifdef ตัวนั้น) จึงเป็นตัวบอกที่
# ตรงกับความจริงของไบนารีที่กำลังรันอยู่ ไม่ใช่การเดาจากรุ่นบอร์ด
_EVA = hasattr(sensors, "snapshot")

_started = False


def _start():
    """เปิดเซ็นเซอร์ครั้งเดียว เรียกซ้ำได้ไม่เสียหาย"""
    global _started
    if _started:
        return
    if not _EVA:
        try:
            sensors.init()
        except OSError:
            # ปล่อยผ่าน แล้วให้การอ่านจริงเป็นคนบอกว่าตัวไหนเสีย
            pass
    _started = True


def ready(quiet=False):
    """รอจนอ่านเซ็นเซอร์ได้จริง คืน True เมื่อพร้อม

    เรียกไว้บรรทัดแรกของโปรแกรมที่ต้องใช้เซ็นเซอร์ บน Eva Kit บรรทัดนี้
    อาจใช้เวลาถึงประมาณ 16 วินาทีหลังรีเซ็ต และนั่นคือปกติ

    ไม่จำเป็นต้องเรียก ทุกฟังก์ชันในไฟล์นี้เปิดเซ็นเซอร์ให้เองอยู่แล้ว
    มีไว้เพื่อให้การรอเกิดตรงจุดที่ผู้เรียนตั้งใจ ไม่ใช่กลางลูปวาดจอ
    """
    _start()
    if not quiet and _EVA:
        print("sense: กำลังรอแกนจอส่งค่าเซ็นเซอร์ อาจนานถึง 16 วินาทีหลังรีเซ็ต")
    try:
        sensors.bmi270.motion()
    except OSError as e:
        if not quiet:
            print("sense: ยังไม่พร้อม - " + str(e))
        return False
    if not quiet:
        print("sense: พร้อมแล้ว")
    return True


def accel():
    """ความเร่งสามแกน (ax, ay, az) หน่วยเมตรต่อวินาทีกำลังสอง

    วางบอร์ดนิ่งบนโต๊ะ แกนที่ตั้งฉากกับพื้นจะอ่านได้ประมาณ 9.8
    """
    _start()
    return sensors.bmi270.acceleration()


def gyro():
    """ความเร็วเชิงมุมสามแกน (gx, gy, gz) หน่วยองศาต่อวินาที

    บอร์ดนิ่งจะได้ค่าใกล้ศูนย์ทั้งสามแกน หมุนเร็วขึ้นตัวเลขก็โตขึ้น
    """
    _start()
    return sensors.bmi270.gyroscope()


def motion():
    """ความเร่งและการหมุนพร้อมกันหกค่า (ax, ay, az, gx, gy, gz)

    ได้มาจากการอ่านครั้งเดียว ทั้งหกค่าจึงเป็นเวลาเดียวกันจริง ต่างจาก
    การเรียก accel() แล้ว gyro() ซึ่งอาจถูกคั่นกลางได้
    """
    _start()
    return sensors.bmi270.motion()


def tilt():
    """มุมเอียงของบอร์ด (roll, pitch) หน่วยองศา

    roll  คือเอียงซ้ายขวา
    pitch คือเงยหน้าก้มหน้า
    วางราบจะได้ใกล้ (0, 0) ทั้งคู่
    """
    _start()
    import dsp
    a = sensors.bmi270.acceleration()
    return dsp.tilt(a[0], a[1], a[2])


def shake():
    """ความแรงของการเขย่า หน่วยเมตรต่อวินาทีกำลังสอง

    เป็นส่วนต่างระหว่างขนาดความเร่งที่วัดได้กับแรงโน้มถ่วง 9.81
    บอร์ดวางนิ่งจะได้ใกล้ 0 เขย่าแรงจะได้เลขโต ไม่มีเกณฑ์ตายตัวว่าเท่าไร
    ถึงเรียกว่าเขย่า ให้ผู้เรียนวัดเองแล้วเลือกเกณฑ์ของตัวเอง
    """
    _start()
    a = sensors.bmi270.acceleration()
    mag = (a[0] * a[0] + a[1] * a[1] + a[2] * a[2]) ** 0.5
    d = mag - 9.81
    return d if d >= 0 else -d


def temperature():
    """อุณหภูมิห้อง หน่วยองศาเซลเซียส

    ใช้ SHT40 ก่อนถ้ามี แล้วค่อยถอยไป DPS368 ทั้งสองตัวมีบน AI Kit และ
    Dev Kit แต่ไม่มีบน Eva Kit ซึ่งจะแจ้งตรง ๆ แทนที่จะคืนตัวเลขมั่ว
    """
    _start()
    if hasattr(sensors, "sht40"):
        return sensors.sht40.temperature()
    if hasattr(sensors, "dps368"):
        return sensors.dps368.temperature()
    raise OSError("บอร์ดใบนี้ไม่มีเซ็นเซอร์อุณหภูมิห้อง (ไม่มีทั้ง SHT40 "
                  "และ DPS368) ใช้ sense.available() ดูว่ามีอะไรให้ใช้บ้าง")


def humidity():
    """ความชื้นสัมพัทธ์ หน่วยเปอร์เซ็นต์ ต้องมี SHT40"""
    _start()
    if hasattr(sensors, "sht40"):
        return sensors.sht40.humidity()
    raise OSError("บอร์ดใบนี้ไม่มี SHT40 จึงวัดความชื้นไม่ได้")


def pressure():
    """ความกดอากาศ หน่วย hPa ต้องมี DPS368"""
    _start()
    if hasattr(sensors, "dps368"):
        return sensors.dps368.pressure()
    raise OSError("บอร์ดใบนี้ไม่มี DPS368 จึงวัดความกดอากาศไม่ได้")


def heading():
    """ทิศที่บอร์ดหันไป หน่วยองศา 0 คือทิศเหนือ

    เข็มทิศต้องแกว่งบอร์ดเป็นเลขแปดสักครู่ก่อน ค่าถึงจะนิ่ง เพราะโค้ดต้อง
    เห็นค่าสูงสุดต่ำสุดของแต่ละแกนก่อนจึงจะหักค่าออฟเซ็ตของโลหะรอบตัวได้
    """
    _start()
    if not hasattr(sensors, "bmm350"):
        raise OSError("บอร์ดใบนี้ไม่มี BMM350 จึงหาทิศไม่ได้")
    return sensors.bmm350.heading()


def knob():
    """ตำแหน่งของลูกบิด 0.0 ถึง 100.0

    ตัวเลขนี้เป็นสัดส่วน ไม่ใช่แรงดันที่วัดได้จริง ค่าอ้างอิงของ ADC บน
    Eva Kit ยังเป็นคำถามที่ยังไม่มีใครเอามัลติมิเตอร์ไปจิ้ม P15[1] จึงห้าม
    เอาไปสรุปเป็นโวลต์
    """
    _start()
    if not hasattr(sensors, "pot"):
        raise OSError("บอร์ดใบนี้ไม่มีลูกบิดปรับค่า")
    return sensors.pot.percent()


def touch():
    """ปุ่มสัมผัสสองปุ่ม คืน (ปุ่มซ้าย, ปุ่มขวา) เป็น True หรือ False"""
    _start()
    if not hasattr(sensors, "capsense"):
        raise OSError("บอร์ดใบนี้ไม่มีปุ่มสัมผัส")
    return sensors.capsense.buttons()


def slide():
    """ตำแหน่งนิ้วบนแถบสัมผัส 0 ถึง 100"""
    _start()
    if not hasattr(sensors, "capsense"):
        raise OSError("บอร์ดใบนี้ไม่มีแถบสัมผัส")
    return sensors.capsense.slider()


def available():
    """รายชื่อสิ่งที่บอร์ดใบนี้วัดได้จริง คืนเป็น list ของข้อความ

    ใช้ตรวจก่อนเขียนโปรแกรมที่ต้องรันได้ทั้งสองบอร์ด
    """
    out = ["accel", "gyro", "motion", "tilt", "shake"]
    if hasattr(sensors, "sht40"):
        out.append("temperature")
        out.append("humidity")
    if hasattr(sensors, "dps368"):
        if "temperature" not in out:
            out.append("temperature")
        out.append("pressure")
    if hasattr(sensors, "bmm350"):
        out.append("heading")
    if hasattr(sensors, "pot"):
        out.append("knob")
    if hasattr(sensors, "capsense"):
        out.append("touch")
        out.append("slide")
    return out


def readings():
    """อ่านทุกตัวที่มีในครั้งเดียว คืนเป็น dict ซ้อน dict

    รูปร่างของผลต่างกันตามบอร์ด ใช้ตอนอยากเห็นภาพรวม ไม่เหมาะกับลูปที่
    ต้องการความเร็ว เพราะสร้าง dict ใหม่ทุกครั้งบน heap ที่มีอยู่ 64 KB
    """
    _start()
    return sensors.read_all()
