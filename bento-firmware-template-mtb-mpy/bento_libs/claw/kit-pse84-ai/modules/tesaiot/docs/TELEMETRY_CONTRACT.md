# Telemetry Publishing — API Contract

**For:** firmware / device client teams
**From:** TESAIoT platform team
**Companion documents:** [CSR_SUBMISSION_CONTRACT.md](CSR_SUBMISSION_CONTRACT.md) ·
[PROTECTED_UPDATE_CONTRACT.md](PROTECTED_UPDATE_CONTRACT.md)
**Status of this document:** topics, envelope and storage path were read out of the
running code; the schema example is the one actually registered for the reference
device. Sources cited.
**Last verified:** 2026-08-05 against `develop` @ `742c5e5ce`

---

## Before you write any code

1. **Nothing appears on a dashboard until the device publishes.** Connecting and
   subscribing is not enough — a device that only subscribes shows up as online and
   produces no data at all. → §1
2. **Keep the session alive first.** With no `PINGREQ` the broker closes the session
   at 90 s (1.5 × keepalive). Telemetry published into a dead session is lost, and a
   graph built from one point every 90 s is not a graph. Fix the loop before you fix
   the payload. → §5
3. **Topics use the device_id UUID**, not the Trust M UID. → CSR contract §3
4. **Send the fields the platform registered for your device.** The schema is not a
   suggestion; it is what the dashboard is built from. → §3

---

## 1. How to tell whether a device has ever sent anything

Ask the platform team to check the broker log. What matters is the *direction*:

```
msg: mqtt_packet_received ... packet: PUBLISH   <- the device sent data
msg: mqtt_packet_sent     ... packet: PUBLISH   <- the BROKER sent something to the device
```

`$SYS/brokers/.../clients/<clientid>/connected` messages carry the device's client id
and are logged around its connection, but they are published *by EMQX*, not by the
device. Counting PUBLISH lines without checking direction makes a silent device look
like a talking one.

---

## 2. Topic

```
device/{device_id}/telemetry
```

The bridge also subscribes to these (`mqtt_telemetry_bridge.py:387-397`):

| topic | note |
|---|---|
| `device/{device_id}/telemetry` | the normal one |
| `device/{device_id}/telemetry/{sensor_type}` | per-sensor variant |
| `device/{device_id}/data/{something}` | |
| `device/{device_id}/status` | |
| `device/{device_id}/heartbeat` | |
| `devices/...` | plural forms of all of the above |

QoS 1. The ACL permits a device to publish `telemetry`, `status`, `events`,
`metrics` and their subtrees (`emqx_auth.py:618-640`).

---

## 3. Payload

A **flat JSON object of your telemetry fields**. Do not wrap it in an envelope — the
bridge builds the envelope for you (§4).

The fields come from the telemetry schema registered for your device in the platform.
For the reference device `905f31fa-92cb-4555-a8ae-f68a65e142fb` the registered schema
is *Healthcare Gateway Telemetry*:

| field | type | required | constraint |
|---|---|---|---|
| `gatewayId` | string | ✅ | |
| `connectedDevices` | integer | ✅ | ≥ 0 |
| `timestamp` | string | ✅ | |
| `hl7MessagesPerMin` | number | | |
| `fhirTransactions` | number | | |
| `dataIntegrity` | string | | **enum:** `verified` \| `pending` \| `error` |
| `hipaaCompliant` | boolean | | |
| `encryptionStatus` | string | | **enum:** `aes256` \| `tls13` \| `none` |
| `batteryBackup` | number | | 0–100 |
| `lastAuditLog` | string | | |

> The enums are closed sets. A value outside them is not "extra data", it fails
> validation.

**Sample — send exactly this shape:**

```json
{
  "gatewayId": "905f31fa-92cb-4555-a8ae-f68a65e142fb",
  "connectedDevices": 3,
  "timestamp": "2026-08-05T11:05:00Z",
  "hl7MessagesPerMin": 12.5,
  "fhirTransactions": 4.0,
  "dataIntegrity": "verified",
  "hipaaCompliant": true,
  "encryptionStatus": "tls13",
  "batteryBackup": 87.5,
  "lastAuditLog": "boot-ok"
}
```

If your device has a different schema registered, ask the platform team for it — or
read it from the bundle: the `telemetry/` folder in your device bundle contains
`data_telemetry.h` / `.c` generated from that exact schema, with the field names,
types and range checks already in C.

### 3.1 What the bridge tolerates

- **A nested object** is flattened before storage
  (`mqtt_telemetry_bridge.py:522`). Prefer flat; nested will not be rejected.
- **A field whose value is a string that looks like a dict** (`"{'a': 1}"`) is parsed
  back into an object — a workaround for firmware that stringifies structures
  (`mqtt_telemetry_bridge.py:60-99`). Do not rely on it; send real JSON.

---

## 4. What happens after you publish

```
device  --PUBLISH-->  EMQX  -->  mqtt-bridge  -->  POST /api/v1/telemetry  -->  TimescaleDB
```

The bridge takes the device id **from the topic**, not from your payload, and wraps
your object (`mqtt_telemetry_bridge.py:474, 532-545`):

```json
{
  "device_id": "<from the topic>",
  "timestamp": "<bridge's own UTC ISO timestamp>",
  "data": { ...your flat object... },
  "metadata": { "source": "mqtt_bridge_vault", "pki_enabled": true, ... }
}
```

Two consequences worth knowing:

- The **ingest timestamp is the bridge's**, not yours. Your `timestamp` field is kept
  inside `data` — useful for the device's own clock, but the series is ordered by the
  ingest time.
- The **device id comes from the topic**. Publishing to another device's topic is
  denied by the ACL, so you cannot submit data on behalf of another device.

---

## 5. Why a correct payload may still produce no graph

Publishing is the last step, not the first. In order:

1. **The MQTT session must survive.** No `PINGREQ` → the broker closes at 1.5 ×
   keepalive. Measured on the reference device: connected cleanly four times,
   `PINGREQ = 0` every time, dropped at 90–92 s every time.
2. **Then publish on an interval.** Every 10–30 s gives a usable series. One point per
   reconnect does not.

Both live in the same place — once the main loop exists, publishing goes inside it:

```c
while (1) {
    MQTT_ProcessLoop(&ctx, 1000);          /* sends PINGREQ on schedule */

    if (now_ms() - last_publish >= 10000) {  /* every 10 s */
        mqtt_publish(telemetry_topic, build_telemetry_json(), MQTT_QOS1);
        last_publish = now_ms();
    }
}
```

Equivalent calls: `loop_forever()` / `loop_start()` (paho), `check_msg()` +
`ping()` (umqtt), `MQTT_ProcessLoop()` (coreMQTT).

---

## 6. Checklist before you say "the graph is empty"

1. Has the device actually published? (`mqtt_packet_received` + `PUBLISH`, not `sent`)
2. Is the topic `device/{device_id-UUID}/telemetry`?
3. Does the payload carry every **required** field for your registered schema?
4. Are enum fields inside their allowed sets?
5. Is the session still alive between publishes — is `PINGREQ` being sent?
6. Are you publishing on an interval, not once per connection?

---

## Source index

| topic | file |
|---|---|
| Subscribed topics, envelope, ingestion endpoint | `src/python/mqtt_telemetry_bridge.py:387-397, 469-560` |
| Payload normalisation and flattening | `src/python/mqtt_telemetry_bridge.py:60-99, 522` |
| ACL — permitted publish types | `src/python/api/controllers/emqx_auth.py:618-640` |
| Per-device telemetry schema | MongoDB `devices.telemetrySchema.schema` |
| Generated C for your schema | `telemetry/data_telemetry.{h,c}` in the device bundle |
