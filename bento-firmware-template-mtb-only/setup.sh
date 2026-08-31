#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# setup.sh — get from a fresh clone to a firmware you can flash, in one command
#
#   ./setup.sh              work out which variant this is, and do the right thing
#   ./setup.sh --check      say what is missing; change nothing
#   ./setup.sh --build      set up, then build
#
# The same script serves both packages. The mtb-only variant needs mtb_shared
# and nothing else; the mtb-mpy variant additionally needs the MicroPython
# port. Which one you are holding is detected, not asked.
#
# Everything it runs is echoed first, so the same result can be reached by
# hand from the README when this script is not available.
# ---------------------------------------------------------------------------
set -euo pipefail

if [ "${BASH_VERSINFO:-0}" -lt 4 ]; then
    echo "setup.sh needs bash 4 or newer; macOS ships 3.2." >&2
    echo "Put Homebrew's bash first:  export PATH=\"/opt/homebrew/bin:\$PATH\"" >&2
    exit 1
fi

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WS="${BENTO_WORKSPACE:-$(cd "$HERE/.." && pwd)}"

PORT_DIR="micropython-psoc-edge-psoc-edge-main"
PORT_REPO="https://github.com/tesaiot/tesaiot-pse84-devkit-sdk"

ok(){ printf '\033[0;32m  ok\033[0m   %s\n' "$*"; }
no(){ printf '\033[0;31m  FAIL\033[0m %s\n' "$*"; }
wr(){ printf '\033[1;33m  warn\033[0m %s\n' "$*"; }
hd(){ printf '\n\033[0;36m== %s ==\033[0m\n' "$*"; }
sh_(){ printf '\033[0;90m  $ %s\033[0m\n' "$*"; }

mode="setup"
case "${1:-}" in
  --check) mode="check" ;;
  --build) mode="build" ;;
  "")      ;;
  *) sed -n '2,12p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
esac

# --- which variant is this? ------------------------------------------------
# Detected from what the package actually carries, not from its directory
# name, because a directory can be renamed and a missing library cannot.
if [ -d "$HERE/lib/mpy_secure" ]; then
    VARIANT="mtb-mpy";  NEEDS_PORT=1
else
    VARIANT="mtb-only"; NEEDS_PORT=0
fi

hd "this package"
ok "variant: $VARIANT"
ok "workspace: $WS"
[ "$NEEDS_PORT" -eq 1 ] \
    && ok "needs the MicroPython port" \
    || ok "does not need the MicroPython port"

# --- toolchain -------------------------------------------------------------
hd "toolchain"
missing=0
if command -v arm-none-eabi-gcc >/dev/null; then
    ok "$(arm-none-eabi-gcc --version | head -1)"
else
    no "arm-none-eabi-gcc not on PATH"
    echo "       install ModusToolbox 3.6 and add its compiler:"
    echo "         export PATH=\"/Applications/mtb-gcc-arm-eabi/14.2.1/gcc/bin:\$PATH\""
    missing=$((missing+1))
fi

if [ -d /Applications/ModusToolbox ] || command -v make >/dev/null; then
    ok "make present"
else
    no "make not found"; missing=$((missing+1))
fi

# --- the two trees the template does not carry -----------------------------
hd "what this template does not carry"

need_getlibs=0
if [ -d "$WS/mtb_shared" ]; then
    ok "mtb_shared at $WS/mtb_shared"
else
    wr "mtb_shared not found — about 1.9 GB, fetched per project"
    need_getlibs=1
fi

need_port=0
if [ "$NEEDS_PORT" -eq 1 ]; then
    if [ -d "$WS/$PORT_DIR" ]; then
        ok "MicroPython port at $WS/$PORT_DIR"
    else
        wr "MicroPython port not found"
        need_port=1
    fi
fi

if [ "$mode" = "check" ]; then
    hd "result"
    if [ "$missing" -eq 0 ] && [ "$need_getlibs" -eq 0 ] && [ "$need_port" -eq 0 ]; then
        ok "ready to build:  ./setup.sh --build"
    else
        echo "  run ./setup.sh to fix what it can"
    fi
    exit 0
fi
[ "$missing" -eq 0 ] || { no "fix the toolchain first — nothing else can proceed"; exit 1; }

# --- fetch the port --------------------------------------------------------
if [ "$need_port" -eq 1 ]; then
    hd "MicroPython port"
    echo "  The port is a separate tree, about 189 MB unpacked. It lives in the"
    echo "  SDK repository beside this template:"
    echo "      $PORT_REPO"
    echo
    echo "  Put it BESIDE this directory, not inside it. When it is right you"
    echo "  will have:"
    echo "      $WS/$PORT_DIR/"
    echo "      $WS/$(basename "$HERE")/    <- this directory"
    echo
    if command -v git >/dev/null; then
        echo "  Fetching only that directory, so you do not clone the whole SDK:"
        sh_ "git clone --depth 1 --filter=blob:none --sparse $PORT_REPO _sdk_tmp"
        ( cd "$WS" \
          && git clone --depth 1 --filter=blob:none --sparse "$PORT_REPO" _sdk_tmp \
          && cd _sdk_tmp \
          && git sparse-checkout set "$PORT_DIR" ) || {
            no "clone failed — download the repository by hand and copy $PORT_DIR beside this directory"
            exit 1
          }
        if [ -d "$WS/_sdk_tmp/$PORT_DIR" ]; then
            mv "$WS/_sdk_tmp/$PORT_DIR" "$WS/$PORT_DIR"
            rm -rf "$WS/_sdk_tmp"
            ok "port in place at $WS/$PORT_DIR"
        else
            no "the clone carried no $PORT_DIR — the repository layout may have changed"
            rm -rf "$WS/_sdk_tmp"
            exit 1
        fi
    else
        no "git not installed — download $PORT_REPO and copy $PORT_DIR beside this directory"
        exit 1
    fi
fi

# --- getlibs, per project, in the order that works -------------------------
if [ "$need_getlibs" -eq 1 ]; then
    hd "fetching dependencies"
    echo "  There is no top-level getlibs. Each project declares its own"
    echo "  deps/*.mtb, and running it only in proj_cm33_ns fetches 33 of the 41"
    echo "  assets — the build then stops inside ninja on a missing"
    echo "  optiga-trust-m file. So: all three, every time."
    echo
    for p in proj_cm33_s proj_cm33_ns proj_cm55; do
        [ -d "$HERE/$p" ] || continue
        sh_ "(cd $p && make getlibs)"
        ( cd "$HERE/$p" && make getlibs ) || { no "getlibs failed in $p"; exit 1; }
    done
    ok "dependencies fetched"
fi

# --- build -----------------------------------------------------------------
if [ "$mode" = "build" ]; then
    hd "building"
    echo "  A clean build of all three cores takes about ten minutes."
    sh_ "make build -j BENTO_WORKSPACE=$WS"
    ( cd "$HERE" && make build -j BENTO_WORKSPACE="$WS" )
    hex="$HERE/build/app_combined.hex"
    if [ -f "$hex" ]; then
        ok "$(basename "$hex") — $(wc -c < "$hex" | tr -d ' ') bytes"
        echo
        echo "  Flash it:   make program BENTO_WORKSPACE=$WS"
        echo "  Then UNPLUG the USB cable, wait ten seconds, and plug it back in."
        echo "  A debugger reset is not enough: the display backlight needs a cold"
        echo "  0->1 edge and stays dark otherwise, which looks exactly like a"
        echo "  failed flash."
    else
        no "the build finished but produced no app_combined.hex"
        exit 1
    fi
else
    hd "next"
    echo "  ./setup.sh --build        build all three cores"
    echo "  ./bento.sh doctor         check again in more detail"
    echo "  ./bento.sh menus          which screens are on"
fi
