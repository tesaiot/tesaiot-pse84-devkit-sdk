# libbento_edge_ai.a — edge_ai

Edge AI inference engine: the model registry, the sensor feed router that keeps several ready models running at once, and the run-time loader that builds a model from a staged manifest. Prebuilt static archive.

## Verify before you use it

```bash
./verify.sh
```

## ABI — cm55 only

Built v8.1-M.mainline, FPv5-D16, **hardfp**, MVE, GCC_ARM. The two cores are not ABI-compatible; linking this into
the other core's image fails at the link step rather than at run time, which is
the good outcome.

## Add it to a ModusToolbox project

In `proj_cm55/Makefile`:

```make
BENTO_DIST := /abs/path/to/dist/edge_ai
INCLUDES += $(BENTO_DIST)/include
LDLIBS   += $(BENTO_DIST)/COMPONENT_CM55/COMPONENT_HARDFP/TOOLCHAIN_GCC_ARM/libbento_edge_ai.a
```

`LDLIBS`, not `LDFLAGS` — an archive placed before the objects that need it
is scanned once, finds nothing undefined yet, and is dropped.

Of the symbols listed under `consumer_must_provide.txt` below, **12 are
model slots, not work items**: every `AIM_<MODEL>_init/enqueue/dequeue/finalize`
is the entry point of one DEEPCRAFT model, and this template already provides
them in `proj_cm55/modules/ai_models/model_<model>.c`. They appear in the list
because the archive references them and does not define them — which is the
list's literal meaning — but a build of this template links them without you
writing anything. You only supply your own if you are adding a model of your
own; `proj_cm55/modules/ai_models/README.md` §"Adding a model" is the
procedure.

The model code itself is not ours to license. Each
`proj_cm55/modules/ai_models/model_<model>.c` is a **DEEPCRAFT™ Studio**
export, *Copyright © 2023– Imagimob AB, an Infineon Technologies company*,
included under Infineon's terms for research and teaching rather than commercial
deployment. `THIRD_PARTY_NOTICES.md` §2.2 carries the full terms; read it
before shipping anything built on these models.

## Two different lists — do not confuse them

**`consumer_must_provide.txt`** — symbols the archive references and does
**not** define at all. Your link fails without them. This build has
58:

    AIM_AUDIO_dequeue
    AIM_AUDIO_enqueue
    AIM_AUDIO_finalize
    AIM_AUDIO_init
    AIM_MOTION_dequeue
    AIM_MOTION_enqueue
    AIM_MOTION_finalize
    AIM_MOTION_init
    AIM_RADAR_dequeue
    AIM_RADAR_enqueue
    AIM_RADAR_finalize
    AIM_RADAR_init
    Cy_IPC_Pipe_SendMessage
    Cy_SysClk_ClkHfGetFrequency
    Cy_SysLib_DelayUs
    ETHOSU_PMU_Enable
    IMAI_ALARM_dequeue
    IMAI_ALARM_enqueue
    IMAI_ALARM_finalize
    IMAI_ALARM_init
    IMAI_COUGH_dequeue
    IMAI_COUGH_enqueue
    IMAI_COUGH_finalize
    IMAI_COUGH_init
    IMAI_SIREN_dequeue
    IMAI_SIREN_enqueue
    IMAI_SIREN_finalize
    IMAI_SIREN_init
    __HeapLimit
    __aeabi_uldivmod
    aligned_alloc
    audio_pdm_get_frame
    audio_pdm_init
    free
    ipc_sensorhub_snapshot
    mallinfo
    malloc
    memcpy
    memmove
    memset
    mtb_ml_deinit
    mtb_ml_ethosu_driver_handle
    mtb_ml_init
    mtb_ml_model_deinit
    mtb_ml_model_get_input_size
    mtb_ml_model_init
    mtb_ml_model_run
    mtb_ml_npu_cycles
    radar_ai_frame_next
    sbrk
    strcmp
    strncpy
    uxTaskGetStackHighWaterMark
    vPortEnterCritical
    vPortExitCritical
    vTaskDelay
    xTaskCreate
    xTaskGetTickCount

**`overridable.txt`** — symbols the archive defines **weakly**. The archive
links and runs without you doing anything; the stubs simply do nothing useful.
Define your own and the linker prefers yours:

| Symbol |
|---|

This archive defines no weak symbols, so there is nothing to override.

## API

`api.txt` is the public exported set, read off the shipped binary rather than
maintained by hand. ~150 internal names are renamed to `bx_N` and hidden. A
handful of `bx_N` remain ELF-global because one object in the archive calls
another; they are excluded from `api.txt` and are not callable API.



## What this does not hide

`objdump -d` disassembles it, and 0 format strings survive in
`.rodata` as plain text. Obfuscation raises the cost of extraction; it does
not prevent it, and nothing in the firmware gates use behind it: the
OPTIGA-UID licence check is compiled into no core image — `tesaiot_license.c`
is `CY_IGNORE`d by both `proj_cm33_ns/Makefile:87` and
`proj_cm55/Makefile:179-184`, and `tesaiot_is_licensed` is in none of the three
Release ELFs (`arm-none-eabi-nm`, 2026-08-29). Use is restricted by the licence
agreement, which is a contractual boundary, not an enforced one.
