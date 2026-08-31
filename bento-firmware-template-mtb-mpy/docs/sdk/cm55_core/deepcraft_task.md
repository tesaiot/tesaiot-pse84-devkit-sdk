# deepcraft_task.h

## Functions (exported by the archive)

### `deepcraft_task_init`

```c
bool deepcraft_task_init(void);
```

File Name        : deepcraft_task.h Description      : CM55 model-link peer — v1 STUB runtime for the DEEPCraft control plane (see common/deepcraft + common/bento_link in BENTO-TESAIoT-Claw-libraries). Per-project file (rule §2): each kit owns its model runtime; the wire contract lives in the shared header ipc_model_link_defs.h. / #ifndef DEEPCRAFT_TASK_H #define DEEPCRAFT_TASK_H #include <stdbool.h> #include <stdint.h> #ifdef __cplusplus extern "C" { #endif /** Create the model-link task + register the pipe client. Call AFTER cm55_ipc_communication_setup().

### `deepcraft_task_request`

```c
void deepcraft_task_request(bool start);
```

Ask the model link to start or stop, exactly as a MicroPython client would. The on-screen page uses this instead of poking the inference engine directly, so the link emits its usual READY / STOPPED events and everything downstream -- not least CM33 raising the accelerometer to the model's training rate -- happens for a tap just as it does for a script.

### `deepcraft_task_select`

```c
void deepcraft_task_select(uint32_t n);
```

Edge AI menu: activate registry model n (clean switch, no reload) exactly as a MicroPython edge_ai.select(n) does. On-screen parity with the dropdown on the Edge AI page — posts SELECT(0x90+n) on the model link's command queue.

### `deepcraft_task_watchdog`

```c
void deepcraft_task_watchdog(void);
```

Return the model-link task to Ready if it has stopped draining its command queue. Call at about 1 Hz from a context that never blocks. Does nothing on a healthy board; see the definition for what it does and does not fix.

## Constants

| Name | Value |
|---|---|
| `DEEPCRAFT_TASK_H` | `#include` |
