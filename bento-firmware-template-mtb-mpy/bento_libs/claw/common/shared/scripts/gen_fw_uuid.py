#!/usr/bin/env python3
"""gen_fw_uuid.py — deterministic UUIDv5 bytes for a firmware SKU (contract §4).

STANDALONE: a person checks a SKU's uuid with it; the build computes its own inline

    uuid = uuid5(TESAIOT_FW_NS, sku)
    TESAIOT_FW_NS = uuid5(NAMESPACE_DNS, "firmware-identity.tesaiot.io")
                  = abc4e100-8876-5985-bef8-73d532d38527   (frozen for contract v1.0)

Usage:
    gen_fw_uuid.py <SKU>            # prints "0xc4,0x1a,0x56,..." (16 bytes, for the C initializer)
    gen_fw_uuid.py <SKU> --uuid     # prints the canonical UUID string
"""
import sys
import uuid

TESAIOT_FW_NS = uuid.uuid5(uuid.NAMESPACE_DNS, "firmware-identity.tesaiot.io")
# frozen constant guard — refuse to run if the namespace ever drifts
_FROZEN_NS = "abc4e100-8876-5985-bef8-73d532d38527"


def fw_uuid(sku):
    return uuid.uuid5(TESAIOT_FW_NS, sku)


def main(argv):
    if str(TESAIOT_FW_NS) != _FROZEN_NS:
        sys.stderr.write(f"FATAL: TESAIOT_FW_NS drift {TESAIOT_FW_NS} != {_FROZEN_NS}\n")
        return 2
    if len(argv) < 2:
        sys.stderr.write("usage: gen_fw_uuid.py <SKU> [--uuid]\n")
        return 2
    sku = argv[1]
    u = fw_uuid(sku)
    if "--uuid" in argv[2:]:
        print(str(u))
    else:
        print(",".join(f"0x{b:02x}" for b in u.bytes))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
