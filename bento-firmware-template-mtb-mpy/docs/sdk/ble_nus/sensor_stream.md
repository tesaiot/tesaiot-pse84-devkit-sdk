# sensor_stream.h

Periodic sensor sampler for the Bento Desktop Buddy bridge. Emits `bento.sensor.data` NUS events at a caller-specified interval. v1 supports a single active stream at a time; the ring-of-active-streams described in SPEC §5.3.2 is deferred until multi-sensor dashboards are needed. Reference: TESAIoT_PLAN/2026-4/Bento_Buddy/SPEC.md §5.3.2–§5.3.3.

## Functions (exported by the archive)

### `sensor_stream_dropped_count`

```c
uint32_t sensor_stream_dropped_count(void);
```

Number of samples dropped because the NUS TX queue refused to accept them (link disconnected or buffer full). Exposed for bento.info.board.

### `sensor_stream_init`

```c
int sensor_stream_init(void);
```

File Name: sensor_stream.h Description: Periodic sensor sampler for the Bento Desktop Buddy bridge. Emits `bento.sensor.data` NUS events at a caller-specified interval. v1 supports a single active stream at a time; the ring-of-active-streams described in SPEC §5.3.2 is deferred until multi-sensor dashboards are needed. Reference: TESAIoT_PLAN/2026-4/Bento_Buddy/SPEC.md §5.3.2–§5.3.3. / #ifndef SENSOR_STREAM_H #define SENSOR_STREAM_H #include <stdbool.h> #include <stddef.h> #include <stdint.h> #ifdef __cplusplus extern "C" { #endif /* One-time FreeRTOS task spawn. Safe to call multiple times — subsequent calls are no-ops. Invoked lazily the first time a stream is started.

### `sensor_stream_is_active`

```c
bool sensor_stream_is_active(void);
```

True iff a stream is currently active. Used by the firmware-update handler to return {"ok":false,"error":"busy"} when a flash attempt races a running stream.

### `sensor_stream_start`

```c
int sensor_stream_start(const char *id, uint32_t interval_ms);
```

Start streaming sensor id at the given interval (clamped to [10, 5000] ms). Replaces any in-progress stream (v1 single-stream). Returns 0 on success, -1 if id is not recognised.

### `sensor_stream_stop`

```c
void sensor_stream_stop(const char *id);
```

Stop the active stream. Idempotent.

### `sensor_stream_stop_all`

```c
void sensor_stream_stop_all(void);
```

Stop any active stream unconditionally. Idempotent. Called by the firmware- update flow on Y-approve so the flasher operates on a quiescent device.

## Constants

| Name | Value |
|---|---|
| `SENSOR_STREAM_H` | `#include` |
