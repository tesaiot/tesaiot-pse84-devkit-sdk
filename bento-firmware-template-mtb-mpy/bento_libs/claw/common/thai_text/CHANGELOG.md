# thai_text — Changelog

All notable changes to the Thai text rendering library are documented here.
This file follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
version numbers follow [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- Layer-2 split: `thai_lvgl_adapter.{c,h}` separated from L1 core so the
  shaper is now reusable in non-LVGL renderers (Qt, SDL2, custom bitmap).
- Standalone CMake build (`CMakeLists.txt`) producing `libthai_shape.a`
  and optional `libthai_lvgl.a`.
- One-shot regen script `tools/regen.sh` (was 6 manual commands prior).
- This README + CHANGELOG.

### Changed
- Directory consolidation: prior `BENTO-TESAIoT-Claw-libraries/common/{fonts_thai,thai_shaping}/`
  flattened into one `thai_text/` package with `api/ src/ data/ build/ tools/`
  subdirs.
- BENTO projects now include via a single `include $(BENTO_COMMON)/thai_text/thai_text.mk`
  (was 5 INCLUDES/SOURCES lines per project, three projects out of sync).
- Source font `NotoSansThai.ttf` co-located in `data/` (was in
  `FreePik-icons/fonts/`, undiscoverable from the library root).

## [0.4.0] — 2026-05-31 — "generic cluster" approach (R4)

### Added
- **Generic 2-char clusters** (above-vowel + mark, 30 entries) — render
  correctly above ANY of the 44 Thai consonants. Thai combining marks have
  heavily-negative LSB in the TTF, so a PUA composite placed after a
  consonant naturally overlays it; the runtime longest-first match still
  prefers any 3-char specific cluster for the small number of words where
  per-consonant precision matters.

### Removed
- The R3 explosion of 92 per-consonant specifics (ฉี่, พี่, นี่, …) —
  superseded by 30 generics with strictly better coverage.

### Changed
- PUA budget: 234 → 172 codepoints used (255 max).
- Flash footprint: −170 KB compiled across the 5 bitmap sizes.
- Coverage: top-20 consonants → ALL 44 (ก-ฮ + อ + ฮ).

## [0.3.0] — 2026-05-31 — thanthakhat + tone-mark visibility (R2)

### Added
- 46 thanthakhat (์) clusters for `<above-vowel> + ์` stacks: สิทธิ์,
  การ์ตูน, อาทิตย์, กิตติ์ etc.

### Fixed
- Tone marks rendered as `.small` variants (~2-3 px, illegible at 16-28 px
  bitmap). `gen_pua_font.py` now passes `ccmp=False` to HarfBuzz, which
  emits the full-size mark glyphs and raises them via the `mark` feature
  (`y_offset` ≈ 236 font units) so the 2-tier vertical separation is
  visually clear.

## [0.2.0] — 2026-05-31 — initial cluster table (R1)

### Added
- 96 pre-composed clusters: 2-tier above-vowel × tone-mark combinations
  with common consonants (ที่, นี้, ครั้ง, ก๋วยเตี๋ยว, ปั๊ม etc.).
- LVGL runtime adapter (`thai_label_set_text`) with static scratch buffer.
- IPC LCD console integration (`ipc_lcd.c` wires `thai_to_pua` into the
  span-rendering path so REPL `lcd.print()` Thai text renders correctly).

## [0.1.0] — 2026-05-31 — initial extract

### Added
- HarfBuzz + fontTools generator producing a Noto Sans Thai TTF with PUA
  composite glyphs.
- LVGL-compatible bitmap fonts at 14/16/20/24/28 px via `lv_font_conv`.
- `thai_to_pua()` runtime substitution (no malloc, libc-only).
