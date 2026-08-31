/*******************************************************************************
 * File Name        : audio_pdm.c
 *
 * Description      : PDM microphone front-end for the Edge AI "audio" variant.
 *                    Ping-pong capture in the PDM RX-trigger ISR; the inference
 *                    task pulls a full frame with audio_pdm_get_frame() and
 *                    streams it, one normalized float per enqueue, into the
 *                    DEEPCRAFT model (see ai_engine.c feed_audio()).
 *
 *                    Data path is faithful to Infineon's reference
 *                    (proj_cm55/audio.c): raw int16 PDM samples, ping-pong
 *                    buffers filled RX_FIFO_TRIG_LEVEL at a time, no scaling
 *                    here — normalization to [-1,1] happens at enqueue in
 *                    ai_engine.c exactly as the reference's pdm_data_process().
 *******************************************************************************/

#include "audio_pdm.h"

#if defined(BENTO_HAS_EDGE_AI) && (BENTO_HAS_EDGE_AI == 1) && \
    (defined(EDGE_AI_MODEL_audio) || defined(EDGE_AI_MODEL_cough))

#include "cybsp.h"
#include "cy_pdl.h"
#include <string.h>

/******************************************************************************
 * Macros — from the reference audio.c, reconciled with the AI Kit BSP.
 *****************************************************************************/
/* PDM PCM interrupt priority (reference used 2). */
#define PDM_PCM_ISR_PRIORITY                    (2u)

/* Channel index — the AI Kit routes its microphone to PDM0 channel 3 (right),
 * exactly like the reference (CYBSP_PDM_CHANNEL_3_IRQ / channel_3_config). */
#define PDM_CHANNEL                             (3u)

/* PDM PCM hardware FIFO size. */
#define HW_FIFO_SIZE                            (64u)

/* Samples drained per RX-trigger interrupt. The AI Kit BSP configures
 * channel_3_config.rxFifoTriggerLevel = 31, so the trigger asserts with 32
 * words present; draining HW_FIFO_SIZE/2 (=32) per ISR consumes exactly that
 * and keeps FRAME_SIZE an integer multiple of the burst. */
#define RX_FIFO_TRIG_LEVEL                      (HW_FIFO_SIZE / 2)

/* Interrupts needed to accumulate one FRAME_SIZE frame. */
#define NUMBER_INTERRUPTS_FOR_FRAME             (AUDIO_PDM_FRAME_SIZE / RX_FIFO_TRIG_LEVEL)

/* Gain applied by the PDM block. Reference uses +5 dB for the baby-cry model. */
#define PDM_PCM_GAIN                            (CY_PDM_PCM_SEL_GAIN_5DB)

/******************************************************************************
 * Global Variables
 *****************************************************************************/
/* Ping-pong: one buffer fills in the ISR while the other can be processed. */
static int16_t  audio_buffer0[AUDIO_PDM_FRAME_SIZE] = {0};
static int16_t  audio_buffer1[AUDIO_PDM_FRAME_SIZE] = {0};
static int16_t *active_rx_buffer;
static int16_t *full_rx_buffer;

/* Detached copy handed to the consumer so a mid-processing ISR buffer flip can
 * never overwrite the frame under the model's feet (the reference read
 * full_rx_buffer directly and lived with that race). */
static int16_t  proc_buffer[AUDIO_PDM_FRAME_SIZE];

/* Set true by the ISR when full_rx_buffer holds a complete frame. */
volatile uint32_t pdm_frames_dropped = 0u;
static volatile bool pdm_pcm_flag;

/* PDM PCM interrupt configuration parameters. */
static const cy_stc_sysint_t PDM_IRQ_cfg =
{
    .intrSrc      = (IRQn_Type)CYBSP_PDM_CHANNEL_3_IRQ,
    .intrPriority = PDM_PCM_ISR_PRIORITY,
};

/******************************************************************************
 * PDM/PCM ISR — drains the FIFO into the active buffer; flips buffers and
 * raises the ready flag once a full frame is collected. Verbatim in behaviour
 * to the reference pdm_pcm_event_handler().
 *****************************************************************************/
static void pdm_pcm_event_handler(void)
{
    static uint16_t frame_counter = 0;

    uint32_t intr_status =
        Cy_PDM_PCM_Channel_GetInterruptStatusMasked(CYBSP_PDM_HW, PDM_CHANNEL);

    if (CY_PDM_PCM_INTR_RX_TRIGGER & intr_status)
    {
        for (uint32_t index = 0; index < RX_FIFO_TRIG_LEVEL; index++)
        {
            int32_t data = (int32_t)Cy_PDM_PCM_Channel_ReadFifo(CYBSP_PDM_HW, PDM_CHANNEL);
            active_rx_buffer[frame_counter * RX_FIFO_TRIG_LEVEL + index] = (int16_t)(data);
        }
        Cy_PDM_PCM_Channel_ClearInterrupt(CYBSP_PDM_HW, PDM_CHANNEL, CY_PDM_PCM_INTR_RX_TRIGGER);
        frame_counter++;
    }

    if ((NUMBER_INTERRUPTS_FOR_FRAME) <= frame_counter)
    {
        int16_t *temp   = active_rx_buffer;
        active_rx_buffer = full_rx_buffer;
        full_rx_buffer   = temp;

        /* Count the frame the consumer never collected. The flip is
         * unconditional and the ping-pong is only two deep, so missing the
         * 64 ms deadline overwrites a frame and punches a silent hole in the
         * audio -- which is exactly the discontinuity the parallel mode's
         * settle gate exists to prevent, arriving by a door nobody was
         * watching. Zero on a healthy board. */
        if (pdm_pcm_flag) {
            pdm_frames_dropped++;
        }
        pdm_pcm_flag  = true;
        frame_counter = 0;
    }

    if ((CY_PDM_PCM_INTR_RX_FIR_OVERFLOW | CY_PDM_PCM_INTR_RX_OVERFLOW |
         CY_PDM_PCM_INTR_RX_IF_OVERFLOW  | CY_PDM_PCM_INTR_RX_UNDERFLOW) & intr_status)
    {
        Cy_PDM_PCM_Channel_ClearInterrupt(CYBSP_PDM_HW, PDM_CHANNEL, CY_PDM_PCM_INTR_MASK);
    }
}

/******************************************************************************
 * Public API
 *****************************************************************************/
cy_rslt_t audio_pdm_init(void)
{
    cy_rslt_t result;

    memset(audio_buffer0, 0, sizeof(audio_buffer0));
    memset(audio_buffer1, 0, sizeof(audio_buffer1));
    active_rx_buffer = audio_buffer0;
    full_rx_buffer   = audio_buffer1;

    /* Initialize the PDM PCM block (AI Kit design.modus config). */
    result = Cy_PDM_PCM_Init(CYBSP_PDM_HW, &CYBSP_PDM_config);
    if (CY_PDM_PCM_SUCCESS != result)
    {
        return result;
    }

    Cy_PDM_PCM_Channel_Enable(CYBSP_PDM_HW, PDM_CHANNEL);
    Cy_PDM_PCM_Channel_Init(CYBSP_PDM_HW, &channel_3_config, (uint8_t)PDM_CHANNEL);

    Cy_PDM_PCM_SetGain(CYBSP_PDM_HW, PDM_CHANNEL, PDM_PCM_GAIN);

    Cy_PDM_PCM_Channel_ClearInterrupt(CYBSP_PDM_HW, PDM_CHANNEL, CY_PDM_PCM_INTR_MASK);
    Cy_PDM_PCM_Channel_SetInterruptMask(CYBSP_PDM_HW, PDM_CHANNEL, CY_PDM_PCM_INTR_MASK);

    result = Cy_SysInt_Init(&PDM_IRQ_cfg, &pdm_pcm_event_handler);
    if (CY_SYSINT_SUCCESS != result)
    {
        return result;
    }
    NVIC_ClearPendingIRQ(PDM_IRQ_cfg.intrSrc);
    NVIC_EnableIRQ(PDM_IRQ_cfg.intrSrc);

    pdm_pcm_flag = false;

    active_rx_buffer = audio_buffer0;
    full_rx_buffer   = audio_buffer1;

    Cy_PDM_PCM_Activate_Channel(CYBSP_PDM_HW, PDM_CHANNEL);

    return CY_RSLT_SUCCESS;
}

const int16_t *audio_pdm_get_frame(void)
{
    if (!pdm_pcm_flag)
    {
        return NULL;
    }

    /* Snapshot the completed frame with the PDM ISR masked so a buffer flip in
     * the middle of the copy cannot tear the frame. */
    uint32_t st = Cy_SysLib_EnterCriticalSection();
    pdm_pcm_flag = false;
    memcpy(proc_buffer, full_rx_buffer, sizeof(proc_buffer));
    Cy_SysLib_ExitCriticalSection(st);

    return proc_buffer;
}

#endif /* BENTO_HAS_EDGE_AI && (EDGE_AI_MODEL_audio || EDGE_AI_MODEL_cough) */
