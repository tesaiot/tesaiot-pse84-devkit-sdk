# 05 -- Security API

BENTO PSoC Edge E84 Firmware SDK -- OPTIGA Trust M, Hardware Security, and Cryptographic Operations

---

## Architecture Overview

The security subsystem provides hardware-backed cryptography via the OPTIGA Trust M V3 secure element (CC EAL6+ certified). The OPTIGA chip connects to the PSoC Edge E84 via I2C on **SCB0**, which is shared with the CM55 touch controller. A touch pause/resume IPC mechanism ensures exclusive bus access during OPTIGA operations.

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

**Architecture decision (ADR-1):** CM33_NS calls OPTIGA library directly (no IPC proxy). This is simpler and avoids IPC pipe deadlock issues.

---

## 1. MicroPython `optiga` Module

**Source:** `common/mpy/modoptiga.c`

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
| `0xF1D0`--`0xF1D3` | User data slots 0-3 | 140 bytes |
| `0xF1D5`--`0xF1DB` | User data slots 5-11 | 140 bytes |
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
optiga.DATA_0..DATA_6 # 0xF1D0-0xF1D6 - User data slots (except DATA_4)
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

OID `0xE0E8` (trust anchor) is blocked from write operations in `is_writable_oid()`.

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

## 5. TESAIoT Crypto Library (C API)

**Headers:**
- `kit-pse84-ai/tesaiot/include/tesaiot_crypto.h` -- Developer-facing crypto utilities
- `kit-pse84-ai/tesaiot/include/tesaiot_optiga.h` -- OPTIGA integration layer

These are higher-level C wrappers around the OPTIGA library. All functions are license-gated and thread-safe via an optiga_manager mutex.

### tesaiot_crypto.h Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `tesaiot_random_generate` | `int tesaiot_random_generate(uint8_t *buffer, uint16_t length)` | TRNG random bytes (8-256). CC EAL6+ certified. |
| `tesaiot_secure_store_write` | `int tesaiot_secure_store_write(uint8_t slot, const uint8_t *data, uint16_t length)` | Write to OPTIGA data slot (0-13, except 4). Slot 0-11: max 140B, slot 12-13: max 1500B. |
| `tesaiot_secure_store_read` | `int tesaiot_secure_store_read(uint8_t slot, uint8_t *data, uint16_t *length)` | Read from OPTIGA data slot. |
| `tesaiot_aes_generate_key` | `int tesaiot_aes_generate_key(uint16_t key_bits)` | Generate AES key in OID 0xE200 (128/192/256 bits). |
| `tesaiot_aes_encrypt` | `int tesaiot_aes_encrypt(const uint8_t *plaintext, uint16_t plain_len, const uint8_t *iv, uint8_t *iv_out, uint8_t *ciphertext, uint16_t *cipher_len)` | AES-CBC encrypt. NULL iv = auto-generate from TRNG. |
| `tesaiot_aes_decrypt` | `int tesaiot_aes_decrypt(const uint8_t *ciphertext, uint16_t cipher_len, const uint8_t *iv, uint8_t *plaintext, uint16_t *plain_len)` | AES-CBC decrypt. |
| `tesaiot_hmac_sha256` | `int tesaiot_hmac_sha256(uint8_t secret_slot, const uint8_t *data, uint16_t data_len, uint8_t *mac, uint16_t *mac_len)` | HMAC-SHA256 with hardware-stored key. |
| `tesaiot_ecdh_shared_secret` | `int tesaiot_ecdh_shared_secret(uint16_t key_oid, const uint8_t *peer_pubkey, uint16_t peer_pubkey_len, uint8_t *shared_secret, uint16_t *secret_len)` | ECDH key agreement (P-256). |
| `tesaiot_hkdf_derive` | `int tesaiot_hkdf_derive(uint16_t secret_oid, const uint8_t *salt, uint16_t salt_len, const uint8_t *info, uint16_t info_len, uint8_t *derived_key, uint16_t key_len)` | HKDF-SHA256 (RFC 5869). |
| `tesaiot_optiga_hash` | `int tesaiot_optiga_hash(const uint8_t *data, uint16_t data_len, uint8_t *hash, uint16_t *hash_len)` | Hardware SHA-256. |
| `tesaiot_sign_data` | `int tesaiot_sign_data(uint16_t key_oid, const uint8_t *data, uint16_t data_len, uint8_t *signature, uint16_t *sig_len)` | SHA-256 hash + ECDSA sign (composite). |
| `tesaiot_counter_read` | `int tesaiot_counter_read(uint8_t counter_id, uint32_t *value)` | Read monotonic counter (0-3 maps to 0xE120-0xE123). |
| `tesaiot_counter_increment` | `int tesaiot_counter_increment(uint8_t counter_id, uint32_t step)` | Increment counter. NVM limit: ~600,000 per lifetime. |
| `tesaiot_health_check` | `int tesaiot_health_check(tesaiot_health_report_t *report)` | Comprehensive device health check (OPTIGA, certs, license, MQTT, NTP). |

### tesaiot_optiga.h Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `tesaiot_optiga_generate_keypair` | `bool tesaiot_optiga_generate_keypair(uint16_t key_oid, uint8_t *public_key_der, uint16_t *pubkey_len)` | Generate ECC P-256 keypair. |
| `tesaiot_optiga_generate_csr` | `bool tesaiot_optiga_generate_csr(uint16_t key_oid, const uint8_t *public_key_der, uint16_t pubkey_len, const char *subject, char *csr_pem, size_t csr_pem_len)` | Generate CSR signed by OPTIGA. |
| `tesaiot_check_certificate_validity` | `bool tesaiot_check_certificate_validity(uint16_t oid, tesaiot_cert_validation_result_t *result)` | Validate certificate in OID. |
| `tesaiot_get_cert_days_until_expiry` | `uint32_t tesaiot_get_cert_days_until_expiry(uint16_t oid)` | Days until certificate expiry. |
| `tesaiot_read_factory_uid` | `bool tesaiot_read_factory_uid(char *uid_hex, size_t uid_hex_len)` | Read factory UID as hex string. |
| `tesaiot_read_factory_certificate` | `bool tesaiot_read_factory_certificate(char *cert_pem, uint16_t *cert_pem_length)` | Read factory certificate (PEM). |

### Error Codes

| Code | Name | Description |
|------|------|-------------|
| 0 | `TESAIOT_OK` | Success |
| -1 | `TESAIOT_ERROR_NOT_LICENSED` | License not valid |
| -2 | `TESAIOT_ERROR_INVALID_PARAM` | Invalid parameter |
| -3 | `TESAIOT_ERROR_NOT_INITIALIZED` | OPTIGA not initialized |
| -4 | `TESAIOT_ERROR_OPTIGA` | Hardware error |
| -5 | `TESAIOT_ERROR_BUFFER_TOO_SMALL` | Output buffer insufficient |
| -6 | `TESAIOT_ERROR_RESERVED_OID` | Attempted access to reserved OID |

---

## 6. Board Support

| Feature | AI Kit | Eva Kit | Game Console |
|---------|--------|---------|--------------|
| OPTIGA Trust M V3 | Yes | Yes | No |
| I2C bus (SCB0) | Shared w/ touch | Shared w/ touch | N/A |
| Touch pause/resume IPC | Yes | Yes | N/A |
| HSM LVGL page | Yes | Yes | No |
| MicroPython `optiga` module | Yes | Yes | No |
| TESAIoT crypto C API | Yes | Yes | No |

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
| MicroPython optiga module | `common/mpy/modoptiga.c` |
| TESAIoT crypto API | `kit-pse84-ai/tesaiot/include/tesaiot_crypto.h` |
| TESAIoT OPTIGA integration | `kit-pse84-ai/tesaiot/include/tesaiot_optiga.h` |
| IPC command definitions | `common/shared/include/ipc_communication.h` |
