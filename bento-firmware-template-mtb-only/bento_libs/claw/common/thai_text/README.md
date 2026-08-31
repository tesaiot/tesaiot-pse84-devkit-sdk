# thai_text — Thai cluster shaping for bitmap text renderers

A self-contained library that makes Thai 2-tier diacritic clusters (e.g.
`ที่`, `นี้`, `ขึ้น`, `ก๋วยเตี๋ยว`, `สิทธิ์`) render correctly on
renderers that do not implement OpenType GSUB/GPOS — LVGL bitmap fonts
being the canonical case.

Originally extracted from the BENTO PSoC Edge AI firmware (2026-05). Now
structured for reuse outside BENTO too.

## The problem

LVGL renders Unicode text glyph-by-glyph using each glyph's natural
advance width. Thai combining marks (above-vowels U+0E31/0E34..0E37, tone
marks U+0E48..0E4B, mai-tai-khu U+0E47, thanthakhat U+0E4C) only stack
correctly when shaped by HarfBuzz or a similar complex-text shaper. Without
a shaper they overlap or scatter — visually broken.

## The fix

We pre-compose every needed cluster **off-line** using HarfBuzz, then place
the composite glyph at a Private Use Area codepoint (U+E001..U+E0FF) inside
a copy of the source TTF. The bitmap font produced by `lv_font_conv`
contains both the standard Thai block AND these PUA composites.

At runtime, `thai_to_pua()` (Layer 1) substitutes known Thai cluster byte
sequences with their PUA codepoints — LVGL then renders the cluster as a
single pre-shaped glyph. Unknown sequences pass through unchanged.

For the common above-vowel + mark stacks the cluster table contains
**generic 2-char entries** (e.g. `ี่`, `ื้`, `ั๊`). These match after any
consonant because Thai combining marks have heavily-negative left side
bearings — the PUA composite's pixels naturally draw over the preceding
consonant. Result: ALL Thai consonants × 5 above-vowels × 6 marks render
correctly with only ~30 PUA glyphs.

## Library structure (three layers)

```
thai_text/
├── api/                    Public headers — copy alongside src/ for users
│   ├── thai_shaping.h      L1: thai_to_pua() — renderer-agnostic, libc-only
│   └── thai_lvgl_adapter.h L2: thai_label_set_text() — LVGL convenience
│
├── src/                    Implementation — compile into your project
│   ├── thai_shaping.c      L1 impl (no LVGL deps)
│   ├── cluster_table.h     Generated PUA mapping
│   ├── lvgl_adapter/
│   │   └── thai_lvgl_adapter.c   L2 impl (depends on lvgl.h)
│   └── fonts/              L3: pre-rasterised bitmap fonts
│       └── lv_font_noto_thai_{14,16,20,24,28}.c
│
├── data/                   Generator inputs (committed)
│   ├── NotoSansThai.ttf    Source font, Noto Sans Thai 2.002 (OFL 1.1 — see UPSTREAM)
│   ├── OFL.txt             SIL Open Font License 1.1, verbatim upstream text
│   ├── UPSTREAM            Font provenance: version, copyright, derivatives
│   └── cluster_list.txt    UTF-8, one Thai cluster per line; '#' = comment
│
├── build/                  Intermediate artifacts (.gitignored)
│   └── NotoSansThai-pua.ttf  Output of gen_pua_font.py; input to lv_font_conv
│
├── tools/
│   ├── gen_pua_font.py     Off-line composite generator
│   └── regen.sh            One-shot: gen + lv_font_conv ×5
│
├── thai_text.mk            Make include for BENTO-style projects
├── CMakeLists.txt          Standalone CMake build (libthai_shape.a, etc.)
├── CHANGELOG.md
└── README.md               (this file)
```

The three layers exist so consumers pay only for what they use:

| Layer | Purpose | Deps | Distribute as |
|-------|---------|------|---------------|
| **L1** `thai_shaping.{c,h}` + `cluster_table.h` | UTF-8 cluster → PUA substitute | libc only | `libthai_shape.a` — works with ANY renderer |
| **L2** `thai_lvgl_adapter.{c,h}` | `thai_label_set_text()` convenience | + LVGL header | `libthai_lvgl.a` — only if you use LVGL |
| **L3** `src/fonts/lv_font_noto_thai_*.c` | Bitmap font containing PUA composites | + LVGL | Compile per-project (sizes vary) |

## Quick start — using thai_text in a Make-based project

```make
# In your project's Makefile, after BENTO_COMMON is defined:
include $(BENTO_COMMON)/thai_text/thai_text.mk
```

That single line adds: API includes, L1 + L2 sources, and all 5 bitmap fonts.

If you want only the renderer-agnostic L1 core (no LVGL):
```make
THAI_TEXT_SKIP_LVGL := 1
include $(BENTO_COMMON)/thai_text/thai_text.mk
```

Then in your code:
```c
#include "thai_shaping.h"        /* L1 always available */
#include "thai_lvgl_adapter.h"   /* L2 — needs LVGL */

/* L1: pure UTF-8 in / out — feed result to any text renderer */
char pua_buf[256];
thai_to_pua("สิทธิ์ คือ ที่นี่", pua_buf, sizeof(pua_buf));

/* L2: convenience for LVGL labels */
thai_label_set_text(my_label, "ที่นี่ ขึ้น ก๋วยเตี๋ยว");
```

## Quick start — using thai_text standalone (CMake)

```sh
mkdir build && cd build
cmake .. -DTHAI_TEXT_LVGL_DIR=/path/to/lvgl   # omit for L1-only
cmake --build .
# Produces: libthai_shape.a (always), libthai_lvgl.a (if LVGL provided)
```

## Adding a new cluster

1. Append the cluster (one UTF-8 line) to `data/cluster_list.txt`.
2. Run `tools/regen.sh`.
3. Rebuild every downstream project (the cluster_table.h hash changes,
   invalidating cached `.o` files that include it).

The PUA range has 255 codepoints (U+E001..U+E0FF). Current usage is ~172;
the runtime cluster table is sorted longest-first so 3-char specifics
take precedence over generic 2-char above-vowel+mark entries.

## Regen workflow

```sh
cd thai_text/tools
./regen.sh
```

That runs `gen_pua_font.py` once + `lv_font_conv` five times in parallel.
Total wall time: ~10-20 s on a recent Mac.

After regen, the BENTO build chain still requires a clean rebuild because
the bitmap `.c` files and `cluster_table.h` changed:
```sh
cd <bento root>
./clean_build.sh all              # rebuild every downstream project
./clean_build.sh --flash <kit>    # verify on hardware
```

## Generator design (gen_pua_font.py)

For each cluster `C`:
1. Pass `C` (as a Python string) to HarfBuzz `hb.shape()` with `ccmp=False`
   to disable Noto Sans Thai's "small tone-mark" contextual substitution
   (the `.small` variants are 2-3 px at our bitmap sizes — illegible).
2. HarfBuzz returns a list of `(gid, x_offset, y_offset, x_advance)` —
   correctly shaped, with `mark` feature applied for stacking.
3. Build a fontTools `Glyph` composite that references each component at
   the HarfBuzz-computed offsets. Sum advances for total width.
4. Insert the composite at PUA codepoint U+E001+, update cmap and hmtx.
5. After all clusters processed, write the modified TTF and emit
   `cluster_table.h` (sorted longest-first for trivial longest-match).

**Why HarfBuzz + fontTools instead of doing it all in HarfBuzz?** HarfBuzz
shapes but does not modify fonts. fontTools owns font structure mutation
but cannot shape. Both are pure-Python — no native build deps beyond
their wheel installs.

## License & attribution

- Source font: Noto Sans Thai 2.002, Copyright 2022 The Noto Project Authors
  (https://github.com/notofonts/thai), licensed under the SIL Open Font
  License 1.1. The full licence text is `data/OFL.txt` and the provenance
  record is `data/UPSTREAM`; both ship with this directory, as OFL 1.1 §2
  requires of any redistribution. The licence is OFL only — an earlier
  revision of this file said "Apache 2.0 / OFL", which was wrong: the font's
  own `name` table (ID 13) carries the OFL notice and no Apache grant.
- The pre-rasterised fonts `src/fonts/lv_font_noto_thai_{14,16,20,24,28}.c`
  are OFL derivatives of that font, so `data/OFL.txt` governs them too and
  must travel with any package that ships them.
- Generator: fontTools (MIT) + uharfbuzz (Apache 2.0) — build-time only, not
  redistributed in the firmware.
- Library code (`thai_shaping.{c,h}` + adapter + cluster_table.h): same
  license as the parent BENTO project (currently project-internal; will
  be relicensed permissively when extracted as a standalone OSS package).

## Caveats

- **Sara-am NFC**: U+0E33 (composed) is not in the cluster table today.
  When a sara-am-with-tone cluster is needed, expand U+0E33 to its NFD
  pair `<U+0E4D, U+0E32>` before the longest-match scan. Marked TODO inline.
- **PUA leakage**: `thai_to_pua()` output MUST stay on the render path.
  Do not log it, send via IPC/BLE/MQTT, or display in the REPL — PUA bytes
  are an internal optimisation, the user data stays UTF-8 Thai.
- **Font specific**: Glyph offsets are tuned for Noto Sans Thai. Switching
  fonts requires regen (the negative LSB pattern is similar across modern
  Thai fonts, but offsets differ).
