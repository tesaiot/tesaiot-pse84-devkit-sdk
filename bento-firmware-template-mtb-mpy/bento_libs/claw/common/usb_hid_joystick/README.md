# usb_hid_joystick — USB Host gamepad library (Bento)

Reads USB gamepads on PSoC Edge E84 (**CM55**, SEGGER **emUSB-Host**), normalizes
every controller into one common 8-byte report, and ships it to **CM33** over IPC
where the MicroPython `joystick` module exposes it to games and the REPL.

Originally extracted from the BENTO PSoC Edge AI firmware
(`refactor/joystick-lib-v2`, 2026-06-01) as a reusable 3-layer library. Extended
2026-06-24 to multi-controller auto-detect (F310 + NUBWO NJ43).

---

## 1. Supported controllers — auto-detected by USB VID/PID

| Controller | VID:PID | USB protocol | Decode |
|---|---|---|---|
| Logitech **F310** (+ F710 / RumblePad) | `046D:C216` (VID-only) | HID / DirectInput | native `f310_report_t` |
| **NUBWO NJ43** | `0079:0006` (VID+PID) | HID / DirectInput (DragonRise) | remap → `f310_report_t` |
| Xbox Series (wired) | `045E:0B12` | GIP / vendor 0xFF·0x47·0xD0 | gated off (see §6) |

**Auto-detect:** on connect, the L2 adapter reads the device VID/PID
(`USBH_HID_GetDeviceInfo`) and selects a decoder, recorded in
`joystick_state_t.source` (`JOY_SRC_*`). The report callback dispatches on that
field. Plug in any supported pad — or hot-swap one for another — and the right
decoder is chosen automatically. Games stay controller-agnostic because every pad
is normalized to the same layout before anyone downstream sees it.

`joystick.controller()` (MicroPython) returns the live name — `"F310"` / `"NJ43"`
/ `"none"` — for showing the active controller on screen / in the REPL.

---

## 2. Architecture — 3 layers

```
usb_hid_joystick/
├── api/
│   ├── hid_f310_report.h    L1: common 8-byte report struct + button/hat masks + VID/PIDs
│   ├── hid_f310_parser.h    L1: decode raw -> f310_decoded_t + deadzone helper
│   └── usb_hid_joystick.h   L2: USB driver public API
├── src/
│   ├── hid_f310_parser.c    L1 impl (libc only)
│   └── usb_adapter/
│       └── usb_hid_joystick.c  L2: emUSB-Host + FreeRTOS glue, connect/report callbacks, decoders
├── tests/test_hid_f310_parser.c  host-runnable unit tests
└── usb_hid_joystick.mk / CMakeLists.txt
```

| Layer | Purpose | Deps |
|---|---|---|
| **L1** `hid_f310_parser` | decode raw bytes → decoded struct; the common report shape lives here | libc only |
| **L2** `usb_hid_joystick` | USB host integration, per-controller detection + per-controller byte translation into the common report | + emUSB-Host, FreeRTOS |

The **common report layout** (`f310_report_t`) is the contract every layer agrees on:

| byte | meaning |
|---|---|
| 0 | Left stick X (0x80 center) · 1: Left stick Y (0x7F center) |
| 2 | Right stick X · 3: Right stick Y |
| 4 | `buttons1` = hat[0:3] + X(4) A(5) B(6) Y(7) |
| 5 | `buttons2` = LB(0) RB(1) LT(2) RT(3) Back(4) Start(5) L3(6) R3(7) |
| 6 | mode · 7: status |

Hat: 0=Up, 2=Right, 4=Down, 6=Left (odd = diagonals), 8 = neutral. Y axis is
inverted in the raw report (0x00=up); the L1 parser flips it for `f310_decoded_t`.

---

## 3. NUBWO NJ43 — technical reference (DragonRise / "PC TWIN SHOCK")

Probed from the physical pad on macOS (`hidutil list` + `ioreg` HID report
descriptor), 2026-06-24. It is a plain USB **HID Joystick** (Usage Page 0x01 /
Usage 0x04) — wired USB, **DirectInput only, no XInput, no D/X switch** — so it
rides the same `USBH_HID` path as the F310 (no vendor/BULK, no Xbox blocker).

**Raw HID report descriptor:**
```
05010904a101a10275089502150026ff00350046ff00093009318102950181019502093209358102
750495012507463b0165140939814265007501950c2501450105091901290c81020600ff75019508
2501450109018102c0a1027508950746ff0026ff0009029102c0c0
```

**NJ43 INPUT report (8 bytes) → common layout (differs from F310 → remap needed):**

| byte | NJ43 | → common field |
|---|---|---|
| 0 | Left X | left_x |
| 1 | Left Y | left_y |
| 2 | constant / padding | (ignored) |
| 3 | Right X (Usage Z) | right_x |
| 4 | Right Y (Usage Rz) | right_y |
| 5 | low nibble = Hat (0..7, 8/0x0F neutral); high nibble = Button 1..4 | hat + buttons1 |
| 6 | Button 5..12 | buttons1/buttons2 |
| 7 | vendor bits | (ignored) |

Decode packs the 12 HID buttons into one field (bit N-1 = HID button N: 1..4 from
byte5 high nibble, 5..12 from byte6), then maps to the common bit positions.

> **Button order is ASSUMED (DragonRise convention) — VERIFY ON HARDWARE.** The
> descriptor only declares "Button 1..12", not which is A/B/X/Y/LB/RB/.... The
> assumed index→action map is in `usb_hid_joystick.h` (`NJ43_BTN_*`). To verify:
> plug the NJ43 into the board, press each button one at a time, watch which bit
> flips in the raw report (recorded for any HID device, matched or not — read it
> via the `joystick` debug state in the REPL), and reorder `NJ43_BTN_*` if a press
> lights the wrong action.

The pad also exposes a 7-byte **OUTPUT** report (rumble). Unused — the driver is
read-only; force-feedback needs a host→device OUT write (the same path that
currently blocks Xbox/GIP).

---

## 4. Data path (CM55 → CM33 → MicroPython)

```
emUSB-Host (CM55)
 └─ report callback → select decoder by state.source → fill f310_report_t
     └─ usb_hid_joystick_get_state()  (joystick_state_t, incl. .source)
         └─ ipc_service.c → ipc_joystick_state_t (incl. .source)  [IPC_CMD_JOYSTICK_STATE]
             └─ modjoystick.c (CM33) → js_fetch_state() → Python dicts + joystick.controller()
```

`ipc_joystick_state_t.source` carries the controller identity to CM33 (values
`IPC_JOY_SRC_*` in `ipc_communication.h`, mirror of `JOY_SRC_*`).

---

## 5. How to add another controller

**DirectInput-HID pad (easy — the F310/NJ43 path):**
1. `api/hid_f310_report.h` (or `usb_hid_joystick.h` in the flat Game copy): add
   `XXX_VID`/`XXX_PID`, a `JOY_SRC_XXX`, and (if the layout differs) byte-offset +
   button-index constants. Grep-verify the report layout from the real descriptor.
2. `src/usb_adapter/usb_hid_joystick.c` connect callback: add an `else if` on the
   VID (and PID for shared clone VIDs like 0x0079) → set `state.source = JOY_SRC_XXX`.
3. If the byte layout differs, add `xxx_decode_to_f310()` and branch to it in the
   report callback; pack bits with a single 0-based shift to avoid negative-shift
   warnings. If byte-compatible with the common layout, no decode needed.
4. Surface the name: `joystick_source_name()`, `IPC_JOY_SRC_XXX`, and the
   `modjoystick.c joystick_controller()` switch.

**XInput / vendor pad (hard):** needs the BULK/vendor path (claim 0xFF interface,
alt-setting, EP discovery, decode) AND a working interrupt-OUT write for the
enable handshake — currently blocked. Use the gated Xbox/GIP code as the template.

---

## 6. Caveats

- **Per-controller layout is hardcoded** in the decoder. A pad with an unknown
  layout decodes to garbage even if it connects — always map it from its real
  descriptor first.
- **Xbox/GIP is compiled out** (`ENABLE_XBOX_GIP=0`): the GIP power-on handshake on
  the interrupt-OUT endpoint returns TIMEOUT/BUSY through `USBH_BULK_Write`. Likely
  fix: the low-level USBH URB API. DirectInput-HID pads (F310, NJ43) avoid this.
- **NJ43 button order** is an assumption pending a hardware press-test (see §3).
- **Read-only**: no rumble / force-feedback (would need a host→device OUT write).
- **Shared clone VID**: 0x0079 is used by many unrelated pads — NJ43 is gated on
  VID **and** PID so it doesn't grab the wrong device.

---

## 7. Two copies — keep in sync

This driver exists in **two standalone git repos** (workspace rule §2):

| Repo | Path | Shape |
|---|---|---|
| Claw (this) | `BENTO-TESAIoT-Claw-libraries/common/usb_hid_joystick/` | 3-layer (api/ + src/usb_adapter/), unit tests |
| Game | `BENTO-TESAIoT-Game-libraries/common/modules/usb_hid_joystick/` | flat single file, has `.source` + gated GIP |

Add a controller to BOTH if both products need it. Shared-lib change → rebuild
every consumer (`./clean_build.sh eva-game` / `all`; nuke stale `.o` +
`libmicropython.a` first). Commit to each library's **own** GitHub repo.

> Note: `./clean_build.sh game` targets the AI-Game hybrid, which currently fails
> to build for an unrelated reason (`bento_sfx.h` missing in its ipc_ui). Use
> `eva-game` to verify joystick changes.

## 8. Host-side unit tests (L1)

```sh
gcc -Wall -Iapi tests/test_hid_f310_parser.c src/hid_f310_parser.c -o /tmp/t && /tmp/t
```
Run manually before commits that touch L1 decode logic.
