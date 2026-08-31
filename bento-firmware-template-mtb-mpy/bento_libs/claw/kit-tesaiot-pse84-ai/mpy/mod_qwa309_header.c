/*******************************************************************************
 * File Name: mod_qwa309_header.c
 *
 * Description: MicroPython 'header' module for CM33_NS (QWA309 base board).
 *
 *              Exposes the QWA309 Arduino-style expansion header I/O that is
 *              native to the CM33_NS domain and lives on free ports:
 *
 *                - GPIO   : P13.0 / P13.3 / P13.4 / P13.5 / P13.6 / P13.7
 *                - PWM    : TCPWM0 counter 5 -> P13.3 (line5), P13.4 is the
 *                           hardware complement (line_compl5)
 *                - UART   : SCB9  P15.0 = RX, P15.1 = TX, 115200 8N1
 *
 *              Pure-code peripheral bring-up (Cy_GPIO / Cy_TCPWM / Cy_SCB_UART
 *              + PDL clock helpers), no Device Configurator changes. Pin/clock
 *              choices adapted from the tested qwa309_header_hw_test example
 *              (reference/qwa309-training-base) and the generated
 *              CYBSP_PWM_LED_CTRL TCPWM template in the AI Kit BSP.
 *
 * Usage:
 *   import header
 *
 *   # Digital GPIO (logical pin index 0..5 -> P13.0/3/4/5/6/7)
 *   header.gpio_out(0, 1)          # drive P13.0 high
 *   v = header.gpio_in(2)          # read P13.4 as input (pull-up)
 *
 *   # PWM on P13.3 (P13.4 = complementary)
 *   header.pwm(0, 50, 1000)        # 50 % duty, 1 kHz  (pin arg reserved = 0)
 *   header.pwm(0, 0, 1000)         # 0 % duty stops output low
 *
 *   # UART over SCB9 (115200 8N1)
 *   header.uart_write(b'hello\r\n')
 *   n = header.uart_any()          # bytes available in RX FIFO
 *   data = header.uart_read(n)     # read up to n bytes -> bytes
 *
 * Gated by BSP_HAS_QWA309_BASEBOARD.
 *******************************************************************************/

#include "bsp_feature_flags.h"

#if BSP_HAS_QWA309_BASEBOARD

#include "py/runtime.h"
#include "py/obj.h"
#include "py/mphal.h"

#include "cy_pdl.h"
#include "gpio_pse84_bga_220.h"

#include <string.h>

/*******************************************************************************
 * Header GPIO — logical index -> P13.x
 *  idx 0 = P13.0, 1 = P13.3, 2 = P13.4, 3 = P13.5, 4 = P13.6, 5 = P13.7
 * (P13.1/P13.2 are not exposed on the header, matching the reference layout.)
 *******************************************************************************/
#define HDR_GPIO_COUNT   (6U)

typedef struct {
    GPIO_PRT_Type *port;
    uint32_t       pin;
    const char    *name;
} hdr_gpio_t;

static const hdr_gpio_t s_gpio[HDR_GPIO_COUNT] = {
    { P13_0_PORT, P13_0_PIN, "P13.0" },
    { P13_3_PORT, P13_3_PIN, "P13.3" },
    { P13_4_PORT, P13_4_PIN, "P13.4" },
    { P13_5_PORT, P13_5_PIN, "P13.5" },
    { P13_6_PORT, P13_6_PIN, "P13.6" },
    { P13_7_PORT, P13_7_PIN, "P13.7" },
};

/* Per-pin bring-up state so we only FastInit a pin's direction once it changes. */
static uint8_t s_gpio_dir[HDR_GPIO_COUNT];   /* 0 = unset, 1 = out, 2 = in */

/*******************************************************************************
 * PWM — TCPWM0 counter 5, P13.3 = line5, P13.4 = line_compl5.
 * A dedicated 16-bit peripheral divider feeds the counter so freq math is
 * self-contained (no reliance on Device Configurator dividers).
 *******************************************************************************/
#define HDR_PWM_HW            TCPWM0
#define HDR_PWM_CNT_NUM       (5U)
#define HDR_PWM_PCLK          (PCLK_TCPWM0_CLOCK_COUNTER_EN5)
#define HDR_PWM_DIV_TYPE      (CY_SYSCLK_DIV_16_BIT)
#define HDR_PWM_DIV_NUM       (7U)     /* dedicated divider instance */
#define HDR_PWM_MAX_PERIOD    (65535U) /* 16-bit counter */

#define HDR_PWM_OUT_PORT      P13_3_PORT
#define HDR_PWM_OUT_PIN       P13_3_PIN
#define HDR_PWM_OUT_HSIOM     P13_3_TCPWM0_LINE5
#define HDR_PWM_OUTN_PORT     P13_4_PORT
#define HDR_PWM_OUTN_PIN      P13_4_PIN
#define HDR_PWM_OUTN_HSIOM    P13_4_TCPWM0_LINE_COMPL5

static bool s_pwm_initialized = false;

/*******************************************************************************
 * UART — SCB9, P15.0 RX / P15.1 TX, 115200 8N1
 *******************************************************************************/
#define HDR_UART_HW          (SCB9)
#define HDR_UART_RX_PORT     (GPIO_PRT15)
#define HDR_UART_RX_PIN      (0U)
#define HDR_UART_RX_HSIOM    (P15_0_SCB9_UART_RX)
#define HDR_UART_TX_PORT     (GPIO_PRT15)
#define HDR_UART_TX_PIN      (1U)
#define HDR_UART_TX_HSIOM    (P15_1_SCB9_UART_TX)
#define HDR_UART_OVERSAMPLE  (10U)
#define HDR_UART_TARGET_BAUD (115200U)
#define HDR_UART_DIV_TYPE    (CY_SYSCLK_DIV_16_BIT)
#define HDR_UART_DIV_NUM     (6U)     /* dedicated divider instance */

static cy_stc_scb_uart_context_t s_uart_ctx;
static bool s_uart_initialized = false;

/*******************************************************************************
 * GPIO helpers
 *******************************************************************************/
static void hdr_check_idx(int idx)
{
    if (idx < 0 || idx >= (int)HDR_GPIO_COUNT) {
        mp_raise_msg_varg(&mp_type_ValueError,
            MP_ERROR_TEXT("header pin %d out of range (0-%d)"),
            idx, HDR_GPIO_COUNT - 1);
    }
}

/* header.gpio_out(idx, val) -> None */
static mp_obj_t mod_header_gpio_out(mp_obj_t idx_obj, mp_obj_t val_obj)
{
    int idx = mp_obj_get_int(idx_obj);
    hdr_check_idx(idx);
    uint32_t val = (mp_obj_is_true(val_obj)) ? 1UL : 0UL;

    if (s_gpio_dir[idx] != 1U) {
        Cy_GPIO_Pin_FastInit(s_gpio[idx].port, s_gpio[idx].pin,
                             CY_GPIO_DM_STRONG_IN_OFF, val, HSIOM_SEL_GPIO);
        s_gpio_dir[idx] = 1U;
    }
    Cy_GPIO_Write(s_gpio[idx].port, s_gpio[idx].pin, val);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(mod_header_gpio_out_obj, mod_header_gpio_out);

/* header.gpio_in(idx) -> int (0/1). Configures pull-up input on first use. */
static mp_obj_t mod_header_gpio_in(mp_obj_t idx_obj)
{
    int idx = mp_obj_get_int(idx_obj);
    hdr_check_idx(idx);

    if (s_gpio_dir[idx] != 2U) {
        Cy_GPIO_Pin_FastInit(s_gpio[idx].port, s_gpio[idx].pin,
                             CY_GPIO_DM_PULLUP, 1UL, HSIOM_SEL_GPIO);
        s_gpio_dir[idx] = 2U;
    }
    uint32_t v = Cy_GPIO_Read(s_gpio[idx].port, s_gpio[idx].pin);
    return mp_obj_new_int((v != 0U) ? 1 : 0);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_header_gpio_in_obj, mod_header_gpio_in);

/* header.name(idx) -> str */
static mp_obj_t mod_header_name(mp_obj_t idx_obj)
{
    int idx = mp_obj_get_int(idx_obj);
    hdr_check_idx(idx);
    return mp_obj_new_str(s_gpio[idx].name, strlen(s_gpio[idx].name));
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_header_name_obj, mod_header_name);

/*******************************************************************************
 * PWM: TCPWM0 counter 5 on P13.3 (+ complementary P13.4)
 *
 * Modeled on the generated CYBSP_PWM_LED_CTRL_config so every device-specific
 * field is populated correctly. period/compare are computed at run time from
 * the requested freq/duty against the actual peri-clock frequency.
 *******************************************************************************/
static const cy_stc_tcpwm_pwm_config_t s_pwm_cfg = {
    .pwmMode              = CY_TCPWM_PWM_MODE_PWM,
    .clockPrescaler       = CY_TCPWM_PWM_PRESCALER_DIVBY_1,
    .pwmAlignment         = CY_TCPWM_PWM_LEFT_ALIGN,
    .deadTimeClocks       = 0,
    .runMode              = CY_TCPWM_PWM_CONTINUOUS,
    .period0              = 2000,
    .period1              = 32768,
    .enablePeriodSwap     = false,
    .compare0             = 1000,
    .compare1             = 16384,
    .enableCompareSwap    = false,
    .interruptSources     = 0UL,
    .invertPWMOut         = CY_TCPWM_PWM_INVERT_DISABLE,
    .invertPWMOutN        = CY_TCPWM_PWM_INVERT_ENABLE,
    .killMode             = CY_TCPWM_PWM_ASYNC_KILL,
    .swapInputMode        = CY_TCPWM_INPUT_LEVEL & 0x3U,
    .swapInput            = CY_TCPWM_INPUT_0,
    .reloadInputMode      = CY_TCPWM_INPUT_LEVEL & 0x3U,
    .reloadInput          = CY_TCPWM_INPUT_0,
    .startInputMode       = CY_TCPWM_INPUT_LEVEL & 0x3U,
    .startInput           = CY_TCPWM_INPUT_0,
    .killInputMode        = CY_TCPWM_INPUT_LEVEL & 0x3U,
    .killInput            = CY_TCPWM_INPUT_0,
    .countInputMode       = CY_TCPWM_INPUT_LEVEL & 0x3U,
    .countInput           = CY_TCPWM_INPUT_1,
    .swapOverflowUnderflow = false,
    .immediateKill        = false,
    .tapsEnabled          = 45,
    .compare2             = CY_TCPWM_GRP_CNT_CC0_DEFAULT,
    .compare3             = CY_TCPWM_GRP_CNT_CC0_BUFF_DEFAULT,
    .enableCompare1Swap   = false,
    .compare0MatchUp      = true,
    .compare0MatchDown    = false,
    .compare1MatchUp      = true,
    .compare1MatchDown    = false,
    .kill1InputMode       = CY_TCPWM_INPUT_LEVEL & 0x3U,
    .kill1Input           = CY_TCPWM_INPUT_0,
    .pwmOnDisable         = CY_TCPWM_PWM_OUTPUT_LOW,
    .trigger0Event        = CY_TCPWM_CNT_TRIGGER_ON_DISABLED,
    .trigger1Event        = CY_TCPWM_CNT_TRIGGER_ON_DISABLED,
    .reloadLineSelect     = false,
    .line_out_sel         = CY_TCPWM_OUTPUT_PWM_SIGNAL,
    .linecompl_out_sel    = CY_TCPWM_OUTPUT_INVERTED_PWM_SIGNAL,
    .line_out_sel_buff    = CY_TCPWM_OUTPUT_PWM_SIGNAL,
    .linecompl_out_sel_buff = CY_TCPWM_OUTPUT_INVERTED_PWM_SIGNAL,
    .deadTimeClocks_linecompl_out = 0,
#if defined (CY_IP_MXS40TCPWM)
    .hrpwm_enable         = false,
    .hrpwm_input_freq     = CY_TCPWM_HRPWM_FREQ_80MHZ_OR_100MHZ,
    .kill_line_polarity   = CY_TCPWM_LINEOUT_AND_LINECMPOUT_IS_LOW,
    .deadTimeClocksBuff   = 0,
    .deadTimeClocksBuff_linecompl_out = 0,
    .buffer_swap_enable   = false,
    .glitch_filter_enable = false,
    .gf_depth             = CY_GLITCH_FILTER_DEPTH_SUPPORT_VALUE_0,
    .dithering_mode       = CY_TCPWM_DITHERING_DISABLE,
    .period_dithering_value = 128,
    .duty_dithering_value = 128,
    .limiter              = CY_TCPWM_DITHERING_LIMITER_7,
    .pwm_tc_sync_kill_dt  = false,
    .pwm_sync_kill_dt     = false,
#endif /* CY_IP_MXS40TCPWM */
};

static void hdr_pwm_init_clock(void)
{
    /* Dedicated divider @ divide-by-1 so the counter sees the full peri clock. */
    (void)Cy_SysClk_PeriPclkSetDivider(HDR_PWM_PCLK, HDR_PWM_DIV_TYPE,
                                       HDR_PWM_DIV_NUM, 0U);
    (void)Cy_SysClk_PeriPclkAssignDivider(HDR_PWM_PCLK, HDR_PWM_DIV_TYPE,
                                          HDR_PWM_DIV_NUM);
    (void)Cy_SysClk_PeriPclkEnableDivider(HDR_PWM_PCLK, HDR_PWM_DIV_TYPE,
                                          HDR_PWM_DIV_NUM);
}

static bool hdr_pwm_ensure_init(void)
{
    if (s_pwm_initialized) {
        return true;
    }

    hdr_pwm_init_clock();

    if (CY_TCPWM_SUCCESS != Cy_TCPWM_PWM_Init(HDR_PWM_HW, HDR_PWM_CNT_NUM, &s_pwm_cfg)) {
        return false;
    }
    Cy_TCPWM_PWM_Enable(HDR_PWM_HW, HDR_PWM_CNT_NUM);

    /* Route P13.3 = line5, P13.4 = line_compl5. */
    Cy_GPIO_Pin_FastInit(HDR_PWM_OUT_PORT, HDR_PWM_OUT_PIN,
                         CY_GPIO_DM_STRONG_IN_OFF, 0U, HDR_PWM_OUT_HSIOM);
    Cy_GPIO_Pin_FastInit(HDR_PWM_OUTN_PORT, HDR_PWM_OUTN_PIN,
                         CY_GPIO_DM_STRONG_IN_OFF, 0U, HDR_PWM_OUTN_HSIOM);

    s_pwm_initialized = true;
    return true;
}

/* header.pwm(pin, duty, freq) — pin arg reserved (P13.3 fixed line5).
 * duty 0..100 (%), freq in Hz. duty 0 stops the output low. */
static mp_obj_t mod_header_pwm(size_t n_args, const mp_obj_t *args)
{
    (void)args; /* pin index reserved for future multi-channel support */
    int duty = mp_obj_get_int(args[1]);
    int freq = mp_obj_get_int(args[2]);

    if (duty < 0 || duty > 100) {
        mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("duty must be 0..100"));
    }
    if (freq <= 0) {
        mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("freq must be > 0"));
    }

    if (!hdr_pwm_ensure_init()) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("PWM init failed"));
    }

    uint32_t clk = Cy_SysClk_PeriPclkGetFrequency(HDR_PWM_PCLK, HDR_PWM_DIV_TYPE,
                                                  HDR_PWM_DIV_NUM);
    if (clk == 0U) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("PWM clock unavailable"));
    }

    /* period register = clocks per PWM cycle - 1 (16-bit counter). */
    uint32_t period = clk / (uint32_t)freq;
    if (period == 0U) {
        period = 1U;
    }
    if (period > (HDR_PWM_MAX_PERIOD + 1U)) {
        mp_raise_msg(&mp_type_ValueError,
            MP_ERROR_TEXT("freq too low for 16-bit counter"));
    }
    uint32_t compare = (period * (uint32_t)duty) / 100U;

    Cy_TCPWM_PWM_SetPeriod0(HDR_PWM_HW, HDR_PWM_CNT_NUM, period - 1U);
    Cy_TCPWM_PWM_SetCompare0(HDR_PWM_HW, HDR_PWM_CNT_NUM, compare);
    Cy_TCPWM_TriggerStart_Single(HDR_PWM_HW, HDR_PWM_CNT_NUM);

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_header_pwm_obj, 3, 3, mod_header_pwm);

/*******************************************************************************
 * UART: SCB9 115200 8N1
 *******************************************************************************/
static bool hdr_uart_ensure_init(void)
{
    static const cy_stc_scb_uart_config_t uart_cfg = {
        .uartMode                   = CY_SCB_UART_STANDARD,
        .enableMultiProcessorMode   = false,
        .smartCardRetryOnNack       = false,
        .irdaInvertRx               = false,
        .irdaEnableLowPowerReceiver = false,
        .oversample                 = HDR_UART_OVERSAMPLE,
        .enableMsbFirst             = false,
        .dataWidth                  = 8UL,
        .parity                     = CY_SCB_UART_PARITY_NONE,
        .stopBits                   = CY_SCB_UART_STOP_BITS_1,
        .enableInputFilter          = false,
        .breakWidth                 = 11UL,
        .dropOnFrameError           = false,
        .dropOnParityError          = false,
        .breaklevel                 = false,
        .receiverAddress            = 0x0UL,
        .receiverAddressMask        = 0x0UL,
        .acceptAddrInFifo           = false,
        .enableCts                  = false,
        .ctsPolarity                = CY_SCB_UART_ACTIVE_LOW,
        .rtsRxFifoLevel             = 0UL,
        .rtsPolarity                = CY_SCB_UART_ACTIVE_LOW,
        .rxFifoTriggerLevel         = 0UL,
        .rxFifoIntEnableMask        = 0UL,
        .txFifoTriggerLevel         = 0UL,
        .txFifoIntEnableMask        = 0UL,
    };

    if (s_uart_initialized) {
        return true;
    }

    /* Bring SCB9 out of the peri group and clock it. Then set a dedicated
     * 16-bit divider so 115200 * oversample(10) = 1.152 MHz SCB clock.
     *
     * Strategy: assign a fresh divider @ divide-by-1 first, read the resulting
     * output frequency (== the raw peri-clock feeding this divider, since /1),
     * then reprogram the divider to hit the target SCB clock. */
    Cy_SysClk_PeriGroupSlaveInit(CY_MMIO_SCB9_PERI_NR, CY_MMIO_SCB9_GROUP_NR,
                                 CY_MMIO_SCB9_SLAVE_NR, CY_MMIO_SCB9_CLK_HF_NR);

    (void)Cy_SysClk_PeriPclkAssignDivider(PCLK_SCB9_CLOCK_SCB_EN,
                                          HDR_UART_DIV_TYPE, HDR_UART_DIV_NUM);
    (void)Cy_SysClk_PeriPclkDisableDivider(PCLK_SCB9_CLOCK_SCB_EN,
                                           HDR_UART_DIV_TYPE, HDR_UART_DIV_NUM);
    (void)Cy_SysClk_PeriPclkSetDivider(PCLK_SCB9_CLOCK_SCB_EN,
                                       HDR_UART_DIV_TYPE, HDR_UART_DIV_NUM, 0U);
    (void)Cy_SysClk_PeriPclkEnableDivider(PCLK_SCB9_CLOCK_SCB_EN,
                                          HDR_UART_DIV_TYPE, HDR_UART_DIV_NUM);

    uint32_t peri_hz = Cy_SysClk_PeriPclkGetFrequency(PCLK_SCB9_CLOCK_SCB_EN,
                                                      HDR_UART_DIV_TYPE,
                                                      HDR_UART_DIV_NUM);
    if (peri_hz != 0U) {
        uint32_t want = HDR_UART_TARGET_BAUD * HDR_UART_OVERSAMPLE; /* 1.152 MHz */
        uint32_t div  = (peri_hz + (want / 2U)) / want;            /* nearest */
        if (div == 0U) {
            div = 1U;
        }
        (void)Cy_SysClk_PeriPclkDisableDivider(PCLK_SCB9_CLOCK_SCB_EN,
                                               HDR_UART_DIV_TYPE, HDR_UART_DIV_NUM);
        (void)Cy_SysClk_PeriPclkSetDivider(PCLK_SCB9_CLOCK_SCB_EN,
                                           HDR_UART_DIV_TYPE, HDR_UART_DIV_NUM,
                                           div - 1U);
        (void)Cy_SysClk_PeriPclkEnableDivider(PCLK_SCB9_CLOCK_SCB_EN,
                                              HDR_UART_DIV_TYPE, HDR_UART_DIV_NUM);
    }

    Cy_GPIO_Pin_FastInit(HDR_UART_RX_PORT, HDR_UART_RX_PIN,
                         CY_GPIO_DM_HIGHZ, 0U, HDR_UART_RX_HSIOM);
    Cy_GPIO_Pin_FastInit(HDR_UART_TX_PORT, HDR_UART_TX_PIN,
                         CY_GPIO_DM_STRONG_IN_OFF, 1U, HDR_UART_TX_HSIOM);

    if (Cy_SCB_UART_Init(HDR_UART_HW, &uart_cfg, &s_uart_ctx) != CY_SCB_UART_SUCCESS) {
        return false;
    }
    Cy_SCB_UART_Enable(HDR_UART_HW);
    s_uart_initialized = true;
    return true;
}

/* header.uart_write(bytes) -> int (bytes written) */
static mp_obj_t mod_header_uart_write(mp_obj_t data_obj)
{
    if (!hdr_uart_ensure_init()) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("UART init failed"));
    }

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(data_obj, &bufinfo, MP_BUFFER_READ);
    if (bufinfo.len > 0U) {
        Cy_SCB_UART_PutArrayBlocking(HDR_UART_HW, bufinfo.buf, bufinfo.len);
    }
    return mp_obj_new_int((mp_int_t)bufinfo.len);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_header_uart_write_obj, mod_header_uart_write);

/* header.uart_any() -> int (bytes waiting in RX FIFO) */
static mp_obj_t mod_header_uart_any(void)
{
    if (!hdr_uart_ensure_init()) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("UART init failed"));
    }
    uint32_t n = Cy_SCB_UART_GetNumInRxFifo(HDR_UART_HW);
    return mp_obj_new_int((mp_int_t)n);
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_header_uart_any_obj, mod_header_uart_any);

/* header.uart_read(n) -> bytes (up to n bytes currently available) */
static mp_obj_t mod_header_uart_read(mp_obj_t n_obj)
{
    if (!hdr_uart_ensure_init()) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("UART init failed"));
    }

    int want = mp_obj_get_int(n_obj);
    if (want < 0) {
        mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("n must be >= 0"));
    }

    uint32_t avail = Cy_SCB_UART_GetNumInRxFifo(HDR_UART_HW);
    uint32_t count = ((uint32_t)want < avail) ? (uint32_t)want : avail;

    vstr_t vstr;
    vstr_init_len(&vstr, count);
    for (uint32_t i = 0U; i < count; i++) {
        ((byte *)vstr.buf)[i] = (byte)Cy_SCB_UART_Get(HDR_UART_HW);
    }
    return mp_obj_new_bytes_from_vstr(&vstr);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_header_uart_read_obj, mod_header_uart_read);

/*******************************************************************************
 * Module definition
 *******************************************************************************/
static const mp_rom_map_elem_t header_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),    MP_ROM_QSTR(MP_QSTR_header) },
    { MP_ROM_QSTR(MP_QSTR_gpio_out),    MP_ROM_PTR(&mod_header_gpio_out_obj) },
    { MP_ROM_QSTR(MP_QSTR_gpio_in),     MP_ROM_PTR(&mod_header_gpio_in_obj) },
    { MP_ROM_QSTR(MP_QSTR_name),        MP_ROM_PTR(&mod_header_name_obj) },
    { MP_ROM_QSTR(MP_QSTR_pwm),         MP_ROM_PTR(&mod_header_pwm_obj) },
    { MP_ROM_QSTR(MP_QSTR_uart_write),  MP_ROM_PTR(&mod_header_uart_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_uart_any),    MP_ROM_PTR(&mod_header_uart_any_obj) },
    { MP_ROM_QSTR(MP_QSTR_uart_read),   MP_ROM_PTR(&mod_header_uart_read_obj) },
};
static MP_DEFINE_CONST_DICT(header_module_globals, header_module_globals_table);

const mp_obj_module_t mp_module_header = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&header_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_header, mp_module_header);

#endif /* BSP_HAS_QWA309_BASEBOARD */
