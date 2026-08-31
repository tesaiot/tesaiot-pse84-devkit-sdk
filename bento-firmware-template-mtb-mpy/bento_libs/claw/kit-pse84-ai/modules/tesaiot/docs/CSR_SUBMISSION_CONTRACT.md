# CSR Submission — API Contract

**For:** firmware / device client teams
**From:** TESAIoT platform team
**Status of this document:** every topic, field name and rule below was read out of
the running code, not from memory. Sources are cited so you can check them.
**Last verified:** 2026-08-06 against `develop` @ `35d9db043`

---

## Before you write any code

Four rules. Every one of them has already cost someone a debugging session.

1. **Topics use the device_id UUID, never the Trust M UID** — the UID is your MQTT
   client id, and a topic built from it is denied while the connection stays up,
   so the message just seems to vanish. → §3
2. **SUBSCRIBE to `device/{device_id}/commands/#` before you publish the CSR** —
   replies are `retain=false` and retained messages are cleared on every connect.
   A reply sent before your subscription lands is gone for good. → §4.3, §4.4
3. **Something must service the connection.** No `PINGREQ` and the broker closes the
   session at 90 s (1.5 × keepalive); a reply arriving after that is published into
   nothing. Missing loop and crashed-task look identical from here — check the device
   log, not just the broker. → §5.3
4. **A rejected CSR is silent on the wire.** Nothing is published back on the CSR
   topic. The reason is in the platform's device logs. → §4.2
5. **Write the certificate from `commands/pubkey` into 0xE0E8.** It is the trust
   anchor the manifest names. Writing it anywhere else — 0xE0E3 is the usual
   mistake — leaves 0xE0E8 empty and every Protected Update fails with `0x800F`
   even though the signature is correct. → §4.5

---

## 1. Which path do I use?

There are two ways a CSR reaches the platform. They are not interchangeable.

| | **MQTT** (device-initiated) | **HTTPS** (operator / backend) |
|---|---|---|
| Who calls it | the device itself, over its existing mTLS session | an admin tool or backend service |
| Auth | the device's MQTT connection (mTLS + platform authn) | JWT bearer token, `CERTIFICATE_CREATE` permission |
| Response | asynchronous, over MQTT topics | synchronous, in the HTTP response |
| Use when | the device generates its own key pair and must never expose it | you are enrolling a device from the platform side |

**A device on the factory OPTIGA certificate uses the MQTT path.** The rest of
this document is mostly about that path; the HTTPS one is in §6.

---

## 2. Prerequisite: the device must already be connected

CSR submission is a message on an *existing* MQTT session. Before any of this
works the device must have completed:

1. TLS with a client certificate the platform trusts
2. MQTT CONNECT accepted by the platform authenticator
3. an MQTT loop that keeps the session alive (see §5.3 — this is where devices
   most often fail)

If the connection drops before the platform replies, the reply is lost: the
response topics are published with `retain=false`
(`protected_update_mqtt_bridge.py:1730`, `protected_update_service.py:1357`).

---

## 3. ⚠️ The one thing that catches everyone: two different identifiers

A Trust M device has **two** identifiers, and they are used in different places.

| | value for the reference device | used as |
|---|---|---|
| **Trust M UID** | `CD16xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx` | the **MQTT client id** |
| **device_id** (UUID) | `905f31fa-92cb-4555-a8ae-f68a65e142fb` | the **MQTT username**, and **every topic** |

```
MQTT CONNECT:
    client_id = CD16334D0100...        <- Trust M UID
    username  = 905f31fa-92cb-...      <- device_id

Topics:
    device/905f31fa-92cb-.../commands/csr      <- device_id, NOT the UID
```

The ACL builds the permitted topic prefixes from the resolved `device_id` only
(`emqx_auth.py:604-608`). Publishing to `device/<TrustM-UID>/...` passes the
ownership check but then matches no permitted pattern, and the publish is
denied — with the connection still up, which makes it look like the message
simply vanished.

> Hex case does not matter for the UID itself — the platform compares it
> case-insensitively as of `70bdd4a3a`. It matters that you use the **UUID** in
> topics.

---

## 4. MQTT path

### 4.1 Request

**Topic**

```
device/{device_id}/commands/csr
```

**QoS 1.** The platform subscribes at QoS 1 (`csr_bridge.py:154`).

**Payload** — UTF-8 JSON:

```json
{
  "device_id": "905f31fa-92cb-4555-a8ae-f68a65e142fb",
  "csr": "-----BEGIN CERTIFICATE REQUEST-----\nMIIB...\n-----END CERTIFICATE REQUEST-----\n"
}
```

| field | required | notes |
|---|---|---|
| `device_id` | yes | must be the UUID. If it disagrees with the topic, **the topic wins** and a warning is logged (`csr_bridge.py:270-274`) |
| `csr` | yes | PEM. Must be **longer than 100 bytes** or it is rejected outright (`csr_bridge.py:276-281`) |
| `csr_b64` | — | accepted as an alternative to `csr` when forwarding (`csr_bridge.py:331`); prefer `csr` |

PEM is converted to base64 DER by the bridge, so send PEM.

### 4.2 Validation the platform applies, in order

Fail any of these and **the CSR is dropped silently from the device's point of
view** — there is no error published back on the CSR topic itself:

1. topic structure is exactly `device/{id}/commands/csr` (`csr_bridge.py:202-219`)
2. payload is valid UTF-8
3. payload is valid JSON
4. `device_id` and `csr` are both present
5. `csr` is longer than 100 bytes

Errors are recorded in the platform's device logs, visible in the admin UI —
ask the platform team if a CSR appears to disappear.

### 4.3 Response

The device must be subscribed to `device/{device_id}/commands/#` **before**
submitting. The reply arrives as a Protected Update bundle, not as a plain
certificate:

| topic | payload |
|---|---|
| `device/{device_id}/commands/pubkey` | `{"pubkey": "<X.509 PEM>", "correlation_id": "..."}` — **the Protected Update trust anchor**, see §4.5 |
| `device/{device_id}/commands/manifest` | `{"manifest": "<base64>", "num_fragments": N, "target_oid": "0xE0E1", "trust_anchor_oid": "0xE0E8", "payload_version": V, "correlation_id": "..."}` |
| `device/{device_id}/commands/fragment` | `{"fragment": "<base64>", "fragment_index": i, "is_final": bool, "correlation_id": "..."}` — one message per fragment |
| `device/{device_id}/commands/status` | `{"status": "bundle_published"\|"bundle_publish_failed", "detail": "...", "target_oid": "...", "trust_anchor_oid": "...", "payload_version": V, "correlation_id": "..."}` |

Source: `protected_update_service.py:1305-1420`.

> **Correction, 2026-08-06.** An earlier revision of this document listed
> `commands/status` while no code published it — the publish path ended after the
> last fragment and disconnected. `is_final` covered the happy path only, so a run
> that died between fragments was indistinguishable from one still in progress.
> The status message now exists on both the success and the failure path. If you
> are reading this against a platform build older than 2026-08-06, do not wait
> for it.

`correlation_id` ties the pubkey, manifest, fragments and status together — use
it, do not assume ordering.

### 4.5 ⚠️ The trust anchor arrives on `commands/pubkey` — write it to 0xE0E8

This is the single most common reason a Protected Update fails after every other
step succeeds.

The manifest is a COSE_Sign1 whose `kid` header names **0xE0E8**.
`optiga_util_protected_update_start()` reads that object to verify the signature.
The certificate that must be in it is the one the platform sends you on
`commands/pubkey` — there is no separate download for it, and no bundle file
contains it.

```c
/* wrong — 0xE0E3 is a data object, not a trust anchor */
optiga_util_write_data(me_util, 0xE0E3, OPTIGA_UTIL_ERASE_AND_WRITE, 0, der, len);

/* right — the object the manifest actually names */
optiga_util_write_data(me_util, OPTIGA_TRUST_ANCHOR_OID, OPTIGA_UTIL_ERASE_AND_WRITE,
                       0, der, len);
```

If 0xE0E8 reads back as zeros, that is your fault mode: the chip has nothing to
verify the manifest against and returns **0x800F**, no matter how correct the
signature is.

Two known traps in the reference clients:

- the PSoC sample writes the received certificate to a hard-coded `0xE0E3`
  (`subscriber_task.c:883`) while `optiga_oid_config.h:78` defines the anchor as
  `0xE0E8`;
- the RPi/Linux sample logs the certificate and never writes it at all
  (`protected_update_workflow.c:132`).

If your deployment provisioned the anchor elsewhere, send `trust_anchor_oid`
explicitly in the request — the platform will put your value in the `kid`.

### 4.4 Retained messages

On every device connect the platform clears retained messages on
`commands/{protected_update,certificate,manifest,fragment,pubkey,status}`
(`protected_update_mqtt_bridge.py:396-401`), so a reconnecting device does not
replay a stale bundle. Do not rely on retained delivery for anything.

---

## 5. Practical notes from a real bring-up

These come from debugging this exact device on 2026-08-04/05.

### 5.1 What the ACL permits a device to publish

Built from the resolved `device_id` (`emqx_auth.py:618-640`):

- `telemetry`, `status`, `events`, `metrics` — and their subtrees
- `commands/csr`, `commands/request`, `commands/check_certificate`,
  `commands/upload_certificate`, `commands/sync_certificate`, `commands/init`

Subscribe is permitted for `commands`, `config`, `firmware`.

### 5.2 CertificateVerify with an OPTIGA key

If the server negotiates a SHA-384 ciphersuite (ours currently selects
`ECDHE_RSA_AES256_GCM_SHA384`), mbedTLS TLS 1.2 derives the CertificateVerify
hash from the ciphersuite PRF, **not** from the client key's curve. A P-256
OPTIGA key whose PSA policy only permits `PSA_ALG_ECDSA(PSA_ALG_SHA_256)` then
returns `PSA_ERROR_NOT_PERMITTED (-133)` and no CertificateVerify is sent at
all. Per SEC1 / FIPS 186-4 §6.4 the signer truncates a digest longer than the
group order to its leftmost 256 bits; the driver must do that, and the key
policy must allow the hash.

Note also that PSA returns a raw `r‖s` pair; TLS wants DER
`SEQUENCE { INTEGER r, INTEGER s }`. Watch the leading zero byte when the MSB
of `r` or `s` is ≥ 0x80.

### 5.3 Keep the session alive — and read the symptom correctly

The broker closes a session at 1.5 × keepalive (90 s with the default 60 s) if no
`PINGREQ` arrives. A CSR reply that arrives after that is gone.

**What the absence of `PINGREQ` actually tells you:** nothing is servicing the
connection. That is not the same as "the loop is missing". Two causes produce an
identical trace from the broker's side:

| cause | how to tell |
|---|---|
| the MQTT loop is never called | the client task is alive and doing other work |
| **the task that runs the loop has died** | nothing after CONNACK — no publishes, no logs, no anything |

> Worked example. A PSoC Edge board dropped at 90–92 s across four measured attempts
> with `PINGREQ = 0` every time. The platform team read that as a missing loop and
> said so. It was wrong: the firmware's `Subscriber` task overflowed its FreeRTOS
> stack immediately after CONNACK — `FATAL: Stack overflow in task 'Subscriber'`, one
> line after `Connected to broker`. The loop had been there all along; the task
> running it was dead. Enlarging the stack fixed it, and the same board then sat
> silent for 142 s and still published successfully.
>
> **Check the device log first.** The broker can only tell you that nothing arrived,
> never why.

Once the task is alive, call the library's loop — `loop_forever()` / `check_msg()` /
`MQTT_ProcessLoop()` — at least once per keepalive interval.

---

## 6. HTTPS path (operator / backend)

```
POST /api/v1/devices/{device_id}/certificate/sign-csr
Authorization: Bearer <JWT>
Content-Type: application/json

{
  "csr": "-----BEGIN CERTIFICATE REQUEST-----...",
  "validity_days": 365
}
```

`csr`, `csrContent` and `csr_content` are all accepted; `validity_days` is
optional and defaults to 365. Requires the `CERTIFICATE_CREATE` permission.

Responses: `200` signed · `400` invalid CSR · `403` denied · `404` unknown
device · `500` signing failed. Source: `certificates.py:365-390`.

There is also an internal service endpoint,
`POST /internal/v1/protected-update/csr`, authenticated with
`X-Internal-Service` + `X-Service-Secret`. It is for platform components such
as the MQTT bridge, not for devices or third parties.

---

## 7. Checklist before you say "the CSR isn't working"

1. Is the MQTT session still up at the moment you publish? (`PINGREQ` sent?)
2. Does the topic use the **device_id UUID**, not the Trust M UID?
3. Is `device_id` inside the payload as well as in the topic?
4. Is the CSR PEM longer than 100 bytes?
5. Are you subscribed to `device/{device_id}/commands/#` *before* publishing?
6. Are you reading `correlation_id` rather than assuming message order?

If all six hold and nothing comes back, ask the platform team to check the
device logs — every validation failure above is recorded there with a reason.

---

## Source index

| topic | file |
|---|---|
| CSR intake, validation, forwarding | `src/python/services/csr_bridge.py` |
| MQTT bridge, response topics, retained clearing | `src/python/api/services/protected_update_mqtt_bridge.py` |
| Manifest / fragment publishing | `src/python/api/services/protected_update_service.py` |
| ACL rules | `src/python/api/controllers/emqx_auth.py` |
| MQTT authentication | `src/python/api/services/mqtt_auth_service.py` |
| HTTPS sign-csr | `src/python/api/controllers/certificates.py` |
