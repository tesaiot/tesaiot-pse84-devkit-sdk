/*
 @licstart  The following is the entire license notice for the JavaScript code in this file.

 The MIT License (MIT)

 Copyright (C) 1997-2020 by Dimitri van Heesch

 Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 and associated documentation files (the "Software"), to deal in the Software without restriction,
 including without limitation the rights to use, copy, modify, merge, publish, distribute,
 sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all copies or
 substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 @licend  The above is the entire license notice for the JavaScript code in this file
*/
var NAVTREE =
[
  [ "SDK สำหรับ TESAIoT Dev Kit", "index.html", [
    [ "ภาพรวม", "index.html", null ],
    [ "เริ่มต้นใช้งาน", "group__feat__getting__started.html", [
      [ "A0 — สร้างอะไรได้บ้าง และแต่ละส่วนอยู่ที่ใด", "group__a0__orientation.html", null ],
      [ "A1 — จากไฟล์ zip ถึงโปรแกรมแรก", "group__a1__first__build.html", null ]
    ] ],
    [ "สถาปัตยกรรมและการบูต", "group__feat__architecture.html", [
      [ "B1 — เดินดูลำดับการบูตของ CM33_NS", "group__b1__cm33__boot.html", null ],
      [ "B2 — การบูต CM55 จนถึงเฟรมแรก", "group__b2__cm55__boot.html", null ],
      [ "B3 — แกนหลักของ IPC: การตั้งค่า deferred binding และ snapshot", "group__b3__ipc__backbone.html", null ]
    ] ],
    [ "อุปกรณ์ต่อพ่วง: เซนเซอร์และ GPIO", "group__feat__peripherals.html", [
      [ "Tutorial", "group__feat__peripherals__tut.html", [
        [ "J1 — บัสของเซนเซอร์และ lock ของมัน", "group__j1__sensor__bus.html", null ],
        [ "J2 — การอ่านเซนเซอร์ทั้ง 6 ตัว", "group__j2__sensors__api.html", null ],
        [ "J3 — task ดันข้อมูลอัตโนมัติกับ sensor hub", "group__j3__sensor__auto.html", null ],
        [ "J4 — LED และปุ่มกด", "group__j4__gpio.html", null ],
        [ "J5 — บอร์ดฐาน QWA309: pot ปุ่ม RGB matrix และ header", "group__j5__qwa309.html", null ],
        [ "J6 — เรดาร์", "group__j6__radar.html", null ]
      ] ],
      [ "อุปกรณ์ต่อพ่วงโดยสรุป", "group__peripherals__quickref.html", null ],
      [ "API ของอุปกรณ์ต่อพ่วงอยู่ที่ใด", "group__feat__peripherals__apis.html", null ]
    ] ],
    [ "Edge AI", "group__feat__edge__ai.html", [
      [ "Tutorial", "group__feat__edge__ai__tut.html", [
        [ "E1 — Select, confirm, start: REQUESTED เทียบกับ ACTIVE", "group__tut__e1__select__confirm__start.html", null ],
        [ "E2 — Parallel set และการวนถามผลลัพธ์", "group__tut__e2__parallel__sets.html", null ],
        [ "E3 — Stop, unload และโมเดล staged", "group__tut__e3__stop__unload__staged.html", null ],
        [ "E4 — การอ่านข้อมูลวินิจฉัยของ Edge AI", "group__tut__e4__diagnostics.html", null ]
      ] ],
      [ "Edge AI (libbento_edge_ai.a)", "group__edge__ai__api.html", null ]
    ] ],
    [ "ความปลอดภัย / HSM", "group__feat__security.html", [
      [ "Tutorial", "group__feat__security__tut.html", [
        [ "D1 — วินัยการเข้าถึงชิป: gate, lock, touch-hold", "group__d1__chip__access__discipline.html", null ],
        [ "D2 — การลงทะเบียนและ Protected Update ตั้งแต่ต้นจนจบ", "group__d2__enrolment__protected__update.html", null ],
        [ "D3 — Weak symbol, `ENABLE_OPTIGA_CLM` และข้อผูกพันของผู้ใช้ไลบรารี", "group__d3__weak__symbols__clm.html", null ]
      ] ],
      [ "TESAIoT HSM (libbento_hsm.a)", "group__tesaiot__hsm__api.html", null ]
    ] ],
    [ "การเชื่อมต่อ: WiFi และคลาวด์", "group__feat__connectivity.html", [
      [ "Tutorial", "group__feat__connectivity__tut.html", [
        [ "C1 — WiFi: ที่เก็บข้อมูลรับรอง 2 แห่งกับการเชื่อมต่ออัตโนมัติตอนบูต", "group__c1__wifi__two__stores.html", null ],
        [ "C2 — WiFi จาก UI ผ่าน IPC", "group__c2__wifi__ui__ipc.html", null ],
        [ "C3 — TESAIoT cloud: ไฟล์ config → MQTT task → broker", "group__c3__cloud__mqtt.html", null ],
        [ "C4 — mTLS: ตัวตนของ TLS ที่ยึดกับ OPTIGA", "group__c4__mtls__optiga.html", null ],
        [ "C5 — TESAIoT cloud: HTTPS REST, Device API Key และ mTLS", "group__c5__cloud__https.html", null ]
      ] ],
      [ "API แต่ละตัวอยู่ที่ใด", "group__feat__connectivity__apis.html", null ]
    ] ],
    [ "หน้า UI และ IPC", "group__feat__ui__ipc.html", [
      [ "Tutorial", "group__feat__ui__ipc__tut.html", [
        [ "F1 — การเพิ่มหน้าจอ: 3 ไฟล์ กับความจริงเรื่อง Makefile", "group__tut__f1__adding__a__screen.html", null ],
        [ "F2 — การขับ widget จาก MicroPython ผ่าน IPC", "group__tut__f2__widgets__over__ipc.html", null ]
      ] ],
      [ "IPC Core (libbento_ipc.a)", "group__ipc__core__api.html", null ],
      [ "CM55 Core (libbento_cm55.a)", "group__cm55__core__api.html", null ]
    ] ],
    [ "BLE / Bento Buddy", "group__feat__ble.html", [
      [ "Tutorial", "group__feat__ble__tut.html", [
        [ "I1 — การ bring-up BLE และกฎวิทยุเดียว", "group__tut__i1__ble__bringup__single__rf.html", null ],
        [ "I2 — ส่วนที่เรียกใช้ได้ของโปรโตคอล NUS", "group__tut__i2__nus__protocol__surface.html", null ]
      ] ],
      [ "BLE NUS / Bento Buddy (libbento_secure.a)", "group__ble__nus__api.html", null ]
    ] ],
    [ "ที่เก็บข้อมูลและข้อมูลรับรอง", "group__feat__storage.html", [
      [ "Tutorial", "group__feat__storage__tut.html", [
        [ "G1 — bento_storage กับที่เก็บข้อมูลรับรองฝั่ง C", "group__g1__bento__storage.html", null ],
        [ "G2 — สัญญาณชีพ: การอยู่ได้โดยไม่มี REPL", "group__g2__heartbeat.html", null ]
      ] ],
      [ "ที่เก็บข้อมูลและข้อมูลรับรอง WiFi", "group__storage__creds__api.html", null ]
    ] ],
    [ "MicroPython Agent (BentoClaw)", "group__feat__mpy__agent.html", [
      [ "Tutorial", "group__feat__mpy__agent__tut.html", [
        [ "H1 — TACP: โปรโตคอลสายสื่อสารของ IDE", "group__tut__h1__tacp.html", null ],
        [ "H2 — safety gate ของ claw", "group__tut__h2__claw__safety__gates.html", null ],
        [ "H3 — เซสชัน transport และ backend", "group__tut__h3__sessions__transport__backends.html", null ]
      ] ],
      [ "MicroPython Secure (libbento_mpy.a)", "group__mpy__secure__api.html", null ]
    ] ],
    [ "ภาคผนวก", "group__feat__appendices.html", [
      [ "ภาคผนวก W — แผนที่สัญญาณ", "group__tut__w__signal__atlas.html", null ],
      [ "ภาคผนวก X — กับดักและ anti-pattern", "group__tut__x__traps__antipatterns.html", null ],
      [ "ภาคผนวก Y — symbol ที่ผู้ใช้ไลบรารีต้องจัดหา และ symbol ที่เขียนทับได้", "group__tut__y__consumer__contracts.html", null ]
    ] ]
  ] ]
];

var NAVTREEINDEX =
[
"group__a0__orientation.html"
];

const SYNCONMSG = 'click to disable panel synchronization';
const SYNCOFFMSG = 'click to enable panel synchronization';
const LISTOFALLMEMBERS = 'List of all members';