#!/usr/bin/env bash
# =============================================================================
# regen.sh — one-shot regenerator for the Thai PUA font + cluster table.
#
# Runs the full pipeline:
#   1. gen_pua_font.py reads data/NotoSansThai.ttf + data/cluster_list.txt
#      and writes build/NotoSansThai-pua.ttf + src/cluster_table.h
#   2. lv_font_conv rasterises 5 bitmap sizes (14/16/20/24/28 px) into
#      src/fonts/lv_font_noto_thai_<size>.c
#
# Prerequisites (one-time install):
#   pip3 install --user fonttools uharfbuzz
#   npm install -g lv_font_conv   (or: brew install lv_font_conv)
#
# After running, REBUILD ALL DOWNSTREAM PROJECTS (per CLAUDE.md §4 — shared
# lib regeneration invalidates every .o linked against the font/PUA table).
# A clean_build.sh full sweep is the safest path.
# =============================================================================
set -euo pipefail

# Locate ourselves: tools/ -> parent is thai_text/
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
DATA="$ROOT/data"
BUILD="$ROOT/build"
SRC="$ROOT/src"
FONTS="$SRC/fonts"
TOOLS="$ROOT/tools"

mkdir -p "$BUILD" "$FONTS"

# 1) Find Python with fontTools + uharfbuzz
PY="${PYTHON:-python3}"
if ! "$PY" -c "import uharfbuzz, fontTools" 2>/dev/null; then
    # Try pyenv-installed Python (matches what worked during development)
    if [ -x "$HOME/.pyenv/versions/3.10.11/bin/python3" ]; then
        PY="$HOME/.pyenv/versions/3.10.11/bin/python3"
    fi
    if ! "$PY" -c "import uharfbuzz, fontTools" 2>/dev/null; then
        echo "ERROR: Need a Python with fontTools + uharfbuzz."
        echo "       pip3 install --user fonttools uharfbuzz"
        exit 1
    fi
fi

# 2) Generate PUA TTF + cluster_table.h
echo ">>> generating PUA composite TTF + cluster_table.h"
"$PY" "$TOOLS/gen_pua_font.py" \
    --in       "$DATA/NotoSansThai.ttf" \
    --clusters "$DATA/cluster_list.txt" \
    --out      "$BUILD/NotoSansThai-pua.ttf" \
    --header   "$SRC/cluster_table.h"

# 3) Rasterise each size with lv_font_conv. Sizes match what BENTO UI uses.
SIZES=(14 16 20 24 28)
LV_FONT_CONV="${LV_FONT_CONV:-lv_font_conv}"
if ! command -v "$LV_FONT_CONV" >/dev/null 2>&1; then
    echo "ERROR: lv_font_conv not in PATH."
    echo "       npm install -g lv_font_conv  (or: brew install lv_font_conv)"
    exit 1
fi

echo ">>> rasterising 5 bitmap fonts (14/16/20/24/28 px) in parallel"
PIDS=()
for SZ in "${SIZES[@]}"; do
    "$LV_FONT_CONV" \
        --font   "$BUILD/NotoSansThai-pua.ttf" \
        --range  0x0020-0x007E --range 0x0E00-0x0E7F --range 0xE001-0xE0FF \
        --bpp 4 --size "$SZ" --format lvgl --lv-include "lvgl.h" --no-compress \
        -o "$FONTS/lv_font_noto_thai_${SZ}.c" &
    PIDS+=($!)
done
for PID in "${PIDS[@]}"; do wait "$PID"; done

echo ""
echo ">>> regen complete"
echo "    cluster_table.h : $SRC/cluster_table.h"
echo "    bitmap fonts    : $FONTS/lv_font_noto_thai_{${SIZES[*]// /,}}.c"
echo "    intermediate    : $BUILD/NotoSansThai-pua.ttf"
echo ""
echo ">>> NEXT: clean_build.sh all   # rebuild every downstream project"
