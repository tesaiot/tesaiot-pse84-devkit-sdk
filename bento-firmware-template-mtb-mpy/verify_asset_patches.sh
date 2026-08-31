#!/usr/bin/env bash
# Are the patched ModusToolbox assets in place, and are they the RIGHT ones?
#
# Existence is not enough. Ten of the eleven changes fail silently — without the
# cy_tls.c change the OPTIGA key is never bound to the TLS session and mTLS
# quietly falls back to a software key — so "the file is there" says very
# little. GNU patch also defaults to fuzz factor 2, which lets a hunk land
# somewhere it does not belong and still exit 0; its own CAVEATS note that
# compiling cleanly is not proof, and Yocto's patch-fuzz QA check exists
# because "it is entirely possible for an incorrectly patched file to still
# compile without errors".
#
# So this compares SHA-256 against PATCHED.sha256, which records the eleven
# files exactly as they must end up.
#
# Exit 0 = all present and correct. Exit 1 = something to fix, printed.
set -u

ws="${1:-}"
sums="${2:-}"
[[ -n "$ws"   ]] || { echo "usage: verify_asset_patches.sh <workspace> <PATCHED.sha256>"; exit 2; }
[[ -f "$sums" ]] || { echo "no manifest at $sums"; exit 2; }
[[ -d "$ws/mtb_shared" ]] || { echo "no $ws/mtb_shared — run 'make getlibs' in each project first"; exit 1; }

missing=(); wrong=()
while read -r want path; do
    [[ -n "${path:-}" ]] || continue
    if [[ ! -f "$ws/mtb_shared/$path" ]]; then
        missing+=("$path")
    else
        have=$(shasum -a 256 "$ws/mtb_shared/$path" | awk '{print $1}')
        [[ "$have" == "$want" ]] || wrong+=("$path")
    fi
done < "$sums"

if [[ ${#missing[@]} -eq 0 && ${#wrong[@]} -eq 0 ]]; then
    exit 0
fi

echo "The patched ModusToolbox assets are not in place."
echo
[[ ${#missing[@]} -gt 0 ]] && { echo "  absent (${#missing[@]}):"; printf '    %s\n' "${missing[@]}"; }
[[ ${#wrong[@]}   -gt 0 ]] && { echo "  present but not the patched version (${#wrong[@]}):"; printf '    %s\n' "${wrong[@]}"; }
cat <<'HINT'

  Apply them from inside mtb_shared, refusing fuzz:

      cd <workspace>/mtb_shared
      for p in $(cat <patches>/series); do
          patch -p1 -F0 --forward < "<patches>/$p" || exit 1
      done
      shasum -a 256 -c <patches>/PATCHED.sha256

  What each change does: THIRD_PARTY_PATCHES.md
HINT
exit 1
