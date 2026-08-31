# usb_hid_joystick — Changelog

All notable changes to this library are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Version numbers follow [Semantic Versioning](https://semver.org/).

## [Unreleased] — 2026-07-17 — wire-level diagnostics (NJ43 debug)

### Added
- **L2** `joystick_state_t` gains debugger-readable wire diagnostics:
  `raw[8]` + `raw_len` (last HID report exactly as received, captured
  BEFORE the size guard and BEFORE any decode) and `reject_cnt` +
  `last_reject_size` (size-guard drop accounting). Invariant:
  `report_cnt == sequence + reject_cnt` (mod counter wrap). Not part of
  the IPC contract (`ipc_joystick_state_t` unchanged).

### Changed
- `hid_f310_report.h` NJ43 section now records the 2026-07-17 hardware
  evidence (TESAIoT Dev Kit + NUBWO NJ43, live openocd reads): stream and
  decode proven (`sequence == report_cnt` mod 256, ~33 reports/s, zero
  rejects), axis offsets + hat nibble consistent with the documented
  DragonRise idle report `7f 7f xx 7f 7f 0f 00 c0`. Button byte sources
  and button order still require a physical press to verify — read
  `joystick_state_t.raw[]` (or the Joystick page `raw:` diag line) while
  pressing.

## [0.1.0] — 2026-06-01 — initial extract (Phase 2)

Promoted from a single-file BENTO module
(`common/modules/usb_hid_joystick/usb_hid_joystick.{c,h}`) into a 3-layer
portable library mirroring the `thai_text/` pattern.

### Added
- **L1** `api/hid_f310_report.h` + `api/hid_f310_parser.h` — pure C
  decoder for the Logitech F310 DirectInput 8-byte HID report. Libc-only,
  host-testable.
- **L1** `src/hid_f310_parser.c` — implementation: signed/centered analog
  axes (with Y-axis inversion), individual booleans per button, dpad
  decoded into `(dx, dy)` ∈ `{-1, 0, +1}` each, `f310_deadzone()` helper.
- **L2** `api/usb_hid_joystick.h` — public driver API, re-exports L1
  symbols so existing BENTO consumers continue to work unchanged.
- **L2** `src/usb_adapter/usb_hid_joystick.c` — SEGGER emUSB-Host + FreeRTOS
  task glue. Byte-identical content to the prior single-file location.
- **Tests** `tests/test_hid_f310_parser.c` — 8 host-runnable test cases
  covering null inputs, neutral, full-deflection sticks, face buttons,
  multi-button combos, all 8 hat directions + neutral, deadzone math.
- **Build** `usb_hid_joystick.mk` (BENTO) + `CMakeLists.txt` (standalone).
- **Docs** `README.md` describing 3-layer architecture, scope, usage, and
  caveats.

### Migrated from
- `BENTO-TESAIoT-Claw-libraries/common/modules/usb_hid_joystick/` (Phase 1
  `.mk`-wrap shipped 2026-05-31 in `refactor(joystick)` commit
  `d221869`).

### Verification
- Symbol-level canary diff vs pre-Phase-2 build: all previously-defined
  public symbols present at same names; new L1 symbols (`f310_parse`,
  `f310_deadzone`) added; no regressions.
- AI Kit / Eva Kit / DualBand TinyPython all `./clean_build.sh all` PASS.
- Joystick page on AI Kit + Eva Kit: F310 deflection reads correctly
  (manual verification — same behaviour as Phase 1).
