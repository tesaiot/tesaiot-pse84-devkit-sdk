/*
 * fw_identity.c — the section-anchored firmware identity instance.
 *
 * Emitted into the ".fw_identity" section, which the CM55 non-secure linker script places at
 * FW_IDENTITY_ADDR (contract §2.1). Values come from a build-time generated header
 * "fw_id_config.h" (produced by fw_identity.mk from the project's firmware_identity.mk).
 *
 * Self-contained fallback: if a project has not opted in (no fw_id_config.h on the include
 * path), this still compiles with an explicit UNSET identity — so adding this file to the
 * shared CM55 source tree never breaks a project that has not wired the mechanism yet.
 *
 * CRC/build_epoch are 0 here; a POSTBUILD step (patch_fw_identity.py) may fill them BEFORE the
 * MCUboot sign step. CRC == 0 means "unchecked" to readers (contract §4).
 */
#include "fw_identity.h"

#if defined(__has_include)
#  if __has_include("fw_id_config.h")
#    include "fw_id_config.h"
#  endif
#endif

#ifndef FW_ID_SKU
#  define FW_ID_SKU        "TESAIOT-UNSET"
#  define FW_ID_VERSION    "0.0.0"
#  define FW_ID_BOARD      "UNSET"
#  define FW_ID_FAMILY     "UNSET"
#  define FW_ID_UUID_BYTES 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
#  define FW_ID_RELEASE    0
#endif

__attribute__((used, section(".fw_identity")))
const fw_identity_t g_fw_identity = {
    .magic       = FW_IDENTITY_MAGIC,
    .struct_ver  = FW_IDENTITY_STRUCT_VER,
    .flags       = (FW_ID_RELEASE ? FW_IDENTITY_FLAG_RELEASE : FW_IDENTITY_FLAG_DEBUG),
    .uuid        = { FW_ID_UUID_BYTES },
    .sku         = FW_ID_SKU,
    .version     = FW_ID_VERSION,
    .board       = FW_ID_BOARD,
    .family      = FW_ID_FAMILY,
    .build_epoch = 0u,          /* optionally patched in POSTBUILD (pre-sign) */
    .reserved    = { 0 },
    .crc32       = 0u,          /* 0 = unchecked; patched in POSTBUILD (pre-sign) */
};
