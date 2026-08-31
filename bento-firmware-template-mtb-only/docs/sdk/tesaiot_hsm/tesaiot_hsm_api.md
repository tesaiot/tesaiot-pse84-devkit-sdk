# tesaiot_hsm_api.h

The complete API of libbento_hsm.a — every function the archive exports, and nothing it does not. OPTIGA chip ownership, CSR publication and Protected Update entry points.

## Functions (exported by the archive)

### `optiga_chip_enter`

```c
bool optiga_chip_enter(void);
```

Open and close a chip session around a burst of operations.

### `optiga_chip_exit`

```c
void optiga_chip_exit(void);
```

_No description in the header._

### `optiga_manager_acquire`

```c
optiga_util_t *optiga_manager_acquire(void);
```

The shared optiga_util instance, or NULL before init.

### `optiga_manager_init`

```c
bool optiga_manager_init(callback_handler_t callback, void *context);
```

File Name: tesaiot_hsm_api.h Description: The complete API of libbento_hsm.a — every function the archive exports, and nothing it does not. OPTIGA chip ownership, CSR publication and Protected Update entry points. / /* tesaiot_hsm_api.h — the API libbento_hsm.a exports. Written by hand, and checked by the release pipeline against api.txt on every package run: the archive's exported set and this file must name exactly the same eighteen symbols or packaging fails. It was briefly generated instead, by pruning the internal headers with a regular expression. That is not a thing regular expressions can do. The result kept ten `static inline` wrapper BODIES — a definition has a brace, so the declaration pattern never matched one — which republished nine of the internal names the archive renames to bx_N, under their real signatures. It also removed each source header's closing #endif while leaving its #ifndef, so the file did not compile. Eighteen prototypes are cheap to maintain and can be read; a parser for C is neither. What is NOT here is deliberate. tesaiot_optiga.h and its two siblings declare ~52 functions between them and this archive exports 18. The rest are the enrolment and Protected Update machinery, and they are renamed in the archive precisely so a consumer cannot reach them — shipping their declarations would undo that. / #ifndef TESAIOT_HSM_API_H #define TESAIOT_HSM_API_H #include <stdint.h> #include <stdbool.h> #include <stddef.h> /* optiga_util_t, optiga_lib_status_t and callback_handler_t come from the OPTIGA Trust M library, which is an ordinary ModusToolbox asset and is not part of this archive. */ #include "optiga_util.h" #include "common/optiga_lib_types.h" #ifdef __cplusplus extern "C" { #endif /* --------------------------------------------------------------------------- Chip manager — ownership of the single OPTIGA instance. The chip is one device behind one I2C bus, shared by the MQTT/TLS path, the HSM provisioning screen and the MicroPython optiga module. Everything that touches it goes through here. ------------------------------------------------------------------------- */ /** Bring up the OPTIGA stack. Returns false if the chip did not answer.

### `optiga_manager_lock`

```c
bool optiga_manager_lock(void);
```

Take and release the manager mutex. Task context only.

### `optiga_manager_release`

```c
void optiga_manager_release(void);
```

Release the instance taken with optiga_manager_acquire().

### `optiga_manager_touch_hold`

```c
void optiga_manager_touch_hold(void);
```

Hold the chip powered between operations, and let it go again. _reason is the same hold with a string for the log.

### `optiga_manager_touch_hold_reason`

```c
void optiga_manager_touch_hold_reason(const char *reason);
```

_No description in the header._

### `optiga_manager_touch_release`

```c
void optiga_manager_touch_release(void);
```

_No description in the header._

### `optiga_manager_unlock`

```c
void optiga_manager_unlock(void);
```

_No description in the header._

### `publish_csr`

```c
int publish_csr(uint8_t *csr, size_t csr_length, uint16_t target_oid, uint16_t trust_anchor_oid, uint32_t payload_version);
```

--------------------------------------------------------------------------- Enrolment and Protected Update. ------------------------------------------------------------------------- */ /** Where the enrolment state machine is. */ typedef enum { TRUSTM_STATE_IDLE = 0, TRUSTM_STATE_PUBLISHING_CSR, TRUSTM_STATE_WAITING_FOR_MANIFEST, TRUSTM_STATE_APPLYING_UPDATE, TRUSTM_STATE_WAITING_FOR_CERTIFICATE, TRUSTM_STATE_COMPLETE, TRUSTM_STATE_ERROR, TRUSTM_STATE_WAITING_FOR_JSON_BUNDLE, TRUSTM_STATE_PROCESSING_JSON_BUNDLE, TRUSTM_STATE_WRITING_TRUST_ANCHOR, TRUSTM_STATE_VERIFYING_MANIFEST, TRUSTM_STATE_APPLYING_FRAGMENTS, TRUSTM_STATE_PROTECTED_UPDATE_SUCCESS, TRUSTM_STATE_PROTECTED_UPDATE_FAILED } trustm_state_t; /** Publish a CSR the caller has already built, for the platform to sign. target_oid is the slot the resulting certificate belongs in and trust_anchor_oid the anchor that will authorise writing it.

### `tesaiot_publish_protected_update`

```c
int tesaiot_publish_protected_update(const char *target_oid, const char *trust_anchor_oid, uint32_t payload_version, bool with_csr);
```

Ask the platform for a Protected Update of target_oid, optionally enrolling a fresh key with a CSR in the same exchange. The OIDs are hex strings, e.g. "E0E1".

### `tesaiot_run_protected_update_isolated_test`

```c
void tesaiot_run_protected_update_isolated_test(void);
```

Run the Protected Update path end to end against the isolated test slot, leaving the live certificate untouched.

### `trustm_current_correlation_id`

```c
const char *trustm_current_correlation_id(void);
```

_No description in the header._

### `trustm_requested_anchor_oid`

```c
uint16_t trustm_requested_anchor_oid(void);
```

_No description in the header._

### `trustm_requested_target_oid`

```c
uint16_t trustm_requested_target_oid(void);
```

The OIDs the in-flight request named, and the id that correlates the platform's reply with it. NULL when nothing is in flight.

### `trustm_reset_state`

```c
void trustm_reset_state(void);
```

_No description in the header._

### `trustm_update_state`

```c
void trustm_update_state(trustm_state_t new_state, const char *status_code, const char *detail);
```

Move the state machine, and put it back at IDLE. status_code and detail are surfaced on the provisioning screen.

## Enums

### `trustm_state_t`

```c
typedef enum {
    TRUSTM_STATE_IDLE = 0,
    TRUSTM_STATE_PUBLISHING_CSR,
    TRUSTM_STATE_WAITING_FOR_MANIFEST,
    TRUSTM_STATE_APPLYING_UPDATE,
    TRUSTM_STATE_WAITING_FOR_CERTIFICATE,
    TRUSTM_STATE_COMPLETE,
    TRUSTM_STATE_ERROR,
    TRUSTM_STATE_WAITING_FOR_JSON_BUNDLE,
    TRUSTM_STATE_PROCESSING_JSON_BUNDLE,
    TRUSTM_STATE_WRITING_TRUST_ANCHOR,
    TRUSTM_STATE_VERIFYING_MANIFEST,
    TRUSTM_STATE_APPLYING_FRAGMENTS,
    TRUSTM_STATE_PROTECTED_UPDATE_SUCCESS,
    TRUSTM_STATE_PROTECTED_UPDATE_FAILED} trustm_state_t;
```

## Constants

| Name | Value |
|---|---|
| `TESAIOT_HSM_API_H` | `#include` |
