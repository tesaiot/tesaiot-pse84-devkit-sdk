# net.py - หน้าบ้านของการต่อเน็ตและส่งข้อมูลขึ้น MQTT
#
# ทำไมต้องมี: การส่งข้อความหนึ่งบรรทัดขึ้น broker ตอนนี้ต้องเรียกสามโมดูล
# ตามลำดับที่ห้ามสลับ และถ้าพลาดลำดับ ผู้เรียนจะไม่ได้ข้อความบอกเหตุ
#
#     wifi.connect(ssid, pw)                       # คืน False เฉย ๆ ไม่บอกว่าทำไม
#     mqtt.connect(broker, port=1883,              # port กับ client_id
#                  client_id="psoc-edge")          #   ไม่ใช่สิ่งที่ผู้เรียนตัดสินใจ
#     mqtt.publish(topic, payload)                 # ถ้ายังไม่ต่อ จะ raise OSError
#
# สามอย่างที่ผู้เรียนต้องตัดสินใจจริง ๆ มีแค่ ชื่อ WiFi รหัสผ่าน และ broker
# ที่เหลือ (พอร์ต ชื่อเครื่อง keepalive qos ลำดับการเรียก การลองใหม่) เป็นของ
# บอร์ดและของโปรโตคอล ไม่ใช่ของบทเรียน
#
#   ชั้นต้น    net.connect(ssid, pw) - net.broker(host) - net.send(topic, text)
#   ชั้นกลาง   net.online() net.ip() net.info() net.listen() net.receive()
#              net.networks() net.ping() net.close()
#   ชั้นสูง    โมดูล wifi และ mqtt ยังอยู่ครบทุกฟังก์ชัน ไม่ถูกแตะต้อง
#
# ข้อจำกัดที่ต้องพูดตรง ๆ ไม่กลบ
#
# 1) เฟิร์มแวร์ไม่ได้รายงานชื่อ AP และความแรงสัญญาณ wifi.status() ใส่
#    ssid="" และ rssi=0 ตายตัวไว้ใน modwifi.c บรรทัด 306-312 ดังนั้น
#    net.info()['ssid'] จะเป็นชื่อที่ "รอบนี้" เราสั่งต่อเองเท่านั้น ถ้าบอร์ด
#    ต่อเองตอนบูต ค่านี้จะเป็น None และนั่นคือความจริง ไม่ใช่ความผิดพลาด
#    net.info() จึงไม่มีคีย์ rssi เลย ดีกว่ารายงานเลข 0 ที่ไม่ได้วัดอะไร
#
# 2) net.connect() ใช้เวลานานได้ modwifi.c ลองต่อสูงสุด 6 ครั้ง ครั้งละ
#    ประมาณ 13 วินาที และพักระหว่างครั้ง 1.5 วินาที การรอครึ่งนาทีในการต่อ
#    ครั้งแรกหลังเปิดเครื่องจึงเป็นเรื่องปกติ ไม่ใช่บอร์ดค้าง
#
# 3) โมดูลนี้ยังไม่ได้ทดสอบบนบอร์ดจริง เขียนขึ้นจากการอ่านโค้ด C ทุกบรรทัด
#    ที่เรียกใช้ ทุกฟังก์ชันที่เรียกมีอยู่จริงในตาราง globals ของ modwifi.c
#    และ modmqtt.c แต่ยังไม่มีใครรันจริง ถ้าพบพฤติกรรมต่างจากที่เขียนไว้
#    ให้เชื่อบอร์ดก่อนเชื่อไฟล์นี้

import wifi
import mqtt

# ค่าที่จำไว้เพื่อชดเชยสิ่งที่เฟิร์มแวร์ไม่ได้รายงานกลับมา
_ssid = None
_broker = None
_port = 1883
_name = None
_user = None
_pass = None


def connect(ssid, password, force=False):
    """ต่อ WiFi คืน True เมื่อต่อได้

    ssid     ชื่อเครือข่าย
    password รหัสผ่าน
    force    ถ้าบอร์ดต่ออยู่แล้วจะข้ามการต่อใหม่ ตั้ง force=True เมื่อ
             ต้องการย้ายไป AP อื่นจริง ๆ ที่ต้องมีตัวเลือกนี้เพราะเฟิร์มแวร์
             ไม่บอกว่าตอนนี้เกาะ AP ไหนอยู่ เราจึงเดาแทนไม่ได้

    ใช้เวลาได้ถึงประมาณครึ่งนาทีในการต่อครั้งแรก ระหว่างนั้นบอร์ดไม่ได้ค้าง
    """
    global _ssid
    if not force and wifi.is_connected():
        print("net: ต่ออยู่แล้ว ip=" + wifi.ip() + " (ใส่ force=True ถ้าจะย้าย AP)")
        return True
    try:
        ok = wifi.connect(ssid, password)
    except OSError as e:
        # เกิดกับรุ่นที่เปิด BLE ไว้ วิทยุมีชุดเดียว ใช้พร้อมกันไม่ได้
        print("net: ต่อไม่ได้ - " + str(e))
        return False
    if ok:
        _ssid = ssid
        print("net: ต่อแล้ว ip=" + wifi.ip())
        return True
    _ssid = None
    print("net: ต่อ '" + str(ssid) + "' ไม่สำเร็จ")
    print("net: ตรวจสามข้อ ชื่อและรหัสผ่านตรงตัวพิมพ์เล็กใหญ่ไหม")
    print("net:   AP อยู่ในระยะไหม และรองรับ WPA2 หรือ WPA3 ไหม")
    print("net:   ดูรายชื่อที่บอร์ดเห็นจริงด้วย net.networks()")
    return False


def online():
    """ตอนนี้ต่อ WiFi อยู่ไหม"""
    return wifi.is_connected()


def ip():
    """หมายเลข IP ของบอร์ด เป็น '0.0.0.0' เมื่อยังไม่ได้ต่อ"""
    return wifi.ip()


def info():
    """สรุปสถานะเป็น dict สำหรับพิมพ์ดู

    ssid เป็นชื่อที่สั่งต่อในรอบนี้ ถ้าเป็น None แปลว่าไม่รู้ ไม่ใช่ว่าไม่ได้ต่อ
    ไม่มีคีย์ rssi เพราะเฟิร์มแวร์ไม่ได้วัดค่านั้นส่งกลับมา
    """
    d = {}
    d["online"] = wifi.is_connected()
    d["ip"] = wifi.ip()
    d["ssid"] = _ssid
    d["broker"] = _broker
    d["port"] = _port
    d["mqtt"] = mqtt.is_connected()
    return d


def networks(top=8):
    """สแกนหา WiFi รอบตัว คืน list ของ (ชื่อ, ความแรง) เรียงจากแรงไปเบา

    ความแรงเป็น dBm ยิ่งใกล้ 0 ยิ่งแรง เช่น -45 แรงกว่า -80
    top จำกัดจำนวนที่คืน เพื่อไม่ให้กิน heap ที่มีอยู่ 64 KB
    """
    try:
        found = wifi.scan()
    except OSError as e:
        print("net: สแกนไม่ได้ - " + str(e))
        return []
    out = []
    for row in found:
        out.append((row[0], row[1]))
    out.sort(key=lambda r: r[1], reverse=True)
    if top is not None and top < len(out):
        return out[:top]
    return out


def ping(host, timeout=5000):
    """วัดเวลาไปกลับเป็นมิลลิวินาที คืน -1 เมื่อไม่ตอบ

    ต้องเป็นเลข IP เช่น '8.8.8.8' เฟิร์มแวร์ยังไม่มี DNS ให้ ping ใช้
    """
    if not wifi.is_connected():
        print("net: ยังไม่ได้ต่อ WiFi - เรียก net.connect() ก่อน")
        return -1
    try:
        return wifi.ping(host, timeout)
    except ValueError:
        print("net: ping ต้องใส่เป็นเลข IP เช่น '8.8.8.8' ไม่ใช่ชื่อโดเมน")
        return -1
    except OSError as e:
        print("net: ping ไม่ได้ - " + str(e))
        return -1


def _board_name():
    """ตั้งชื่อเครื่องให้ MQTT เอง ไม่ให้ผู้เรียนต้องคิด

    สองบอร์ดที่ชื่อซ้ำกันจะถูก broker ไล่ออกสลับกันไปมา เป็นอาการที่หาสาเหตุ
    ยากมากในห้องเรียน จึงต่อท้ายด้วยเลขที่ต่างกัน

    เลขนั้นมาจากนาฬิกาไมโครวินาที ไม่ใช่รหัสประจำเครื่อง เพราะพอร์ตนี้ยัง
    ไม่มี machine.unique_id() ให้เรียก (ตรวจแล้วในซอร์สของพอร์ต) ผลคือชื่อ
    จะเปลี่ยนทุกครั้งที่รีเซ็ต ซึ่งพอสำหรับกันชนกัน แต่ถ้าต้องการชื่อคงที่
    ให้ส่ง name= เข้ามาเอง วิธีเดียวกับที่ bentogame.py ใช้อยู่แล้ว
    """
    try:
        import time
        return "bento-%04x" % (time.ticks_us() & 0xFFFF)
    except Exception:
        return "bento"


def broker(host, port=1883, name=None, user=None, password=None):
    """บอกว่าจะส่งข้อมูลไปที่ broker ไหน แล้วต่อให้เลย

    host     ที่อยู่ broker เช่น 'test.mosquitto.org' หรือเลข IP
    port     ปกติ 1883 ไม่ต้องเปลี่ยนถ้าไม่มีเหตุผล
    name     ชื่อเครื่องบน broker ถ้าไม่ใส่จะตั้งให้เอง เป็นชื่อที่เปลี่ยน
             ทุกครั้งที่รีเซ็ต ดูเหตุผลใน _board_name() ถ้าอยากได้ชื่อคงที่
             ให้ใส่เอง
    user     ชื่อผู้ใช้ ถ้า broker บังคับ
    password รหัสผ่าน ถ้า broker บังคับ

    หมายเหตุ พอร์ต 1883 คือช่องที่ไม่เข้ารหัส เฟิร์มแวร์รุ่นนี้ต่อ MQTT
    แบบไม่มี TLS เท่านั้น (modmqtt.c ส่ง NULL ให้ช่อง TLS credentials)
    งานที่ต้องเข้ารหัสจริงให้ใช้โมดูล tesaiot ซึ่งเป็นคนละทางเดิน
    """
    global _broker, _port, _name, _user, _pass
    if not wifi.is_connected():
        print("net: ยังไม่ได้ต่อ WiFi - เรียก net.connect() ก่อน")
        return False
    _broker = host
    _port = port
    _name = name if name else _board_name()
    _user = user
    _pass = password
    return _open()


def _open():
    """ต่อ broker ด้วยค่าที่จำไว้ ใช้ทั้งตอนเริ่มและตอนต่อใหม่"""
    if _broker is None:
        return False
    try:
        ok = mqtt.connect(_broker, port=_port, client_id=_name,
                          username=_user, password=_pass)
    except OSError as e:
        print("net: ต่อ broker ไม่ได้ - " + str(e))
        return False
    if ok:
        print("net: ต่อ broker " + str(_broker) + " แล้ว ในชื่อ " + str(_name))
        return True
    print("net: ต่อ broker '" + str(_broker) + "' ไม่สำเร็จ")
    print("net: ตรวจว่าที่อยู่ถูก พอร์ตถูก และเครือข่ายนี้ยอมให้ออกพอร์ต "
          + str(_port) + " ไหม")
    return False


def send(topic, message):
    """ส่งข้อความขึ้น topic คืน True เมื่อส่งออกไปแล้ว

    message เป็นข้อความหรือตัวเลขก็ได้ ตัวเลขจะถูกแปลงเป็นข้อความให้
    ถ้าการเชื่อมต่อหลุดไป จะต่อใหม่ให้หนึ่งครั้งแล้วส่งซ้ำ
    """
    if _broker is None:
        print("net: ยังไม่ได้บอกว่าจะส่งไปที่ไหน - เรียก net.broker(host) ก่อน")
        return False
    if not isinstance(message, str):
        message = str(message)
    if not mqtt.is_connected():
        if not _open():
            return False
    try:
        return mqtt.publish(topic, message)
    except OSError:
        # หลุดระหว่างส่ง ลองต่อใหม่หนึ่งรอบ ไม่วนซ้ำ
        if not _open():
            return False
        try:
            return mqtt.publish(topic, message)
        except OSError as e:
            print("net: ส่งไม่ได้ - " + str(e))
            return False


def listen(topic):
    """สมัครรับข้อความจาก topic แล้วไปอ่านด้วย net.receive()"""
    if _broker is None:
        print("net: ยังไม่ได้บอกว่าจะฟังจากที่ไหน - เรียก net.broker(host) ก่อน")
        return False
    if not mqtt.is_connected():
        if not _open():
            return False
    try:
        return mqtt.subscribe(topic)
    except OSError as e:
        print("net: สมัครรับไม่ได้ - " + str(e))
        return False


def receive():
    """อ่านข้อความที่เข้ามาหนึ่งชิ้น คืน (topic, ข้อความ) หรือ None เมื่อยังไม่มี

    เฟิร์มแวร์เก็บไว้ได้ชิ้นเดียว ชิ้นใหม่ทับชิ้นเก่า ถ้าจะไม่ให้ตกหล่น
    ต้องเรียกในลูปที่วนถี่กว่าข้อความที่ส่งเข้ามา
    """
    m = mqtt.get_message()
    if m is None:
        return None
    payload = m[1]
    try:
        payload = payload.decode()
    except Exception:
        pass
    return (m[0], payload)


def close():
    """ปิด MQTT และ WiFi คืนวิทยุกับหน่วยความจำ"""
    global _broker
    try:
        mqtt.disconnect()
    except Exception:
        pass
    try:
        wifi.disconnect()
    except Exception:
        pass
    _broker = None
    return True
