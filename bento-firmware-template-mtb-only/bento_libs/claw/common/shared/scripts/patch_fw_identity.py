#!/usr/bin/env python3
"""patch_fw_identity.py — POSTBUILD: fill build_epoch + crc32 into the identity record.

Phase 2. MUST run on the CM55 image BEFORE the MCUboot sign step, because the record sits inside
the signed application region — patching after signing would break the signature.

    patch_fw_identity.py <in.hex> <out.hex> [--addr 0x60580400] [--epoch N]

Reads the 256-byte record at --addr, verifies the magic, writes build_epoch (LE @0x68) and
crc32 = CRC-32/ISO-HDLC over bytes [0x00..0xFB] (LE @0xFC), re-emits the hex with corrected
record + line checksums. Idempotent-ish: recomputes CRC over the freshly-patched epoch.
"""
import argparse
import struct
import sys
import zlib

MAGIC = 0x49415354
SIZE = 256


def load(path):
    """Return (records) where each record is (addr16, rectype, data) plus base handling done lazily."""
    lines = []
    with open(path) as f:
        for ln in f:
            s = ln.rstrip("\n")
            if s.startswith(":"):
                lines.append(s)
    return lines


def build_mem(lines):
    """Return (mem, extras). extras keeps type-03/05 start-address records verbatim so
    re-emission is faithful (they carry no flashed bytes but name the entry point)."""
    mem, base, extras = {}, 0, []
    for s in lines:
        raw = bytes.fromhex(s[1:])
        if (sum(raw) & 0xFF) != 0:
            raise ValueError(f"bad ihex checksum: {s}")
        n, addr16, rt = raw[0], (raw[1] << 8) | raw[2], raw[3]
        data = raw[4:4 + n]
        if rt == 0x00:
            for i, b in enumerate(data):
                mem[base + addr16 + i] = b
        elif rt == 0x04:
            base = (data[0] << 24) | (data[1] << 16)
        elif rt == 0x02:
            base = ((data[0] << 8) | data[1]) << 4
        elif rt in (0x03, 0x05):           # start segment / start linear address
            extras.append((rt, addr16, data))
    return mem, extras


def ihex_line(rectype, addr16, data):
    n = len(data)
    body = bytes([n, (addr16 >> 8) & 0xFF, addr16 & 0xFF, rectype]) + data
    chk = (-sum(body)) & 0xFF
    return ":" + (body + bytes([chk])).hex().upper()


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("infile")
    ap.add_argument("outfile")
    ap.add_argument("--addr", default="0x60580400")
    ap.add_argument("--epoch", type=int, default=0)
    a = ap.parse_args(argv[1:])
    addr = int(a.addr, 0)

    mem, extras = build_mem(load(a.infile))
    rec = bytearray()
    for x in range(addr, addr + SIZE):
        if x not in mem:
            sys.stderr.write(f"FATAL: record byte {x:#x} absent in {a.infile}\n")
            return 3
        rec.append(mem[x])
    magic, = struct.unpack_from("<I", rec, 0)
    if magic != MAGIC:
        sys.stderr.write(f"FATAL: magic {magic:#010x} != TSAI at {addr:#x}\n")
        return 3

    struct.pack_into("<I", rec, 0x68, a.epoch & 0xFFFFFFFF)
    crc = zlib.crc32(bytes(rec[0:0xFC])) & 0xFFFFFFFF
    struct.pack_into("<I", rec, 0xFC, crc)
    for i in range(SIZE):
        mem[addr + i] = rec[i]

    # re-emit: rebuild the hex from scratch as sorted data blocks (16 bytes/line) + ELA records
    out = []
    cur_base = None
    addrs = sorted(mem)
    i = 0
    while i < len(addrs):
        a0 = addrs[i]
        base = a0 & 0xFFFF0000
        if base != cur_base:
            out.append(ihex_line(0x04, 0, bytes([(base >> 24) & 0xFF, (base >> 16) & 0xFF])))
            cur_base = base
        # gather a run of up to 16 contiguous bytes within this base
        chunk = []
        start = a0
        while (i < len(addrs) and addrs[i] == start + len(chunk)
               and (addrs[i] & 0xFFFF0000) == base and len(chunk) < 16):
            chunk.append(mem[addrs[i]])
            i += 1
        out.append(ihex_line(0x00, start & 0xFFFF, bytes(chunk)))
    for rt, addr16, data in extras:        # preserve start-address records verbatim
        out.append(ihex_line(rt, addr16, data))
    out.append(":00000001FF")
    with open(a.outfile, "w") as f:
        f.write("\n".join(out) + "\n")
    print(f"patched: epoch={a.epoch} crc32={crc:#010x} @ {addr:#x} -> {a.outfile}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
