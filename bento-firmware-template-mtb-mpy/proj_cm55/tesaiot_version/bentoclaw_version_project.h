#ifndef BENTOCLAW_VERSION_PROJECT_H
#define BENTOCLAW_VERSION_PROJECT_H

/*
 * Per-project firmware version — TESAIoT Dev Kit MicroPython Core.
 *
 * Single source of truth: the on-screen Home version, the CM33 boot banner
 * ("fw" field via modbentoclaw), the .fw_identity record (via
 * firmware_identity.mk), and the GitHub release tag `v<this>` on
 * wiroon/TESAIoT_KIT_PSE84_AI-Micropython-BentoClaw all derive from this.
 * Bump on every released build.
 *
 * Picked up by the shared bentoclaw_version.h through __has_include.
 */
#define BENTOCLAW_VERSION "1.10.0"

#endif /* BENTOCLAW_VERSION_PROJECT_H */
