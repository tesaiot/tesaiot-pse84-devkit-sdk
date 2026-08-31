#!/usr/bin/env python3
"""gen_manifest.py — emit the release manifest.json (contract §6 catalog binding).

Reads the on-device identity record from the FINAL (CRC-stamped) hex and pairs it with the
artifact's sha256 so a tool can map an on-board identity to the catalog and answer
"is this the latest?" (matching key = firmware_uuid, ordered by firmware_version).

    gen_manifest.py --hex build/app_combined.hex --addr 0x60900000 \
        --file app_combined.hex --out build/manifest.json

Field names are frozen by contract §6 (sku / firmware_uuid / firmware_version) so Programmer
(spec 02) and Remote (spec 03) consume the same shape. Derives every identity field from the
record itself — nothing is hand-typed, so the manifest can never drift from the flashed image.
"""
import argparse
import hashlib
import json
import sys

# Reuse the single source of truth for the record layout.
import read_fw_identity as rfi


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--hex", required=True, help="final CRC-stamped Intel HEX")
    ap.add_argument("--addr", default="0x60900000", help="record address (Option B tail)")
    ap.add_argument("--file", required=True, help="release artifact filename (as uploaded)")
    ap.add_argument("--out", required=True, help="manifest.json output path")
    a = ap.parse_args(argv[1:])

    addr = int(a.addr, 0)
    mem = rfi.parse_ihex(a.hex)
    rec = rfi.extract(mem, addr, 256)
    if rec is None:
        sys.stderr.write(f"FATAL: no record at {addr:#x} in {a.hex}\n")
        return 3
    info, err = rfi.decode(rec)
    if info is None:
        sys.stderr.write(f"FATAL: record at {addr:#x} failed decode: {err}\n")
        return 3
    if not info["uuid_matches_sku"]:
        sys.stderr.write(f"FATAL: uuid {info['uuid']} != uuid5(ns, {info['sku']})\n")
        return 3
    if not info["crc_checked"]:
        sys.stderr.write("FATAL: record crc32 == 0 (not stamped) — run patch_fw_identity.py first\n")
        return 3
    if not info["crc_ok"]:
        sys.stderr.write(f"FATAL: crc stored {info['crc_stored']:#010x} != calc {info['crc_calc']:#010x}\n")
        return 3

    with open(a.hex, "rb") as f:
        sha256 = hashlib.sha256(f.read()).hexdigest()

    manifest = {
        "sku": info["sku"],                    # contract §6 — matches on-device sku
        "firmware_uuid": info["uuid"],         # contract §6 — matches on-device uuid
        "firmware_version": info["version"],   # contract §6 — matches on-device version
        "board": info["board"],
        "family": info["family"],
        "file": a.file,
        "sha256": sha256,
        "status": "OK",
    }
    with open(a.out, "w") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    print(f"manifest: {info['sku']} v{info['version']} uuid={info['uuid']} sha256={sha256[:12]}… -> {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
