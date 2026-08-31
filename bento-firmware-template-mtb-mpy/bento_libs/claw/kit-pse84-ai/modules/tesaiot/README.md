# OPTIGA Trust M + TESAIoT platform — how this actually works

> **How to actually do this, step by step:**
> [`docs/18-device-provisioning-csr-and-protected-update.md`](../../../../../TESAIoT_KIT_PSE84_AI-Micropython-BentoClaw/docs/18-device-provisioning-csr-and-protected-update.md)
> in the Dev Kit project — configuration, CSR and Protected Update from
> MicroPython, the same two from the HSM Security panel, and the life-cycle rule
> that decides whether a lock can be undone. Thai: same name with `-th`.
>
> It lives there, not here, because it covers the panel as well as the API, and
> one copy cannot drift from itself.

Everything here was established by running it on a Dev Kit against
`mqtt.tesaiot.dev`, not by reading documentation. Where a document and the
hardware disagreed, the hardware is recorded and the document is cited so you can
see the gap.

`docs/` holds the platform's API contracts and the three Infineon slides. Read
[`docs/INFINEON_SLIDES.md`](docs/INFINEON_SLIDES.md) first if you are about to
trust one of those slides for something — they are each authoritative for a
different question and they disagree in one place.

---

## 1. Which library family am I in?

`BENTO-TESAIoT-libraries` carries **three** copies of this module:

| family | state |
|---|---|
| `claw/kit-pse84-ai`, `claw/kit-pse84-eval-epc2` | **this one** — everything below is implemented and hardware-verified here |
| `game/kit-pse84-ai`, `game/kit-pse84-eval-epc2` | older; still writes certificates to `0xE0E2` |
| `kit-pse84-ai` (no prefix) | older; same |

`0xE0E2` is Certificate **3**, which belongs with Key 3. Enrolling into `0xE0F1`
and writing the certificate to `0xE0E2` produces a device holding a certificate
whose private key it does not have. Nothing complains at write time — see §4.

---

## 2. The two identities, and where each one goes

A Trust M device has two identifiers and they are not interchangeable.

| | example | used as |
|---|---|---|
| Trust M UID | `CD16xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx...` | the MQTT **client id** |
| device_id (UUID) | `905f31fa-92cb-4555-a8ae-f68a65e142fb` | the MQTT **username**, and **every topic** |

The broker's ACL builds permitted topic prefixes from the UUID only. A topic
built from the UID passes the ownership check, matches no permitted pattern, and
is denied **with the connection still up** — so the message appears to vanish.

In this firmware the values are runtime configuration
(`tesaiot_config_t.device_id` / `.mtls_device_id`), not compile-time constants.
Sources carried over from `official_pse84_trustm_mTLS_tesaiot` expect
`MQTT_CLIENT_IDENTIFIER` / `MQTT_USERNAME` / `DEVICE_ID` as macros; those resolve
to accessors declared in `mqtt_client_config.h` so the carried files stay
byte-identical.

---

## 3. Slot map

From the project's pre-provisioning plan, confirmed on hardware.

| OID | contents |
|---|---|
| `0xE0F0` | ECC Key 1 — Infineon factory key |
| `0xE0E0` | Certificate 1 — Infineon factory certificate, CN `InfineonIoTNode` |
| `0xE0F1` | ECC Key 2 — **the key this device enrols with** |
| `0xE0E1` | Certificate 2 — **where the enrolled certificate goes** |
| `0xE0E8` | Trust Anchor for certificate Protected Update — holds the platform's signing certificate |
| `0xE0E9` | Trust Anchor in the key-rotation configuration (slides 02/03); unused here |
| `0xF1D4` | confidentiality secret for Protected Update |

Every factory chip ships with the same `CN=InfineonIoTNode`, so the certificate
common name cannot identify a device. Until enrolment, the platform has to
identify the board by its Trust M UID. That is what enrolment exists to fix.

---

## 4. The chip binds a certificate to a key — it does not

There is **no** hardware relationship between a certificate slot and a key slot.
Certificate slots are ordinary data objects; key slots are key objects. The only
metadata that binds two objects is the MUD, which links a Protected Update target
to its trust anchor — nothing to do with cert↔key.

`Key N ↔ Cert N` is provisioning convention. Which means:

> Writing a certificate to the wrong slot **succeeds**, silently, and surfaces
> much later as a handshake that fails at CertificateVerify — which looks exactly
> like a server-side rejection.

`optiga_verify_cert_key_pair(cert_oid, key_oid)` is the answer to that. It signs
a fixed challenge with the private half inside the chip and verifies it against
the public key in the certificate; it succeeds only if the two are one pair.
Exposed as `optiga.verify_pair()`, and called automatically at the end of every
certificate install.

---

## 5. Enrolment — CSR to a certificate the chip can prove it owns

Verified end to end in about two seconds.

```
device                                          platform
  |-- CONNECT  (mTLS on the factory pair) ------>|
  |-- SUBSCRIBE device/{uuid}/commands/# ------->|   BEFORE requesting
  |-- PUB .../commands/request ----------------->|   {"client_type":"mcu","csr":"..."}
  |<-- .../commands/certificate -----------------|   the signed PEM, nothing else
  [ write to 0xE0E1, then prove it against 0xE0F1 ]
```

From MicroPython:

```python
import tesaiot, optiga, json
tesaiot.connect()
optiga.init()
csr = optiga.csr(0xE0F1)              # keypair generated in the chip; CN = device_id
req = json.dumps({"request_type":"protected_update",
                  "device_id": "<uuid>", "client_type":"mcu",
                  "payload_version":1, "correlation_id":"...", "csr":csr})
tesaiot.publish(req, "device/<uuid>/commands/request")
# firmware installs and verifies on its own:
#   [PU-Ingest] Certificate installed in 0xE0E1 and verified against key 0xE0F1
```

**The CSR reply is not a Protected Update bundle.** When the platform signs a CSR
it publishes the certificate straight to `commands/certificate` — the log line is
"Publishing certificate DIRECTLY for CSR workflow" — and never touches
`commands/manifest`, `commands/fragment` or `commands/protected_update`. It sends
only `device_id` and `certificate_pem`: **no target_oid, no trust_anchor_oid**.
The firmware chooses the slot. The entire `E0E8`-vs-`E0E9` question is irrelevant
to this path.

The first attempt at this was signed successfully by the platform and dropped on
the floor here, because the subscriber recognised only the bundle and status
topics and everything else fell to a branch whose logging was compiled out. See
§8.

---

## 6. Protected Update — target a certificate slot

Protected Update writes an object the chip will only accept through a manifest
signed against a trust anchor. **The target must be a certificate slot** —
`0xE0E1`, `0xE0E2` or `0xE0E3`, which are interchangeable. `0xE0E0` is not a
target at all: it holds the Infineon certificate and its Change access condition
is `NEV`.

Naming a key slot does not merely write to the wrong object. The platform builds
`ePAYLOAD_DATA` for a certificate OID and `ePAYLOAD_KEY` for a key OID, so a key
slot makes the manifest describe your certificate as a key.

> An earlier revision of this file said the opposite — that Protected Update
> targets key slots, and that the contract's `E0F1` default was right. Both the
> contract and this note were corrected on 2026-08-06 after a run that asked for
> `E0F1` and failed. Two Infineon slides describe Protected Update of an ECC key,
> which is a real capability; it is not what certificate delivery uses.

Three chip-side prerequisites, none of which announces itself when missing:

- **The target's Change access condition must name the trust anchor** — `0x21`
  followed by the anchor OID. The factory default is `LcsO < op`, which is not
  that. The metadata write that establishes it is only permitted while
  `LcsO < op`, so it must happen before the object goes operational.
- **The trust anchor object must actually hold the certificate.** The platform
  sends it in-band every time, as `signing_certificate` in the bundle. If the
  anchor reads back as zeros the chip answers `0x800F` — an error about the
  signature, caused by having no key to check it with.
- **`payload_version` must increase.** The platform tracks this per device and
  target and sends `max(yours, last) + 1`, so sending `1` every time is fine.

`tesaiot_pu_ingest.c` carries the apply path (parse single_bundle JSON → write
and read back the trust anchor → decode manifest and fragments → set the target's
MUD → `protected_update()`), lifted from the reference subscriber task with two
mechanical adaptations noted at the top of the file. **Not yet exercised
end to end.** Before it is, read the MUD off the chip:

```python
optiga.init(); print(optiga.read_metadata(0xE0F1)); optiga.deinit()
```

Read 2026-08-06: there is no MUD on any object, so nothing is being enforced yet
and the anchor is ours to choose. What is not ours to choose is which key signs
the platform's manifests — the MUD has to point at that one.

---

## 7. CSR or Protected Update — which one, and when

Both put a certificate into the same slot. They are not interchangeable, and on
any given slot only one of them is possible at a time — though which one that is
can be changed back on a development board. See "The lock, and when it is
permanent".

### The short version

| | CSR enrolment | Protected Update |
|---|---|---|
| who holds the private key | the chip, always | the chip **only if you send a CSR** |
| how it is written | plain write | manifest verified by the chip |
| works on a fresh slot | yes | yes |
| works on a slot that has been protected-updated | not until the lock is cleared | yes |
| proves the device is what it claims | yes | only with a CSR |
| leaves the slot writable afterwards | yes | no — see below |
| needs a trust anchor certificate on the chip | no | yes, in `E0E8` |

**Use CSR enrolment** to give a device its first real identity. It is the
simpler flow and it is the one that proves possession: the key pair is generated
inside the chip, the private half never exists anywhere else, and the CSR is
signed by that key, so a certificate issued from it is bound to hardware.

**Use Protected Update** when the write itself has to be trustworthy — when the
device might be on a network you do not control and you need the chip, not the
firmware, to decide whether to accept the new certificate. Its value is that
the chip verifies a signature before writing, so a compromised host cannot plant
a certificate.

**The catch**: Protected Update changes the target's Change access condition
from `E1 FC 07` (anyone may write) to `21 E0 E8` (only a manifest signed by the
certificate in `E0E8`). That is the whole point. From then on CSR enrolment
cannot write that slot, because CSR enrolment writes ordinarily.

### The lock, and when it is permanent

This section used to say the change was one-way and that only another Protected
Update could reverse it. That is true of a device you have shipped. It is not
true of the board on your desk, and the difference matters because the board on
your desk is the one people learn on.

What gates a *metadata* write is the object's own Change access condition, and
the factory value is `E1 FC 07` — LcsO below operational. Every object on this
board reads `LcsO = 0x01`, Creation. So the seven bytes that put `D0` back to
`E1 FC 07` are accepted, and CSR enrolment works again. Measured on 2026-08-08:

```
before   d0 03 21 e0 e8      only a signed manifest
after    d0 03 e1 fc 07      an ordinary write is accepted
```

Those are the same seven bytes this firmware already writes to a key slot on
every key generation (`reset_metadata[]`), and the HSM Security page exposes
them as "Allow ordinary writes again" on the Enrol screen.

Advance a device to operational and it stops being reversible: the metadata
write is refused, and LcsO only ever moves forward (`cr → in → op → te`). So:

| | development board, LcsO 0x01 | shipped device, LcsO 0x07 |
|---|---|---|
| Protected Update locks the slot | yes | yes |
| the lock can be cleared afterwards | **yes** | **no** |

State which one you mean. Saying only "permanent" taught this project's own
client brief something false about hardware they were holding.

### So Protected Update needs a CSR too

Protected Update on its own answers "may this be written?" It does not answer
"whose key is this?" The platform, given a request with no CSR, generates a key
pair in its own memory, issues the certificate against it, and discards the
private half. The result installs cleanly and reports success at every layer —
and the device cannot prove it holds the key, because it does not.

That is not hypothetical. On 2026-08-07 a Protected Update reported
`bundle_published`, `Manifest verification OK`, `certificate_installed: success`
in 2831 ms, and `verify_pair(0xE0E1, 0xE0F1)` then returned `False`.

```python
tesaiot.protected_update("E0E1", "E0E8", csr=True)   # bound to this chip
tesaiot.protected_update("E0E1", "E0E8")             # bound to nothing
```

`csr=True` generates a fresh key pair in the slot that pairs with the target and
sends the CSR with the request. It is destructive by necessity: the chip cannot
hand out the public half of a key it already holds, so producing a CSR means
producing a new pair, and any certificate issued against the old one stops
matching that instant.

### Before you run either one

```python
optiga.slot_info(0xE0E1)
# {'oid': 0xE0E1, 'used_size': 0x1F1, 'data_type': 0x12,
#  'payload_version': 0x5, 'manifest_anchor': 0xE0E8}
```

`manifest_anchor` is `0` while the slot still takes a plain write. Anything else
means Protected Update has run and CSR enrolment is no longer possible there.

`payload_version` is the chip's anti-rollback counter, and it only appears after
a Protected Update. The next manifest must carry a strictly greater value or the
chip rejects it **as a bad signature** — the same `0x800F` you get from a wrong
trust anchor, with nothing to tell them apart. The firmware sends this value as
`current_version` so the platform can compute from the chip rather than from its
own records.

### Afterwards, check the only thing that matters

```python
optiga.verify_pair(0xE0E1, 0xE0F1)   # True, or the certificate is decoration
```

A certificate installing successfully says nothing about whether the device can
use it. This signs a challenge inside the chip with the private half and checks
it against the public key in the certificate; it is true only if the two are one
key pair. Nothing else in either flow asks that question.

## 8. The mTLS path

`mqtt_mtls_setup.c` bootstraps on the **factory pair** `0xE0E0`/`0xE0F0`. Four
things about it are not obvious:

- **The key policy must be `PSA_ALG_ECDSA(PSA_ALG_ANY_HASH)`.** TLS 1.2 takes the
  CertificateVerify hash from the negotiated ciphersuite's PRF, not from the
  client key's curve, so a SHA-384 suite asks a P-256 key for a 48-byte digest. A
  policy naming only SHA-256 makes `psa_sign_hash()` refuse before the driver is
  reached.
- **The PSA driver must truncate.** SEC1 §4.1.3 / FIPS 186-4 §6.4: the message
  representative is the leftmost min(N, outlen) bits. Built-in mbedTLS does this
  in `derive_mpi()`; a secure-element driver gets the untruncated digest and must
  do it itself.
- **PSA is torn down with TLS.** `cy_tls_deinit()` calls
  `mbedtls_psa_crypto_free()`, which wipes volatile keys *and* unregisters
  secure-element drivers. Setup probes its key handle with
  `psa_get_key_attributes()` and, on a wiped store, re-registers the driver
  before `psa_crypto_init()` and clears the driver's slot map — which PSA never
  tells it about, so it would otherwise leak one of four slots per reconnect.
- **Signing holds touch off the bus.** The secure element and the touch
  controller share SCB5. `trustm_ecdsa_sign()` takes a counted touch hold for the
  whole transaction; without it the signature races CM55 polling and sometimes
  never returns, inside the vendor library where no timeout catches it.

`s_prefer_device_pair` switches to the enrolled pair once it verifies. It is
**off** until the platform is known to accept the enrolled certificate.

---

## 9. Failure shapes worth recognising

Every one of these cost hours.

| what you see | what it is |
|---|---|
| TCP connects then closes in milliseconds, no ClientHello | the opaque key was destroyed — `mbedtls_pk_free()` on an opaque PK context calls `psa_destroy_key()` |
| handshake dies at CertificateVerify, no alert from either side | the chip was never asked to sign, or was asked with an algorithm the key policy forbids |
| the same binary connects instantly one run and hangs forever the next | two cores driving one I2C bus |
| a message the platform logged as sent, that the device never saw | a topic the classifier does not know, falling into a branch whose `printf` is muted |
| a task that worked for months, fatal the first time it did real work | it had never actually run — `Subscriber` overflowed 768 words the first time a connection succeeded |
| `CONNECTED` at the broker, no telemetry, dropped at 90 s | no `PINGREQ` — usually because the device died right after CONNACK |

Three files mute `printf` with a function-like macro (`subscriber_task.c`,
`publisher_task.c`, `mqtt_task.c`). A plain call inside them compiles to nothing.
Use `(printf)(...)` in parentheses to bypass the macro when something must be
seen.

---

## 10. Where the pieces live

| file | what it does |
|---|---|
| `optiga_trust_helpers.c` | chip primitives: open/close (reference counted), read certificate, `trustm_ecdsa_sign`, `optiga_verify_cert_key_pair`, `write_device_certificate_and_verify` |
| `optiga_psa_se.c` | the PSA secure-element driver — `p_sign` is where TLS reaches the chip |
| `tesaiot_optiga_manager.c` | the mutex every chip operation serialises on, and the counted touch hold |
| `tesaiot_pu_ingest.c` | bundle apply path + direct certificate installer, reached from `subscriber_task.c` through weak symbols |
| `tesaiot_optiga_trust_m.c` | the MQTT side: request and status publishing |
| `tesaiot_csr_workflow.c`, `tesaiot_protected_update_*.c` | carried over from the reference, unmodified |
| `../../../common/mpy/modoptiga.c` | the `optiga.*` MicroPython surface; every direct chip entry point runs under an `nlr_push` guard so a raise cannot leak the mutex |

Build with `ENABLE_OPTIGA_CLM=1` to include the CSR and Protected Update sources.
The default build links without them; the subscriber's weak symbols resolve to
null and it logs that it cannot install.

---

## 11. Still open

- **Trust anchor** — settled: `E0E8`, the platform's default, with its signing
  certificate written there and read back verified. It was never a fact to
  discover; the anchor is whatever the target's metadata names, and this firmware
  writes that metadata. `E0E9` belongs to the key-rotation configuration in slides
  02/03. See [`docs/INFINEON_SLIDES.md`](docs/INFINEON_SLIDES.md).
- **The Protected Update prerequisites are not in place.** No MUD on the target
  means the chip cannot verify a manifest (error `0x8007`). Slide 03 Steps 0–3 —
  reset target metadata, write the trust anchor, write the confidentiality
  secret, set the MUD — have to run first. Everything is still `LcsO = 0x01` with
  `Change: LcsO < 0x07`, so metadata can be written directly.
- **Protected Update** has never run end to end over MQTT.
- **`s_prefer_device_pair`** is off; the device still authenticates on the factory pair.
- **The other two library families** still write certificates to `0xE0E2` (§1).
