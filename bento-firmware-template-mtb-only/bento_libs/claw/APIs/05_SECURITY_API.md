# 05 -- Security API

BENTO PSoC Edge E84 Firmware SDK -- OPTIGA Trust M, Hardware Security, and Cryptographic Operations

---

## Architecture Overview

The security subsystem provides hardware-backed cryptography via the OPTIGA Trust M V3 secure element (CC EAL6+ certified). The OPTIGA chip connects to the PSoC Edge E84 via I2C: on **SCB0** on the AI Kit and Eva Kit, and on **SCB5** (the 3V3 display bus, P17.0/P17.1, `OPTIGA_I2C_ON_3V3_BUS`) on the TESAIoT Dev Kit. SCB5 is shared with CM55's display, touch and CapSense drivers; every transfer on it, from either core, holds the cross-core lock in `ipc_scb5_lock.h` (IPC semaphore 16). The touch pause/resume IPC remains as a courtesy for long OPTIGA sessions, not as the arbitration.

```
MicroPython (CM33_NS)              OPTIGA Trust M V3
  |                                  |
  |-- optiga.init() ----IPC------->  | (pause CM55 touch)
  |-- optiga_util_* / crypt_* ---->  | (I2C @ SCB0, 100kHz)
  |-- optiga.deinit() --IPC------->  | (resume CM55 touch)
  |                                  |
  CM55 (Display/Touch)               |
  |-- IPC_CMD_TOUCH_PAUSE -------->  | (deferred reinit)
  |-- IPC_CMD_TOUCH_RESUME ------->  |
```

**Architecture decision:** CM33_NS calls OPTIGA library directly (no IPC proxy). This is simpler and avoids IPC pipe deadlock issues.

---

## 1. MicroPython `optiga` Module

**Source:** the mpy_secure implementation (in `libbento_mpy.a`)

### Lifecycle Functions

| Function | Signature | Returns | Description |
|----------|-----------|---------|-------------|
| `optiga.init()` | `init()` | `True` | Initialize OPTIGA hardware. Pauses CM55 touch polling, creates util/crypt instances, opens application. 500ms settle delay after open. |
| `optiga.deinit()` | `deinit()` | `None` | Close OPTIGA session. Destroys instances, resumes CM55 touch polling + reinit controller. |
| `optiga.is_ready()` | `is_ready()` | `bool` | Check if OPTIGA is initialized. |
| `optiga.setup(verbose=True)` | `setup(verbose=True)` | `dict` | Configure OPTIGA metadata for advanced crypto. Returns `{'ok': N, 'fail': N}`. Idempotent -- safe to call multiple times. |
| `optiga.is_configured()` | `is_configured()` | `bool` | Check if metadata is configured for AES/HMAC/counters. |
| `optiga.require_setup()` | `require_setup()` | `None` | Auto-run `setup()` if not configured. Call at start of examples. |

### Identity Functions

| Function | Signature | Returns | Description |
|----------|-----------|---------|-------------|
| `optiga.uid()` | `uid()` | `str` | Read 27-byte factory UID from OID 0xE0C2. Returns hex string (54 chars). |

### Data Read/Write Functions

| Function | Signature | Returns | Description |
|----------|-----------|---------|-------------|
| `optiga.read_data(oid)` | `read_data(oid)` | `bytes` | Read raw data from any OID. Max 1728 bytes (certificate size). |
| `optiga.write_data(oid, data)` | `write_data(oid, data)` | `None` | Write data to user-writable OIDs only. Uses erase-and-write mode. |
| `optiga.read_metadata(oid)` | `read_metadata(oid)` | `bytes` | Read OID metadata in raw TLV format. |
| `optiga.write_metadata(oid, metadata)` | `write_metadata(oid, metadata)` | `None` | Write OID metadata. Must start with 0x20 wrapper tag. 3-255 bytes. |

**Writable OID whitelist:**

| OID Range | Description | Max Data Size |
|-----------|-------------|---------------|
| `0xF1D0`--`0xF1D3` | Slots 0-3 -- writable, but they hold the device ID, licence key, HSM page PIN hash (0xF1D2) and MQTT password; see the slot table in 08_IPC_API.md | 140 bytes |
| `0xF1D5`--`0xF1DB` | Slots 5-11 -- writable, but 0xF1D5, 0xF1D6 and 0xF1D8--0xF1DB hold the saved WiFi networks and 0xF1D7 the platform API key; see the slot table in 08_IPC_API.md | 140 bytes |
| `0xF1E0`--`0xF1E1` | Large data slots | 1500 bytes |
| `0xE120`--`0xE123` | Monotonic counters | 8 bytes |

**Note:** OID `0xF1D4` is **reserved** for Protected Update shared secret and is blocked from write operations. OID `0xE0E8` (Trust Anchor) is also blocked.

### Cryptographic Functions

| Function | Signature | Returns | Description |
|----------|-----------|---------|-------------|
| `optiga.random(length)` | `random(length)` | `bytes` | Generate true random bytes from TRNG. Length: 8-256. |
| `optiga.sha256(data)` | `sha256(data)` | `bytes` (32) | Hardware SHA-256 hash. Input: `bytes` or buffer-compatible object. |
| `optiga.sign(digest, key_oid=0xE0F1)` | `sign(digest, key_oid=0xE0F1)` | `bytes` | ECDSA-P256 signature. Digest must be exactly 32 bytes. Returns DER-encoded signature (up to 80 bytes). |
| `optiga.gen_keypair(key_oid=0xE0F1)` | `gen_keypair(key_oid=0xE0F1)` | `bytes` | Generate ECC P-256 keypair. Private key stays in OPTIGA. Returns public key in DER format (up to 100 bytes). Key usage: sign + auth + key_agreement. |
| `optiga.ecdh(peer_pubkey, key_oid=0xE0F1)` | `ecdh(peer_pubkey, key_oid=0xE0F1)` | `bytes` (32) | ECDH key agreement. Accepts raw 65-byte (04\|\|X\|\|Y) or DER 68-byte format. Returns 32-byte shared secret. |

### AES Encryption Functions

Requires `optiga.setup()` to configure OID 0xE200 metadata first.

| Function | Signature | Returns | Description |
|----------|-----------|---------|-------------|
| `optiga.aes_generate_key(bits=256)` | `aes_generate_key(bits=256)` | `None` | Generate AES key in OID 0xE200. Key never leaves hardware. Bits: 128, 192, or 256. |
| `optiga.aes_encrypt(plaintext)` | `aes_encrypt(plaintext)` | `(ciphertext, iv)` | AES-CBC encrypt with auto-generated IV from TRNG. Plaintext must be multiple of 16 bytes (max 1500). |
| `optiga.aes_decrypt(ciphertext, iv)` | `aes_decrypt(ciphertext, iv)` | `bytes` | AES-CBC decrypt. IV must be 16 bytes. Ciphertext must be multiple of 16 bytes. |

### HMAC and Key Derivation Functions

Requires `optiga.setup()` to configure OID 0xF1D5 metadata (Type=PRESSEC) first.

> **Conflict on the Dev Kit.** 0xF1D5 is also saved WiFi network #1 of the on-screen WiFi store (see the slot table in 08_IPC_API.md). A network saved on the screen overwrites an HMAC secret kept there, and `optiga.setup()` changes the metadata of an object the WiFi store uses. Pass a different `secret_oid` (0xF1E0 or 0xF1E1) on a board whose WiFi page is in use.

| Function | Signature | Returns | Description |
|----------|-----------|---------|-------------|
| `optiga.hmac(data, secret_oid=0xF1D5)` | `hmac(data, secret_oid=0xF1D5)` | `bytes` (32) | HMAC-SHA256. Secret key must be pre-stored in OPTIGA data object. |
| `optiga.hkdf(secret_oid, salt, info, length=32)` | `hkdf(secret_oid, salt, info, length=32)` | `bytes` | HKDF-SHA256 key derivation (RFC 5869). Output length: 1-256 bytes. |

### Monotonic Counter Functions

Requires `optiga.setup()` to initialize counter metadata first.

| Function | Signature | Returns | Description |
|----------|-----------|---------|-------------|
| `optiga.counter_read(counter_id)` | `counter_read(counter_id)` | `int` | Read counter value. counter_id: 0-3 maps to OID 0xE120-0xE123. |
| `optiga.counter_increment(counter_id, increment=1)` | `counter_increment(counter_id, increment=1)` | `int` | Increment counter and return new value. NVM write limit: ~600,000 per counter lifetime. |

### OID Constants (Module Attributes)

```python
# Certificates
optiga.CERT_FACTORY   # 0xE0E0 - Factory certificate (read-only)
optiga.CERT_DEVICE    # 0xE0E1 - Device certificate
optiga.CERT_2         # 0xE0E2 - Certificate slot 2
optiga.CERT_3         # 0xE0E3 - Certificate slot 3

# Key Pairs (private key stays in OPTIGA)
optiga.KEY_DEVICE     # 0xE0F1 - Device key (default for sign/ecdh)
optiga.KEY_2          # 0xE0F2 - Application key
optiga.KEY_3          # 0xE0F3 - Spare key
# NOTE: KEY_FACTORY (0xE0F0) intentionally NOT exposed

# Other
optiga.UID_OID        # 0xE0C2 - Factory UID
optiga.AES_KEY        # 0xE200 - AES symmetric key
optiga.DATA_0..DATA_6 # 0xF1D0-0xF1D6 - not free: platform data and saved WiFi (see 08_IPC_API.md); DATA_4 reserved
optiga.DATA_LARGE_0   # 0xF1E0 - Large data slot (1500 bytes)
optiga.DATA_LARGE_1   # 0xF1E1 - Large data slot (1500 bytes)
optiga.COUNTER_0..3   # 0xE120-0xE123 - Monotonic counters
```

---

## 2. Security Policies and Access Control

### Factory Key Protection

OID `0xE0F0` (factory private key) is **blocked** in three operations:
- `optiga.sign()` -- raises `ValueError: factory key (0xE0F0) not available`
- `optiga.gen_keypair()` -- raises `ValueError` (gen_keypair would permanently overwrite)
- `optiga.ecdh()` -- raises `ValueError`

Valid key OIDs: `0xE0F1` through `0xE0F3`.

### Trust Anchor Protection

OID `0xE0E8` (trust anchor) is blocked from write operations: the firmware refuses writes to protected OIDs.

### OID Lifecycle States

| State | Value | Behavior |
|-------|-------|----------|
| Creation | 0x01 | Metadata can be modified. Data can be written. |
| Operational | 0x07 | Metadata is locked. Counter can only increment. |

The `optiga.setup()` function transitions OIDs from Creation to Operational after configuring their metadata. This is **irreversible** -- once in Operational state, metadata cannot be changed.

### Setup Configuration Details

`optiga.setup()` configures these OIDs (idempotent -- skips OIDs already in Operational state):

| OID | Configuration | Purpose |
|-----|--------------|---------|
| 0xE200 (AES key) | Change=ALWAYS, Execute=ALWAYS | Allow AES key generation and use |
| 0xF1D5 (HMAC secret) | Change=ALWAYS, Read=ALWAYS, Execute=ALWAYS, Type=PRESSEC | Allow HMAC secret storage and use |
| 0xE120-0xE123 (Counters) | Change=ALWAYS, Read=ALWAYS, Execute=ALWAYS, threshold=600000, LCS=Operational | Initialize counters with NVM endurance threshold |

---

## 3. I2C Bus Sharing and Touch Control

OPTIGA and the capacitive touch controller share SCB0 (I2C). The bus operates at **100kHz** (BSP clock divider=31). Operating at 400kHz (divider=9) breaks CM55 display/touch.

### Touch Pause/Resume IPC Mechanism

| Command | Code | Direction | Description |
|---------|------|-----------|-------------|
| `IPC_CMD_TOUCH_PAUSE` | `0xD6` | CM33 --> CM55 | Pause touch I2C polling. CM55 stops accessing SCB0. |
| `IPC_CMD_TOUCH_RESUME` | `0xD7` | CM33 --> CM55 | Resume touch polling + reinit controller (`lv_port_indev_request_reinit()`). |

**Flow:**
1. `optiga.init()` sends `IPC_CMD_TOUCH_PAUSE` then waits 50ms for any in-progress touch transaction
2. All OPTIGA operations proceed with exclusive SCB0 access
3. `optiga.deinit()` sends `IPC_CMD_TOUCH_RESUME` which triggers deferred touch controller reinit in the GFX task

The IPC send uses 50 retries with 1ms delay between attempts. The deferred reinit pattern (`lv_port_indev_request_reinit()`) is ISR-safe -- it sets a volatile flag that the GFX task checks on its next iteration.

---

## 4. HSM IPC Commands (CM55 LVGL UI)

These IPC commands are used by the HSM page on CM55 to perform OPTIGA operations via CM33_NS. They are not used by MicroPython code.

| Command | Code | Direction | Description |
|---------|------|-----------|-------------|
| `IPC_CMD_HSM_REQUEST` | `0xB5` | CM55 --> CM33 | Read chip data (UID, LCS, certs, counters) |
| `IPC_CMD_HSM_BENCHMARK` | `0xB6` | CM55 --> CM33 | Run crypto benchmarks (ECC, SHA, RNG) |
| `IPC_CMD_HSM_READ_CERT` | `0xB7` | CM55 --> CM33 | Read + parse certificate DER |
| `IPC_CMD_HSM_PIN_CHECK` | `0xB8` | CM55 --> CM33 | Check if PIN exists in DATA_3 |
| `IPC_CMD_HSM_PIN_SET` | `0xB9` | CM55 --> CM33 | Write SHA-256(digits) to DATA_3 |
| `IPC_CMD_HSM_PIN_VERIFY` | `0xBA` | CM55 --> CM33 | Verify PIN against stored hash |
| `IPC_CMD_HSM_HEALTH` | `0xBB` | CM55 --> CM33 | Run 8 self-tests |
| `IPC_CMD_HSM_PIN_RESET` | `0xBC` | CM55 --> CM33 | Erase PIN (requires old PIN verify) |

CM33 callback client ID for HSM requests: `CM33_IPC_HSM_CLIENT_ID = 2`.

---

## 5. TESAIoT OPTIGA C API

**Headers:**
- `kit-pse84-ai/tesaiot/include/tesaiot_optiga_core.h` -- serialised access to the chip
- `kit-pse84-ai/tesaiot/include/tesaiot_optiga.h` -- certificate enrolment and data-object helpers

Chip access and enrolment are provided by `libbento_hsm.a`; the data-object helpers are defined in `optiga_trust_helpers.c`, which ships as source. TLS, provisioning and the MicroPython `optiga` module all use the same chip, so everything that needs it goes through `tesaiot_optiga_core.h`. Each data-object helper borrows the chip that way, waits for the operation to finish, and returns the OPTIGA library status.

### tesaiot_optiga_core.h Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `optiga_manager_init` | `bool optiga_manager_init(callback_handler_t on_done, void *on_done_ctx)` | Start the OPTIGA stack; call once during start-up. Returns false when the chip could not be brought up. |
| `optiga_manager_acquire` | `optiga_util_t *optiga_manager_acquire(void)` | Borrow the shared `optiga_util` instance for exclusive use (NULL before `optiga_manager_init()`). |
| `optiga_manager_release` | `void optiga_manager_release(void)` | Hand back the borrowed instance; forgetting to blocks every other user of the chip. |
| `optiga_manager_lock` | `bool optiga_manager_lock(void)` | Exclusive access without borrowing the shared instance. Returns false if access could not be obtained in time. |
| `optiga_manager_unlock` | `void optiga_manager_unlock(void)` | End `optiga_manager_lock()`. |
| `optiga_chip_enter` | `bool optiga_chip_enter(void)` | Open a chip session (may nest). |
| `optiga_chip_exit` | `void optiga_chip_exit(void)` | Close a chip session. |

### tesaiot_optiga.h Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `publish_csr` | `int publish_csr(uint8_t *req, size_t req_len, uint16_t cert_oid, uint16_t anchor_oid, uint32_t version)` | Send a certificate signing request, built by the caller, to the platform for signing. |
| `tesaiot_publish_protected_update` | `int tesaiot_publish_protected_update(const char *target, const char *anchor, uint32_t version, bool new_key)` | Ask the platform for a Protected Update of object `target` (hex text, e.g. `"E0E1"`), authorised by trust anchor `anchor`. |
| `trustm_requested_target_oid` | `uint16_t trustm_requested_target_oid(void)` | Target object id of the request in flight. |
| `trustm_requested_anchor_oid` | `uint16_t trustm_requested_anchor_oid(void)` | Trust-anchor object id of the request in flight. |
| `trustm_current_correlation_id` | `const char *trustm_current_correlation_id(void)` | Correlation id pairing the platform's reply with the request in flight; NULL when there is none. |
| `trustm_update_state` | `void trustm_update_state(trustm_state_t stage, const char *code, const char *text)` | Record a new stage of the exchange. |
| `trustm_reset_state` | `void trustm_reset_state(void)` | Return to `TRUSTM_STATE_IDLE`. |
| `tesaiot_read_lcso` | `optiga_lib_status_t tesaiot_read_lcso(uint8_t *state)` | Fetch the chip's life-cycle state byte. |
| `tesaiot_read_data` | `optiga_lib_status_t tesaiot_read_data(uint16_t oid, uint8_t *buf, uint16_t *len)` | Fetch the content of object `oid`; `*len` is the buffer size in and the data size out. |
| `tesaiot_write_data` | `optiga_lib_status_t tesaiot_write_data(uint16_t oid, const uint8_t *buf, uint16_t len)` | Store `len` bytes in object `oid`. |
| `tesaiot_read_metadata` | `optiga_lib_status_t tesaiot_read_metadata(uint16_t oid, uint8_t *meta, uint16_t *len)` | Fetch the metadata of object `oid` (same `*len` rule). |
| `tesaiot_write_metadata` | `optiga_lib_status_t tesaiot_write_metadata(uint16_t oid, uint8_t *meta, uint16_t len)` | Replace the metadata of object `oid`. |
| `tesaiot_write_trust_anchor` | `optiga_lib_status_t tesaiot_write_trust_anchor(uint16_t oid, const uint8_t *buf, uint16_t len)` | Store a trust anchor in object `oid`, replacing whatever it held. |
| `tesaiot_erase_data` | `optiga_lib_status_t tesaiot_erase_data(uint16_t obj_oid)` | Empty object `obj_oid`. |
| `tesaiot_verify_manifest_with_trustanchor` | `optiga_lib_status_t tesaiot_verify_manifest_with_trustanchor(const uint8_t *manifest, uint16_t manifest_len, uint16_t anchor_oid)` | Check the signature of a Protected Update manifest against the trust anchor in `anchor_oid`. |

---

## 6. Board Support

| Feature | AI Kit | Eva Kit | Game Console |
|---------|--------|---------|--------------|
| OPTIGA Trust M V3 | Yes | Yes | No |
| I2C bus (SCB0) | Shared w/ touch | Shared w/ touch | N/A |
| Touch pause/resume IPC | Yes | Yes | N/A |
| HSM LVGL page | Yes | Yes | No |
| MicroPython `optiga` module | Yes | Yes | No |

---

## 7. Usage Examples

### Basic OPTIGA Operations

```python
import optiga

optiga.init()

# Read device UID
uid = optiga.uid()
print("Device UID:", uid)

# Generate random bytes
rand = optiga.random(32)
print("Random:", rand.hex())

# SHA-256 hash
digest = optiga.sha256(b"Hello BENTO!")
print("SHA-256:", digest.hex())

optiga.deinit()
```

### Digital Signature

```python
import optiga

optiga.init()

# Generate a keypair (private key stays in OPTIGA)
pubkey = optiga.gen_keypair(key_oid=0xE0F1)
print("Public key:", pubkey.hex())

# Sign a digest
digest = optiga.sha256(b"message to sign")
signature = optiga.sign(digest, key_oid=0xE0F1)
print("Signature:", signature.hex())

optiga.deinit()
```

### AES Encryption

```python
import optiga

optiga.init()
optiga.require_setup()  # Configure metadata if needed

# Generate AES-256 key (stored in OPTIGA, never exported)
optiga.aes_generate_key(256)

# Encrypt data (must be multiple of 16 bytes)
plaintext = b"BENTO SecureData" * 4  # 64 bytes
ciphertext, iv = optiga.aes_encrypt(plaintext)

# Decrypt
decrypted = optiga.aes_decrypt(ciphertext, iv)
assert decrypted == plaintext

optiga.deinit()
```

### Monotonic Counter

```python
import optiga

optiga.init()
optiga.require_setup()

# Read counter
val = optiga.counter_read(0)
print("Counter 0:", val)

# Increment
new_val = optiga.counter_increment(0)
print("After increment:", new_val)

optiga.deinit()
```

---

## Source Files

| File | Path |
|------|------|
| MicroPython optiga module | the mpy_secure implementation (in `libbento_mpy.a`) |
| TESAIoT chip access | `kit-pse84-ai/tesaiot/include/tesaiot_optiga_core.h` |
| TESAIoT OPTIGA integration | `kit-pse84-ai/tesaiot/include/tesaiot_optiga.h` |
| IPC command definitions | `common/shared/include/ipc_communication.h` |

