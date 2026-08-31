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
{text:"Getting Started",url:"group__feat__getting__started.html",children:[
{text:"A0 — What you can build, and where each piece lives",url:"group__a0__orientation.html"},
{text:"A1 — From the zip to your first program",url:"group__a1__first__build.html"}]},
{text:"Architecture & Boot",url:"group__feat__architecture.html",children:[
{text:"B1 — CM33_NS boot walk-through",url:"group__b1__cm33__boot.html"},
{text:"B2 — CM55 boot to first frame",url:"group__b2__cm55__boot.html"},
{text:"B3 — The IPC backbone",url:"group__b3__ipc__backbone.html"}]},
{text:"Peripherals: Sensors & GPIO",url:"group__feat__peripherals.html",children:[
{text:"Tutorials",url:"group__feat__peripherals__tut.html",children:[
{text:"J1 — The sensor bus and its lock",url:"group__j1__sensor__bus.html"},
{text:"J2 — Reading the six sensors",url:"group__j2__sensors__api.html"},
{text:"J3 — The auto-push task and the sensor hub",url:"group__j3__sensor__auto.html"},
{text:"J4 — LEDs and buttons",url:"group__j4__gpio.html"},
{text:"J5 — The QWA309 base board",url:"group__j5__qwa309.html"},
{text:"J6 — Radar",url:"group__j6__radar.html"}]},
{text:"Peripherals at a glance",url:"group__peripherals__quickref.html"},
{text:"Where the peripheral APIs live",url:"group__feat__peripherals__apis.html"}]},
{text:"Edge AI",url:"group__feat__edge__ai.html",children:[
{text:"Tutorials",url:"group__feat__edge__ai__tut.html",children:[
{text:"E1 — Select, confirm, start",url:"group__tut__e1__select__confirm__start.html"},
{text:"E2 — Parallel sets and result polling",url:"group__tut__e2__parallel__sets.html"},
{text:"E3 — Stop, unload, and staged models",url:"group__tut__e3__stop__unload__staged.html"},
{text:"E4 — Reading the Edge AI diagnostics",url:"group__tut__e4__diagnostics.html"}]},
{text:"API Reference",url:"group__edge__ai__api.html"}]},
{text:"Security / HSM",url:"group__feat__security.html",children:[
{text:"Tutorials",url:"group__feat__security__tut.html",children:[
{text:"D1 — The chip-access discipline",url:"group__d1__chip__access__discipline.html"},
{text:"D2 — Enrolment and Protected Update",url:"group__d2__enrolment__protected__update.html"},
{text:"D3 — Weak symbols and ENABLE_OPTIGA_CLM",url:"group__d3__weak__symbols__clm.html"}]},
{text:"API Reference",url:"group__tesaiot__hsm__api.html"}]},
{text:"Connectivity: WiFi & Cloud",url:"group__feat__connectivity.html",children:[
{text:"Tutorials",url:"group__feat__connectivity__tut.html",children:[
{text:"C1 — The two credential stores",url:"group__c1__wifi__two__stores.html"},
{text:"C2 — WiFi from the UI over IPC",url:"group__c2__wifi__ui__ipc.html"},
{text:"C3 — TESAIoT cloud: config, MQTT, broker",url:"group__c3__cloud__mqtt.html"},
{text:"C4 — mTLS: the OPTIGA-backed TLS identity",url:"group__c4__mtls__optiga.html"}]},
{text:"Where the APIs live",url:"group__feat__connectivity__apis.html"}]},
{text:"UI Pages & IPC",url:"group__feat__ui__ipc.html",children:[
{text:"Tutorials",url:"group__feat__ui__ipc__tut.html",children:[
{text:"F1 — Adding a screen",url:"group__tut__f1__adding__a__screen.html"},
{text:"F2 — Driving widgets from MicroPython over IPC",url:"group__tut__f2__widgets__over__ipc.html"}]},
{text:"API Reference — IPC Core",url:"group__ipc__core__api.html"},
{text:"API Reference — CM55 Core",url:"group__cm55__core__api.html"}]},
{text:"BLE / Bento Buddy",url:"group__feat__ble.html",children:[
{text:"Tutorials",url:"group__feat__ble__tut.html",children:[
{text:"I1 — BLE bring-up and the single-RF rule",url:"group__tut__i1__ble__bringup__single__rf.html"},
{text:"I2 — The NUS protocol surface",url:"group__tut__i2__nus__protocol__surface.html"}]},
{text:"API Reference",url:"group__ble__nus__api.html"}]},
{text:"Storage & Credentials",url:"group__feat__storage.html",children:[
{text:"Tutorials",url:"group__feat__storage__tut.html",children:[
{text:"G1 — bento_storage and the C credential store",url:"group__g1__bento__storage.html"},
{text:"G2 — The heartbeat: living without a REPL",url:"group__g2__heartbeat.html"}]},
{text:"API Reference",url:"group__storage__creds__api.html"}]},
{text:"MicroPython Agent (BentoClaw)",url:"group__feat__mpy__agent.html",children:[
{text:"Tutorials",url:"group__feat__mpy__agent__tut.html",children:[
{text:"H1 — TACP: the IDE wire protocol",url:"group__tut__h1__tacp.html"},
{text:"H2 — The claw safety gates",url:"group__tut__h2__claw__safety__gates.html"},
{text:"H3 — Sessions, transport, and backends",url:"group__tut__h3__sessions__transport__backends.html"}]},
{text:"API Reference",url:"group__mpy__secure__api.html"}]},
{text:"Appendices",url:"group__feat__appendices.html",children:[
{text:"Appendix W — The signal atlas",url:"group__tut__w__signal__atlas.html"},
{text:"Appendix X — Traps and anti-patterns",url:"group__tut__x__traps__antipatterns.html"},
{text:"Appendix Y — Consumer contracts",url:"group__tut__y__consumer__contracts.html"}]}]}
