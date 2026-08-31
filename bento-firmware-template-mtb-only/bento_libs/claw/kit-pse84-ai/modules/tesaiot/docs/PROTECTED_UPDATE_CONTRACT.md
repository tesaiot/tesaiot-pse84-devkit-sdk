# Protected Update — API Contract

**For:** firmware / device client teams
**From:** TESAIoT platform team
**Companion document:** [CSR_SUBMISSION_CONTRACT.md](CSR_SUBMISSION_CONTRACT.md) — read §3
(the two identifiers) and §5.3 (keep the session alive) there first; they apply here too.
**Status of this document:** topics, field names, defaults and status codes were read out
of the running code. Sources cited.
**Last verified:** 2026-08-06 against `develop` @ `35d9db043`

---

## Before you write any code

1. **Send `"client_type": "mcu"`** if this is MCU firmware. Omit it and you get the
   multi-topic protocol — your bundle goes to different topics than the ones you
   are watching. **This includes the bare `protected_update` string payload.** → §2, §3.1
2. **Topics use the device_id UUID, never the Trust M UID** — the UID is your MQTT
   client id. A topic built from it is denied while the connection stays up. → §6
3. **SUBSCRIBE to `device/{device_id}/commands/#` before you request** — responses
   are `retain=false` and retained messages are cleared on every connect. → §6
4. **Something must service the connection.** Bundle generation is asynchronous; if
   the session dies at 90 s (1.5 × keepalive) the bundle is published into nothing.
   A missing loop and a crashed task are indistinguishable from the broker. → §6
5. **Wait for `status: "manifest_published"`**, and match responses by
   `correlation_id` rather than by arrival order. `bundle_published` follows once
   the last fragment is out. → §4.3
6. **Write the certificate from `commands/pubkey` into the trust anchor OID the
   manifest names** (`0xE0E8` by default). If that object reads back as zeros, the
   chip returns `0x800F` no matter how correct the signature is — this is the most
   common cause of a Protected Update that fails after everything else succeeds.
   → §3.5

---

## 1. What Protected Update is, and why the certificate arrives this way

An OPTIGA Trust M will not accept a certificate written as plain bytes into a
protected object. Infineon's Protected Update requires a **manifest** — signed
by a key the chip already trusts — plus one or more **fragments** carrying the
payload. The chip verifies the manifest against a trust anchor OID before it
writes anything.

So the platform never sends you "the certificate". It sends a manifest and
fragments, and your firmware feeds them to the OPTIGA API. This is why the CSR
reply looks the way it does.

| OID | default | meaning |
|---|---|---|
| `target_oid` | `E0E1` | which OPTIGA slot the new certificate is written to — see §3.4 |
| `trust_anchor_oid` | `E0E8` | object holding the key the chip verifies the manifest against — **must match what the target object's metadata names**, see §3.5 |

Both are normalised: `0x` prefix stripped, uppercased
(`protected_update_schema.py:328-331`).

---

## 2. Two protocols — pick the right one, it is not automatic

The platform speaks **two different wire formats** for the same bundle, chosen
by the `client_type` field in your request (`protected_update_service.py:119-153`):

| `client_type` | protocol | shape |
|---|---|---|
| `"mcu"` | **single_bundle** | one JSON message on one topic |
| `"linux"` / `"rpi"` | **multi_topic** | manifest and each fragment on separate topics |
| omitted / unknown | **multi_topic** ← default | |

> **For a PSoC / MCU firmware, send `"client_type": "mcu"`.** If you omit it you
> get the multi-topic protocol and will be waiting for a message that never
> arrives on the topic you are watching.

---

## 3. Request

**Topic**

```
device/{device_id}/commands/request
```

`{device_id}` is the **UUID**, not the Trust M UID. QoS 1.

### 3.1 Minimal form (PSoC firmware style)

The bridge accepts a bare string payload (`protected_update_mqtt_bridge.py:590-595`):

```
protected_update
```

That is taken as `request_type=protected_update` with every default applied.

> ⚠️ **This is almost certainly not what MCU firmware wants.** A bare string
> carries no `client_type`, so the request falls through to **multi_topic**: the
> bundle is published to `commands/manifest` and `commands/fragment` instead of
> the single `commands/protected_update` message an MCU client is usually written
> to expect. The request succeeds, the platform reports `manifest_published`, and
> the device sits waiting on a topic nothing was ever sent to.
>
> Send the JSON form in §3.2 with `"client_type": "mcu"`.

### 3.2 Full form

```json
{
  "request_type": "protected_update",
  "device_id": "905f31fa-92cb-4555-a8ae-f68a65e142fb",
  "client_type": "mcu",
  "payload_version": 1,
  "target_oid": "E0E1",
  "trust_anchor_oid": "E0E8",
  "csr": "-----BEGIN CERTIFICATE REQUEST-----...",
  "correlation_id": "any-unique-string",
  "firmware_version": "0.5.54",
  "status_topic": "device/.../commands/status",
  "metadata": {}
}
```

| field | required | default | notes |
|---|---|---|---|
| `request_type` | yes | `protected_update` | validated; anything else is rejected (`protected_update_schema.py:319-324`) |
| `device_id` | **yes** | — | must match the topic |
| `client_type` | no | *(none → multi_topic)* | **send `"mcu"` for MCU firmware** |
| `payload_version` | no | `1` | |
| `target_oid` | no | `E0E1` | must be a certificate slot — see §3.4 |
| `trust_anchor_oid` | no | `E0E8` | |
| `csr` | no | — | inline CSR-based Protected Update: sign and deliver in one round trip |
| `correlation_id` | no | — | echoed on every response; **use it** |
| `status_topic` | no | `device/{device_id}/commands/status` | override where status goes |
| `firmware_version` | no | — | telemetry for the platform |
| `metadata` | no | `{}` | free-form string map |

Source: `protected_update_schema.py:266-317`.

### 3.3 Two ways to get a certificate

- **`csr` included** — the platform signs it and returns a bundle built from the
  freshly signed certificate. One round trip.
- **`csr` omitted** — the platform looks up the certificate already stored for
  this device and builds a bundle from that. Fails with `certificate_not_found`
  if there is none, so either enrol first (see the CSR contract) or send a CSR.

### 3.4 ⚠️ target_oid must be a certificate slot, not a key slot

An OPTIGA Trust M keeps certificates and keys in different families of objects,
and the platform classifies them (`protected_update_mqtt_bridge.py:794-795`):

```
CERT_OIDS = {"E0E1", "E0E2", "E0E3"}                          # E0E0 is Change=NEV
KEY_OIDS  = {"E0F0", "E0F1", "E0F2", "E0F3", "E0FC", "E0FD", "E200"}
```

(KEY_OIDS is Infineon's `optiga_key_id_t` verbatim. Anything in neither set is a
data object — trust anchors `E0E8`/`E0E9`/`E0EF`, arbitrary data `F1D0-F1DB`,
`F1E0-F1E1` — and is built as `ePAYLOAD_DATA`.)

`target_oid` is where the **certificate** lands, so it must come from the first
set. It also decides how the manifest is built
(`protected_update_csr_worker.py:88-94`): a key OID produces `ePAYLOAD_KEY (-3)`,
a certificate OID produces `ePAYLOAD_DATA (-1)`. Naming a key slot therefore does
not merely write to the wrong object — it describes your certificate as a key.

> **This document previously stated the default was `E0F1`. That was wrong**, and
> wrong in the way above. It is now `E0E1`. The platform is otherwise flexible: it
> uses whatever `target_oid` you send (`protected_update_service.py:881`) and never
> reads the certificate back from the chip, so there is no later verification step
> expecting a particular slot. Send `target_oid` explicitly and the platform will
> follow you.
>
> Reported by the BENTO firmware team on 2026-08-05, who found the platform's own
> reference code writing to `0xE0E2` — a certificate slot, and a valid choice.

**`E0E1`, `E0E2` and `E0E3` are interchangeable.** Same `max_size` (1728), same
access conditions, no capability difference. `E0E0` is not a target at all: it holds
the Infineon-issued certificate and its Change access condition is `NEV` on every
part, so a request naming it is now rejected up front rather than failing on the
chip seconds later.

**There is no fixed pairing between a certificate slot and a key slot.** Infineon's
own configuration guide pairs certificate `0xE0E2` with key `0xE0FC`, and
`mtb-example-optiga-mqtt-client` documents both the certificate and key OIDs as
"can be of your choice". What decides whether a certificate and a key belong
together is that the certificate was issued for that key — the CSR came from it —
not that the slot indices line up.

> **Erratum, 2026-08-05.** We briefly told the BENTO team the opposite: that
> `verify_pair(0xE0E2, 0xE0F1)` was "cross-paired" and had to become
> `(0xE0E1, 0xE0F1)`. That was wrong. It came from reading a slot-allocation diagram
> — our own project's plan, drawn on an Infineon template — as though the row
> alignment were a hardware constraint. It is not. Their original choice was correct.
>
> If such a check fails, look at which key slot generated the CSR, not at the slot
> numbers.

---

### 3.5 Before a Protected Update can succeed at all

These are chip-side prerequisites. The platform cannot do them for you, and none of
them looks like its own cause when it fails.

**The target object's Change access condition must already name the trust anchor.**
The SRM requires the Change AC to contain the `Int` identifier (`0x21`) followed by
the OID of the trust anchor that verifies the manifest signature. The factory
default on `E0E1-E0E3` is `LcsO < op` — *not* that. The metadata write which
establishes it is itself only permitted while `LcsO < op`, so it has to happen
before the object's life-cycle state advances. Read it back before you start:

```
optiga.read_metadata(0xE0E1)     # tag 0xD0 = Change AC, tag 0xC0 = LcsO
```

If `0xC0` reads `0x07` (operational) and the Change AC does not name a trust anchor,
that object is closed — no Protected Update to it will ever succeed.

**`payload_version` must strictly increase — and the platform does that for you.**
The chip records the version applied to each object and rejects a manifest whose
version does not exceed it (SRM error `0x10`). You do not have to track it: the
platform looks up the highest version already used for this *device and target OID*
and sends `max(yours, last) + 1` (`protected_update_service.py:1610`). Sending
`payload_version: 1` on every request is fine and is what devices actually do —
verified against production data, where one device on `E0E1` has run through
versions 3 to 53 with no repeats.

**The trust anchor object must actually contain the certificate.** This is the one
that costs the most time, because every other step reports success. The platform
signs the manifest and names the anchor OID in the COSE_Sign1 `kid` header. The chip
reads *that object* to verify the signature. If it is empty, you get `0x800F` — an
error about the signature, caused by there being no key to check it with.

Read it back before you conclude anything about your signature:

```
optiga.read_data(0xE0E8)      # zeros here means the anchor was never installed
```

The certificate is sent to you in-band with every update, on
`device/{device_id}/commands/pubkey` (or as `signing_certificate` in the
single-topic bundle). There is no separate download and no bundle file that carries
it. Write **that** certificate into the OID the manifest names.

Two traps we have hit in the reference clients — check yours against both:

| client | what it does | consequence |
|---|---|---|
| PSoC sample | writes the certificate to a hard-coded `0xE0E3` (`subscriber_task.c:883`) while the anchor is defined as `0xE0E8` (`optiga_oid_config.h:78`) | `0xE0E8` stays zeros → `0x800F` |
| RPi/Linux sample | `on_pubkey_received()` hex-dumps and returns (`protected_update_workflow.c:132`) | anchor never installed at all |

Generated firmware headers before 2026-08-06 made this worse: they instructed you to
store the *Infineon factory CA* in `0xE0E8`, which is the same object Protected
Update needs. If you provisioned from one of those headers, `0xE0E8` holds the wrong
certificate and the symptom is identical. Current headers put the factory CA in
`0xE0EF` and reserve `0xE0E8`.

**The trust anchor OID is a binding, not a constant.** Whatever OID the target
object's metadata names is the one the platform must sign against and put in the
manifest. Our default is `E0E8` (settable platform-wide with
`PROTECTED_UPDATE_TRUST_ANCHOR_OID`); deployments that provisioned their anchor at
`E0E9` must send `trust_anchor_oid` explicitly. The chip's metadata is the source of
truth here — not this document, and not the platform's default.

**Confidentiality is supported but not automatic.** If the target's access condition
includes `Conf-<OID>`, fragments must be encrypted with the shared secret in that
object. Send `secret_hex` (the secret) and `secret_oid` (where it lives on the chip)
in the request; the schema requires them together
(`protected_update_schema.py:151-154`). Omit them and fragments go out in the clear,
which an object requiring confidentiality will refuse.

> None of this applies to the CSR certificate path in §4.3 — that publishes a PEM
> directly and involves no manifest, no trust anchor and no confidentiality. It is
> only once an object is locked to operational that Protected Update becomes the
> only way to write it.

---

## 4. Response

### 4.1 single_bundle (`client_type: "mcu"`)

One message on:

```
device/{device_id}/commands/protected_update
```

```json
{
  "type": "...",
  "device_id": "905f31fa-...",
  "correlation_id": "...",
  "payload_version": 1,
  "target_oid": "E0E1",
  "trust_anchor_oid": "E0E8",
  "signing_certificate": "<x509 of the manifest signing key>",
  "manifest": "<base64>",
  "fragment_count": 2,
  "fragment_0": "<base64>",
  "fragment_1": "<base64>",
  "fragment_2": "<base64>"
}
```

Fragments use a **flat, numbered structure** — `fragment_0`, `fragment_1`,
`fragment_2` — deliberately, not a JSON array
(`protected_update_service.py:1186`). `fragment_1` and `fragment_2` are present
only when the payload needs them; check `fragment_count`.

### 4.2 multi_topic (`linux` / `rpi` / default)

| topic | payload |
|---|---|
| `device/{device_id}/commands/manifest` | `{"manifest": "<base64>", "num_fragments": N, "pubkey": "...", "target_oid": "...", "correlation_id": "..."}` |
| `device/{device_id}/commands/fragment` | `{"fragment": "<base64>", "fragment_index": i, "is_final": bool, "correlation_id": "..."}` — one message per fragment |

Source: `protected_update_service.py:1345-1387`.

Do not assume fragments arrive in order — use `fragment_index`, and treat
`is_final` as the end marker.

### 4.3 commands/certificate — the CSR workflow does NOT send a bundle

There is a third shape, and it is the one a CSR submission actually produces.

When the certificate arrives via the CSR path (you published to
`commands/csr`, or included `csr` in a Protected Update request), the platform
takes a shortcut: it publishes the signed certificate **directly**, with no
manifest and no fragments.

```
device/{device_id}/commands/certificate
```

Observed end to end on 2026-08-05 for `correlation_id: bento-enrol-1`:

```
23:21:31.211  EMQX received the device's PUBLISH
23:21:31.254  API   Internal Protected Update CSR request
23:21:33.584  API   Successfully signed CSR (attempts=1)
23:21:34.784  API   "Publishing certificate directly for CSR workflow"
23:21:35.967  API   csr_publish_success
```

Nothing was published to `commands/manifest` or `commands/fragment` in that run.

> A firmware classifier that only knows `commands/protected_update` and
> `commands/status` will drop this message into its default branch. That is
> exactly what happened on the first successful enrolment: the certificate
> reached the device's subscriber queue, the timestamps matched to the second,
> and it was discarded silently because the default branch had its logging muted.
>
> **Handle `commands/certificate`**, or you will discard a certificate that
> arrived correctly and conclude the platform never sent it.

### 4.4 Status

```
device/{device_id}/commands/status        (override with status_topic)
```

```json
{"status": "<code>", "detail": "<≤256 chars, optional>", "correlation_id": "..."}
```

Two components publish here, and they report **different events**. Both are useful;
do not treat them as duplicates.

| code | published by | means |
|---|---|---|
| `manifest_published` | the bridge, when your request is accepted | the dataset was generated and handed to the publisher — *not* that fragments have arrived |
| `bundle_published` | the publisher, after the last fragment | every fragment is now on the wire; carries `target_oid`, `trust_anchor_oid` and `payload_version` |
| `bundle_publish_failed` | the publisher, on any failure mid-publish | the run stopped part-way; `detail` says why. Without this a dead run and a slow run look identical |

`bundle_published` / `bundle_publish_failed` exist only on platform builds from
2026-08-06 onward. Before that the publisher sent nothing after the last fragment,
so firmware had to infer completion from `is_final` and a timeout.

Remaining codes from the bridge (`protected_update_mqtt_bridge.py`):

| code | meaning |
|---|---|
| `csr_queued` | CSR accepted for signing |
| `request_invalid` | malformed request (bad topic, non-UTF-8, wrong `request_type`) |
| `invalid_json` | payload was not JSON |
| `device_mismatch` | `device_id` in payload ≠ topic |
| `validation_failed` | schema validation failed |
| `csr_invalid` | CSR could not be parsed |
| `csr_rate_limited` | too many CSR requests |
| `csr_enqueue_failed` / `csr_signing_failed` | platform-side signing problem |
| `certificate_not_found` | no certificate stored and no CSR supplied |
| `certificate_invalid` | stored certificate could not be used |
| `certificate_generation_failed` / `generation_failed` | bundle generation failed |
| `internal_error` | unexpected platform error |

`detail` is truncated to 256 characters (`protected_update_mqtt_bridge.py:1725`).

---

## 5. Sequence

```
device                                    platform
  |                                          |
  |-- CONNECT (mTLS, keepalive 60) --------->|
  |-- SUBSCRIBE device/{id}/commands/# ----->|   <- BEFORE requesting
  |                                          |
  |-- PUB .../commands/request ------------->|   {"request_type":"protected_update",
  |    {client_type:"mcu", csr:"..."}        |    "client_type":"mcu", ...}
  |                                          |
  |<-- .../commands/status ------------------|   {"status":"csr_queued"}
  |<-- .../commands/protected_update --------|   manifest + fragment_0..n   (mcu)
  |    or .../commands/manifest + fragment   |   (linux/rpi)
  |<-- .../commands/status ------------------|   {"status":"manifest_published"}
  |                                          |
  |-- PINGREQ every <60s -------------------->|  <- or the session dies at 90s
  |                                          |     (silence here = nothing is
  |                                          |      servicing the connection)
  |                                          |
  [ feed manifest + fragments to OPTIGA ]
```

---

## 6. Things that will bite you

**Subscribe before you request.** Responses are published with `retain=false`
(`protected_update_service.py:1357`, `protected_update_mqtt_bridge.py:1730`).
A reply sent before your SUBSCRIBE completes is gone.

**Retained messages are cleared on every connect.** The platform wipes retained
messages on `commands/{protected_update,certificate,manifest,fragment,pubkey,status}`
the moment your device connects, so a reconnect cannot replay a stale bundle
(`protected_update_mqtt_bridge.py:386-401`). Never rely on retained delivery.

**A dropped session loses the bundle.** Bundle generation is asynchronous, so the
session has to outlive it. If nothing services the connection, the broker closes it
at 1.5 × keepalive and the reply is published into nothing. This is the single most
common failure we have seen.

Note what "no `PINGREQ`" does and does not tell you: it says nothing is servicing the
connection, not that the loop is missing. A crashed client task looks exactly the
same from the broker. On this project it *was* a crashed task — a FreeRTOS stack
overflow one line after `Connected to broker`. See the CSR contract §5.3.

**`client_type` decides the wire format.** Omitting it silently selects
multi_topic.

**Topics use the device_id UUID.** Not the Trust M UID, which is your MQTT
client id. The ACL builds permitted prefixes from the UUID only.

---

## 7. Checklist before you say "Protected Update isn't working"

1. Did you send `"client_type": "mcu"`? (a bare `protected_update` string does not)
2. Are you listening on the topic your protocol actually uses —
   `commands/protected_update` for single_bundle, `commands/manifest` +
   `commands/fragment` for multi_topic?
3. Does the topic use the **device_id UUID**, not the Trust M UID?
4. Did you SUBSCRIBE to `commands/#` *before* publishing the request?
5. Is the session still alive when the bundle arrives — and if not, is the loop
   missing or has the task running it died? (check the device log, not the broker)
6. What did `commands/status` say? `manifest_published` means the platform sent it;
   anything else is a reason, listed in §4.3.
7. Are you reading `fragment_count` / `is_final` rather than assuming two fragments?
8. If you expected the platform to use a stored certificate, does one exist? No
   certificate and no inline `csr` gives `certificate_not_found`.

If all eight hold and the status says `manifest_published` but nothing usable
arrived, ask the platform team — the correlation id makes it traceable end to end.

---

## 8. Related device commands

Same `device/{device_id}/commands/...` namespace, all permitted to publish by
the ACL (`emqx_auth.py:618-640`):

| topic | purpose | response topic |
|---|---|---|
| `commands/check_certificate` | does the platform hold a certificate for me? | `commands/check_certificate_response` — `{"success":..,"status":..,"has_certificate":bool}` |
| `commands/upload_certificate` | push the device's certificate to the platform | `commands/upload_certificate_response` |
| `commands/sync_certificate` | unified certificate synchronisation (v0.5.51) | `commands/sync_certificate_response` |
| `commands/csr` | submit a CSR — see the CSR contract | manifest / fragment / status |
| `commands/init` | MQTT slot initialisation (v0.5.54 workaround) | — |

---

## Source index

| topic | file |
|---|---|
| Request schema, defaults, validators | `src/python/api/models/protected_update_schema.py:266-331` |
| Request handling, status codes, retained clearing | `src/python/api/services/protected_update_mqtt_bridge.py` |
| Protocol selection, bundle publishing | `src/python/api/services/protected_update_service.py:119-153, 1186-1387` |
| ACL rules | `src/python/api/controllers/emqx_auth.py:604-640` |
