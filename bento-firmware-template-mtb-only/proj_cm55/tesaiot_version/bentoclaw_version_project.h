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
/* 1.12.0 - QWA309 base board on the GPIO & RGB Matrix page: the pin beside
 * every input, pots in mV as well as raw, the CapSense link state, SW1-SW4
 * from CapSense firmware 0x0D/0x0E, and the push-button labels from one
 * constant pair. The Dashboard redraws the CapSense line only when it
 * changes; BMI270/DPS368/SHT40 init retries four times at start-up.
 */
/* 1.11.0 — fixes for the TESA university Dev Kit report: display bus clear
 * before the panel init, SCB5 master-only and a cross-core SCB5 lock (OPTIGA vs
 * touch/CapSense/RGB), the XIP guard for C-storage writes, a read-only HSM
 * self-test that no longer erases saved WiFi #3, and the OID table corrected.
 */
#define BENTOCLAW_VERSION "1.13.0"

#endif /* BENTOCLAW_VERSION_PROJECT_H */
