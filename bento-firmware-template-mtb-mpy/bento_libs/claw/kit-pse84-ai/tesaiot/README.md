# TESAIoT Library

**OPTIGA Trust M certificate enrolment for PSoC Edge**

## Overview

This directory holds the public headers of the TESAIoT OPTIGA Trust M
integration that the template calls: serialised access to the chip,
certificate enrolment and Protected Update requests to the TESAIoT platform,
blocking data-object helpers, MQTT connection state and wall-clock queries.

Chip access and enrolment are provided by `libbento_hsm.a`
(`lib/tesaiot_hsm/`). The data-object helpers are defined in
`optiga_trust_helpers.c`, which ships as source. The complete, checked list of
what `libbento_hsm.a` exports is its own public header,
`lib/tesaiot_hsm/include/tesaiot_hsm_api.h`.

---

## Header Files

| Header | Declares |
|--------|----------|
| `tesaiot.h` | Nothing of its own: includes the headers below |
| `tesaiot_config.h` | Log level switches, log line prefixes, two constants |
| `tesaiot_optiga_core.h` | Serialised access to the chip |
| `tesaiot_optiga.h` | Enrolment requests, their progress state, and data-object helpers |
| `tesaiot_platform.h` | MQTT connect / connected, wall-clock queries |
| `tesaiot_protected_update.h` | Offline Protected Update self-test |
| `tesaiot_crypto.h`, `tesaiot_license.h`, `tesaiot_license_config.h` | Nothing (kept so existing includes build) |

`include/README.md` says where each group of calls is implemented.

### Usage

```c
#include "tesaiot.h"            // everything above
```

or include just the header you need, for example `tesaiot_optiga_core.h` for
chip access alone.

---

## Calling Surface

### Chip access (`tesaiot_optiga_core.h`)

| Function | Description |
|----------|-------------|
| `optiga_manager_init()` | Start the OPTIGA stack; call once during start-up |
| `optiga_manager_acquire()` / `optiga_manager_release()` | Borrow and hand back the shared `optiga_util` instance |
| `optiga_manager_lock()` / `optiga_manager_unlock()` | Exclusive access without borrowing the shared instance |
| `optiga_chip_enter()` / `optiga_chip_exit()` | Open and close a chip session |

### Enrolment (`tesaiot_optiga.h`)

| Function | Description |
|----------|-------------|
| `publish_csr()` | Send a certificate signing request, built by the caller, to the platform |
| `tesaiot_publish_protected_update()` | Ask the platform for a Protected Update of an object |
| `trustm_requested_target_oid()` / `trustm_requested_anchor_oid()` | Object ids named by the request in flight |
| `trustm_current_correlation_id()` | Correlation id of the request in flight |
| `trustm_update_state()` / `trustm_reset_state()` | Record or reset the stage of the exchange (`trustm_state_t`) |

### Data-object helpers (`tesaiot_optiga.h`)

| Function | Description |
|----------|-------------|
| `tesaiot_read_lcso()` | Read the chip's life-cycle state |
| `tesaiot_read_data()` / `tesaiot_write_data()` | Fetch or store the content of an object |
| `tesaiot_read_metadata()` / `tesaiot_write_metadata()` | Fetch or replace the metadata of an object |
| `tesaiot_write_trust_anchor()` | Store a trust anchor in an object |
| `tesaiot_erase_data()` | Empty an object |
| `tesaiot_verify_manifest_with_trustanchor()` | Check a Protected Update manifest against a trust anchor |

### Platform (`tesaiot_platform.h`)

| Function | Description |
|----------|-------------|
| `tesaiot_mqtt_connect()` / `tesaiot_mqtt_is_connected()` | Connect to the MQTT broker / query the connection |
| `tesaiot_sntp_get_time()` / `tesaiot_sntp_is_time_synced()` | Wall-clock queries |

### Protected Update self-test (`tesaiot_protected_update.h`)

| Function | Description |
|----------|-------------|
| `tesaiot_run_protected_update_isolated_test()` | Offline self-test; it overwrites provisioned objects, so read the header first |

---

## What the Secure Element Provides

1. **Hardware root of trust**
   - OPTIGA Trust M is a CC EAL6+ certified secure element
   - Tamper-resistant hardware with secure key storage
   - The factory UID cannot be modified or cloned

2. **Private key protection**
   - Keys are generated inside OPTIGA and never exported
   - Signing happens on-chip; firmware cannot read the private key bytes

3. **mTLS mutual authentication**
   - Device and broker verify each other
   - The device proves its identity with a hardware-bound key

---

## Platform Support

| Component | Specification |
|-----------|---------------|
| MCU | Infineon PSoC Edge E84 (Cortex-M33) |
| Secure Element | OPTIGA Trust M |
| RTOS | FreeRTOS |
| Network | lwIP |
| TLS | mbedTLS |

---

## Support

For licensing and technical support:

- Email: support@tesaiot.com
- Website: https://tesaiot.com

---

## Authors

**Assoc. Prof. Wiroon Sriborrirux (BDH)**

- Thai Embedded Systems Association (TESA)
- TESAIoT Platform Creator
- Email: wiroon@tesa.or.th


**TESAIoT Platform Developer Team**

- In collaboration with Infineon Technologies AG

---

## Copyright

(c) 2025-2026 TESAIoT AIoT Foundation Platform. All rights reserved.

Developed by Assoc. Prof. Wiroon Sriborrirux (BDH) and Thai Embedded Systems Association (TESA).

Unauthorized use, copying, or distribution is prohibited.

