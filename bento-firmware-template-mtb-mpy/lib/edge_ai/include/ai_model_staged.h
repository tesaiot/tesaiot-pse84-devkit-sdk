#ifndef AI_MODEL_STAGED_H
#define AI_MODEL_STAGED_H

/* A model built at run time from bytes CM33 staged in shared memory.
 *
 * The loader accepts one pipeline shape: a fixed window slid over a sensor
 * stream, quantized, inferred, dequantized. Motion models are that shape.
 * Audio and radar models are not — their feature front ends are ordered code
 * and coefficient tables that no .tflite carries — and the loader cannot
 * detect the difference, so the limit is stated here rather than discovered
 * as a wrong answer on screen. Load only IMU models; the checks below refuse
 * the cases they can see.
 *
 * Each loaded model owns one slot, and slots are not reused within a boot: a
 * descriptor a slot has published, and the strings it points at, stay valid
 * for the life of the boot.
 *
 * The staging ABI — the manifest layout these bytes must follow — is
 * ipc_model_stage_defs.h. */

#include <stdbool.h>
#include <stdint.h>

/* How many staged models can be built in one boot. A slot is claimed for the
 * life of the boot, exactly as a registry row is. */
#define AI_STAGED_MAX  (24u)

/* Build a model from the manifest at `header_addr` and register it.
 *
 * Returns the new registry index, or a negative code:
 *   -1  bad manifest (magic, version, CRC, bounds, alignment, not TFL3)
 *   -2  no free slot, OR the registry refused the row — which it also does for
 *       a DUPLICATE MODEL NAME, so loading the same file twice looks exactly
 *       like hitting the ceiling
 *   -3  mtb_ml_model_init() failed — usually an arena too small for the model,
 *       but also plain CM55 heap exhaustion
 *   -4  sensor not supported by this loader (only IMU: window + quantize)
 *   -5  axis convention absent or not the one this firmware applies
 *   -6  training sample rate is not the rate this firmware can feed
 *   -7  frame width is not the six floats this firmware supplies. A
 *       nine-channel export satisfies every other check and then reads past
 *       the caller's stack array on every frame
 *
 * These are the LOADER's codes, reported by diag()['staged_last_rc']. Signature
 * verdicts are a different namespace on a different field — stage_info()
 * ['sig_rc'] — and deliberately do not overlap these values.
 *
 * Runs in TASK CONTEXT ONLY. It allocates from the CM55 heap that LVGL also
 * draws from, and it takes a critical section. An IPC callback must queue this
 * call, not make it. */
int ai_model_staged_load(uint32_t header_addr);

/* Counters, for diagnosis off-core. A refused load that looked like a
 * completed one would make any staging measurement unreadable. */
/* Free bytes on the CM55 heap, including unclaimed sbrk headroom. */
uint32_t ai_model_staged_heap_free(void);

uint32_t ai_model_staged_count(void);
uint32_t ai_model_staged_rejects(void);
int32_t  ai_model_staged_last_rc(void);

#endif /* AI_MODEL_STAGED_H */
