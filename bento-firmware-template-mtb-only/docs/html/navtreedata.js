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
  [ "SDK for TESAIoT Dev Kit", "index.html", [
    [ "Overview", "index.html", null ],
    [ "Getting Started", "group__feat__getting__started.html", [
      [ "A0 — What you can build, and where each piece lives", "group__a0__orientation.html", null ],
      [ "A1 — From the zip to your first program", "group__a1__first__build.html", null ]
    ] ],
    [ "Architecture & Boot", "group__feat__architecture.html", [
      [ "B1 — CM33_NS boot walk-through", "group__b1__cm33__boot.html", null ],
      [ "B2 — CM55 boot to first frame", "group__b2__cm55__boot.html", null ],
      [ "B3 — The IPC backbone", "group__b3__ipc__backbone.html", null ]
    ] ],
    [ "Peripherals: Sensors & GPIO", "group__feat__peripherals.html", [
      [ "Tutorials", "group__feat__peripherals__tut.html", [
        [ "J1 — The sensor bus and its lock", "group__j1__sensor__bus.html", null ],
        [ "J3 — The auto-push task and the sensor hub", "group__j3__sensor__auto.html", null ],
        [ "J6 — Radar", "group__j6__radar.html", null ]
      ] ],
      [ "Peripherals at a glance", "group__peripherals__quickref.html", null ],
      [ "Where the peripheral APIs live", "group__feat__peripherals__apis.html", null ]
    ] ],
    [ "Edge AI", "group__feat__edge__ai.html", [
      [ "Tutorials", "group__feat__edge__ai__tut.html", [
        [ "E1 — Select, confirm, start", "group__tut__e1__select__confirm__start.html", null ],
        [ "E2 — Parallel sets and result polling", "group__tut__e2__parallel__sets.html", null ],
        [ "E3 — Stop, unload, and staged models", "group__tut__e3__stop__unload__staged.html", null ],
        [ "E4 — Reading the Edge AI diagnostics", "group__tut__e4__diagnostics.html", null ]
      ] ],
      [ "API Reference", "group__edge__ai__api.html", null ]
    ] ],
    [ "Security / HSM", "group__feat__security.html", [
      [ "Tutorials", "group__feat__security__tut.html", [
        [ "D1 — The chip-access discipline", "group__d1__chip__access__discipline.html", null ],
        [ "D2 — Enrolment and Protected Update", "group__d2__enrolment__protected__update.html", null ],
        [ "D3 — Weak symbols and ENABLE_OPTIGA_CLM", "group__d3__weak__symbols__clm.html", null ]
      ] ],
      [ "API Reference", "group__tesaiot__hsm__api.html", null ]
    ] ],
    [ "Connectivity: WiFi & Cloud", "group__feat__connectivity.html", [
      [ "Tutorials", "group__feat__connectivity__tut.html", [
        [ "C1 — The two credential stores", "group__c1__wifi__two__stores.html", null ],
        [ "C2 — WiFi from the UI over IPC", "group__c2__wifi__ui__ipc.html", null ],
        [ "C3 — TESAIoT cloud: config, MQTT, broker", "group__c3__cloud__mqtt.html", null ],
        [ "C4 — mTLS: the OPTIGA-backed TLS identity", "group__c4__mtls__optiga.html", null ],
        [ "C5 — TESAIoT cloud: HTTPS REST, the Device API Key, and mTLS", "group__c5__cloud__https.html", null ]
      ] ],
      [ "Where the APIs live", "group__feat__connectivity__apis.html", null ]
    ] ],
    [ "UI Pages & IPC", "group__feat__ui__ipc.html", [
      [ "Tutorials", "group__feat__ui__ipc__tut.html", [
        [ "F1 — Adding a screen", "group__tut__f1__adding__a__screen.html", null ],
        [ "F2 — Driving widgets from MicroPython over IPC", "group__tut__f2__widgets__over__ipc.html", null ]
      ] ],
      [ "API Reference — IPC Core", "group__ipc__core__api.html", null ],
      [ "API Reference — CM55 Core", "group__cm55__core__api.html", null ]
    ] ],
    [ "BLE / Bento Buddy", "group__feat__ble.html", [
      [ "Tutorials", "group__feat__ble__tut.html", [
        [ "I1 — BLE bring-up and the single-RF rule", "group__tut__i1__ble__bringup__single__rf.html", null ],
        [ "I2 — The NUS protocol surface", "group__tut__i2__nus__protocol__surface.html", null ]
      ] ],
      [ "API Reference", "group__ble__nus__api.html", null ]
    ] ],
    [ "Storage & Credentials", "group__feat__storage.html", [
      [ "Tutorials", "group__feat__storage__tut.html", [
        [ "G1 — bento_storage and the C credential store", "group__g1__bento__storage.html", null ],
        [ "G2 — The heartbeat: living without a REPL", "group__g2__heartbeat.html", null ]
      ] ],
      [ "API Reference", "group__storage__creds__api.html", null ]
    ] ],
    [ "Appendices", "group__feat__appendices.html", [
      [ "Appendix W — The signal atlas", "group__tut__w__signal__atlas.html", null ],
      [ "Appendix X — Traps and anti-patterns", "group__tut__x__traps__antipatterns.html", null ],
      [ "Appendix Y — Consumer contracts", "group__tut__y__consumer__contracts.html", null ]
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