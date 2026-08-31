/*
 * fw_identity.h — on-device firmware identity record.
 *
 * Implements the byte layout defined normatively in
 * TESAIoT_PLAN/Firmware_Identity/00_Firmware_Identity_Contract.md (contract v1.0).
 * A packed 256-byte record placed at a fixed CM55 flash address so a flashed board can
 * report which firmware it runs (UUID + SKU + version + board) — read back over SWD.
 *
 * DO NOT edit the field order / sizes / offsets here without a contract version bump.
 */
#ifndef FW_IDENTITY_H
#define FW_IDENTITY_H

#include <stdint.h>
#include <stddef.h>   /* offsetof */

/* Contract §2.2 constants */
#define FW_IDENTITY_MAGIC        0x49415354u   /* "TSAI" as uint32 LE (bytes 54 53 41 49) */
#define FW_IDENTITY_STRUCT_VER   0x0001u       /* contract layout version */
#define FW_IDENTITY_ADDR         0x60580400u   /* base(m55_nvm_sel) + MCUBOOT_HEADER_SIZE(0x400) */
#define FW_IDENTITY_SIZE         256u

/* flags (contract §2.3) */
#define FW_IDENTITY_FLAG_RELEASE 0x0001u        /* version equals a published GitHub release */
#define FW_IDENTITY_FLAG_DEBUG   0x0002u        /* non-release / debug image */

typedef struct __attribute__((packed)) {
    uint32_t magic;         /* 0x00  FW_IDENTITY_MAGIC */
    uint16_t struct_ver;    /* 0x04  FW_IDENTITY_STRUCT_VER */
    uint16_t flags;         /* 0x06  bitfield */
    uint8_t  uuid[16];      /* 0x08  UUIDv5(sku), RFC-4122 network byte order */
    char     sku[32];       /* 0x18  ASCII, NUL-padded — TESAIOT-<FAMILY>-<BOARD>-<APP>[-<VARIANT>] */
    char     version[16];   /* 0x38  ASCII semver "MAJOR.MINOR.PATCH", NUL-padded */
    char     board[16];     /* 0x48  ASCII board token, NUL-padded (e.g. "KIT_PSE84_AI") */
    char     family[16];    /* 0x58  ASCII lib/product family (e.g. "CLAW", "GAME") */
    uint32_t build_epoch;   /* 0x68  uint32 LE Unix seconds, 0 if unused (POSTBUILD, informational) */
    uint8_t  reserved[144]; /* 0x6C  zero-filled, reserved for future contract versions */
    uint32_t crc32;         /* 0xFC  CRC-32/ISO-HDLC over [0x00..0xFB]; 0 = unchecked (pilot) */
} fw_identity_t;            /* sizeof == 256 */

#ifdef __cplusplus
static_assert(sizeof(fw_identity_t) == 256, "fw_identity must be 256 bytes");
#else
_Static_assert(sizeof(fw_identity_t) == 256, "fw_identity must be 256 bytes");
_Static_assert(offsetof(fw_identity_t, magic)      == 0x00, "magic offset");
_Static_assert(offsetof(fw_identity_t, struct_ver) == 0x04, "struct_ver offset");
_Static_assert(offsetof(fw_identity_t, flags)      == 0x06, "flags offset");
_Static_assert(offsetof(fw_identity_t, uuid)       == 0x08, "uuid offset");
_Static_assert(offsetof(fw_identity_t, sku)        == 0x18, "sku offset");
_Static_assert(offsetof(fw_identity_t, version)    == 0x38, "version offset");
_Static_assert(offsetof(fw_identity_t, board)      == 0x48, "board offset");
_Static_assert(offsetof(fw_identity_t, family)     == 0x58, "family offset");
_Static_assert(offsetof(fw_identity_t, build_epoch) == 0x68, "build_epoch offset");
_Static_assert(offsetof(fw_identity_t, reserved)   == 0x6C, "reserved offset");
_Static_assert(offsetof(fw_identity_t, crc32)      == 0xFC, "crc32 offset");
#endif

/* The linker-anchored instance (defined in fw_identity.c, section ".fw_identity"). */
extern const fw_identity_t g_fw_identity;

#endif /* FW_IDENTITY_H */
