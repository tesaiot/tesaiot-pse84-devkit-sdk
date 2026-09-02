#!/usr/bin/env python3
"""Tests for gen_firmware_index.py. Run: python3 tools/test_gen_firmware_index.py

Every case here is one that would put a wrong row in front of a student, or would
make the release workflow commit noise. Stdlib only, so CI needs no setup step.
"""
import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
GEN = os.path.join(HERE, "gen_firmware_index.py")

H = {
    "a": "a" * 64,
    "b": "b" * 64,
    "c": "c" * 64,
}

failures = []


def run(args, cwd):
    return subprocess.run([sys.executable, GEN] + args, cwd=cwd,
                          capture_output=True, text=True)


def manifest(cwd, name, **fields):
    row = {"sku": "T-SKU", "firmware_version": "1.0.0", "board": "KIT_X",
           "file": "app_combined.hex", "sha256": H["a"], "description": "d"}
    row.update(fields)
    path = os.path.join(cwd, name)
    with open(path, "w") as f:
        json.dump(row, f)
    return path


def index(cwd):
    with open(os.path.join(cwd, "firmware-index.json")) as f:
        return json.load(f)


def check(name, cond, detail=""):
    if cond:
        print("  ok   %s" % name)
    else:
        print("  FAIL %s %s" % (name, detail))
        failures.append(name)


def case(name):
    def deco(fn):
        print(name)
        with tempfile.TemporaryDirectory() as d:
            fn(d)
        return fn
    return deco


@case("first release from nothing")
def _(d):
    m = manifest(d, "m1.json")
    r = run(["--release-tag", "v1", m], d)
    check("exit 0", r.returncode == 0, r.stderr)
    doc = index(d)
    check("one row", len(doc["firmware"]) == 1)
    check("tag stamped", doc["firmware"][0]["release_tag"] == "v1")
    check("schema_version", doc["schema_version"] == "1.0.0")


@case("second release adds a board, keeps the old one")
def _(d):
    run(["--release-tag", "v1", manifest(d, "m1.json")], d)
    r = run(["--release-tag", "v2", manifest(d, "m2.json", sha256=H["b"], board="KIT_Y")], d)
    check("exit 0", r.returncode == 0, r.stderr)
    check("two rows", len(index(d)["firmware"]) == 2)


@case("re-running the same release is a true no-op")
def _(d):
    m = manifest(d, "m1.json")
    run(["--release-tag", "v1", m], d)
    before = open(os.path.join(d, "firmware-index.json")).read()
    r = run(["--release-tag", "v1", m], d)
    after = open(os.path.join(d, "firmware-index.json")).read()
    check("exit 0", r.returncode == 0, r.stderr)
    # generated_at must not move, or the release workflow commits an empty diff
    # every single time and the history stops meaning anything.
    check("byte-identical", before == after,
          "generated_at moved on an unchanged index")
    check("says unchanged", "unchanged" in r.stdout, r.stdout)


@case("a row with no sha256 is rejected, not silently merged")
def _(d):
    m = manifest(d, "bad.json", sha256=None)
    r = run(["--release-tag", "v1", m], d)
    check("exit 2", r.returncode == 2, "rc=%d" % r.returncode)
    check("no index written", not os.path.exists(os.path.join(d, "firmware-index.json")))


@case("a truncated or upper-case sha256 is rejected")
def _(d):
    for bad in ("abc123", H["a"].upper(), H["a"] + "0"):
        r = run(["--release-tag", "v1", manifest(d, "bad.json", sha256=bad)], d)
        check("rejected %r" % bad[:12], r.returncode == 2, r.stdout + r.stderr)


@case("a row with no board is rejected — attribution is the whole point")
def _(d):
    r = run(["--release-tag", "v1", manifest(d, "bad.json", board=None)], d)
    check("exit 2", r.returncode == 2, "rc=%d" % r.returncode)
    check("names the field", "board" in r.stderr, r.stderr)


@case("1.10.0 sorts after 1.9.1, not before")
def _(d):
    run(["--release-tag", "v1", manifest(d, "a.json", sha256=H["a"], firmware_version="1.9.1")], d)
    run(["--release-tag", "v2", manifest(d, "b.json", sha256=H["b"], firmware_version="1.10.0")], d)
    versions = [r["firmware_version"] for r in index(d)["firmware"]]
    check("order", versions == ["1.9.1", "1.10.0"], str(versions))


@case("identical bytes republished take the newer tag by default")
def _(d):
    run(["--release-tag", "v1", manifest(d, "m.json")], d)
    run(["--release-tag", "v2", manifest(d, "m.json")], d)
    rows = index(d)["firmware"]
    check("still one row", len(rows) == 1, str(len(rows)))
    check("tag is v2", rows[0]["release_tag"] == "v2", rows[0].get("release_tag"))


@case("--keep-first-release-tag records first publication instead")
def _(d):
    r1 = run(["--release-tag", "v1", manifest(d, "m.json")], d)
    check("first run ok", r1.returncode == 0, r1.stderr)
    check("starts at v1", index(d)["firmware"][0]["release_tag"] == "v1")
    r2 = run(["--keep-first-release-tag", "--release-tag", "v2", manifest(d, "m.json")], d)
    check("second run ok", r2.returncode == 0, r2.stderr)
    check("tag stayed v1", index(d)["firmware"][0]["release_tag"] == "v1")
    # Prove the case is not vacuous: without the flag the same input moves the tag.
    r3 = run(["--release-tag", "v2", manifest(d, "m.json")], d)
    check("control: without the flag it becomes v2",
          r3.returncode == 0 and index(d)["firmware"][0]["release_tag"] == "v2", r3.stderr)


@case("a bare array index is read, and rewritten in the wrapped shape")
def _(d):
    with open(os.path.join(d, "firmware-index.json"), "w") as f:
        json.dump([{"sku": "old", "firmware_version": "0.9.0", "board": "KIT_Z",
                    "sha256": H["c"], "description": "d"}], f)
    r = run(["--release-tag", "v2", manifest(d, "m.json")], d)
    check("exit 0", r.returncode == 0, r.stderr)
    doc = index(d)
    check("wrapped", isinstance(doc, dict) and "firmware" in doc)
    check("old row carried", len(doc["firmware"]) == 2, str(len(doc["firmware"])))


@case("--check validates an existing index and writes nothing")
def _(d):
    m = manifest(d, "m.json")
    run(["--release-tag", "v1", m], d)
    before = open(os.path.join(d, "firmware-index.json")).read()
    r = run(["--check"], d)
    check("exit 0", r.returncode == 0, r.stderr)
    check("unchanged", open(os.path.join(d, "firmware-index.json")).read() == before)
    bad = json.loads(before)
    bad["firmware"][0]["sha256"] = "nope"
    with open(os.path.join(d, "firmware-index.json"), "w") as f:
        json.dump(bad, f)
    check("catches a corrupt row", run(["--check"], d).returncode == 2)


@case("two boards claiming one sha256 is refused, not silently deduped")
def _(d):
    # The failure this guards against: group_by/dict-keying on sha256 drops one of the
    # two rows and exits 0. The board whose row was dropped is then offered nothing.
    run(["--release-tag", "v1", manifest(d, "a.json", board="KIT_A")], d)
    r = run(["--release-tag", "v1", manifest(d, "b.json", board="KIT_B")], d)
    check("exit 2", r.returncode == 2, "rc=%d" % r.returncode)
    check("names both boards", "kit_a" in r.stderr.lower() and "kit_b" in r.stderr.lower(), r.stderr)
    check("index untouched", [x["board"] for x in index(d)["firmware"]] == ["KIT_A"])


@case("two builds of one version coexist, with a warning")
def _(d):
    run(["--release-tag", "v1", manifest(d, "a.json", sha256=H["a"])], d)
    r = run(["--release-tag", "v2", manifest(d, "b.json", sha256=H["b"])], d)
    check("exit 0", r.returncode == 0, r.stderr)
    check("both kept", len(index(d)["firmware"]) == 2)
    check("warned", "side by side" in r.stderr, r.stderr)


@case("a missing description warns but does not fail")
def _(d):
    r = run(["--release-tag", "v1", manifest(d, "m.json", description=None)], d)
    check("exit 0", r.returncode == 0, r.stderr)
    check("warned", "description" in r.stderr, r.stderr)


@case("a sha256 carrying a trailing newline is rejected")
def _(d):
    # `shasum ... | cut` hands back a newline. `$` matches before one, so this used to
    # validate and enter the index as a key that can never match the relay's hash.
    r = run(["--release-tag", "v1", manifest(d, "m.json", sha256=H["a"] + "\n")], d)
    check("exit 2", r.returncode == 2, "rc=%d" % r.returncode)
    check("no index written", not os.path.exists(os.path.join(d, "firmware-index.json")))
    for pad in (" " + H["a"], H["a"] + " ", H["a"] + "\t"):
        check("rejects padded %r" % pad[:3],
              run(["--release-tag", "v1", manifest(d, "m.json", sha256=pad)], d).returncode == 2)


@case("wrong types give a clean error, not a traceback")
def _(d):
    for field, value in (("sha256", 12345678), ("firmware_version", ["1", "0"]),
                         ("firmware_version", 2.0), ("board", {"a": 1})):
        r = run(["--release-tag", "v1", manifest(d, "m.json", **{field: value})], d)
        check("%s=%r -> exit 2" % (field, value), r.returncode == 2,
              "rc=%d stderr=%s" % (r.returncode, r.stderr[-200:]))
        check("%s=%r -> no traceback" % (field, value), "Traceback" not in r.stderr,
              r.stderr[-200:])


@case("a corrupt existing index is named, not crashed on")
def _(d):
    open(os.path.join(d, "firmware-index.json"), "w").write("{not json")
    r = run(["--check"], d)
    check("exit 2", r.returncode == 2, "rc=%d" % r.returncode)
    check("no traceback", "Traceback" not in r.stderr, r.stderr[-200:])
    check("names the file", "firmware-index.json" in r.stderr, r.stderr)


@case("an index row that is not an object is rejected")
def _(d):
    with open(os.path.join(d, "firmware-index.json"), "w") as f:
        json.dump(["justastring"], f)
    r = run(["--check"], d)
    check("exit 2", r.returncode == 2, "rc=%d" % r.returncode)
    check("no traceback", "Traceback" not in r.stderr, r.stderr[-200:])


@case("board matching folds case and -/_ the way the consumer does")
def _(d):
    # KIT_PSE84_AI and kit-pse84-ai are ONE board to the consumer. Treating them as two
    # keys hid a duplicate that a student would see as two identical picker entries.
    run(["--release-tag", "v1", manifest(d, "a.json", sha256=H["a"], board="KIT_PSE84_AI")], d)
    r = run(["--release-tag", "v2", manifest(d, "b.json", sha256=H["b"], board="kit-pse84-ai")], d)
    check("exit 0", r.returncode == 0, r.stderr)
    check("warned about the duplicate", "side by side" in r.stderr, r.stderr)


@case("one sha256 cannot carry two versions — no row is dropped in silence")
def _(d):
    run(["--release-tag", "v1", manifest(d, "a.json", firmware_version="1.0.0")], d)
    r = run(["--release-tag", "v2", manifest(d, "b.json", firmware_version="2.0.0")], d)
    check("exit 2", r.returncode == 2, "rc=%d" % r.returncode)
    check("old row survives", len(index(d)["firmware"]) == 1)


print()
if failures:
    print("%d FAILED: %s" % (len(failures), ", ".join(failures)))
    sys.exit(1)
print("all passed")
