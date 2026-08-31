#ifndef IPC_MODEL_STAGE_DEFS_H
#define IPC_MODEL_STAGE_DEFS_H

/* Staging a downloaded model for CM55 — the contract both cores compile.
 *
 * ── Why a shared buffer and not an IPC stream ─────────────────────────────
 * A model arrives on CM33 over TACP and lands on LittleFS. CM55 cannot read
 * it there: the filesystem volume is 0x60C00000-0x63FFFFFF and CM55's only
 * external-flash grant is 0x60580000-0x60BBFFFF (cycfg_system.c, M55_mpc_regions),
 * so not one byte overlaps — and CM55 has no filesystem driver at all
 * (LV_USE_FS_* are 0). The widest reliable CM33->CM55 hop in this tree is 240
 * bytes, and bento_link's data plane is `return false;`.
 *
 * So the bytes travel through memory both cores already map. m33_m55_shared
 * (0x262FC000, 256 KB) is granted RW to PC5 (CM33_NS) and PC6 (CM55_NS) on the
 * same SOCMEM MPC block that already serves .ml_weights, and CM55's MPU region
 * covering it is Normal NON-CACHEABLE — ARM_MPU_ATTR_NON_CACHEABLE is the value
 * 4 that cycfg_system.c stores in a field named `cacheable`, which reads as the
 * opposite of what it means. CM33 has no data cache at all (__DCACHE_PRESENT 0).
 * Both facts together are why this needs no cache maintenance, and they are the
 * same reason the existing IPC windows need none.
 *
 * ── Why the parameters ride in the buffer, not in the message ─────────────
 * MODEL_LINK_OP_CTRL carries one command byte and one uint32. A model needs a
 * dozen numbers plus a name and its class labels. Rather than invent a
 * multi-message parameter protocol, the manifest sits at the head of the shared
 * buffer and the message carries only its address. One command, one pointer,
 * and CM55 reads the rest from memory it can already see.
 *
 * ── Alignment ─────────────────────────────────────────────────────────────
 * The Ethos-U driver rejects a command stream or base address that is not
 * 16-byte aligned (ethosu_driver.c, MASK_16_BYTE_ALIGN) and Vela pads the
 * command stream relative to the FLATBUFFER BASE, so the flatbuffer itself must
 * be aligned. We require 32 rather than 16: the CM55 cache-maintenance granule
 * is 32 bytes (__SCB_DCACHE_LINE_SIZE), and while this region is non-cacheable
 * today, a buffer that is exclusive on both granules cannot be broken by a
 * future MPU change. 32 satisfies both and costs at most 31 bytes.
 *
 * Alignment is NOT inherited from the linker. .cy_shared_socmem is declared
 * ALIGN(4) and its base in the current link is 0x26301A3C, which is 12 mod 16 —
 * the section's alignment is a function of HSM symbol sizes in an unrelated
 * translation unit. The staging code aligns its own pointer at run time and
 * reports it, rather than trusting a section. */

#include <stdint.h>

/* CTRL command: build a model from the manifest at .value.
 *
 * 0x85 sits beside UNLOAD (0x84) in the command space: above the intent range
 * (0x7F), clear of the DEEPCraft wire bytes 0x82/0x83, and below the SELECT
 * band at 0x90. It does NOT borrow a SELECT index — 0x90+6..12 is the
 * registry's declared growth room for KEYWORD/FALL/GESTURE. */
#define MODEL_LINK_CMD_LOAD_STAGED      (0x85u)

/* 'BSTG' little-endian. Checked before anything in the manifest is believed. */
#define AI_STAGE_MAGIC                  (0x47545342u)
#define AI_STAGE_VERSION                (2u)

/* Alignment the flatbuffer must sit on, and the manifest with it. */
#define AI_STAGE_ALIGN                  (32u)

/* Mirrors AI_MAX_CLASSES / AI_MAX_LABEL_LEN in ai_engine.h. Duplicated rather
 * than included because this header is compiled by CM33, which has no
 * ai_engine.h; the static asserts on the CM55 side tie them together. */
#define AI_STAGE_MAX_CLASSES            (8u)
/* 48 bytes, not 16.
 *
 * The limit is in BYTES and the labels are UTF-8, so 16 gave English fifteen
 * characters and Thai five. Measured against ordinary activity labels: เดิน
 * (walk, 12 B) fits, but กระโดด (jump), ปรบมือ (clap), โบกมือ (wave) and
 * อยู่นิ่ง (idle) are all 18-24 B and were refused, while their English
 * equivalents -- climbing_stairs is exactly 15 -- all passed. A byte limit is
 * not language-neutral; at 3 bytes per Thai character it is a threefold penalty
 * on the language this product is built in.
 *
 * 48 gives Thai sixteen characters, which covers the label sets we have seen.
 * The cost is 8 x 32 extra bytes per manifest and per IPC descriptor. */
#define AI_STAGE_LABEL_LEN              (48u)
/* 48, matching the wire's name field. 24 bytes was eight Thai characters. */
#define AI_STAGE_NAME_LEN               (48u)

/* How the IMU axes were oriented when the model was trained.
 *
 * feed_imu() negates X and Y to match the DEEPCRAFT reference rig, and until
 * now that transform lived only in ai_engine.c -- it appeared in no artifact,
 * not the .tflite, not the generated C, not the project file. A model trained
 * on a differently-mounted board therefore ran, produced confident output, and
 * was wrong, with nothing anywhere to compare against.
 *
 * Declaring it does not make the firmware adapt. It makes the mismatch a
 * REFUSAL instead of a silent misclassification, which is the whole point. */
#define AI_STAGE_AXES_UNDECLARED        (0u)  /* refused for IMU models       */
#define AI_STAGE_AXES_DEEPCRAFT_XY_NEG  (1u)  /* what feed_imu() applies      */
#define AI_STAGE_AXES_RAW               (2u)  /* board axes, no remap         */

/* The only IMU rate this firmware can feed. dc_desired_sensor_rate() asks CM33
 * for a 20 ms push when an IMU model is active, so a staged model must have
 * been trained at this rate or its window covers the wrong span of time. */
#define AI_STAGE_IMU_FEED_HZ            (50u)

/* The width of ONE frame the IMU path actually hands a model: feed_imu() builds
 * a `float in[6]` -- accel xyz then gyro xyz -- on its stack and passes that.
 *
 * A staged model declares its own frame width in the manifest, and until this
 * constant existed nothing compared the two. The check that did exist,
 * window_depth * frame_bytes == in_elements * 4, is an identity between three
 * declared numbers: it is satisfied by a consistent LIE. A genuine 9-channel
 * export -- accel + gyro + magnetometer, and this board carries a BMM350 --
 * declares 9 floats per frame, passes that identity, passes the input-tensor
 * check because the tensor really does have that many elements, and then the
 * copy reads 36 bytes out of a 24-byte stack array, fifty times a second.
 *
 * No adversary is needed for that: bento_deepcraft_import.py reads
 * frame_floats straight out of the DEEPCRAFT export, so the supported
 * toolchain produces it from an honest model. Nothing crashes -- the read is
 * in bounds of the stack frame, just past the array -- so the failure is
 * silently wrong answers, which is the worst shape this system has. */
/* Signature verdicts reported by stage_info()['sig_rc']. Positive 1 is "the
 * chip verified it"; everything else is a reason it did not.
 *
 * -10, not -6. The loader's own reject codes run -1..-7 and are reported
 * through a DIFFERENT field, diag()['staged_last_rc'] -- but -6 already means
 * "wrong sample rate" there, and the first version of this constant reused it.
 * Two fields, two namespaces, one number: the README ended up defining -6 twice
 * on the same page, sixty lines apart, and a reader looking at `sig_rc: -6`
 * had everything needed to conclude the rate was wrong. Numbers that mean two
 * things are cheap to avoid and expensive to debug. */
#define AI_STAGE_SIG_NO_VERIFIER        (-10)  /* no verifier on this board */

#define AI_STAGE_IMU_FRAME_FLOATS       (6u)
#define AI_STAGE_IMU_FRAME_BYTES        (AI_STAGE_IMU_FRAME_FLOATS * 4u)

/* Sensor feed the model consumes. Values match ai_sensor_t; asserted on CM55. */
#define AI_STAGE_SENSOR_IMU             (0u)
#define AI_STAGE_SENSOR_RADAR           (1u)
#define AI_STAGE_SENSOR_MIC             (2u)

/* Manifest, at offset 0 of the shared staging buffer. The flatbuffer follows at
 * model_offset, which must itself be a multiple of AI_STAGE_ALIGN.
 *
 * Every field here is something a downloaded .tflite cannot tell us:
 *
 *  - arena_bytes. NOT derivable from the file. Measured on this hardware
 *    2026-08-10: the Motion model's own OfflineMemoryAllocation extent is
 *    18,192 bytes and initialising with it FAILS (rc -2); the true floor is
 *    between 18,192 and 19,336. A loader that reads the plan and trusts it is
 *    wrong every time. Declare it -- or leave it 0 and CM55 will walk a ladder
 *    of sizes until one takes. Probing is safe because an undersized arena was
 *    measured to fail cleanly: error returned, engine idle, REPL alive, no
 *    fault. This is the one required field a human can safely forget.
 *  - the window geometry. A .tflite describes a tensor, not how a sensor
 *    stream becomes one. Motion's is frame 24 B, depth 100, stride 10.
 *  - class labels, which live in the generated C, not the model.
 *
 * This is the honest boundary of what this path can load: models whose
 * preprocessing is "slide a window, then quantize". Motion qualifies. Audio and
 * radar do NOT — their DSP graphs (Hann window, RFFT, mel filterbank, bin
 * selection) and kilobytes of coefficient tables are C code that no .tflite
 * carries. Do not extend this struct to pretend otherwise; a real answer there
 * is a described pipeline, not more fields. */
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
    uint32_t header_crc32;          /* over every byte above this field      */
} ai_stage_header_t;

_Static_assert(sizeof(ai_stage_header_t) % 4u == 0u,
               "manifest must stay word-sized so header_crc32 covers whole words");

/* ── Signature ─────────────────────────────────────────────────────────────
 * A TRAILER, after the flatbuffer, not a header field.
 *
 * The header is 504 bytes and the flatbuffer starts at 512 -- numbers that are
 * now proven on hardware, including the 32-byte alignment the Ethos-U requires
 * of a Vela command stream. Adding a field would move every one of them and
 * re-open a question that has been answered. A trailer changes nothing that
 * already works: an unsigned blob is byte-identical to what shipped before,
 * and the board tells them apart by looking past the model for the magic.
 *
 * CRC32 answers "did this arrive intact". It answers nothing about who sent
 * it -- anyone can recompute a CRC over anything. That distinction is the
 * whole reason this exists: mtb_ml_model_init() validates NOTHING (the shipped
 * libtensorflow-microlite.a exports zero Verify* symbols), so a malformed blob
 * becomes wild pointer arithmetic inside AllocateTensors on the core that owns
 * the display, and CM55's fault handler disables interrupts and blinks until
 * someone pulls power. A signature is what makes "the store sent this" a
 * checkable claim rather than an assumption.
 *
 * ECDSA P-256 over SHA-256: the curve OPTIGA Trust M does in hardware, and the
 * one this project already signs and verifies with elsewhere.
 *
 * The signature covers every byte from offset 0 to the start of this trailer
 * -- manifest and model together. Signing only the model would leave the
 * window geometry, the training rate and the axis convention unauthenticated,
 * and those are precisely the fields whose wrongness cannot be detected at
 * run time. */
#define AI_STAGE_SIG_MAGIC              (0x47495342u)  /* 'BSIG' */
#define AI_STAGE_SIG_ALG_ECDSA_P256     (1u)

typedef struct {
    uint32_t magic;                 /* AI_STAGE_SIG_MAGIC                     */
    uint32_t alg;                   /* AI_STAGE_SIG_ALG_*                     */
    uint8_t  sig[64];               /* raw r||s, 32 bytes each                */
    uint8_t  key_id[8];             /* which signing key, for rotation        */
    uint32_t reserved;
    uint32_t footer_crc32;          /* over every byte above this field       */
} ai_stage_sig_t;

_Static_assert(sizeof(ai_stage_sig_t) % 4u == 0u, "trailer must stay word-sized");

/* Where the trailer sits, given a manifest. Word-aligned so the CRC over it
 * covers whole words on both cores. */
static inline uint32_t ai_stage_sig_offset(const ai_stage_header_t *h)
{
    return (h->model_offset + h->model_bytes + 3u) & ~3u;
}

/* CRC-32/ISO-HDLC (reflected 0xEDB88320), the same polynomial the IDE already
 * uses. Table-free: this runs once per upload on CM33 and once per load on
 * CM55, so a 1 KB table would cost more than the loop saves.
 *
 * static inline in the shared header so both cores compute it from ONE source.
 * Two implementations of a checksum is how a checksum stops meaning anything. */
static inline uint32_t ai_stage_crc32(uint32_t crc, const uint8_t *p, uint32_t n)
{
    crc = ~crc;
    for (uint32_t i = 0u; i < n; i++) {
        crc ^= p[i];
        for (uint32_t b = 0u; b < 8u; b++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
        }
    }
    return ~crc;
}

#endif /* IPC_MODEL_STAGE_DEFS_H */
