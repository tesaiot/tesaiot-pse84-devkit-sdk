#!/usr/bin/env python3
"""gen_firmware_index.py — merge release manifests into firmware-index.json.

The index is the only file a browser can read that STATES which board a published
.hex belongs to. Release assets live on release-assets.githubusercontent.com, which
sends no access-control-allow-origin, so a web page cannot fetch manifest.json where
it is; a file committed to the repository tree is served by raw.githubusercontent.com
with `access-control-allow-origin: *` and no API rate limit. Hence this file, at the
repository root, on the default branch.

    gen_firmware_index.py --index firmware-index.json \
        --release-tag v1.2.0-mtb_mpy build/*/manifest.json

Rows are keyed by sha256 — the same hash the flash relay reports for an artifact —
so old versions coexist with new ones and re-running a release is a no-op.

Only stdlib: this runs unchanged in GitHub Actions with no setup step.

Exit codes: 0 index written or already current, 1 usage/IO error, 2 a row was
rejected (see stderr — nothing is written when a row is rejected).
"""
import argparse
import datetime
import json
import os
import re
import sys

SCHEMA_VERSION = "1.0.0"

# fullmatch, not match: `$` also matches before a trailing newline, so a hash carrying
# the newline from `shasum ... | cut` used to validate and enter the index as a key that
# can never match the relay's hash. The row is then dead and its artifact silently
# unattributable — the exact failure this file exists to prevent.
SHA256_RE = re.compile(r"[0-9a-f]{64}")

# Fields the consumer requires. A row missing any of these cannot be attributed to a
# board, which is the entire purpose of the index, so it is an error and not a warning.
REQUIRED = ("sha256", "board", "firmware_version")

# Recommended by the contract: without one the picker falls back to edge_ai_models and
# then to the bare sku, which reads as noise to a first-year student.
RECOMMENDED = ("description", "sku")


def die(msg):
    sys.stderr.write("FATAL: %s\n" % msg)
    raise SystemExit(2)


def board_key(b):
    """Fold the board the way the consumer matches it: case-insensitive, - == _."""
    return str(b or "").lower().replace("-", "_")


def version_key(v):
    """Sort 1.10.0 after 1.9.1. A plain string sort puts it before, which would show a
    student an older firmware as the newest one."""
    parts = re.split(r"[._-]", str(v or ""))
    key = []
    for p in parts:
        key.append((0, int(p), "") if p.isdigit() else (1, 0, p))
    return key


def row_sort_key(r):
    return (str(r.get("board") or ""), version_key(r.get("firmware_version")),
            str(r.get("sku") or ""), str(r.get("sha256") or ""))


def load_rows(path):
    """Read an existing index. Accepts both shapes the contract allows."""
    if not os.path.exists(path):
        return [], None
    try:
        with open(path, encoding="utf-8") as f:
            doc = json.load(f)
    except (OSError, ValueError) as e:
        die("%s could not be read as JSON: %s" % (path, e))
    if isinstance(doc, list):
        return doc, None
    if isinstance(doc, dict) and isinstance(doc.get("firmware"), list):
        return doc["firmware"], doc.get("generated_at")
    die("%s is neither a bare array nor an object with a `firmware` array" % path)


def check(row, origin):
    if not isinstance(row, dict):
        die("%s: expected an object, got %s" % (origin, type(row).__name__))
    for field in REQUIRED:
        if not row.get(field):
            die("%s: missing required field `%s`" % (origin, field))
        if not isinstance(row[field], str):
            die("%s: `%s` must be a string, got %s (%r)"
                % (origin, field, type(row[field]).__name__, row[field]))
    if not isinstance(row["sha256"], str) or not SHA256_RE.fullmatch(row["sha256"]):
        die("%s: sha256 %r is not 64 lowercase hex characters" % (origin, row["sha256"]))
    for field in RECOMMENDED:
        if not row.get(field):
            sys.stderr.write("warning: %s: no `%s`\n" % (origin, field))


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("manifests", nargs="*", help="manifest.json files from this release")
    ap.add_argument("--index", default="firmware-index.json", help="index to update in place")
    ap.add_argument("--release-tag", default=os.environ.get("GITHUB_REF_NAME"),
                    help="tag to stamp on this release's rows (default: $GITHUB_REF_NAME)")
    ap.add_argument("--keep-first-release-tag", action="store_true",
                    help="on a re-publish of identical bytes, keep the tag that first "
                         "carried them instead of the newest one")
    ap.add_argument("--check", action="store_true",
                    help="validate the existing index and exit; write nothing")
    a = ap.parse_args(argv[1:])

    old, old_generated = load_rows(a.index)
    for i, row in enumerate(old):
        check(row, "%s[%d]" % (a.index, i))

    if a.check:
        print("%s: %d row(s), all valid" % (a.index, len(old)))
        return 0

    if not a.manifests:
        ap.error("no manifests given (use --check to validate the existing index)")

    new = []
    for path in a.manifests:
        try:
            with open(path, encoding="utf-8") as f:
                row = json.load(f)
        except (OSError, ValueError) as e:
            die("%s could not be read as JSON: %s" % (path, e))
        if not isinstance(row, dict):
            die("%s: expected a single manifest object" % path)
        if a.release_tag:
            row = dict(row, release_tag=a.release_tag)
        check(row, path)
        new.append(row)

    # Two rows may share a sha256 only when they are the same artifact republished.
    # If they disagree about the board, one board would silently vanish from the index
    # and its owners would be offered nothing — six of the nine firmware projects build
    # with TARGET=KIT_PSE84_AI, so byte-identical images across kits are a live risk
    # here, not a hypothetical. A human has to resolve it.
    def identity(r):
        return (board_key(r["board"]), r["firmware_version"], r.get("sku"))

    by_hash = {}
    for row in old + new:
        prior = by_hash.setdefault(row["sha256"], row)
        if identity(prior) != identity(row):
            die("sha256 %s carries two identities, %r and %r. Merging keeps one and the "
                "other artifact silently leaves the index. Identical bytes cannot be two "
                "different firmwares — fix the manifest, or drop one row."
                % (row["sha256"], identity(prior), identity(row)))

    # Later wins, so this release replaces an identical-bytes row published earlier —
    # which is normally right, the artifact IS in this release. --keep-first-release-tag
    # inverts that when first publication is what you want recorded.
    merged = {}
    for row in (new + old) if a.keep_first_release_tag else (old + new):
        merged[row["sha256"]] = row
    rows = sorted(merged.values(), key=row_sort_key)

    # Not fatal — the contract keeps every version, and a rebuild of the same version
    # legitimately produces a second hash. But the picker then has two entries a
    # student cannot tell apart, so the descriptions have to do that work.
    seen_version = {}
    for r in rows:
        k = (board_key(r["board"]), r["firmware_version"])
        seen_version.setdefault(k, []).append(r["sha256"])
    for (board, ver), hashes in sorted(seen_version.items()):
        if len(hashes) > 1:
            sys.stderr.write(
                "warning: %s %s has %d artifacts (%s) — the picker shows them side by "
                "side, so their descriptions must tell them apart\n"
                % (board, ver, len(hashes), ", ".join(h[:12] + "…" for h in hashes)))

    # generated_at must not change when nothing else did, or every release commits a
    # one-line diff and the history stops meaning anything.
    unchanged = rows == sorted(old, key=row_sort_key)
    if unchanged and old_generated:
        generated = old_generated
    else:
        generated = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

    doc = {"schema_version": SCHEMA_VERSION, "generated_at": generated, "firmware": rows}
    body = json.dumps(doc, indent=2, ensure_ascii=False) + "\n"

    if unchanged and os.path.exists(a.index):
        with open(a.index, encoding="utf-8") as f:
            if f.read() == body:
                print("%s: unchanged (%d row(s))" % (a.index, len(rows)))
                return 0

    # Unique per process: two releases publishing at once must not clobber
    # each other's partially written file.
    tmp = "%s.%d.tmp" % (a.index, os.getpid())
    with open(tmp, "w", encoding="utf-8") as f:
        f.write(body)
    os.replace(tmp, a.index)
    seen = {r["sha256"] for r in old}
    added = sum(1 for r in new if r["sha256"] not in seen)
    print("%s: %d row(s) — %d added, %d replaced, %d carried"
          % (a.index, len(rows), added, len(new) - added, len(old) - (len(new) - added)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
