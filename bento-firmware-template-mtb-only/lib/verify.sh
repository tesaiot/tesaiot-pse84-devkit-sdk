#!/usr/bin/env bash
# Verify a downloaded BENTO library release. Runs from anywhere: the
# manifest paths are relative to this directory, so it cd s here.
set -euo pipefail
cd "$(dirname "$0")"
[ -f release_public.pub.pem ] || { echo "missing release_public.pub.pem" >&2; exit 1; }
openssl dgst -sha256 -verify release_public.pub.pem \
             -signature manifest.txt.sig manifest.txt
# macOS ships shasum; most Linux distributions ship sha256sum and not shasum.
if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 -c manifest.txt
else
    sha256sum -c manifest.txt
fi
echo "OK: signature valid and every file matches its digest"
