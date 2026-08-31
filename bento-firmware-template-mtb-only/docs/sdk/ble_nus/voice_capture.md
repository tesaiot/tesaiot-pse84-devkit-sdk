# voice_capture.h

Microphone capture → `bento.voice.chunk` NUS event emitter. STUB. The real I²S mic capture path is not yet wired (the AI Kit / Eva Kit PDM-PCM pipeline lands in a follow-up change). For now the stub emits a single synthetic 1-second silence clip split across `bento.voice.chunk` frames so the desktop side can validate its end-to-end parser before a real mic is hooked up. Shape of each outbound frame: {"cmd":"bento.voice.chunk","id":"<clip>", "chunk_index":0,"pcm_base64":"<b64>", "sample_rate":16000,"final":false} The last frame in a clip carries `"final":true` with an empty pcm_base64. Control entry points: voice_capture_start(id, sample_rate) voice_capture_stop()

## Functions (exported by the archive)

### `voice_capture_is_running`

```c
bool voice_capture_is_running(void);
```

True iff a capture is currently running. Exposed so the UI page can toggle its mic-button label.

### `voice_capture_start`

```c
int voice_capture_start(const char *id, uint32_t sample_rate_hz);
```

File Name: voice_capture.h Description: Microphone capture → `bento.voice.chunk` NUS event emitter. STUB. The real I²S mic capture path is not yet wired (the AI Kit / Eva Kit PDM-PCM pipeline lands in a follow-up change). For now the stub emits a single synthetic 1-second silence clip split across `bento.voice.chunk` frames so the desktop side can validate its end-to-end parser before a real mic is hooked up. Shape of each outbound frame: {"cmd":"bento.voice.chunk","id":"<clip>", "chunk_index":0,"pcm_base64":"<b64>", "sample_rate":16000,"final":false} The last frame in a clip carries `"final":true` with an empty pcm_base64. Control entry points: voice_capture_start(id, sample_rate) voice_capture_stop() / #ifndef VOICE_CAPTURE_H #define VOICE_CAPTURE_H #include <stdbool.h> #include <stddef.h> #include <stdint.h> #ifdef __cplusplus extern "C" { #endif /* Default sample rate reported in `bento.voice.chunk`. Matches the planned PDM decimation target and the desktop TTS output rate. Real capture paths may override when they land. */ #define VOICE_CAPTURE_DEFAULT_SAMPLE_RATE_HZ   (16000U) /* Number of audio samples carried per outbound chunk. 1600 samples at 16 kHz = 100 ms, which is small enough that the BLE link can keep up over a typical 244-byte NUS MTU without backpressure. */ #define VOICE_CAPTURE_SAMPLES_PER_CHUNK        (1600U) /* Start mic capture. `id` is the caller-supplied clip identifier that travels on every outbound `bento.voice.chunk` frame (typically matches what the desktop will see echoed back through STT). `sample_rate_hz` is advisory — the stub ignores it and uses the default. Returns 0 on success, -1 if capture was already running or if no link is available.

### `voice_capture_stop`

```c
void voice_capture_stop(void);
```

Stop mic capture. Idempotent. Emits a final `"final":true` chunk if a capture is active, so the desktop side can close out its accumulator.

## Constants

| Name | Value |
|---|---|
| `VOICE_CAPTURE_H` | `#include` |
| `VOICE_CAPTURE_DEFAULT_SAMPLE_RATE_HZ` | `(16000U)` |
| `VOICE_CAPTURE_SAMPLES_PER_CHUNK` | `(1600U)` |
