/*******************************************************************************
 * File Name: modcan.c
 *
 * Description: MicroPython 'can' module for CM33_NS (QWA309 training base board).
 *
 *              Classic CAN 2.0A @ 500 kbps over CANFD0 channel 1
 *              (P16.2 = RX, P16.3 = TX) into an SN65HVD230 transceiver on the
 *              QWA309 base board. Pure-code peripheral bring-up: HSIOM + PDL,
 *              no Device Configurator changes. Adapted from the tested
 *              canbus_test example (reference/qwa309-training-base).
 *
 *              The RX path uses an ISR-filled software ring buffer so that
 *              recv() can drain frames without loss between polls.
 *
 * Usage:
 *   import can
 *   can.init()                       # 500 kbps (only supported bitrate)
 *   can.send(0x123, b'\x01\x02\x03') # standard 11-bit ID, 0..8 data bytes
 *   frame = can.recv()               # -> (id, bytes) or None if empty
 *   can.deinit()
 *
 * Gated by BSP_HAS_QWA309_BASEBOARD (base board carries the CAN transceiver).
 *******************************************************************************/

#include "bsp_feature_flags.h"

#if BSP_HAS_QWA309_BASEBOARD

#include "py/runtime.h"
#include "py/obj.h"
#include "py/mphal.h"

#include "cybsp.h"
#include "cy_pdl.h"
#include "cy_canfd.h"
#include "gpio_pse84_bga_220.h"

#include <string.h>

/*******************************************************************************
 * CANFD target instance / channel
 *******************************************************************************/
#define CANBUS_HW              CANFD0
#define CANBUS_CHANNEL         (1U)
#define CANBUS_CHANNEL_MASK    (1UL << CANBUS_CHANNEL)
#define CANBUS_IRQ_LINE0       (canfd_0_interrupts0_1_IRQn)
#define CANBUS_PCLK_DST        (PCLK_CANFD0_CLOCK_CAN_EN1)

/* ---- Pin assignment ------------------------------------------------------- */
#define CANBUS_RX_PORT         GPIO_PRT16
#define CANBUS_RX_PIN          (2U)
#define CANBUS_RX_HSIOM        P16_2_CANFD0_TTCAN_RX1
#define CANBUS_TX_PORT         GPIO_PRT16
#define CANBUS_TX_PIN          (3U)
#define CANBUS_TX_HSIOM        P16_3_CANFD0_TTCAN_TX1

/* ---- Peripheral clock divider (100 MHz CAN clock -> keep divide-by-1) ------ */
#define CANBUS_DIV_TYPE        (CY_SYSCLK_DIV_8_BIT)
#define CANBUS_DIV_NUM         (4U)
#define CANBUS_DIV_VALUE       (0U)        /* register field = (divide_by - 1) */

/* ---- Bitrate (500 kbps from 100 MHz CAN clock) ----------------------------
 * bitrate = clock / (prescaler+1) / (1 + (ts1+1) + (ts2+1))
 *         = 100M / 10 / 20 = 500_000
 * sample point = (1 + ts1+1) / 20 = 16/20 = 80%
 * -------------------------------------------------------------------------- */
#define CANBUS_BITRATE_PRESCALER   (10U - 1U)
#define CANBUS_BITRATE_TS1         (15U - 1U)
#define CANBUS_BITRATE_TS2         (4U - 1U)
#define CANBUS_BITRATE_SJW         (4U - 1U)

#define CAN_SUPPORTED_BITRATE      (500000U)

/* ---- TX buffer index ------------------------------------------------------ */
#define CAN_TX_BUFFER_IDX      (0U)

/* ---- Message RAM ---------------------------------------------------------- */
#define CANBUS_MRAM_ADDRESS    (CY_CAN0MRAM_BASE + 0U)
#define CANBUS_MRAM_SIZE       (4096U)

/* ---- Interrupt sources dispatched through callbacks ----------------------- */
#define CANBUS_INT_MASK    ( CY_CANFD_RX_FIFO_0_NEW_MESSAGE \
                           | CY_CANFD_TRANSMISSION_COMPLETE \
                           | CY_CANFD_BUS_OFF_STATUS        \
                           | CY_CANFD_WARNING_STATUS        \
                           | CY_CANFD_ERROR_PASSIVE )

/* ---- Software RX ring buffer (filled from ISR) ---------------------------- */
#define CAN_RX_RING_DEPTH      (16U)

typedef struct {
    uint32_t id;
    uint8_t  dlc;               /* 0..8 (Classic CAN) */
    uint8_t  data[8];
} can_frame_t;

static volatile can_frame_t g_rx_ring[CAN_RX_RING_DEPTH];
static volatile uint32_t    g_rx_head = 0U;   /* producer (ISR) */
static volatile uint32_t    g_rx_tail = 0U;   /* consumer (recv) */
static volatile uint32_t    g_rx_dropped = 0U;

static volatile uint32_t    g_tx_count  = 0U;
static volatile uint32_t    g_err_latch = 0U;

static bool g_can_initialized = false;

static cy_stc_canfd_context_t g_canfd_ctx;

/* TX buffer storage */
static cy_stc_canfd_t0_t        g_t0;
static cy_stc_canfd_t1_t        g_t1;
static uint32_t                 g_tx_data_words[2];
static cy_stc_canfd_tx_buffer_t g_tx_buf = {
    .t0_f        = &g_t0,
    .t1_f        = &g_t1,
    .data_area_f = g_tx_data_words,
};

/*******************************************************************************
 * Callbacks (called from CAN ISR -- keep them quick, no printf)
 *******************************************************************************/
static void canfd_tx_callback(void)
{
    g_tx_count++;
}

static void canfd_rx_callback(bool rxFIFOMsg, uint8_t bufOrFifoNum,
                              cy_stc_canfd_rx_buffer_t *basemsg)
{
    (void)rxFIFOMsg;
    (void)bufOrFifoNum;
    if (basemsg == NULL || basemsg->r0_f == NULL || basemsg->r1_f == NULL) {
        return;
    }

    uint32_t head = g_rx_head;
    uint32_t next = (head + 1U) % CAN_RX_RING_DEPTH;
    if (next == g_rx_tail) {
        /* Ring full: drop the INCOMING frame. Do NOT advance g_rx_tail here —
         * it is owned by the consumer recv(); writing it from the ISR would race
         * the reader (both writing tail) and corrupt the SPSC ring. */
        g_rx_dropped++;
        return;
    }

    uint32_t dlc = basemsg->r1_f->dlc;
    if (dlc > 8U) {
        dlc = 8U;   /* Classic CAN 2.0 caps at 8 data bytes */
    }

    g_rx_ring[head].id  = basemsg->r0_f->id;
    g_rx_ring[head].dlc = (uint8_t)dlc;

    const uint8_t *src = (const uint8_t *)basemsg->data_area_f;
    for (uint32_t i = 0U; i < 8U; i++) {
        g_rx_ring[head].data[i] = (i < dlc) ? src[i] : 0U;
    }

    g_rx_head = next;
}

static void canfd_error_callback(uint32_t errorMask)
{
    g_err_latch |= errorMask;
}

static void canfd_isr_handler(void)
{
    Cy_CANFD_IrqHandler(CANBUS_HW, CANBUS_CHANNEL, &g_canfd_ctx);
}

/*******************************************************************************
 * CANFD config structures
 *******************************************************************************/
static const cy_stc_canfd_bitrate_t g_canfd_nominal_bitrate = {
    .prescaler     = CANBUS_BITRATE_PRESCALER,
    .timeSegment1  = CANBUS_BITRATE_TS1,
    .timeSegment2  = CANBUS_BITRATE_TS2,
    .syncJumpWidth = CANBUS_BITRATE_SJW,
};

static const cy_stc_canfd_bitrate_t g_canfd_fast_bitrate = {
    .prescaler     = CANBUS_BITRATE_PRESCALER,
    .timeSegment1  = CANBUS_BITRATE_TS1,
    .timeSegment2  = CANBUS_BITRATE_TS2,
    .syncJumpWidth = CANBUS_BITRATE_SJW,
};

static const cy_stc_canfd_transceiver_delay_compensation_t g_canfd_tdc = {
    .tdcEnabled = false, .tdcOffset = 0U, .tdcFilterWindow = 0U,
};

static const cy_stc_canfd_sid_filter_config_t g_canfd_sid_filter_cfg = {
    .numberOfSIDFilters = 0U, .sidFilter = NULL,
};

static const cy_stc_canfd_extid_filter_config_t g_canfd_xid_filter_cfg = {
    .numberOfEXTIDFilters = 0U, .extidFilter = NULL,
    .extIDANDMask = 0x1FFFFFFFUL,
};

static const cy_stc_canfd_global_filter_config_t g_canfd_global_filter_cfg = {
    .nonMatchingFramesStandard  = CY_CANFD_ACCEPT_IN_RXFIFO_0,
    .nonMatchingFramesExtended  = CY_CANFD_ACCEPT_IN_RXFIFO_0,
    .rejectRemoteFramesStandard = false,
    .rejectRemoteFramesExtended = false,
};

static const cy_en_canfd_fifo_config_t g_canfd_rx_fifo0_cfg = {
    .mode                   = CY_CANFD_FIFO_MODE_BLOCKING,
    .watermark              = 0U,
    .numberOfFIFOElements   = 4U,
    .topPointerLogicEnabled = false,
};
static const cy_en_canfd_fifo_config_t g_canfd_rx_fifo1_cfg = {
    .mode                   = CY_CANFD_FIFO_MODE_BLOCKING,
    .watermark              = 0U,
    .numberOfFIFOElements   = 1U,
    .topPointerLogicEnabled = false,
};

static cy_stc_canfd_config_t g_canfd_config = {
    .txCallback         = canfd_tx_callback,
    .rxCallback         = canfd_rx_callback,
    .errorCallback      = canfd_error_callback,
    .canFDMode          = false,
    .bitrate            = &g_canfd_nominal_bitrate,
    .fastBitrate        = &g_canfd_fast_bitrate,
    .tdcConfig          = &g_canfd_tdc,
    .sidFilterConfig    = &g_canfd_sid_filter_cfg,
    .extidFilterConfig  = &g_canfd_xid_filter_cfg,
    .globalFilterConfig = &g_canfd_global_filter_cfg,
    .rxBufferDataSize   = CY_CANFD_BUFFER_DATA_SIZE_8,
    .rxFIFO1DataSize    = CY_CANFD_BUFFER_DATA_SIZE_8,
    .rxFIFO0DataSize    = CY_CANFD_BUFFER_DATA_SIZE_8,
    .txBufferDataSize   = CY_CANFD_BUFFER_DATA_SIZE_8,
    .rxFIFO0Config      = &g_canfd_rx_fifo0_cfg,
    .rxFIFO1Config      = &g_canfd_rx_fifo1_cfg,
    .noOfRxBuffers      = 1U,
    .noOfTxBuffers      = 1U,
    .messageRAMaddress  = CANBUS_MRAM_ADDRESS,
    .messageRAMsize     = CANBUS_MRAM_SIZE,
};

/*******************************************************************************
 * Peripheral bring-up (pure code)
 *******************************************************************************/
static void can_init_pins(void)
{
    cy_stc_gpio_pin_config_t tx_cfg = {
        .outVal    = 1U,
        .driveMode = CY_GPIO_DM_STRONG_IN_OFF,
        .hsiom     = CANBUS_TX_HSIOM,
    };
    (void)Cy_GPIO_Pin_Init(CANBUS_TX_PORT, CANBUS_TX_PIN, &tx_cfg);

    cy_stc_gpio_pin_config_t rx_cfg = {
        .outVal    = 0U,
        .driveMode = CY_GPIO_DM_HIGHZ,
        .hsiom     = CANBUS_RX_HSIOM,
    };
    (void)Cy_GPIO_Pin_Init(CANBUS_RX_PORT, CANBUS_RX_PIN, &rx_cfg);
}

static void can_init_clock(void)
{
    /* Must come BEFORE any CANFD register access. */
    Cy_SysClk_PeriGroupSlaveInit(CY_MMIO_CANFD0_PERI_NR,
                                 CY_MMIO_CANFD0_GROUP_NR,
                                 CY_MMIO_CANFD0_SLAVE_NR,
                                 CY_MMIO_CANFD0_CLK_HF_NR);

    Cy_SysClk_PeriPclkSetDivider(CANBUS_PCLK_DST, CANBUS_DIV_TYPE,
                                 CANBUS_DIV_NUM, CANBUS_DIV_VALUE);
    Cy_SysClk_PeriPclkAssignDivider(CANBUS_PCLK_DST, CANBUS_DIV_TYPE,
                                    CANBUS_DIV_NUM);
    Cy_SysClk_PeriPclkEnableDivider(CANBUS_PCLK_DST, CANBUS_DIV_TYPE,
                                    CANBUS_DIV_NUM);
}

static cy_en_canfd_status_t can_init_canfd(void)
{
    Cy_CANFD_EnableMRAM(CANBUS_HW, CANBUS_CHANNEL_MASK, 6U);

    cy_en_canfd_status_t st = Cy_CANFD_Init(CANBUS_HW, CANBUS_CHANNEL,
                                            &g_canfd_config, &g_canfd_ctx);
    if (CY_CANFD_SUCCESS != st) {
        return st;
    }

    Cy_CANFD_SetInterruptMask(CANBUS_HW, CANBUS_CHANNEL, CANBUS_INT_MASK);

    cy_stc_sysint_t can_int_cfg = {
        .intrSrc      = CANBUS_IRQ_LINE0,
        .intrPriority = 3U,
    };
    (void)Cy_SysInt_Init(&can_int_cfg, &canfd_isr_handler);
    NVIC_EnableIRQ(CANBUS_IRQ_LINE0);

    /* Exit init mode -> operational. */
    Cy_CANFD_ConfigChangesEnable(CANBUS_HW, CANBUS_CHANNEL);
    Cy_CANFD_TestModeConfig(CANBUS_HW, CANBUS_CHANNEL, CY_CANFD_TEST_MODE_DISABLE);
    Cy_CANFD_ConfigChangesDisable(CANBUS_HW, CANBUS_CHANNEL);

    return CY_CANFD_SUCCESS;
}

/*******************************************************************************
 * can.init(bitrate=500000)
 *******************************************************************************/
static mp_obj_t mod_can_init(size_t n_args, const mp_obj_t *args)
{
    uint32_t bitrate = CAN_SUPPORTED_BITRATE;
    if (n_args >= 1) {
        bitrate = (uint32_t)mp_obj_get_int(args[0]);
    }
    if (bitrate != CAN_SUPPORTED_BITRATE) {
        mp_raise_msg_varg(&mp_type_ValueError,
            MP_ERROR_TEXT("only %u bps supported"),
            (unsigned int)CAN_SUPPORTED_BITRATE);
    }

    if (g_can_initialized) {
        return mp_const_none;   /* idempotent */
    }

    /* Reset software state. */
    g_rx_head = 0U;
    g_rx_tail = 0U;
    g_rx_dropped = 0U;
    g_tx_count = 0U;
    g_err_latch = 0U;

    can_init_pins();
    can_init_clock();

    cy_en_canfd_status_t st = can_init_canfd();
    if (CY_CANFD_SUCCESS != st) {
        mp_raise_msg_varg(&mp_type_OSError,
            MP_ERROR_TEXT("CANFD init failed (status=%u)"), (unsigned int)st);
    }

    g_can_initialized = true;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_can_init_obj, 0, 1, mod_can_init);

/*******************************************************************************
 * can.send(id, data)
 *******************************************************************************/
static mp_obj_t mod_can_send(mp_obj_t id_obj, mp_obj_t data_obj)
{
    if (!g_can_initialized) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("can not initialized"));
    }

    uint32_t id = (uint32_t)mp_obj_get_int(id_obj);
    if (id > 0x7FFU) {
        mp_raise_msg(&mp_type_ValueError,
            MP_ERROR_TEXT("id must be an 11-bit standard id (0..0x7FF)"));
    }

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(data_obj, &bufinfo, MP_BUFFER_READ);
    if (bufinfo.len > 8U) {
        mp_raise_msg(&mp_type_ValueError,
            MP_ERROR_TEXT("data must be 0..8 bytes"));
    }

    g_t0.id  = id;
    g_t0.rtr = CY_CANFD_RTR_DATA_FRAME;
    g_t0.xtd = CY_CANFD_XTD_STANDARD_ID;
    g_t0.esi = CY_CANFD_ESI_ERROR_ACTIVE;

    g_t1.dlc = (uint32_t)bufinfo.len;
    g_t1.brs = false;
    g_t1.fdf = CY_CANFD_FDF_STANDARD_FRAME;
    g_t1.efc = false;
    g_t1.mm  = 0U;

    uint8_t *p = (uint8_t *)g_tx_data_words;
    memset(p, 0, sizeof(g_tx_data_words));
    if (bufinfo.len > 0U) {
        memcpy(p, bufinfo.buf, bufinfo.len);
    }

    cy_en_canfd_status_t st = Cy_CANFD_UpdateAndTransmitMsgBuffer(
            CANBUS_HW, CANBUS_CHANNEL, &g_tx_buf, CAN_TX_BUFFER_IDX, &g_canfd_ctx);
    if (CY_CANFD_SUCCESS != st) {
        mp_raise_msg_varg(&mp_type_OSError,
            MP_ERROR_TEXT("CAN send failed (status=%u)"), (unsigned int)st);
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(mod_can_send_obj, mod_can_send);

/*******************************************************************************
 * can.recv() -> (id, bytes) or None
 *******************************************************************************/
static mp_obj_t mod_can_recv(void)
{
    if (!g_can_initialized) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("can not initialized"));
    }

    if (g_rx_tail == g_rx_head) {
        return mp_const_none;   /* ring empty */
    }

    /* Snapshot the frame at tail (ISR only advances head; safe single reader). */
    uint32_t tail = g_rx_tail;
    can_frame_t frame;
    frame.id  = g_rx_ring[tail].id;
    frame.dlc = g_rx_ring[tail].dlc;
    memcpy(frame.data, (const void *)g_rx_ring[tail].data, sizeof(frame.data));
    g_rx_tail = (tail + 1U) % CAN_RX_RING_DEPTH;

    mp_obj_t tuple[2];
    tuple[0] = mp_obj_new_int(frame.id);
    tuple[1] = mp_obj_new_bytes(frame.data, frame.dlc);
    return mp_obj_new_tuple(2, tuple);
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_can_recv_obj, mod_can_recv);

/*******************************************************************************
 * can.deinit()
 *******************************************************************************/
static mp_obj_t mod_can_deinit(void)
{
    if (!g_can_initialized) {
        return mp_const_none;
    }

    NVIC_DisableIRQ(CANBUS_IRQ_LINE0);
    (void)Cy_CANFD_DeInit(CANBUS_HW, CANBUS_CHANNEL, &g_canfd_ctx);

    g_can_initialized = false;
    g_rx_head = 0U;
    g_rx_tail = 0U;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_can_deinit_obj, mod_can_deinit);

/*******************************************************************************
 * can.stats() -> dict  (diagnostics; tx count, error latch, dropped rx)
 *******************************************************************************/
static mp_obj_t mod_can_stats(void)
{
    mp_obj_t dict = mp_obj_new_dict(3);
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_tx),
                      mp_obj_new_int_from_uint(g_tx_count));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_dropped),
                      mp_obj_new_int_from_uint(g_rx_dropped));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_errors),
                      mp_obj_new_int_from_uint(g_err_latch));
    return dict;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_can_stats_obj, mod_can_stats);

/*******************************************************************************
 * Module definition
 *******************************************************************************/
static const mp_rom_map_elem_t can_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_can) },
    { MP_ROM_QSTR(MP_QSTR_init),     MP_ROM_PTR(&mod_can_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_send),     MP_ROM_PTR(&mod_can_send_obj) },
    { MP_ROM_QSTR(MP_QSTR_recv),     MP_ROM_PTR(&mod_can_recv_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit),   MP_ROM_PTR(&mod_can_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_stats),    MP_ROM_PTR(&mod_can_stats_obj) },
};
static MP_DEFINE_CONST_DICT(can_module_globals, can_module_globals_table);

const mp_obj_module_t mp_module_can = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&can_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_can, mp_module_can);

#endif /* BSP_HAS_QWA309_BASEBOARD */
