#!/usr/bin/env python3
"""manifests_from_releases.py — build manifest rows for published releases.

Used to seed firmware-index.json for releases that predate manifest.json being
attached. Going forward, `release_fw.sh` attaches a manifest per artifact and the
workflow folds it in directly; this tool exists for the back-catalogue.

    gh api --paginate repos/tesaiot/tesaiot-pse84-devkit-sdk/releases > releases.json
    tools/manifests_from_releases.py --releases releases.json --out /tmp/manifests

The split is deliberate. **Hashes are never hand-typed** — every sha256 comes from the
`digest` GitHub computes over the stored asset bytes, so a stale copy-paste cannot enter
the index. That failure has already happened here once: the SHA256SUMS.txt attached to
v1.1.0-mtb_mpy, and the v1.1.0-mtb release body, both record ae6fafa2… for the mtb-only
artifact, which is the v1.0.0-mtb build. The published v1.1.0-mtb bytes are a1a61934….

The editorial fields — board, firmware_version, description — cannot be derived from the
API and live in release-rows.json, where a human reviews them.

Only stdlib.
"""
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def die(msg):
    sys.stderr.write("FATAL: %s\n" % msg)
    raise SystemExit(2)


def hash_asset(tag, name):
    """Download one release asset and hash the bytes. Used only when the API gives no
    digest — a hash still must never be typed by a person."""
    import hashlib
    import subprocess
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        r = subprocess.run(["gh", "release", "download", tag, "--pattern", name, "-D", d],
                           capture_output=True, text=True)
        if r.returncode != 0:
            die("could not download %s/%s to hash it: %s" % (tag, name, r.stderr.strip()))
        h = hashlib.sha256()
        with open(os.path.join(d, name), "rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
        return h.hexdigest()


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--releases", required=True,
                    help="JSON array from `gh api --paginate repos/OWNER/REPO/releases`")
    ap.add_argument("--rows", default=os.path.join(HERE, "release-rows.json"),
                    help="curated per-release editorial fields")
    ap.add_argument("--out", required=True, help="directory to write manifests into")
    a = ap.parse_args(argv[1:])

    with open(a.releases) as f:
        releases = json.load(f)
    if not isinstance(releases, list):
        die("--releases must be the array the API returns")
    with open(a.rows) as f:
        curated = json.load(f)["releases"]

    os.makedirs(a.out, exist_ok=True)
    written, skipped = [], []

    for rel in releases:
        tag = rel["tag_name"]
        hexes = [x for x in rel.get("assets", []) if x["name"].endswith(".hex")]
        if not hexes:
            continue
        if tag not in curated:
            skipped.append("%s: no entry in %s" % (tag, os.path.basename(a.rows)))
            continue
        if curated[tag].get("exclude"):
            skipped.append("%s: excluded — %s" % (tag, curated[tag].get("reason", "no reason given")))
            continue
        if len(hexes) > 1:
            die("%s publishes %d .hex assets; release-rows.json describes one per tag"
                % (tag, len(hexes)))
        asset = hexes[0]

        # GitHub does not always populate `digest` — the flash relay received null for
        # exactly these assets and had to fall back to comparing name and size. When it
        # is missing, download and hash the bytes; never fall back to typing a hash.
        digest = asset.get("digest") or ""
        if digest.startswith("sha256:"):
            sha = digest[len("sha256:"):]
        else:
            sys.stderr.write("%s/%s: no digest from the API — hashing the asset itself\n"
                             % (tag, asset["name"]))
            sha = hash_asset(rel["tag_name"], asset["name"])

        row = dict(curated[tag])
        row.pop("exclude", None)
        row.pop("reason", None)
        row.pop("evidence", None)
        row.update({
            "file": asset["name"],
            "sha256": sha,
            "release_tag": tag,
            "status": "OK",
        })
        for field in ("board", "firmware_version", "description"):
            if not row.get(field):
                die("%s: release-rows.json leaves `%s` empty. It cannot be guessed — "
                    "`board` must come from `fw-loader --info` on the real kit." % (tag, field))

        path = os.path.join(a.out, "%s.manifest.json" % tag)
        with open(path, "w") as f:
            json.dump(row, f, indent=2, ensure_ascii=False)
            f.write("\n")
        written.append(path)

    for line in skipped:
        sys.stderr.write("skipped %s\n" % line)
    print("wrote %d manifest(s) to %s" % (len(written), a.out))
    for p in written:
        print("  %s" % os.path.basename(p))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
