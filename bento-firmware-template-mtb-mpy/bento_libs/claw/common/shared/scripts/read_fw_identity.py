#!/usr/bin/env python3
"""read_fw_identity.py — read + validate the 256-byte firmware identity record.

Two modes:
  --hex <file.hex>   Offline: parse an Intel HEX and extract the record (no hardware).
  --openocd          On target: read 256 bytes over SWD via openocd (needs a board + cfg).

Validates magic and (if non-zero) CRC-32/ISO-HDLC, then prints SKU / UUID / version / board.
This is the reader that Programmer (spec 02) and Remote (spec 03) reuse.
"""
import argparse
import struct
import subprocess
import sys
import uuid
import zlib

# ORIGIN(m55_nvm_sel) + 0x380000, matching the linker script and
# release_fw.sh. Was 0x60580400 until 2026-09-02 — 3.5 MB low, in vector-table
# data — so the default reported "record absent" on valid images.
FW_IDENTITY_ADDR = 0x60900000
FW_IDENTITY_SIZE = 256
FW_IDENTITY_MAGIC = 0x49415354  # "TSAI" LE


# ---- Intel HEX -> sparse bytes -------------------------------------------------
def parse_ihex(path):
    mem = {}
    base = 0
    with open(path, "r") as f:
        for ln in f:
            ln = ln.strip()
            if not ln or ln[0] != ":":
                continue
            raw = bytes.fromhex(ln[1:])
            n, addr_hi, rectype = raw[0], (raw[1] << 8) | raw[2], raw[3]
            data = raw[4:4 + n]
            if (sum(raw) & 0xFF) != 0:
                raise ValueError(f"bad ihex checksum: {ln}")
            if rectype == 0x00:            # data
                a = base + addr_hi
                for i, b in enumerate(data):
                    mem[a + i] = b
            elif rectype == 0x04:          # extended linear address
                base = (data[0] << 24) | (data[1] << 16)
            elif rectype == 0x02:          # extended segment address
                base = ((data[0] << 8) | data[1]) << 4
            elif rectype == 0x01:          # EOF
                break
    return mem


def extract(mem, addr, size):
    out = bytearray()
    for a in range(addr, addr + size):
        if a not in mem:
            return None
        out.append(mem[a])
    return bytes(out)


def find_record(mem):
    """Locate the record by its own magic instead of trusting an address.

    The record is self-identifying — 'TSAI' plus a CRC over its own bytes — so an
    address is a convenience, not a requirement. It is also the thing most often
    wrong: the constant in fw_identity.h pointed 3.5 MB below the linker's placement
    until 2026-09-02, and every reader that trusted it reported "record absent" on
    valid images. Projects also differ: a 6 MB m55_nvm can hold the record at
    ORIGIN+0x380000, a 3.75 MB one cannot, so it sits at the region tail there.

    Returns (addr, bytes) for the first plausible record, or (None, None).
    """
    magic = FW_IDENTITY_MAGIC.to_bytes(4, "little")
    for a in sorted(mem):
        if (mem.get(a), mem.get(a + 1), mem.get(a + 2), mem.get(a + 3)) != tuple(magic):
            continue
        rec = extract(mem, a, FW_IDENTITY_SIZE)
        if rec is None:
            continue
        info, err = decode(rec)
        if info is not None:
            return a, rec
    return None, None


# ---- openocd read --------------------------------------------------------------
def read_openocd(addr, size, cfg, openocd):
    # dumps `size` bytes at `addr` to a temp bin via openocd, then read it back
    import tempfile, os
    fd, tmp = tempfile.mkstemp(suffix=".bin")
    os.close(fd)
    cmds = f"init; halt; dump_image {tmp} {addr:#x} {size}; resume; shutdown"
    args = [openocd]
    for c in cfg:
        args += ["-f", c]
    args += ["-c", cmds]
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stderr + "\n")
        raise RuntimeError("openocd read failed")
    with open(tmp, "rb") as f:
        data = f.read()
    os.unlink(tmp)
    return data


# ---- decode + validate ---------------------------------------------------------
def decode(rec):
    if len(rec) != FW_IDENTITY_SIZE:
        return None, "size != 256"
    magic, struct_ver, flags = struct.unpack_from("<IHH", rec, 0)
    if magic != FW_IDENTITY_MAGIC:
        return None, f"magic {magic:#010x} != TSAI (record absent)"
    uuid_raw = rec[0x08:0x18]
    sku = rec[0x18:0x38].split(b"\x00")[0].decode("ascii", "replace")
    version = rec[0x38:0x48].split(b"\x00")[0].decode("ascii", "replace")
    board = rec[0x48:0x58].split(b"\x00")[0].decode("ascii", "replace")
    family = rec[0x58:0x68].split(b"\x00")[0].decode("ascii", "replace")
    build_epoch, = struct.unpack_from("<I", rec, 0x68)
    crc_stored, = struct.unpack_from("<I", rec, 0xFC)
    crc_calc = zlib.crc32(rec[0:0xFC]) & 0xFFFFFFFF
    info = {
        "magic": "TSAI", "struct_ver": struct_ver, "flags": flags,
        "uuid": str(uuid.UUID(bytes=uuid_raw)), "sku": sku, "version": version,
        "board": board, "family": family, "build_epoch": build_epoch,
        "crc_stored": crc_stored, "crc_calc": crc_calc,
        "crc_ok": (crc_stored == 0) or (crc_stored == crc_calc),
        "crc_checked": crc_stored != 0,
        "release": bool(flags & 0x1), "debug": bool(flags & 0x2),
    }
    # cross-check: UUID must equal uuid5(ns, sku)
    ns = uuid.uuid5(uuid.NAMESPACE_DNS, "firmware-identity.tesaiot.io")
    info["uuid_matches_sku"] = (str(uuid.uuid5(ns, sku)) == info["uuid"])
    return info, None


def main(argv):
    ap = argparse.ArgumentParser()
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--hex", help="Intel HEX file (offline)")
    g.add_argument("--openocd", action="store_true", help="read over SWD via openocd")
    ap.add_argument("--addr", default=hex(FW_IDENTITY_ADDR))
    ap.add_argument("--cfg", action="append", default=[], help="openocd -f config (repeatable)")
    ap.add_argument("--openocd-bin", default="openocd")
    ap.add_argument("--no-scan", action="store_true",
                    help="fail at --addr instead of searching the image for the magic")
    a = ap.parse_args(argv[1:])
    addr = int(a.addr, 0)

    if a.hex:
        mem = parse_ihex(a.hex)
        rec = extract(mem, addr, FW_IDENTITY_SIZE)
        if rec is not None and decode(rec)[0] is None:
            rec = None                      # bytes present but not a record
        if rec is None and not a.no_scan:
            found, rec = find_record(mem)
            if rec is not None and found != addr:
                print(f"  note        : no record at {addr:#010x}; found one at {found:#010x}")
                addr = found
        if rec is None:
            where = (f"at {addr:#010x} (--no-scan)" if a.no_scan
                     else "anywhere in the image — searched for the TSAI magic")
            print(f"NO RECORD in {a.hex} {where}")
            return 3
    else:
        rec = read_openocd(addr, FW_IDENTITY_SIZE, a.cfg, a.openocd_bin)

    info, err = decode(rec)
    if err:
        print(f"INVALID: {err}")
        return 3
    print(f"  addr        : {addr:#010x}")
    print(f"  magic       : {info['magic']}  struct_ver={info['struct_ver']}")
    print(f"  sku         : {info['sku']}")
    print(f"  uuid        : {info['uuid']}  (matches sku: {info['uuid_matches_sku']})")
    print(f"  version     : {info['version']}")
    print(f"  board       : {info['board']}    family: {info['family']}")
    print(f"  flags       : {info['flags']:#06x}  release={info['release']} debug={info['debug']}")
    print(f"  build_epoch : {info['build_epoch']}")
    if info["crc_checked"]:
        print(f"  crc32       : stored={info['crc_stored']:#010x} calc={info['crc_calc']:#010x} "
              f"{'OK' if info['crc_ok'] else 'MISMATCH'}")
    else:
        print(f"  crc32       : 0 (unchecked — pilot; calc would be {info['crc_calc']:#010x})")
    ok = info["uuid_matches_sku"] and info["crc_ok"]
    print("  RESULT      :", "VALID" if ok else "CHECK FAILED")
    return 0 if ok else 4


if __name__ == "__main__":
    sys.exit(main(sys.argv))
