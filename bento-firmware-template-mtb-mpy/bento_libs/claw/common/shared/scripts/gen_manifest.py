#!/usr/bin/env python3
"""gen_manifest.py — emit the release manifest.json (contract §6 catalog binding).

Reads the on-device identity record from the FINAL (CRC-stamped) hex and pairs it with the
artifact's sha256 so a tool can map an on-board identity to the catalog and answer
"is this the latest?" (matching key = firmware_uuid, ordered by firmware_version).

    gen_manifest.py --hex build/app_combined.hex --addr 0x60900000 \
        --file app_combined.hex --out build/manifest.json

Field names are frozen by the release contract at
../fw_identity/RELEASE_CONTRACT.md so Programmer, the flash relay and firmware-index.json
consume the same shape. Every identity field is derived from the record itself — nothing is
hand-typed, so the manifest can never drift from the flashed image.

THE ONLY MANIFEST THAT MAY BE PUBLISHED IS ONE THIS SCRIPT PRODUCED. If it refuses, the
firmware is not ready to release; fix the firmware, not the JSON. This is not hypothetical:
the manifest.json published with KIT_PSE84_EVAL_EPC2-MicroPython-BentoClaw v1.4.0 declares
sku BENTO-CLAW-EVA-MPY for a hex that contains no identity record at all — the TSAI magic
appears zero times in the file. It was typed, not generated (it carries edge_ai_models and
no firmware_uuid, neither of which this script emits). It reached the platform team's
product catalogue and came back to us as a requirement for a SKU no binary has ever
declared. Going around this script is the hole; the script is not.

`sku` is the identifier the flash UI resolves on, because `board` cannot be: five projects
build with TARGET=KIT_PSE84_AI and emit that one string for two physically different kits.
A sku must therefore be unique per PRODUCT, never shared by two.
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
    ap.add_argument("--description", default=None,
                    help="one line shown under the firmware name in the flash picker; "
                         "write it for a first-year student, and make it tell two "
                         "variants of one board apart")
    ap.add_argument("--edge-ai-models", default=None,
                    help="comma-separated model names; the picker falls back to these "
                         "when there is no description")
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
    # Editorial, not derived — the only two fields a human supplies, and neither can
    # misidentify a board.
    if a.description:
        manifest["description"] = a.description
    if a.edge_ai_models:
        manifest["edge_ai_models"] = [m.strip() for m in a.edge_ai_models.split(",") if m.strip()]
    with open(a.out, "w") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    print(f"manifest: {info['sku']} v{info['version']} uuid={info['uuid']} sha256={sha256[:12]}… -> {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
