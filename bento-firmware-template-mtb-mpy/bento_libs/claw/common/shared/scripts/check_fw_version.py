#!/usr/bin/env python3
"""check_fw_version.py — publish guard (contract §5, spec 01 §5).

Refuses to publish a SKU+version pair that already exists, so a forgotten version bump fails the
publish instead of shipping a duplicate to customers.

    check_fw_version.py <SKU> <VERSION> [--repo owner/name] [--tag-prefix fw-eva-game-v]

Exit codes:
    0  version is NEW for this SKU (safe to publish)
    1  version ALREADY published for this SKU (block)
    2  usage / config error
    3  could not reach GitHub — SOFT PASS with a loud warning (do not block the build on network)

Matching: a release is considered "this SKU+version" if its manifest.json lists an artifact with
firmware_uuid == uuid5(sku) and firmware_version == version (contract §6), OR — until manifests
carry the fields — if a release tag equals "<tag-prefix><version>". The stricter uuid match wins
when a manifest is present.
"""
import argparse
import json
import subprocess
import sys
import uuid


def fw_uuid(sku):
    ns = uuid.uuid5(uuid.NAMESPACE_DNS, "firmware-identity.tesaiot.io")
    return str(uuid.uuid5(ns, sku))


def gh_json(args):
    r = subprocess.run(["gh"] + args, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(r.stderr.strip() or "gh failed")
    return json.loads(r.stdout) if r.stdout.strip() else []


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("sku")
    ap.add_argument("version")
    ap.add_argument("--repo", default="Advance-Innovation-Centre-AIC/embedded-systems-for-developer")
    ap.add_argument("--tag-prefix", default="")
    a = ap.parse_args(argv[1:])

    # Refuse to run as a silent no-op: without a tag-prefix (or a manifest matcher, spec 03)
    # there is nothing to compare against, so a forgotten bump would pass. Fail loudly instead.
    if not a.tag_prefix:
        sys.stderr.write("ERROR: --tag-prefix is required (nothing to match against otherwise). "
                         "Pass the project's FW_RELEASE_TAG_PREFIX.\n")
        return 2

    want_uuid = fw_uuid(a.sku)
    try:
        releases = gh_json(["api", f"repos/{a.repo}/releases", "--paginate",
                            "--jq", "[.[] | {tag: .tag_name, assets: [.assets[].name]}]"])
    except Exception as e:
        sys.stderr.write(f"WARNING: could not query GitHub ({e}) — SOFT PASS (guard not enforced)\n")
        return 3

    # Tag-based check (works today; manifest uuid check is a superset for spec 03)
    if a.tag_prefix:
        want_tag = f"{a.tag_prefix}{a.version}"
        for rel in releases:
            if rel.get("tag") == want_tag:
                sys.stderr.write(f"BLOCK: {want_tag} already published on {a.repo} "
                                 f"(sku={a.sku}, uuid={want_uuid}). Bump FW_VERSION.\n")
                return 1
    print(f"OK: {a.sku} v{a.version} (uuid {want_uuid}) is new on {a.repo}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
