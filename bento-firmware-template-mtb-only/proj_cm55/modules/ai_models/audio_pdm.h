/*******************************************************************************
 * File Name        : audio_pdm.h
 *
 * Description      : PDM microphone front-end for the Edge AI "audio" variant
 *                    (DEEPCRAFT Baby Cry Detection). Ported from Infineon's
 *                    mtb-example-psoc-edge-ml-deepcraft-deploy-audio
 *                    (proj_cm55/audio.c) and adapted to the BENTO AI Kit BSP.
 *
 *                    Compiled ONLY for the audio variant, guarded behind
 *                    BENTO_HAS_EDGE_AI + EDGE_AI_MODEL_audio (mirrors how the
 *                    radar feed is guarded in ai_engine.c). For every other
 *                    model the whole translation unit is empty, so the default
 *                    motion build never pulls the PDM peripheral in.
 *
 * Hardware         : PDM0, channel 3 (right), 16-bit words. These symbols come
 *                    from the AI Kit design.modus (CYBSP_PDM_HW / channel_3_config
 *                    / CYBSP_PDM_CHANNEL_3_IRQ) and match the decimation the
 *                    reference example was trained against (CIC/32, FIR1/3).
 *******************************************************************************/

#ifndef AUDIO_PDM_H_
#define AUDIO_PDM_H_

#include "bsp_feature_flags.h"

#if defined(BENTO_HAS_EDGE_AI) && (BENTO_HAS_EDGE_AI == 1) && \
    (defined(EDGE_AI_MODEL_audio) || defined(EDGE_AI_MODEL_cough))

#include <stdint.h>
#include "cy_pdl.h"

/* Samples per audio frame — one ping-pong buffer. Matches the reference's
 * FRAME_SIZE so the model sees the block length it was trained on. */
#define AUDIO_PDM_FRAME_SIZE            (1024)

/* Initialise and start PDM0 channel 3 and its RX-trigger interrupt. Safe to
 * call once; returns CY_RSLT_SUCCESS on success. */
cy_rslt_t audio_pdm_init(void);

/* If a full frame is ready, copy it out (atomically w.r.t. the ISR) and return
 * a pointer to AUDIO_PDM_FRAME_SIZE int16 samples; otherwise return NULL. The
 * flag is cleared on a successful read. */
const int16_t *audio_pdm_get_frame(void);

/** PDM frames the capture ISR overwrote because the consumer had not collected
 *  the previous one. Non-zero means audio was lost -- and therefore that every
 *  window computed since spans a hole. Zero on a healthy board. */
extern volatile uint32_t pdm_frames_dropped;

#endif /* BENTO_HAS_EDGE_AI && EDGE_AI_MODEL_audio */
#endif /* AUDIO_PDM_H_ */
