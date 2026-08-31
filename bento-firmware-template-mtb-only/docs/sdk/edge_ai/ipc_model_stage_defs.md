# ipc_model_stage_defs.h

> Configuration header — constants and macros only, no functions.

## Structs

### `ai_stage_header_t`

```c
typedef struct {
    uint32_t magic;                 /* AI_STAGE_MAGIC                        */
    uint32_t version;               /* AI_STAGE_VERSION                      */
    uint32_t model_offset;          /* flatbuffer start, from this struct    */
    uint32_t model_bytes;           /* flatbuffer length                     */
    uint32_t model_crc32;           /* over the flatbuffer bytes             */
    uint32_t arena_bytes;           /* tensor arena, or 0 to let CM55 probe  */
    uint32_t frame_bytes;           /* one input frame, e.g. 6 floats = 24   */
    uint32_t window_depth;          /* frames per inference, e.g. 100        */
    uint32_t window_stride;         /* frames advanced per inference, e.g 10 */
    uint32_t in_elements;           /* tensor input count, e.g. 600          */
    uint8_t  sensor;                /* AI_STAGE_SENSOR_*                     */
    uint8_t  class_count;           /* 1..AI_STAGE_MAX_CLASSES               */
    uint16_t period_ms;             /* natural output cadence, for the UI    */

    /* The two fields whose wrongness nothing downstream can detect. v1
     * collected sample_rate_hz in the packer, used it once to derive
     * period_ms, and DISCARDED it -- so the value a careful author supplied
     * reached no one, could not be audited, and could not be checked against
     * what the board actually feeds. axis_convention was worse: it existed
     * only as a comment in the packer's template that the packer deleted.
     *
     * Both are recorded now. sample_rate_hz is what the model was TRAINED at;
     * the firmware compares it to the rate it can supply and refuses a
     * mismatch rather than running at the wrong speed and being wrong quietly.
     *
     * DEEPCRAFT emits both facts already. The generated C carries the input
     * parameter's `frequency:` -- 50 for the Motion model, 16000 for audio --
     * so for a Studio-exported model neither field is a human's to type. */
    uint32_t sample_rate_hz;        /* training rate; 0 = undeclared          */
    uint8_t  axis_convention;       /* AI_STAGE_AXES_*; IMU models must say   */
    uint8_t  reserved0;
    uint16_t reserved1;

    /* Provenance. When a fielded model misclassifies, the artifact itself has
     * to be able to answer "which training run produced this". DEEPCRAFT emits
     * a 16-byte GUID per model (AIM_<NAME>_MODEL_ID in the generated header);
     * all-zero means none was supplied. */
    uint8_t  model_id[16];
    char     name[AI_STAGE_NAME_LEN];
    char     labels[AI_STAGE_MAX_CLASSES][AI_STAGE_LABEL_LEN];
    uint32_t header_crc32;          /* over every byte above this field      */} ai_stage_header_t;
```

### `ai_stage_sig_t`

```c
typedef struct {
    uint32_t magic;                 /* AI_STAGE_SIG_MAGIC                     */
    uint32_t alg;                   /* AI_STAGE_SIG_ALG_*                     */
    uint8_t  sig[64];               /* raw r||s, 32 bytes each                */
    uint8_t  key_id[8];             /* which signing key, for rotation        */
    uint32_t reserved;
    uint32_t footer_crc32;          /* over every byte above this field       */} ai_stage_sig_t;
```

## Constants

| Name | Value |
|---|---|
| `IPC_MODEL_STAGE_DEFS_H` | `/*` |
| `MODEL_LINK_CMD_LOAD_STAGED` | `(0x85u)` |
| `AI_STAGE_MAGIC` | `(0x47545342u)` |
| `AI_STAGE_VERSION` | `(2u)` |
| `AI_STAGE_ALIGN` | `(32u)` |
| `AI_STAGE_MAX_CLASSES` | `(8u)` |
| `AI_STAGE_LABEL_LEN` | `(48u)` |
| `AI_STAGE_NAME_LEN` | `(48u)` |
| `AI_STAGE_AXES_UNDECLARED` | `(0u)` |
| `AI_STAGE_AXES_DEEPCRAFT_XY_NEG` | `(1u)` |
| `AI_STAGE_AXES_RAW` | `(2u)` |
| `AI_STAGE_IMU_FEED_HZ` | `(50u)` |
| `AI_STAGE_SIG_NO_VERIFIER` | `(-10)` |
| `AI_STAGE_IMU_FRAME_FLOATS` | `(6u)` |
| `AI_STAGE_IMU_FRAME_BYTES` | `(AI_STAGE_IMU_FRAME_FLOATS` |
| `AI_STAGE_SENSOR_IMU` | `(0u)` |
| `AI_STAGE_SENSOR_RADAR` | `(1u)` |
| `AI_STAGE_SENSOR_MIC` | `(2u)` |
| `AI_STAGE_SIG_MAGIC` | `(0x47495342u)` |
| `AI_STAGE_SIG_ALG_ECDSA_P256` | `(1u)` |
