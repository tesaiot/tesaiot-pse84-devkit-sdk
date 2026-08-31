# TESAIoT MQTT Subsystem

Secure MQTT client for PSoC Edge E84 — connects to `mqtt.tesaiot.com:8884` via TLS 1.2.

## Architecture

```
CM55 (LVGL UI)                         CM33_NS (FreeRTOS)
+-----------------+                    +---------------------------+
| TESAIoT Page    |  IPC Pipe          | mqtt_task.c               |
| [Connect] btn --+-------------------->  mqtt_request_start()     |
|                 |                    |    |                       |
| Status labels <-+--- shared memory --+  mqtt_client_config.c     |
+-----------------+                    |    -> TLS credentials      |
                                       |    -> broker/port/auth     |
                                       |                           |
                                       |  cy_mqtt_connect()        |
                                       |    -> mbedTLS TLS 1.2     |
                                       |    -> serverTLS (mode 2)  |
                                       |                           |
                                       |  subscriber_task.c        |
                                       |    -> commands/# topic    |
                                       |                           |
                                       |  publisher_task.c         |
                                       |    -> telemetry queue     |
                                       +---------------------------+
```

## Files

| File | Purpose |
|------|---------|
| `mqtt_task.c/h` | Main MQTT lifecycle: lazy init, connect, disconnect, reconnect |
| `mqtt_client_config.c/h` | TLS credential setup per auth mode (serverTLS, mTLS) |
| `subscriber_task.c/h` | Subscribes to `device/{id}/commands/#`, handles callbacks |
| `publisher_task.c/h` | Dequeues telemetry JSON, publishes to broker |
| `tesaiot_mqtt.c/h` | MicroPython bridge: `tesaiot.connect()`, status to shared memory |
| `tesaiot_root_ca.h` | TESAIoT CA chain (Intermediate + Root CA), valid 2025-2035 |
| `mqtt_mtls_setup.c/h` | Phase G: OPTIGA Trust M mTLS orchestrator (not yet active) |
| `core_mqtt_config.h` | coreMQTT library config overrides |
| `psa_its_ram_stubs.c` | PSA ITS RAM stubs for mTLS (Phase G) |
| `cy_tls_client_cert.h` | Client cert injection API for mTLS |

## Memory Budget (Critical Knowledge)

### The Problem

PSoC Edge E84 CM33_NS has **256KB SRAM** (`m33_data` region) shared between:
- MicroPython GC heap (static BSS array)
- FreeRTOS task stacks (allocated from C heap at task creation)
- WiFi WHD driver buffers (~40-60KB)
- TLS handshake buffers
- MQTT network buffers
- Application BSS

Every byte of GC heap is one less byte of C heap. TLS and WiFi need C heap.
MicroPython file I/O needs GC heap. Finding the balance is critical.

### Proven Configuration (2026-03-26)

| Parameter | Eva Kit | AI Kit | Why |
|-----------|---------|--------|-----|
| GC Heap | **64KB** | **48KB** | AI Kit has larger BSS (more sensor modules) |
| TLS IN buffer | **6144** | **6144** | Minimum for server cert chain (~4KB PEM) |
| TLS OUT buffer | **2048** | **2048** | Minimum for TLS 1.2 client records |
| MQTT Client stack | **4096 words (16KB)** | same | TLS handshake peak ~12KB stack |
| Publisher stack | **512 words (2KB)** | same | Just dequeue + cy_mqtt_publish() |
| Subscriber stack | **768 words (3KB)** | same | Subscribe + callback dispatch |
| MQTT event thread | **3072 bytes** | same | Defined via `CY_MQTT_EVENT_THREAD_STACK_SIZE` |
| MQTT network buffer | **3200 bytes** | same | Static `mqtt_network_buf[]` in mqtt_task.c |

### Memory Math

```
Eva Kit (64KB GC):
  m33_data           = 256KB
  BSS (incl 64KB GC) ~ 200KB
  Stack (MSP)        =   4KB
  C heap available   ~  52KB
  WiFi WHD runtime   ~ -40KB (variable)
  Remaining for MQTT ~  12KB (tight but works)

AI Kit (48KB GC):
  m33_data           = 252KB (reduced for shared mem fix)
  BSS (incl 48KB GC) ~ 236KB
  Stack (MSP)        =   4KB
  C heap available   ~  12KB
  WiFi WHD runtime   ~ -40KB (dynamic, released after init)
  Note: WiFi buffers overlap with MQTT in time, not simultaneously
```

### Task Stack Sizing Lessons

| Stack Size | Result | Notes |
|-----------|--------|-------|
| 5120 words (20KB) | Works but wastes 4KB | Original, too large |
| **4096 words (16KB)** | Works | Current — proven minimum for TLS 1.2 |
| 3072 words (12KB) | **HardFault** | Stack overflow during mbedTLS RSA verify |
| 2048 words (8KB) | **HardFault** | Immediate crash at handshake |

The TLS 1.2 handshake calls `mbedtls_pk_verify()` which uses ~8-10KB stack for
RSA-2048 signature verification. Combined with FreeRTOS overhead, **16KB is the
proven minimum** for the MQTT client task.

### Publisher Task Creation Failure

If `[MQTT] Publisher task creation failed` appears in logs:
- **Cause**: Not enough C heap for xTaskCreate (needs stack + TCB)
- **Fix**: Reduce task stacks or GC heap
- **History**: Publisher was 1024 words (4KB). Reduced to 512 words (2KB).
  Publisher only calls `cy_mqtt_publish()` from a queue — doesn't need deep stack.

## TLS Configuration

### mbedtls_user_config.h — Mandatory Settings

Located at `proj_cm33_ns/configs/mbedtls_user_config.h` in each project.
**Both AI Kit and Eva Kit MUST use identical TLS settings.**

```c
/* ============================================================
 * PROTOCOL — TLS 1.2 (current), TLS 1.3 (future)
 * ============================================================
 * Current: TLS 1.2 only — proven working with BentoClaw stack.
 * Future:  TLS 1.3 is used by the reference pse84_tesaiot_client
 *          and may be achievable. Requires PSA crypto + testing
 *          that WiFi WPA2 still works with CY_CRYPTO_HAL_DISABLE.
 *          WHD chip (CYW55500) has its own embedded crypto, so
 *          disabling the host-side HAL may not break WiFi. Untested.
 */
#define MBEDTLS_SSL_PROTO_TLS1_2
#undef  MBEDTLS_SSL_PROTO_TLS1_3   /* Enable when PSA coexistence is tested */

/* ============================================================
 * PSA CRYPTO — currently disabled for serverTLS
 * ============================================================
 * MBEDTLS_USE_PSA_CRYPTO routes ALL crypto through PSA layer.
 * Required for mTLS (OPTIGA mbedtls_pk_setup_opaque).
 * Currently causes serverTLS regression (see "Why PSA Breaks serverTLS").
 * Phase G target: enable PSA + CY_CRYPTO_HAL_DISABLE, test WiFi still works.
 */
#undef  MBEDTLS_USE_PSA_CRYPTO
#undef  MBEDTLS_PSA_CRYPTO_CONFIG

/* ============================================================
 * CIPHER SUITES — BOTH ECDHE-ECDSA and ECDHE-RSA required
 * ============================================================ */
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED  /* HTTPS (api.tesaiot.com) */
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED    /* MQTT (mqtt.tesaiot.com) */

/* ============================================================
 * ECC CURVES — SECP256R1 + SECP384R1
 * ============================================================ */
/* SECP256R1: platform default (OPTIGA device cert, MQTT server cert) */
/* SECP384R1: must be explicitly enabled (Let's Encrypt E7 CA for HTTPS) */
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED

/* ============================================================
 * TLS BUFFER REDUCTION — saves 24KB C heap
 * ============================================================ */
#define MBEDTLS_SSL_IN_CONTENT_LEN   6144   /* min for server cert chain */
#define MBEDTLS_SSL_OUT_CONTENT_LEN  2048   /* min for TLS 1.2 records   */
```

### Why ECDHE-RSA Is Required (Incident 2026-03-27)

EMQX broker at `mqtt.tesaiot.com` uses RSA server certificates.
When only `ECDHE_ECDSA` was enabled, server returned `no_suitable_ciphers`:

```
Symptom:  TLS handshake fail → "no_suitable_ciphers" in EMQX log
Cause:    Device only offered ECDHE-ECDSA suites
          Server cert is RSA → needs ECDHE-RSA key exchange
Fix:      #define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
Depends:  MBEDTLS_RSA_C, MBEDTLS_PKCS1_V15 (both in platform default)
```

**Note**: EMQX supports dual cert (ECDSA + RSA). Client may negotiate either.
Keep both cipher families enabled for maximum compatibility.

### Why MBEDTLS_USE_PSA_CRYPTO Breaks serverTLS

Tested 2026-03-27 with systematic bisection — PSA crypto causes TLS failure:

```
WHAT HAPPENS:
  1. MBEDTLS_USE_PSA_CRYPTO → mbedTLS routes ALL crypto through PSA layer
  2. PSA layer dispatches to "accelerator driver" or "software fallback"
  3. Infineon platform defines IFX_PSA_MXCRYPTO_PRESENT by default
  4. Without CY_CRYPTO_HAL_DISABLE → PSA uses HW crypto HAL
  5. With CY_CRYPTO_HAL_DISABLE → PSA tries HW but HAL is gone → TLS crash

CATCH-22:
  - USE_PSA_CRYPTO on + CY_CRYPTO_HAL_DISABLE off → cipher negotiation fails
  - USE_PSA_CRYPTO on + CY_CRYPTO_HAL_DISABLE on → "Unknown CA" (cert verify fails)
  - USE_PSA_CRYPTO off + CY_CRYPTO_HAL_DISABLE off → WORKS ✅

ROOT CAUSE:
  BentoClaw uses Infineon HW crypto HAL for WiFi WPA2 + TLS.
  PSA crypto conflicts with existing HW crypto HAL code paths.
  The reference project (pse84_tesaiot_client) works with PSA because it was
  built from scratch without the BentoClaw WiFi/sensor stack.

CURRENT SAFE RULE:
  serverTLS → MBEDTLS_USE_PSA_CRYPTO = #undef
  mTLS + OPTIGA → needs USE_PSA_CRYPTO for mbedtls_pk_setup_opaque()

PHASE G STRATEGY:
  The reference project (pse84_tesaiot_client) proves that PSA +
  CY_CRYPTO_HAL_DISABLE + TLS 1.3 works. The question is whether
  BentoClaw's WiFi stack (WHD) still works when the host-side HW
  crypto HAL is disabled. WHD has its own firmware crypto on CYW55500,
  so it MIGHT work. This is the key experiment for Phase G:
    1. Enable USE_PSA_CRYPTO + CY_CRYPTO_HAL_DISABLE
    2. Remove IFX_PSA_MXCRYPTO_PRESENT (reference doesn't have it)
    3. Test WiFi scan() + connect() → if WiFi works, PSA is safe
    4. Then enable mTLS + serverTLS on the same firmware
```

### PSA-Related Defines — Impact on serverTLS (Tested 2026-03-27)

These defines MUST stay disabled for serverTLS to work, even when
`MBEDTLS_USE_PSA_CRYPTO` is `#undef`:

| Define | serverTLS setting | Why |
|--------|-------------------|-----|
| `MBEDTLS_USE_PSA_CRYPTO` | **#undef** | Routes crypto through PSA → breaks HW crypto path |
| `CY_CRYPTO_HAL_DISABLE` (Makefile) | **absent** | Removes HW crypto HAL → cert verify fails |
| `IFX_PSA_MXCRYPTO_PRESENT` | **#if 0** | Tells PSA to use HW accel that may be misconfigured |
| `PSA_CRYPTO_ACCELERATOR_DRIVER_PRESENT` | **#if 0** | Same as above |
| `MBEDTLS_PSA_CRYPTO_SE_C` | **#if 0** | Pulls in SE driver code that changes crypto dispatch |
| `MBEDTLS_PSA_CRYPTO_STORAGE_C` | **#undef** | Pulls in ITS storage code (unnecessary for serverTLS) |
| `MBEDTLS_PSA_CRYPTO_DRIVERS` | **#if 0** | Enables driver framework that conflicts with legacy |
| `MBEDTLS_DEPRECATED_REMOVED` | **#define** | SE_C needs DEPRECATED_WARNING instead, but keep REMOVED when SE_C is off |

**CRITICAL**: Do NOT enable these defines "just to compile" OPTIGA source files.
Adding them changes mbedTLS internal code paths globally, not just for mTLS.

### optiga_psa_register() / psa_crypto_init() at Boot

```
TESTED: Calling optiga_psa_register() + psa_crypto_init() in main.c
        BEFORE any TLS operations — even with USE_PSA_CRYPTO off.

RESULT: serverTLS still fails (Unknown CA)

REASON: psa_crypto_init() registers internal PSA dispatch tables.
        Even without USE_PSA_CRYPTO, some mbedTLS code paths check
        if PSA is initialized and take a different branch.
        optiga_psa_register() registers a SE driver that may intercept
        crypto operations meant for the software/hardware path.

SAFE RULE: Do NOT call optiga_psa_register() or psa_crypto_init()
           unless MBEDTLS_USE_PSA_CRYPTO is #define'd AND you are
           ready to use PSA for ALL TLS operations.
```

### TLS Buffer Sizing

```
MBEDTLS_SSL_IN_CONTENT_LEN (input buffer):
  - Must hold the largest TLS record received
  - Server cert chain (TESAIoT): ~3.9KB PEM
  - 6144 bytes = minimum safe value
  - If handshake fails with MBEDTLS_ERR_SSL_BUFFER_TOO_SMALL → increase this

MBEDTLS_SSL_OUT_CONTENT_LEN (output buffer):
  - Must hold the largest TLS record sent
  - Client Hello + key exchange: ~1.5KB typical
  - 2048 bytes = minimum safe value
  - Cannot go below 2048 without fragmentation issues

Savings: (16384 - 6144) + (16384 - 2048) = 24,576 bytes freed
```

### Root CA Certificate (Trust Store)

The device pins to the **Intermediate CA** (RSA-2048), not the Root CA (RSA-4096):

```
File:         tesaiot_root_ca.h
Subject:      CN=TESAIoT Intermediate CA
Issuer:       CN=TESAIoT Root CA (C=TH, O=TESA IoT Platform)
Key:          RSA-2048
Valid:        2025-09-06 to 2030-09-05
Fingerprint:  E3:FF:50:11:70:37:55:B6:97:22:7A:17:94:58:37:B7:
              B4:36:16:A0:60:D1:4B:37:99:5B:62:C2:4E:0D:7E:98
Serial:       4323DDA32C0CBE1CA700B9A83A46954634717603

PKI chain:
  Root CA (RSA-4096) → Intermediate CA (RSA-2048) → server cert
  Device trusts: Intermediate CA (our trust anchor)
  Server sends:  [server cert, Intermediate CA] in TLS handshake

Why Intermediate, not Root:
  RSA-4096 signature verification requires ~30KB stack + heap.
  PSoC Edge CM33_NS cannot provide this during TLS handshake.
  Intermediate CA (RSA-2048) requires only ~12KB → fits safely.
```

**WARNING**: If the server cert is re-issued by a different Intermediate CA,
this embedded certificate MUST be updated. The `Unknown CA` error means
the issuer of the server cert doesn't match our trust anchor.

## WiFi Auto-Connect

### How It Works

```
Boot Sequence:
1. FreeRTOS starts → sensor_auto_task creates WiFi worker
2. WiFi worker calls wifi_boot_auto_connect()
3. Waits up to 10s for g_boot_wifi_creds_count != 0
4. Meanwhile: MicroPython task starts → VFS mount → lfs_wifi_creds_read()
5. lfs_wifi_creds_read() uses Python open()+read() internally
6. Credentials loaded → g_boot_wifi_creds_count set
7. WiFi worker sees count > 0 → SDIO init → cy_wcm_connect_ap()
```

### Critical: gc_collect() Before Credential Read

`lfs_wifi_creds_read()` calls Python `open()` which allocates a LFS read
buffer (~16KB) on the **MicroPython GC heap**. After VFS mount + config init,
GC may have only ~13KB free (at 48KB total) → `MemoryError`.

**Fix**: Call `gc_collect()` before `lfs_wifi_creds_init()` in `mpy_main.c`.
This frees temp objects from VFS mount, giving ~35KB free — enough for file I/O.

### Credential File Format

```
/.wifi_creds (612 bytes, binary):
  Magic:    4 bytes  0x49464957 ("WIFI")
  Version:  2 bytes  0x0001
  Count:    2 bytes  1-6
  Entries:  600 bytes (6 x 100 bytes each)
    SSID:     33 bytes (null-terminated)
    Password: 65 bytes (null-terminated)
    Security: 1 byte
    Flags:    1 byte (0x01 = auto-connect)
  Checksum: 4 bytes  CRC32 (or legacy XOR-32, auto-migrated)
```

## Auth Modes

| Mode | tls_mode | Port | Auth Method | Status |
|------|----------|------|-------------|--------|
| 0 | mTLS + OPTIGA | 8883 | Hardware client cert (OPTIGA Trust M) | Phase G (planned) |
| 1 | mTLS + Software | 8883 | Software client cert | Not implemented |
| 2 | **serverTLS** | **8884** | **API key as MQTT password** | **Working (2026-03-27)** |
| 3 | Plain | 1883 | Username/password only | Not implemented |

### serverTLS (Mode 2) — Proven Working Configuration

**Verified 2026-03-27** — full end-to-end connection confirmed by server team.

#### Connection Parameters

```
Broker:       mqtt.tesaiot.com
Port:         8884 (serverTLS listener)
TLS:          1.2 (ECDHE-RSA-AES256-GCM-SHA384 or ECDHE-ECDSA-AES256-GCM-SHA384)
SNI:          mqtt.tesaiot.com
ALPN:         NULL (not set)
Root CA:      TESAIoT Intermediate CA (RSA-2048, embedded in tesaiot_root_ca.h)
Client cert:  NULL (server-only TLS — no client certificate)
```

#### MQTT Auth

```
Client ID:    cfg.device_id     (UUID from TESAIoT Platform)
Username:     cfg.device_id     (same UUID)
Password:     cfg.mqtt_pass     (API key from TESAIoT Platform)
Clean Start:  true
Keep Alive:   60 seconds
QoS:          1
```

All values read from `.tesaiot_config` in LittleFS flash (NOT hardcoded).
Set via REPL: `tesaiot.config_set("device_id", "...")` or IPC from CM55 page.

#### Server-Side Auth Flow (EMQX)

```
1. TLS handshake → cipher negotiation → cert verify ✅
2. MQTT CONNECT → EMQX receives ClientId + Username + Password
3. Auth chain: built_in_database → ignore → HTTP webhook → allow
4. CONNACK: ReasonCode=0 (success)
5. SUBSCRIBE: device/{id}/commands/# → ACL allow
```

#### Subscribe Topics

```
device/{device_id}/commands/#    QoS 1    (receive commands from platform)
```

#### Publish Topics

```
device/{device_id}/telemetry     QoS 1    (sensor data: BMM350, env, etc.)
```

#### What Makes serverTLS Work (The 5 Essential Properties)

1. **ECDHE-RSA cipher enabled** — server uses RSA cert, ECDHE-RSA key exchange
2. **TLS 1.2 only** — TLS 1.3 pulls in PSA crypto which conflicts with HW crypto
3. **PSA crypto OFF** — `#undef MBEDTLS_USE_PSA_CRYPTO` in mbedtls_user_config.h
4. **HW crypto HAL ON** — NO `CY_CRYPTO_HAL_DISABLE` in Makefile
5. **Correct CA embedded** — TESAIoT Intermediate CA (RSA-2048) matching server cert issuer

Remove ANY of these 5 → TLS handshake fails.

## Troubleshooting

### Common Errors (Device-side)

| Error | Meaning | Fix |
|-------|---------|-----|
| `-0x2700` | Cert verify failed | Check `MBEDTLS_USE_PSA_CRYPTO` is `#undef` |
| `-0x6800` | SSL fetch_input timeout | Normal during handshake retries — retry works |
| `-0x7200` | SSL buffer too small | Increase `MBEDTLS_SSL_IN_CONTENT_LEN` |
| `0x082A000D` | TLS internal error | MQTT event thread stack too small (need 3072+) |
| `Publisher task creation failed` | C heap exhausted | Reduce task stacks or GC heap |
| `No saved credentials` | WiFi creds not loaded | Check gc_collect() before read, verify /.wifi_creds exists |
| `WiFi creds store not ready` | VFS mount failed | Check QSPI flash, MicroPython config |
| HardFault during connect | Stack overflow | MQTT client stack too small (need 4096+ words) |

### Server-side Errors (EMQX log)

| Error | Meaning | Fix |
|-------|---------|-----|
| `no_suitable_ciphers` | Client offered ciphers server doesn't support | Enable `ECDHE_RSA` in mbedtls_user_config.h |
| `No application protocol` | ALPN mismatch | We don't use ALPN — server should not require it |
| `Protocol Version` | TLS version mismatch | Ensure TLS 1.2 is supported on both sides |
| `Unknown CA` / `unknown_ca` | Device rejected server cert | Check embedded CA matches server cert issuer (see fingerprint above) |
| No CONNECT after TLS | Handshake failed before MQTT | Check cipher + CA + PSA settings (see 5 Essential Properties) |

### Systematic Debugging Checklist

When serverTLS fails, check these in order:

```
1. WiFi connected?
   LCD: "WiFi → Connected" ✅
   If not → wifi issue, not TLS

2. Port correct?
   serverTLS = 8884, mTLS = 8883
   Check: EMQX log shows connection on correct port

3. Cipher negotiation?
   EMQX log: "no_suitable_ciphers" → missing ECDHE_RSA or ECDHE_ECDSA
   Fix: Add #define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED

4. CA verification?
   EMQX log: "unknown_ca" → device doesn't trust server cert issuer
   Check: tesaiot_root_ca.h fingerprint matches server cert CA
   Check: MBEDTLS_USE_PSA_CRYPTO is #undef
   Check: CY_CRYPTO_HAL_DISABLE is NOT in Makefile

5. MQTT auth?
   EMQX log: "not_authorized" → wrong device_id or mqtt_pass
   Check: .tesaiot_config has correct credentials
   Verify: tesaiot.config() from REPL shows expected values
```

### Debug Logging

To enable TLS/MQTT debug output, add to project Makefile:
```makefile
DEFINES+=ENABLE_SECURE_SOCKETS_LOGS ENABLE_MQTT_LOGS
```

To enable mbedTLS verbose logging, in `mbedtls_user_config.h`:
```c
#define MBEDTLS_VERBOSE 1
#define MBEDTLS_DEBUG_C
```

**Warning**: Debug logs add significant overhead. Disable in production.

## AI Kit Linker Memory Map

The AI Kit has larger BSS than Eva Kit due to additional sensor modules.
The `m33_allocatable_shared` region (IPC shared memory) was increased from
4KB to 8KB to accommodate TESAIoT + BentoClaw shared data structures.

Modified files (all 3 must be consistent):
- `bsps/TARGET_KIT_PSE84_AI/config/GeneratedSource/cymem_gnu_CM33_0.ld`
- `bsps/TARGET_KIT_PSE84_AI/config/GeneratedSource/cymem_gnu_CM33_0_S.ld`
- `bsps/TARGET_KIT_PSE84_AI/config/GeneratedSource/cymem_gnu_CM55_0.ld`

```
Layout (AI Kit, modified):
  m33_data:                0x240BD000  252KB (was 256KB)
  m33s_allocatable_shared: 0x240FC000    4KB (shifted down)
  m33_allocatable_shared:  0x240FD000    8KB (was 4KB, doubled)
  m55_allocatable_shared:  0x240FF000    4KB (unchanged)
```

If new MicroPython modules add `.cy_sharedmem` data, check that the total
stays under 8KB. Use `arm-none-eabi-size --format=sysv proj_cm33_ns.elf`
or check the linker map for `.cy_sharedmem` section size.
