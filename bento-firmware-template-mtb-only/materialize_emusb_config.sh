#!/usr/bin/env bash
# Materialise the emUSB-Host configuration files from the fetched asset.
#
# WHY THIS EXISTS
#
# emUSB-Host is SEGGER's stack. SEGGER licensed it to Cypress for OBJECT CODE
# redistribution only, and the asset's own root LICENSE
# (mtb_shared/emusb-host/<tag>/LICENSE) is the Cypress EULA, whose section 2(b)
# permits distributing firmware "in binary code form only" and whose section 4
# says "You agree to keep the Source Code confidential." usbh_config_io.c:14-16
# adds "Knowledge of this file may under no circumstances be used to write a
# similar product." We are a third party with no SEGGER licence, so we may not
# redistribute that source. Two of its files used to sit in this tree, tracked
# and shipped in every package.
#
# They did not need to. Infineon ships them inside the asset, at
# export/Config/, purely so an integrator can copy them out — the asset's
# .cyignore lists "export/Config" precisely so MTB will NOT auto-compile them in
# place. Infineon's own API reference says so:
#
#   "This template is automatically copied into your project when middleware is
#    added to project."                       (emusb-host docs/html/index.html)
#
# So the files are fetched, not vendored. `make getlibs` in proj_cm55 brings the
# asset down; this script copies the two templates out of it and applies the
# only changes that are ours. Nothing SEGGER wrote is stored in this repository
# or in any package built from it.
#
# WHAT IS OURS, AND ALL THAT IS OURS
#
#   1. USBH_ISR_PRIO 3 -> 5. The USB host ISR must sit below the GFX and IPC
#      interrupts or a burst of joystick reports stalls the display pipeline.
#   2. usbh_isr_count / usbh_port_power_count, two volatile counters
#      incremented in isr() and on_port_power_control(). usb_hid_joystick.c
#      reads them to tell "no device" from "device present, no reports" — the
#      F310 stall signature. They are declared extern there, so dropping them
#      is a link error, not a silent loss.
#
# Three edits, expressed as edits rather than as a patch file: a unified diff
# carries context lines, and context lines here would be SEGGER's text. Matching
# on an identifier is not reproducing a work.
#
# Idempotent: safe to re-run, and re-runs on every build. Silent on success,
# so the Makefile can treat any output as the error message.
#
# Exit 0 = the two files are in place and carry our changes.
# Exit 1 = something to fix, printed.
set -u

ws="${1:-}"
dst="${2:-}"
[[ -n "$ws"  ]] || { echo "usage: materialize_emusb_config.sh <workspace> <dest Config dir>"; exit 2; }
[[ -n "$dst" ]] || { echo "usage: materialize_emusb_config.sh <workspace> <dest Config dir>"; exit 2; }

# Glob the tag rather than pinning it here. deps/emusb-host.mtb owns the
# version; a second copy of it in this script is a second thing to forget.
src=""
for c in "$ws"/mtb_shared/emusb-host/*/export/Config; do
    [[ -d "$c" ]] && { src="$c"; break; }
done

if [[ -z "$src" ]]; then
    cat <<HINT
The emUSB-Host asset is not present, so its configuration files cannot be
materialised.

  looked for: $ws/mtb_shared/emusb-host/*/export/Config

  Fetch it, then build again:

      cd proj_cm55 && make getlibs

Dependencies are fetched PER PROJECT in this template. Running getlibs only in
proj_cm33_ns leaves this asset absent.
HINT
    exit 1
fi

for f in usbh_config_io.c COMPONENT_PSE84/usbh_config.c; do
    [[ -f "$src/$f" ]] || { echo "the emUSB-Host asset at $src is missing $f"; exit 1; }
done

mkdir -p "$dst/COMPONENT_PSE84" || exit 1
rsync -q "$src/usbh_config_io.c"              "$dst/usbh_config_io.c"              || exit 1
rsync -q "$src/COMPONENT_PSE84/usbh_config.c" "$dst/COMPONENT_PSE84/usbh_config.c" || exit 1
# rsync preserves the asset's read-only mode; the next run must be able to
# overwrite, and the edit below must be able to write.
chmod u+w "$dst/usbh_config_io.c" "$dst/COMPONENT_PSE84/usbh_config.c" 2>/dev/null

# python3, not sed -i: BSD and GNU sed disagree about -i, and this script runs
# on both. Fails loudly if a hunk does not apply -- a silently unpatched
# usbh_config.c links, runs, and then reports zero interrupts forever.
python3 - "$dst/COMPONENT_PSE84/usbh_config.c" <<'PYEOF' || exit 1
import re, sys

p = sys.argv[1]
s = open(p).read()
orig = s
fail = []

# 1. ISR priority.
s, n = re.subn(r'(#define\s+USBH_ISR_PRIO\s+)\(3U\)', r'\g<1>(5U)', s, count=1)
if n == 0 and '#define USBH_ISR_PRIO                           (5U)' not in s \
          and not re.search(r'#define\s+USBH_ISR_PRIO\s+\(5U\)', s):
    fail.append('USBH_ISR_PRIO could not be set to 5')

# 2. The two counters, declared just above isr().
if 'usbh_isr_count' not in s:
    s, n = re.subn(
        r'^(static void isr\(void\)\n\{\n)',
        '/* Read by usb_hid_joystick.c to tell "no device" from "device present,\n'
        ' * no reports" -- the F310 stall signature. Declared extern there. */\n'
        'volatile uint32_t usbh_isr_count = 0;\n'
        'volatile uint32_t usbh_port_power_count = 0;\n\n'
        r'\g<1>    usbh_isr_count++;\n',
        s, count=1, flags=re.M)
    if n == 0:
        fail.append('could not find isr(void) to add usbh_isr_count')

# 3. The port-power counter.
if 'usbh_port_power_count++' not in s:
    s, n = re.subn(
        r'(static void on_port_power_control\(U32 HostControllerIndex, U8 Port, U8 PowerOn\) \{\n'
        r'(?:[^\n]*\n)*?)(\n)',
        r'\g<1>    usbh_port_power_count++;\n\g<2>', s, count=1)
    if n == 0:
        fail.append('could not find on_port_power_control() to add usbh_port_power_count')

if fail:
    sys.stderr.write(
        'The emUSB-Host configuration template has changed shape and our edits\n'
        'no longer apply:\n\n')
    for m in fail:
        sys.stderr.write('    ' + m + '\n')
    sys.stderr.write(
        '\n  file: ' + p + '\n'
        '  This means the pinned asset version moved. Re-check the three edits\n'
        '  described at the top of materialize_emusb_config.sh against the new\n'
        '  template before building.\n')
    sys.exit(1)

if s != orig:
    open(p, 'w').write(s)
PYEOF

# Prove the result rather than trust the substitution, the same way
# verify_asset_patches.sh does for the mtb_shared changes.
c="$dst/COMPONENT_PSE84/usbh_config.c"
miss=()
grep -q 'USBH_ISR_PRIO  *(5U)'      "$c" || miss+=("USBH_ISR_PRIO is not 5")
grep -q 'usbh_isr_count++'          "$c" || miss+=("usbh_isr_count is not incremented in isr()")
grep -q 'usbh_port_power_count++'   "$c" || miss+=("usbh_port_power_count is not incremented")
if [[ ${#miss[@]} -gt 0 ]]; then
    echo "emUSB-Host config materialised but our changes are not in it:"
    printf '    %s\n' "${miss[@]}"
    exit 1
fi
exit 0
