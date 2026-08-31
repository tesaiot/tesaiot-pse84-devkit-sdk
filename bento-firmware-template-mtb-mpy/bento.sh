#!/usr/bin/env bash
#
# bento.sh — the developer front end for this template.
#
# Commands
#   doctor          check the toolchain and the two trees this template needs
#   menus           list every menu and whether it is on
#   enable  <menu>  turn a menu on
#   disable <menu>  turn a menu off, and offer to delete its sources
#   build           build all three cores
#   flash           build and program the board
#   verify          check the prebuilt archives against their signature
#   clean           remove build output
#
# Run it with no arguments for an interactive menu.
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CM55_MK="$ROOT/proj_cm55/Makefile"
PAGES="$ROOT/proj_cm55/modules/page-components"
HOME_C="$PAGES/_core/page_home.c"
UI_C="$PAGES/_core/sensorhub_ui.c"
GCC_BIN="${GCC_BIN:-/Applications/mtb-gcc-arm-eabi/14.2.1/gcc/bin}"

RED=$'\033[0;31m'; GRN=$'\033[0;32m'; YEL=$'\033[1;33m'
CYA=$'\033[0;36m'; DIM=$'\033[2m'; BLD=$'\033[1m'; NC=$'\033[0m'
say()  { echo -e "${CYA}$*${NC}"; }
ok()   { echo -e "${GRN}  ok${NC}   $*"; }
warn() { echo -e "${YEL}  warn${NC} $*"; }
bad()  { echo -e "${RED}  FAIL${NC} $*"; }
die()  { echo -e "${RED}error: $*${NC}" >&2; exit 1; }

banner() {
  echo -e "${BLD}${CYA}"
  echo '   ___  ___ _  _ _____ ___'
  echo '  | _ )| __| \| |_   _/ _ \'
  echo '  | _ \| _|| .` | | || (_) |'
  echo '  |___/|___|_|\_| |_| \___/'
  echo -e "${NC}${DIM}  firmware template  ·  PSoC Edge  ·  three cores${NC}"
  echo
}

# --- menus -----------------------------------------------------------------
# The flag name is derived from the directory, so this cannot drift from what
# is on disk the way a hand-kept list would.
menu_flag() {
  local d="$1"
  echo "ENABLE_PAGE_$(echo "$d" | tr '[:lower:]' '[:upper:]')"
}

# Ask make, do not read the text. proj_cm55/Makefile sets several flags twice —
# `?= 1` inside a board conditional and `:= 0` in its else branch — so grepping
# for the last assignment reports the opposite of what gets built. Measured:
# it called GPIO_RGB and MOTOR_CTRL off while page_gpio_rgb_create and
# page_motor_ctrl_create were both in the ELF.
#
# Only the head of the Makefile matters: everything above the ModusToolbox
# include is plain assignment, so it can be evaluated on its own in a moment
# rather than by standing up the whole build system.
FLAGS_MK=""
flags_mk() {
  [ -n "$FLAGS_MK" ] && { echo "$FLAGS_MK"; return; }
  local n
  n=$(grep -n 'include \$(CY_TOOLS_DIR)' "$CM55_MK" | head -1 | cut -d: -f1)
  [ -n "$n" ] || return 1
  FLAGS_MK=$(mktemp)
  head -n "$n" "$CM55_MK" | grep -v 'include \$(CY_TOOLS_DIR)' > "$FLAGS_MK"
  printf '\nprint-%%:\n\t@echo $($*)\n' >> "$FLAGS_MK"
  echo "$FLAGS_MK"
}

menu_state() {   # -> on | off | unflagged
  local f mk v
  f=$(menu_flag "$1")
  mk=$(flags_mk) || { echo unflagged; return; }
  grep -qE "^$f[[:space:]]*[?:]?=" "$CM55_MK" || { echo unflagged; return; }
  v=$(cd "$ROOT/proj_cm55" && make -s -f "$mk" "print-$f" 2>/dev/null | tail -1 | tr -d '[:space:]')
  case "$v" in
    1) echo on ;;
    0) echo off ;;
    *) echo unflagged ;;
  esac
}

list_menus() {
  local d n state size
  printf "  ${BLD}%-22s %-10s %8s${NC}\n" "menu" "state" "size"
  for d in "$PAGES"/*/; do
    n=$(basename "$d")
    [ "$n" = "_core" ] && continue
    state=$(menu_state "$n")
    size=$(du -sh "$d" 2>/dev/null | cut -f1)
    case "$state" in
      on)  printf "  %-22s ${GRN}%-10s${NC} %8s\n" "$n" "on" "$size" ;;
      off) printf "  ${DIM}%-22s %-10s %8s${NC}\n" "$n" "off" "$size" ;;
      *)   printf "  %-22s ${YEL}%-10s${NC} %8s\n" "$n" "always" "$size" ;;
    esac
  done
  echo
  echo -e "  ${DIM}_core holds the Home screen and the page manager; it is not optional.${NC}"
}

set_menu() {   # $1 = menu, $2 = 0|1
  local n="$1" v="$2" f
  [ -d "$PAGES/$n" ] || die "no such menu: $n  (try: $0 menus)"
  f=$(menu_flag "$n")
  grep -qE "^$f[[:space:]]*[?:]?=" "$CM55_MK" \
    || die "$n has no $f in proj_cm55/Makefile — it is always built"
  # Keep ?= as ?= so a command-line override still wins.
  sed -i.bak -E "s|^($f[[:space:]]*[?:]?=)[[:space:]]*[01].*|\1 $v|" "$CM55_MK"
  rm -f "$CM55_MK.bak"
  ok "$f = $v"
  warn "rebuild for this to take effect:  $0 build"
}

delete_menu() {
  local n="$1"
  [ -d "$PAGES/$n" ] || die "no such menu: $n"
  echo
  warn "This deletes $PAGES/$n and every source in it."
  warn "Two more files still reference the page and must be edited by hand:"
  echo "      proj_cm55/modules/page-components/_core/sensorhub_ui.c  (pm_register)"
  echo "      proj_cm55/modules/page-components/_core/page_home.c      (s_card_defs[])"
  echo "  A page registered with no card is unreachable; a card with no"
  echo "  registration crashes when tapped. Leaving the flag at 0 is safer."
  echo
  printf "  type the menu name to confirm: "
  local a; read -r a </dev/tty || a=""
  [ "$a" = "$n" ] || { echo "  aborted"; return 0; }
  rm -rf "${PAGES:?}/$n"
  ok "deleted $n"
  grep -n "PAGE_ID_$(echo "$n" | tr '[:lower:]' '[:upper:]')" "$HOME_C" "$UI_C" 2>/dev/null | sed 's/^/      /' || true
}

# --- checks ----------------------------------------------------------------
cmd_doctor() {
  local fail=0
  say "== toolchain =="
  if [ -x "$GCC_BIN/arm-none-eabi-gcc" ]; then
    ok "arm-none-eabi-gcc $("$GCC_BIN/arm-none-eabi-gcc" -dumpversion)"
  else
    bad "no compiler at $GCC_BIN — set GCC_BIN"; fail=1
  fi
  if ls -d /Applications/ModusToolbox/tools_* >/dev/null 2>&1; then
    ok "ModusToolbox $(ls -d /Applications/ModusToolbox/tools_* | sed 's|.*tools_||' | tr '\n' ' ')"
  else
    bad "ModusToolbox not found"; fail=1
  fi

  say "== what this template carries =="
  [ -d "$ROOT/bento_libs/claw" ] && ok "bento_libs ($(find "$ROOT/bento_libs" -name '*.c' | wc -l | tr -d ' ') sources)" \
                                 || { bad "bento_libs missing"; fail=1; }
  local na; na=$(find "$ROOT/lib" -name '*.a' 2>/dev/null | wc -l | tr -d ' ')
  [ "$na" -gt 0 ] && ok "lib ($na prebuilt archives)" || { bad "lib/ has no archives"; fail=1; }
  ok "$(($(ls -d "$PAGES"/*/ | wc -l) - 1)) menus under page-components (+ _core)"

  say "== what it does not carry =="
  # The workspace holding mtb_shared and the MicroPython port.
  #
  # A customer unpacks this template as a direct child of their workspace, so
  # it is one level up. In the release repository the same tree sits under
  # template/, so it is two. This said two unconditionally, which meant the
  # first command the README tells a customer to run reported both siblings
  # missing on a workspace where both were present.
  #
  # Same marker as common.mk: bento-release.sh exists only in the release
  # repository. BENTO_WORKSPACE overrides, as it does for the build.
  local ws
  if [ -n "${BENTO_WORKSPACE:-}" ]; then
    ws="$(cd "$BENTO_WORKSPACE" 2>/dev/null && pwd || echo "")"
  elif [ -f "$ROOT/../bento-release.sh" ]; then
    ws="$(cd "$ROOT/../.." 2>/dev/null && pwd || echo "")"
  else
    ws="$(cd "$ROOT/.." 2>/dev/null && pwd || echo "")"
  fi
  # The MicroPython port is a requirement of the mtb-mpy variant only. The
  # mtb-only package builds without it — doctor hard-failing a customer who
  # correctly supplied exactly what their variant needs was a red-team
  # finding, 2026-08-28.
  local variant; variant=$(grep -m1 -E '^BENTO_VARIANT\?=' "$ROOT/common.mk" | cut -d= -f2)
  if [ "$variant" = "mtb-only" ]; then
    ok "variant mtb-only — the MicroPython port is not required"
  elif [ -n "$ws" ] && [ -d "$ws/micropython-psoc-edge-psoc-edge-main" ]; then
    ok "micropython port at $ws/micropython-psoc-edge-psoc-edge-main"
  else
    # Name the download. "Not found" with no source is what sent people
    # hunting for a dependency they had no URL for.
    bad "micropython port not found"
    printf '       ดาวน์โหลดจาก release ที่แจก template นี้:\n'
    printf '         https://github.com/tesaiot/tesaiot-pse84-devkit-sdk/releases\n'
    printf '         ไฟล์ micropython-psoc-edge-port.tar.gz  (12.6 MB)\n\n'
    printf '       แตกไฟล์ไว้ข้าง ๆ โฟลเดอร์นี้ ไม่ใช่ข้างใน:\n'
    printf '         cd ..  &&  tar -xzf ~/Downloads/micropython-psoc-edge-port.tar.gz\n\n'
    printf '       ผลลัพธ์ที่ถูกต้อง:\n'
    printf '         <workspace>/micropython-psoc-edge-psoc-edge-main/\n'
    printf '         <workspace>/%s/   <- โฟลเดอร์นี้\n\n' "$(basename "$PWD")"
    printf '       ถ้าเก็บไว้ที่อื่น ให้ชี้ไปที่นั่นแทน:\n'
    printf '         make build BENTO_WORKSPACE=/path/ที่เก็บ/port\n'
    fail=1
  fi
  if [ -n "$ws" ] && [ -d "$ws/mtb_shared" ]; then
    ok "mtb_shared at $ws/mtb_shared"
  else
    warn "mtb_shared not found — run: make getlibs"
  fi

  echo
  [ "$fail" -eq 0 ] && say "doctor: PASS" || die "doctor: FAIL — fix the above first"
}

cmd_verify() {
  [ -f "$ROOT/lib/verify.sh" ] || die "no lib/verify.sh in this release"
  say "== archive signature =="
  ( cd "$ROOT/lib" && ./verify.sh )
}

cmd_build() { say "== building three cores =="; ( cd "$ROOT" && make build -j ); }
cmd_flash() { cmd_build; say "== programming =="; ( cd "$ROOT" && make program ); }
cmd_clean() {
  rm -rf "$ROOT"/proj_cm33_s/build "$ROOT"/proj_cm33_ns/build \
         "$ROOT"/proj_cm33_ns/build-micropython "$ROOT"/proj_cm55/build "$ROOT"/build
  ok "build output removed"
}

interactive() {
  banner
  while :; do
    echo -e "  ${BLD}1${NC}) doctor        ${DIM}check prerequisites${NC}"
    echo -e "  ${BLD}2${NC}) menus         ${DIM}list menus and their state${NC}"
    echo -e "  ${BLD}3${NC}) enable        ${DIM}turn a menu on${NC}"
    echo -e "  ${BLD}4${NC}) disable       ${DIM}turn a menu off${NC}"
    echo -e "  ${BLD}5${NC}) build         ${DIM}all three cores${NC}"
    echo -e "  ${BLD}6${NC}) flash         ${DIM}build and program${NC}"
    echo -e "  ${BLD}7${NC}) verify        ${DIM}check the archive signature${NC}"
    echo -e "  ${BLD}q${NC}) quit"
    echo
    printf "  > "
    local c; read -r c </dev/tty || break
    echo
    case "$c" in
      1) cmd_doctor ;;
      2) list_menus ;;
      3|4) list_menus
           printf "  menu name: "; local m; read -r m </dev/tty || continue
           [ -n "$m" ] && { [ "$c" = 3 ] && set_menu "$m" 1 || set_menu "$m" 0; } ;;
      5) cmd_build ;;
      6) cmd_flash ;;
      7) cmd_verify ;;
      q|Q|"") break ;;
      *) warn "not a choice: $c" ;;
    esac
    echo
  done
}

case "${1:-}" in
  doctor)  cmd_doctor ;;
  menus)   banner; list_menus ;;
  enable)  [ $# -ge 2 ] || die "enable needs a menu name"; set_menu "$2" 1 ;;
  disable) [ $# -ge 2 ] || die "disable needs a menu name"; set_menu "$2" 0 ;;
  remove)  [ $# -ge 2 ] || die "remove needs a menu name"; delete_menu "$2" ;;
  build)   cmd_build ;;
  flash)   cmd_flash ;;
  verify)  cmd_verify ;;
  clean)   cmd_clean ;;
  -h|--help) awk '/^set -euo/{exit} NR>1{sub(/^# ?/,""); print}' "$0" ;;
  "")      interactive ;;
  *)       die "unknown command: $1  (try --help)" ;;
esac
