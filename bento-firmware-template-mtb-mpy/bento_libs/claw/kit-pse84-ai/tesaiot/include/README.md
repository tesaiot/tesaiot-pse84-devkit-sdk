# TESAIoT OPTIGA Trust M headers

These headers declare what this template can actually call: functions that an
archive shipped in `lib/` exports, and functions defined in sources shipped
here. The one exception is `tesaiot_sntp_get_time()`, declared because the
helper source calls it; nothing in this template defines it.

| Header | Declares | Implemented in |
|--------|----------|----------------|
| `tesaiot.h` | Nothing of its own: includes the headers below | — |
| `tesaiot_config.h` | Log level switches, log line prefixes, two constants | — (macros only) |
| `tesaiot_optiga_core.h` | Serialised access to the chip: init, acquire/release, lock/unlock, session enter/exit | `libbento_hsm.a` |
| `tesaiot_optiga.h` | Enrolment requests and progress state | `libbento_hsm.a` |
| | Blocking data-object helpers (read/write data and metadata, trust anchor, verify manifest) | `optiga_trust_helpers.c` (shipped as source) |
| `tesaiot_protected_update.h` | Offline Protected Update self-test (it overwrites provisioned objects; read the header first) | `libbento_hsm.a` |
| `tesaiot_platform.h` | MQTT connect / connected, wall-clock queries | the MQTT client in `bento_libs/claw/common/modules/tesaiot_mqtt/`; `tesaiot_sntp_get_time()` has no definition in this template |
| `tesaiot_crypto.h` | Nothing (kept so existing includes build) | — |
| `tesaiot_license.h` | Nothing (kept so existing includes build) | — |
| `tesaiot_license_config.h` | Nothing (kept so existing includes build) | — |

## Usage

```c
#include "tesaiot.h"            // everything above
```

or include just the header you need, for example `tesaiot_optiga_core.h` for
chip access alone.

The complete, checked list of what `libbento_hsm.a` exports is its own public
header, `lib/tesaiot_hsm/include/tesaiot_hsm_api.h`.

---

(c) 2025-2026 TESAIoT AIoT Foundation Platform
