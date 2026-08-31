#!/usr/bin/env python3
"""
gen_pua_font.py — Generate a Noto Sans Thai TTF with PUA-mapped pre-composed
Thai cluster glyphs for LVGL bitmap fonts.

The problem this solves: LVGL has no GSUB/GPOS shaper, so a Thai cluster like
"ที่" (ท + ี + ่) renders as three separate glyphs that overlap on the LCD.
The fix: for every cluster used by the firmware, ask HarfBuzz (which DOES
shape Thai correctly) to produce the composed glyph, then add it to the TTF
at a Private Use Area (PUA) codepoint U+E001…. A runtime helper on the
firmware side substitutes Thai cluster sequences with PUA codepoints before
calling lv_label_set_text — LVGL then renders the (now single-codepoint)
glyph correctly without ever needing to shape.

Pipeline:
  input  : NotoSansThai.ttf  +  cluster_list.txt (UTF-8, one cluster/line)
  output : NotoSansThai-pua.ttf (TTF with one extra glyph per cluster at
           U+E001+, using HarfBuzz-shaped composite glyphs)
         + cluster_table.h    (C lookup: cluster utf-8 sequence -> PUA cp)

Usage:
  ./gen_pua_font.py \\
      --in  NotoSansThai.ttf \\
      --clusters cluster_list.txt \\
      --out NotoSansThai-pua.ttf \\
      --header cluster_table.h

The resulting TTF is then fed to lv_font_conv with --range 0x20-0x7E
0x0E00-0x0E7F 0xE000-0xE0FF so the PUA codepoints land in the bitmap font.
"""
import argparse
import sys
from pathlib import Path

from fontTools.ttLib import TTFont
from fontTools.ttLib.tables._g_l_y_f import Glyph, GlyphComponent
from fontTools.ttLib.tables.otTables import GSUB
import uharfbuzz as hb


PUA_START = 0xE001  # U+E000 is reserved/sometimes legacy — start at E001


def hb_shape(font_blob: bytes, text: str) -> list:
    """Return HarfBuzz shaping result for `text`: list of (gid, x_off, y_off,
    x_adv, y_adv) tuples. HarfBuzz applies Thai mark-to-base/mark-to-mark
    positioning here — this is what gives us the correct stacked layout."""
    face = hb.Face(font_blob)
    font = hb.Font(face)
    buf = hb.Buffer()
    buf.add_str(text)
    buf.guess_segment_properties()
    # Force Thai script for safety; HarfBuzz's Thai shaper handles
    # above-vowel + tone-mark stacking via mark classes.
    buf.script = "Thai"
    buf.language = "th"
    # ccmp=False disables Noto Sans Thai's contextual substitution that swaps
    # tone marks for their `.small` variants when stacked over above-vowels.
    # The .small variants render as ~2-3 px at our bitmap sizes (16-28 px) and
    # are illegible. Disabling ccmp uses the full-size mark glyph (uni0E48,
    # uni0E49, …) and HarfBuzz's `mark` feature still positions it ~236 font
    # units higher to clear the above-vowel — so we get clearly visible tone
    # marks AND proper 2-tier vertical separation.
    hb.shape(font, buf, {"kern": True, "liga": True, "ccmp": False})
    return [(g.codepoint, p.x_offset, p.y_offset, p.x_advance, p.y_advance)
            for g, p in zip(buf.glyph_infos, buf.glyph_positions)]


def add_pua_composite(font: TTFont, base_gname: str, parts: list, pua_cp: int):
    """Insert a composite glyph (FontTools `Glyph` of type composite) into
    the TTF that combines the shaped parts at the offsets HarfBuzz computed.
    Map the new glyph to `pua_cp` in cmap. This is what lv_font_conv will
    later render as a single bitmap at the PUA codepoint."""
    glyf  = font["glyf"]
    hmtx  = font["hmtx"]
    cmap  = font["cmap"]

    # Build a composite Glyph that references each part with the HarfBuzz
    # x/y offset. (Glyph composite offsets are in font units; HarfBuzz's
    # x_offset/y_offset are too, so they pass through directly.)
    new_glyph = Glyph()
    new_glyph.numberOfContours = -1  # signals composite
    new_glyph.components = []
    total_adv = 0
    for gid, x_off, y_off, x_adv, _y_adv in parts:
        gname = font.getGlyphName(gid)
        c = GlyphComponent()
        c.glyphName = gname
        c.x = x_off + total_adv
        c.y = y_off
        c.flags = 0x0204  # ARGS_ARE_XY_VALUES | MORE_COMPONENTS-cleared-later
        new_glyph.components.append(c)
        total_adv += x_adv
    # last component has MORE_COMPONENTS bit cleared
    new_glyph.components[-1].flags = 0x0004

    pua_gname = f"pua_{pua_cp:04X}"
    glyf[pua_gname] = new_glyph
    hmtx[pua_gname] = (total_adv, 0)  # (advance width, left side bearing)

    # Register the glyph name in the glyph order (otherwise hmtx etc. complain)
    order = font.getGlyphOrder()
    if pua_gname not in order:
        order.append(pua_gname)
        font.setGlyphOrder(order)

    # cmap: add U+pua_cp -> pua_gname in every Unicode subtable
    for sub in cmap.tables:
        if sub.isUnicode():
            sub.cmap[pua_cp] = pua_gname


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--in",       dest="ttf_in",   required=True)
    ap.add_argument("--clusters", required=True, help="UTF-8 cluster list, one per line; '#' comments OK")
    ap.add_argument("--out",      dest="ttf_out",  required=True)
    ap.add_argument("--header",   required=True, help="C header with utf8->PUA table")
    args = ap.parse_args()

    ttf_path = Path(args.ttf_in)
    font_blob = ttf_path.read_bytes()
    font = TTFont(args.ttf_in)

    # Read cluster list, skip blanks/comments, dedupe in order
    seen = set()
    clusters = []
    for raw in Path(args.clusters).read_text(encoding="utf-8").splitlines():
        s = raw.strip()
        if not s or s.startswith("#"):
            continue
        if s in seen:
            continue
        seen.add(s)
        clusters.append(s)

    mapping = []  # list of (cluster_utf8, pua_cp)
    skipped = []
    for i, cl in enumerate(clusters):
        pua = PUA_START + i
        if pua > 0xE0FF:
            print(f"PUA range exhausted at cluster #{i} ({cl!r})", file=sys.stderr)
            break
        shaped = hb_shape(font_blob, cl)
        # Drop clusters where HarfBuzz emitted .notdef (gid 0) — means the
        # font doesn't cover one of the codepoints. Without this, the
        # composite would have a missing-glyph rectangle.
        if any(g == 0 for (g, *_rest) in shaped):
            skipped.append(cl)
            continue
        add_pua_composite(font, None, shaped, pua)
        mapping.append((cl, pua))

    font.save(args.ttf_out)

    # Emit C header — a small static table the runtime helper walks at
    # substitute time. Sorted longest-first so longest-match works trivially.
    mapping.sort(key=lambda kv: -len(kv[0].encode("utf-8")))
    lines = [
        "/* AUTO-GENERATED by gen_pua_font.py — do not edit by hand. */",
        "/* Maps Thai cluster UTF-8 byte sequences to PUA codepoints. */",
        "#ifndef THAI_CLUSTER_TABLE_H",
        "#define THAI_CLUSTER_TABLE_H",
        "",
        "#include <stdint.h>",
        "#include <stddef.h>",
        "",
        "typedef struct {",
        "    const char *utf8;       /* NUL-terminated UTF-8 cluster */",
        "    uint8_t     byte_len;   /* byte length, for fast prefix match */",
        "    uint32_t    pua;        /* assigned U+E001+ codepoint */",
        "} thai_cluster_t;",
        "",
        f"#define THAI_CLUSTER_TABLE_LEN {len(mapping)}",
        "",
        "static const thai_cluster_t thai_cluster_table[] = {",
    ]
    for cl, pua in mapping:
        bs = cl.encode("utf-8")
        esc = "".join(f"\\x{b:02x}" for b in bs)
        lines.append(f'    {{ "{esc}", {len(bs)}, 0x{pua:04X} }},  /* {cl} */')
    lines.append("};")
    lines.append("")
    lines.append("#endif /* THAI_CLUSTER_TABLE_H */")
    Path(args.header).write_text("\n".join(lines), encoding="utf-8")

    print(f"composed {len(mapping)} clusters -> PUA U+E001..U+{PUA_START + len(mapping) - 1:04X}")
    if skipped:
        print(f"skipped {len(skipped)} clusters (missing glyphs in source TTF):")
        for s in skipped:
            print(f"  {s!r}")


if __name__ == "__main__":
    main()
