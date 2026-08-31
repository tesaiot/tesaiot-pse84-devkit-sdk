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
var menudata={children:[
{text:"Main Page",url:"index.html"},
{text:"เริ่มต้นใช้งาน",url:"group__feat__getting__started.html",children:[
{text:"A0 — สร้างอะไรได้บ้าง และแต่ละส่วนอยู่ที่ใด",url:"group__a0__orientation.html"},
{text:"A1 — จากไฟล์ zip ถึงโปรแกรมแรก",url:"group__a1__first__build.html"}]},
{text:"สถาปัตยกรรมและการบูต",url:"group__feat__architecture.html",children:[
{text:"B1 — เดินดูลำดับการบูตของ CM33_NS",url:"group__b1__cm33__boot.html"},
{text:"B2 — การบูต CM55 จนถึงเฟรมแรก",url:"group__b2__cm55__boot.html"},
{text:"B3 — แกนหลักของ IPC: การตั้งค่า deferred binding และ snapshot",url:"group__b3__ipc__backbone.html"}]},
{text:"อุปกรณ์ต่อพ่วง: เซนเซอร์และ GPIO",url:"group__feat__peripherals.html",children:[
{text:"Tutorial",url:"group__feat__peripherals__tut.html",children:[
{text:"J1 — บัสของเซนเซอร์และ lock ของมัน",url:"group__j1__sensor__bus.html"},
{text:"J3 — task ดันข้อมูลอัตโนมัติกับ sensor hub",url:"group__j3__sensor__auto.html"},
{text:"J6 — เรดาร์",url:"group__j6__radar.html"}]},
{text:"อุปกรณ์ต่อพ่วงโดยสรุป",url:"group__peripherals__quickref.html"},
{text:"API ของอุปกรณ์ต่อพ่วงอยู่ที่ใด",url:"group__feat__peripherals__apis.html"}]},
{text:"Edge AI",url:"group__feat__edge__ai.html",children:[
{text:"Tutorial",url:"group__feat__edge__ai__tut.html",children:[
{text:"E1 — Select, confirm, start: REQUESTED เทียบกับ ACTIVE",url:"group__tut__e1__select__confirm__start.html"},
{text:"E2 — Parallel set และการวนถามผลลัพธ์",url:"group__tut__e2__parallel__sets.html"},
{text:"E3 — Stop, unload และโมเดล staged",url:"group__tut__e3__stop__unload__staged.html"},
{text:"E4 — การอ่านข้อมูลวินิจฉัยของ Edge AI",url:"group__tut__e4__diagnostics.html"}]},
{text:"Edge AI (libbento_edge_ai.a)",url:"group__edge__ai__api.html"}]},
{text:"ความปลอดภัย / HSM",url:"group__feat__security.html",children:[
{text:"Tutorial",url:"group__feat__security__tut.html",children:[
{text:"D1 — วินัยการเข้าถึงชิป: gate, lock, touch-hold",url:"group__d1__chip__access__discipline.html"},
{text:"D2 — การลงทะเบียนและ Protected Update ตั้งแต่ต้นจนจบ",url:"group__d2__enrolment__protected__update.html"},
{text:"D3 — Weak symbol, `ENABLE_OPTIGA_CLM` และข้อผูกพันของผู้ใช้ไลบรารี",url:"group__d3__weak__symbols__clm.html"}]},
{text:"TESAIoT HSM (libbento_hsm.a)",url:"group__tesaiot__hsm__api.html"}]},
{text:"การเชื่อมต่อ: WiFi และคลาวด์",url:"group__feat__connectivity.html",children:[
{text:"Tutorial",url:"group__feat__connectivity__tut.html",children:[
{text:"C1 — WiFi: ที่เก็บข้อมูลรับรอง 2 แห่งกับการเชื่อมต่ออัตโนมัติตอนบูต",url:"group__c1__wifi__two__stores.html"},
{text:"C2 — WiFi จาก UI ผ่าน IPC",url:"group__c2__wifi__ui__ipc.html"},
{text:"C3 — TESAIoT cloud: ไฟล์ config → MQTT task → broker",url:"group__c3__cloud__mqtt.html"},
{text:"C4 — mTLS: ตัวตนของ TLS ที่ยึดกับ OPTIGA",url:"group__c4__mtls__optiga.html"}]},
{text:"API แต่ละตัวอยู่ที่ใด",url:"group__feat__connectivity__apis.html"}]},
{text:"หน้า UI และ IPC",url:"group__feat__ui__ipc.html",children:[
{text:"Tutorial",url:"group__feat__ui__ipc__tut.html",children:[
{text:"F1 — การเพิ่มหน้าจอ: 3 ไฟล์ กับความจริงเรื่อง Makefile",url:"group__tut__f1__adding__a__screen.html"},
{text:"F2 — การขับ widget จาก MicroPython ผ่าน IPC",url:"group__tut__f2__widgets__over__ipc.html"}]},
{text:"IPC Core (libbento_ipc.a)",url:"group__ipc__core__api.html"},
{text:"CM55 Core (libbento_cm55.a)",url:"group__cm55__core__api.html"}]},
{text:"BLE / Bento Buddy",url:"group__feat__ble.html",children:[
{text:"Tutorial",url:"group__feat__ble__tut.html",children:[
{text:"I1 — การ bring-up BLE และกฎวิทยุเดียว",url:"group__tut__i1__ble__bringup__single__rf.html"},
{text:"I2 — ส่วนที่เรียกใช้ได้ของโปรโตคอล NUS",url:"group__tut__i2__nus__protocol__surface.html"}]},
{text:"BLE NUS / Bento Buddy (libbento_secure.a)",url:"group__ble__nus__api.html"}]},
{text:"ที่เก็บข้อมูลและข้อมูลรับรอง",url:"group__feat__storage.html",children:[
{text:"Tutorial",url:"group__feat__storage__tut.html",children:[
{text:"G1 — bento_storage กับที่เก็บข้อมูลรับรองฝั่ง C",url:"group__g1__bento__storage.html"},
{text:"G2 — สัญญาณชีพ: การอยู่ได้โดยไม่มี REPL",url:"group__g2__heartbeat.html"}]},
{text:"ที่เก็บข้อมูลและข้อมูลรับรอง WiFi",url:"group__storage__creds__api.html"}]},
{text:"ภาคผนวก",url:"group__feat__appendices.html",children:[
{text:"ภาคผนวก W — แผนที่สัญญาณ",url:"group__tut__w__signal__atlas.html"},
{text:"ภาคผนวก X — กับดักและ anti-pattern",url:"group__tut__x__traps__antipatterns.html"},
{text:"ภาคผนวก Y — symbol ที่ผู้ใช้ไลบรารีต้องจัดหา และ symbol ที่เขียนทับได้",url:"group__tut__y__consumer__contracts.html"}]}]}
