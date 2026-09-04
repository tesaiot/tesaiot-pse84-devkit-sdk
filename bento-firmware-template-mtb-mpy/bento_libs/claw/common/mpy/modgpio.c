/*******************************************************************************
 * File Name: modgpio.c
 *
 * Description: MicroPython 'gpio' module for CM33_NS.
 *              BSP-aware LED and Button control with object-oriented API.
 *
 *              BSP-aware: AI Kit exposes 5 LEDs, Eval Kit exposes only the
 *              LEDs/buttons defined by its BSP.
 *
 * Usage:
 *   import gpio
 *   gpio.led(0).on()
 *   gpio.led(0).toggle()
 *   gpio.button(0).is_pressed()
 *   gpio.board_info()
 *
 *******************************************************************************/

#include "py/runtime.h"
#include "py/obj.h"
#include "py/mphal.h"
#include "cy_pdl.h"
#include "cycfg_pins.h"
#include "cybsp_types.h"
#include "ipc_communication.h"
#include <string.h>

/*******************************************************************************
 * BSP Pin Definitions
 *******************************************************************************/

typedef struct {
    GPIO_PRT_Type *port;
    uint8_t pin;
    const char *name;
} gpio_pin_def_t;

static const gpio_pin_def_t led_table[] = {
    { CYBSP_USER_LED1_PORT, CYBSP_USER_LED1_NUM, "LED1" },
    { CYBSP_USER_LED2_PORT, CYBSP_USER_LED2_NUM, "LED2" },
#ifdef CYBSP_USER_LED3_PORT
    { CYBSP_USER_LED3_PORT, CYBSP_USER_LED3_NUM, "RGB_RED" },
#endif
#ifdef CYBSP_USER_LED4_PORT
    { CYBSP_USER_LED4_PORT, CYBSP_USER_LED4_NUM, "RGB_BLUE" },
#endif
#ifdef CYBSP_USER_LED5_PORT
    { CYBSP_USER_LED5_PORT, CYBSP_USER_LED5_NUM, "RGB_GREEN" },
#endif
};
#define NUM_LEDS  ((int)(sizeof(led_table) / sizeof(led_table[0])))

static const gpio_pin_def_t btn_table[] = {
    /* "USER Button 1", not a silkscreen name, and this is the one entry in this
     * file where that is deliberate.
     *
     * The SoM prints SW2 on this button (the BSP alias chain is
     * CYBSP_USER_BTN1 = CYBSP_SW1, which is the DEFINE's name, not the
     * button's). But the QWA309 base board also carries switches SW2 through
     * SW14 -- thirteen of them, one part number, and they are POWER switches.
     * On an assembled Dev Kit "SW2" therefore names two different controls and
     * one of them cuts power to a header, so a student told to press SW2 can
     * reach for the wrong one.
     *
     * name() exists so a reader can find the control with a finger. A name
     * that points at two things fails the only job it has.
     *
     * Reported as "SW1" until 2026-08-20, then "SW2" in this tree only;
     * unified here on Wiroon's instruction. */
    { CYBSP_USER_BTN1_PORT, CYBSP_USER_BTN1_NUM, "USER Button 1" },

};
#define NUM_BTNS  1

#ifdef USE_KIT_PSE84_EVAL_EPC2
#define BOARD_NAME  "PSoC Edge Eval Kit"
#else
#define BOARD_NAME  "PSoC Edge AI Dev Kit"
#endif

/* Track initialization state */
static bool gpio_initialized = false;

/* Track LED states for IPC reporting */
static uint8_t led_states[NUM_LEDS];

/* Duty cycle per LED, 0-100, as last requested. Starts at 0 because the LEDs
 * start dark -- it was initialised to 100, so duty() reported a fully lit LED
 * on a board that had just booted with all three off. on() and off() write it
 * too, so it never disagrees with the pin. */
static uint8_t led_duty[NUM_LEDS];

/*******************************************************************************
 * GPIO Hardware Init
 *******************************************************************************/
static void gpio_hw_init(void) {
    if (gpio_initialized) return;

    /* Initialize all LEDs as output, OFF */
    for (int i = 0; i < NUM_LEDS; i++) {
        Cy_GPIO_Pin_FastInit(led_table[i].port, led_table[i].pin,
                             CY_GPIO_DM_STRONG, 0, HSIOM_SEL_GPIO);
        led_states[i] = 0;
    }

    /* Initialize all buttons as input with pull-up */
    for (int i = 0; i < NUM_BTNS; i++) {
        Cy_GPIO_Pin_FastInit(btn_table[i].port, btn_table[i].pin,
                             CY_GPIO_DM_PULLUP, 1, HSIOM_SEL_GPIO);
    }

    gpio_initialized = true;
}

/*******************************************************************************
 * IPC: Send LED state to CM55 (for LVGL UI reflection)
 *******************************************************************************/
/* Non-static: reused by modoptiga.c for touch pause/resume. Safe because both
 * modules only send from the MicroPython task (never concurrent), and the
 * m33_allocatable_shared region is too small on some kits (Eva: 4 KB, full)
 * to afford one buffer per module. */
CY_SECTION_SHAREDMEM ipc_msg_t gpio_ipc_msg;

static void gpio_ipc_send_led_state(void) {
    memset(&gpio_ipc_msg, 0, sizeof(gpio_ipc_msg));
    gpio_ipc_msg.client_id = CM55_IPC_SENSOR_CLIENT_ID;
    gpio_ipc_msg.intr_mask = CY_IPC_CYPIPE_INTR_MASK_EP1;
    gpio_ipc_msg.cmd = IPC_CMD_GPIO_LED_STATE;
    gpio_ipc_msg.value = NUM_LEDS;

    /* Pack LED states as bitmask in data[0] */
    uint8_t bitmask = 0;
    for (int i = 0; i < NUM_LEDS; i++) {
        if (led_states[i]) bitmask |= (1 << i);
    }
    gpio_ipc_msg.data[0] = bitmask;

    Cy_IPC_Pipe_SendMessage(
        CM55_IPC_PIPE_EP_ADDR, CM33_IPC_PIPE_EP_ADDR,
        (void *)&gpio_ipc_msg, NULL);
}


/*******************************************************************************
 * Hardware PWM dimming (Bento Engine Phase 1.3, 2026-08-20)
 *
 * The old comment here claimed "no PWM peripheral routed to these pins". That
 * was wrong: every RGB LED pin has TCPWM0 group-1 alternate functions, and
 * group 1 (counters 256+) is untouched by the BSP, so these routes conflict
 * with nothing. brightness(1..99) muxes the pin to its TCPWM line and dims in
 * hardware — non-blocking, flicker-free. on/off/toggle/value() first release
 * the pin back to plain GPIO, so their semantics (including value() readback,
 * which the curriculum teaches) are exactly what they always were.
 *
 * Clock: 16-bit peri divider #3 of the TCPWM0 PCLK group — configured by the
 * BSP for CYBSP_PWM_LED_CTRL, which nothing consumes (verified 2026-08-20), so
 * we re-rate it: 100 MHz / 100 = 1 MHz; period 1000 => 1 kHz PWM. Divider #4
 * was tried first and DOES NOT EXIST (GR_DIV_16_VECT = 4 -> dividers 0..3);
 * the failure was silent because return codes were ignored — they no longer are.
 ******************************************************************************/
#define LED_PWM_DIV_TYPE  CY_SYSCLK_DIV_16_BIT
#define LED_PWM_DIV_NUM   (3U)
#define LED_PWM_PERIOD    (1000U)

typedef struct {
    int8_t        led_idx;   /* index into led_table */
    uint16_t      cnt;       /* TCPWM0 counter number (group 1 = 256+) */
    en_clk_dst_t  pclk;
    en_hsiom_sel_t hsiom;    /* pin function for the PWM line */
} led_pwm_route_t;

#if defined(CYBSP_USER_LED5_PORT)
/* AI Kit / TESAIoT Dev Kit: RGB LED on P20.6 (red) / P20.5 (blue) / P20.4
 * (green) — group-1 lines 265/264/263, positive polarity. */
static const led_pwm_route_t s_led_pwm_routes[] = {
    { 2, 265U, PCLK_TCPWM0_CLOCK_COUNTER_EN265, P20_6_TCPWM0_LINE265 },
    { 3, 264U, PCLK_TCPWM0_CLOCK_COUNTER_EN264, P20_5_TCPWM0_LINE264 },
    { 4, 263U, PCLK_TCPWM0_CLOCK_COUNTER_EN263, P20_4_TCPWM0_LINE263 },
};
#elif defined(USE_KIT_PSE84_EVAL_EPC2)
/* Eva Kit: LED1 red P16.7 / LED2 green P16.6 / LED3 blue P16.5 —
 * group-1 lines 279/278/261 (แพ็กเกจนี้เว้นเบอร์ไม่ต่อเนื่อง), positive polarity. */
static const led_pwm_route_t s_led_pwm_routes[] = {
    { 0, 279U, PCLK_TCPWM0_CLOCK_COUNTER_EN279, P16_7_TCPWM0_LINE279 },
    { 1, 278U, PCLK_TCPWM0_CLOCK_COUNTER_EN278, P16_6_TCPWM0_LINE278 },
    { 2, 261U, PCLK_TCPWM0_CLOCK_COUNTER_EN261, P16_5_TCPWM0_LINE261 },
};
#else
static const led_pwm_route_t s_led_pwm_routes[] = { { -1, 0U, (en_clk_dst_t)0, HSIOM_SEL_GPIO } };
#endif

static bool s_led_pwm_clk_ready = false;
static bool s_led_pwm_active[NUM_LEDS];

static const led_pwm_route_t *led_pwm_route(uint8_t led_idx) {
    for (size_t i = 0; i < sizeof(s_led_pwm_routes) / sizeof(s_led_pwm_routes[0]); i++) {
        if (s_led_pwm_routes[i].led_idx == (int8_t)led_idx) {
            return &s_led_pwm_routes[i];
        }
    }
    return NULL;
}

/* Return the pin to plain-GPIO control. Called by on/off/toggle/value so the
 * historical semantics of those calls are untouched by the PWM path. */
static void led_pwm_release(uint8_t led_idx) {
    if (led_idx >= NUM_LEDS || !s_led_pwm_active[led_idx]) {
        return;
    }
    const led_pwm_route_t *r = led_pwm_route(led_idx);
    if (r != NULL) {
        Cy_TCPWM_PWM_Disable(TCPWM0, r->cnt);
        Cy_GPIO_SetHSIOM(led_table[led_idx].port, led_table[led_idx].pin,
                         HSIOM_SEL_GPIO);
        /* Restore DM_STRONG (input buffer ON): led.value() reads the pin
         * back, and the PWM path switched to STRONG_IN_OFF. Caught by
         * vv_pwm.py on hardware — value() returned 0 after on(). */
        Cy_GPIO_SetDrivemode(led_table[led_idx].port, led_table[led_idx].pin,
                             CY_GPIO_DM_STRONG);
    }
    s_led_pwm_active[led_idx] = false;
}

/* True if hardware PWM took the request; false = caller falls back. */
static bool led_pwm_set(uint8_t led_idx, uint8_t pct) {
    const led_pwm_route_t *r = led_pwm_route(led_idx);
    if (r == NULL) {
        return false;
    }

    if (!s_led_pwm_clk_ready) {
        /* One shared divider for all LED PWM counters: 100 MHz / 100 = 1 MHz */
        (void)Cy_SysClk_PeriPclkDisableDivider(r->pclk, LED_PWM_DIV_TYPE, LED_PWM_DIV_NUM);
        if (CY_SYSCLK_SUCCESS != Cy_SysClk_PeriPclkSetDivider(
                r->pclk, LED_PWM_DIV_TYPE, LED_PWM_DIV_NUM, 99U)) {
            return false;
        }
        if (CY_SYSCLK_SUCCESS != Cy_SysClk_PeriPclkEnableDivider(
                r->pclk, LED_PWM_DIV_TYPE, LED_PWM_DIV_NUM)) {
            return false;
        }
        s_led_pwm_clk_ready = true;
    }

    if (!s_led_pwm_active[led_idx]) {
        if (CY_SYSCLK_SUCCESS != Cy_SysClk_PeriPclkAssignDivider(
                r->pclk, LED_PWM_DIV_TYPE, LED_PWM_DIV_NUM)) {
            return false;
        }

        cy_stc_tcpwm_pwm_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.pwmMode        = CY_TCPWM_PWM_MODE_PWM;
        cfg.clockPrescaler = CY_TCPWM_PWM_PRESCALER_DIVBY_1;
        cfg.pwmAlignment   = CY_TCPWM_PWM_LEFT_ALIGN;
        cfg.runMode        = CY_TCPWM_PWM_CONTINUOUS;
        cfg.period0        = LED_PWM_PERIOD - 1U;
        cfg.compare0       = (uint32_t)pct * 10U;
        cfg.invertPWMOut   = CY_TCPWM_PWM_INVERT_DISABLE;
        cfg.invertPWMOutN  = CY_TCPWM_PWM_INVERT_DISABLE;
        cfg.killMode       = CY_TCPWM_PWM_ASYNC_KILL;
        cfg.swapInputMode  = 3U;   /* input disabled, same as BSP configs */
        cfg.reloadInputMode = 3U;
        cfg.startInputMode = 3U;
        cfg.killInputMode  = 3U;
        cfg.countInputMode = 3U;
        cfg.countInput     = CY_TCPWM_INPUT_1;
        cfg.compare0MatchUp = true;
        cfg.pwmOnDisable   = CY_TCPWM_PWM_OUTPUT_HIGHZ;
        cfg.trigger0Event  = CY_TCPWM_CNT_TRIGGER_ON_DISABLED;
        cfg.trigger1Event  = CY_TCPWM_CNT_TRIGGER_ON_DISABLED;
        cfg.line_out_sel   = CY_TCPWM_OUTPUT_PWM_SIGNAL;
        cfg.linecompl_out_sel = CY_TCPWM_OUTPUT_INVERTED_PWM_SIGNAL;
        cfg.line_out_sel_buff = CY_TCPWM_OUTPUT_PWM_SIGNAL;
        cfg.linecompl_out_sel_buff = CY_TCPWM_OUTPUT_INVERTED_PWM_SIGNAL;

        if (CY_TCPWM_SUCCESS != Cy_TCPWM_PWM_Init(TCPWM0, r->cnt, &cfg)) {
            return false;
        }
        Cy_TCPWM_PWM_Enable(TCPWM0, r->cnt);
        Cy_TCPWM_TriggerStart_Single(TCPWM0, r->cnt);
        Cy_GPIO_SetDrivemode(led_table[led_idx].port, led_table[led_idx].pin,
                             CY_GPIO_DM_STRONG_IN_OFF);
        Cy_GPIO_SetHSIOM(led_table[led_idx].port, led_table[led_idx].pin,
                         r->hsiom);
        s_led_pwm_active[led_idx] = true;
    } else {
        Cy_TCPWM_PWM_SetCompare0(TCPWM0, r->cnt, (uint32_t)pct * 10U);
    }
    return true;
}

/*******************************************************************************
 * LED Object Type
 *******************************************************************************/

typedef struct {
    mp_obj_base_t base;
    uint8_t index;
} gpio_led_obj_t;

static mp_obj_t gpio_led_on(mp_obj_t self_in) {
    gpio_led_obj_t *self = MP_OBJ_TO_PTR(self_in);
    led_pwm_release(self->index);
    Cy_GPIO_Set(led_table[self->index].port, led_table[self->index].pin);
    led_states[self->index] = 1;
    led_duty[self->index] = 100;
    gpio_ipc_send_led_state();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(gpio_led_on_obj, gpio_led_on);

static mp_obj_t gpio_led_off(mp_obj_t self_in) {
    gpio_led_obj_t *self = MP_OBJ_TO_PTR(self_in);
    led_pwm_release(self->index);
    Cy_GPIO_Clr(led_table[self->index].port, led_table[self->index].pin);
    led_states[self->index] = 0;
    led_duty[self->index] = 0;
    gpio_ipc_send_led_state();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(gpio_led_off_obj, gpio_led_off);

static mp_obj_t gpio_led_toggle(mp_obj_t self_in) {
    gpio_led_obj_t *self = MP_OBJ_TO_PTR(self_in);
    led_pwm_release(self->index);
    Cy_GPIO_Inv(led_table[self->index].port, led_table[self->index].pin);
    led_states[self->index] ^= 1;
    gpio_ipc_send_led_state();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(gpio_led_toggle_obj, gpio_led_toggle);

static mp_obj_t gpio_led_value(size_t n_args, const mp_obj_t *args) {
    gpio_led_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    if (n_args == 1) {
        /* Read current state */
        return mp_obj_new_int(Cy_GPIO_Read(led_table[self->index].port,
                                           led_table[self->index].pin));
    }
    /* Set state */
    int val = mp_obj_get_int(args[1]);
    led_pwm_release(self->index);
    if (val) {
        Cy_GPIO_Set(led_table[self->index].port, led_table[self->index].pin);
        led_states[self->index] = 1;
        led_duty[self->index] = 100;
    } else {
        Cy_GPIO_Clr(led_table[self->index].port, led_table[self->index].pin);
        led_states[self->index] = 0;
        led_duty[self->index] = 0;
    }
    gpio_ipc_send_led_state();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(gpio_led_value_obj, 1, 2, gpio_led_value);

static mp_obj_t gpio_led_name(mp_obj_t self_in) {
    gpio_led_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_str(led_table[self->index].name,
                          strlen(led_table[self->index].name));
}
static MP_DEFINE_CONST_FUN_OBJ_1(gpio_led_name_obj, gpio_led_name);

static void gpio_led_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    gpio_led_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "LED(%d, name='%s')", self->index, led_table[self->index].name);
}


/*******************************************************************************
 * led.brightness(pct) -> None      pct 0-100
 *
 * Pins with a TCPWM route (s_led_pwm_routes — the RGB LED on both boards)
 * dim in HARDWARE: non-blocking, flicker-free, holds the level until the next
 * call. Pins without a route fall back to the old bit-bang: BRIGHT_PERIOD_US
 * per cycle, on for pct% of it, repeated BRIGHT_CYCLES times — that path
 * BLOCKS ~12 ms per call and goes dark between calls. (This comment used to
 * claim no PWM peripheral reaches these pins; gpio_pse84_bga_220.h says
 * otherwise for every RGB pin — corrected 2026-08-20.)
 *
 * 0 and 100 skip the loop entirely and just drive the pin, so the common cases
 * cost nothing.
 ******************************************************************************/
#define BRIGHT_PERIOD_US  200u   /* 5 kHz — above the eye's flicker threshold */
#define BRIGHT_CYCLES      60u   /* ~12 ms per call, long enough to look steady */

static mp_obj_t gpio_led_brightness(mp_obj_t self_in, mp_obj_t pct_in) {
    gpio_led_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_int_t pct = mp_obj_get_int(pct_in);
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;

    GPIO_PRT_Type *port = led_table[self->index].port;
    uint8_t pin = led_table[self->index].pin;
    led_duty[self->index] = (uint8_t)pct;

    if (pct > 0 && pct < 100 && led_pwm_set(self->index, (uint8_t)pct)) {
        led_states[self->index] = 1;
        gpio_ipc_send_led_state();
        return mp_const_none;
    }

    if (pct == 0) {
        led_pwm_release(self->index);
        Cy_GPIO_Clr(port, pin);
        led_states[self->index] = 0;
        gpio_ipc_send_led_state();
        return mp_const_none;
    }
    if (pct == 100) {
        led_pwm_release(self->index);
        Cy_GPIO_Set(port, pin);
        led_states[self->index] = 1;
        gpio_ipc_send_led_state();
        return mp_const_none;
    }

    uint32_t on_us = (BRIGHT_PERIOD_US * (uint32_t)pct) / 100u;
    uint32_t off_us = BRIGHT_PERIOD_US - on_us;
    for (uint32_t i = 0; i < BRIGHT_CYCLES; i++) {
        Cy_GPIO_Set(port, pin);
        Cy_SysLib_DelayUs((uint16_t)on_us);
        Cy_GPIO_Clr(port, pin);
        Cy_SysLib_DelayUs((uint16_t)off_us);
    }
    /* End dark, not lit.
     *
     * This used to finish with Cy_GPIO_Set and a comment claiming that kept the
     * dimmed level visible. It did the opposite: the pin sat HIGH, so between
     * calls the LED was at 100% and every level looked identical. The example
     * written to teach duty cycle -- set 20, set 60, compare the two with your
     * eyes -- showed one brightness for both, which is how this was found.
     *
     * There is no PWM peripheral on these pins and machine.Timer is not built
     * for this board, so nothing can hold a level once this call returns. Ending
     * dark at least makes the truth visible: a single brightness() call is a
     * 12 ms pulse, and holding a level means calling it in a loop. The sticky
     * repaint is done for you by hold_ms below. */
    Cy_GPIO_Clr(port, pin);
    led_states[self->index] = 0;
    gpio_ipc_send_led_state();
    return mp_const_none;
}

/*******************************************************************************
 * led.hold(pct, ms) — keep an LED at pct for ms milliseconds
 *
 * brightness() is one 12 ms pulse; the eye needs the pulse repeated to see a
 * steady level. Writing that loop by hand is the first thing every example had
 * to do, and getting the timing wrong is what made the LED look broken, so it
 * belongs here once rather than in eighty-six files.
 *
 * Yields to MicroPython between pulses, so Ctrl-C and the IDE Stop button stay
 * alive through a long hold -- brightness() alone is deaf for its 12 ms.
 ******************************************************************************/
static mp_obj_t gpio_led_hold(size_t n_args, const mp_obj_t *args) {
    gpio_led_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    mp_int_t pct = mp_obj_get_int(args[1]);
    mp_int_t ms  = (n_args > 2) ? mp_obj_get_int(args[2]) : 500;
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    if (ms  < 0)   ms  = 0;

    GPIO_PRT_Type *port = led_table[self->index].port;
    uint8_t pin = led_table[self->index].pin;
    led_duty[self->index] = (uint8_t)pct;

    if (pct == 0 || pct == 100) {
        if (pct) Cy_GPIO_Set(port, pin); else Cy_GPIO_Clr(port, pin);
        led_states[self->index] = (pct != 0);
        gpio_ipc_send_led_state();
        mp_hal_delay_ms((mp_uint_t)ms);
        return mp_const_none;
    }

    uint32_t on_us  = (BRIGHT_PERIOD_US * (uint32_t)pct) / 100u;
    uint32_t off_us = BRIGHT_PERIOD_US - on_us;
    mp_uint_t deadline = mp_hal_ticks_ms() + (mp_uint_t)ms;
    while ((mp_int_t)(mp_hal_ticks_ms() - deadline) < 0) {
        for (uint32_t i = 0; i < BRIGHT_CYCLES; i++) {
            Cy_GPIO_Set(port, pin);
            Cy_SysLib_DelayUs((uint16_t)on_us);
            Cy_GPIO_Clr(port, pin);
            Cy_SysLib_DelayUs((uint16_t)off_us);
        }
        MICROPY_EVENT_POLL_HOOK;   /* 1 ms gap, ~8% of the cycle. Visible? No. */
    }
    Cy_GPIO_Clr(port, pin);
    led_states[self->index] = 0;
    gpio_ipc_send_led_state();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(gpio_led_hold_obj, 2, 3, gpio_led_hold);
static MP_DEFINE_CONST_FUN_OBJ_2(gpio_led_brightness_obj, gpio_led_brightness);

/* led.duty() -> int   the last percentage handed to brightness() */
static mp_obj_t gpio_led_duty(mp_obj_t self_in) {
    gpio_led_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_int(led_duty[self->index]);
}
static MP_DEFINE_CONST_FUN_OBJ_1(gpio_led_duty_obj, gpio_led_duty);

static const mp_rom_map_elem_t gpio_led_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_on),     MP_ROM_PTR(&gpio_led_on_obj) },
    { MP_ROM_QSTR(MP_QSTR_off),    MP_ROM_PTR(&gpio_led_off_obj) },
    { MP_ROM_QSTR(MP_QSTR_toggle), MP_ROM_PTR(&gpio_led_toggle_obj) },
    { MP_ROM_QSTR(MP_QSTR_value),  MP_ROM_PTR(&gpio_led_value_obj) },
    { MP_ROM_QSTR(MP_QSTR_name),   MP_ROM_PTR(&gpio_led_name_obj) },
    { MP_ROM_QSTR(MP_QSTR_brightness), MP_ROM_PTR(&gpio_led_brightness_obj) },
    { MP_ROM_QSTR(MP_QSTR_hold),   MP_ROM_PTR(&gpio_led_hold_obj) },
    { MP_ROM_QSTR(MP_QSTR_duty),   MP_ROM_PTR(&gpio_led_duty_obj) },
};
static MP_DEFINE_CONST_DICT(gpio_led_locals_dict, gpio_led_locals_table);

MP_DEFINE_CONST_OBJ_TYPE(
    gpio_led_type,
    MP_QSTR_LED,
    MP_TYPE_FLAG_NONE,
    print, gpio_led_print,
    locals_dict, &gpio_led_locals_dict
);

/*******************************************************************************
 * Button Object Type
 *******************************************************************************/

typedef struct {
    mp_obj_base_t base;
    uint8_t index;
} gpio_btn_obj_t;

static mp_obj_t gpio_btn_is_pressed(mp_obj_t self_in) {
    gpio_btn_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint32_t val = Cy_GPIO_Read(btn_table[self->index].port,
                                btn_table[self->index].pin);
    /* Active low: 0 = pressed */
    return mp_obj_new_bool(val == CYBSP_BTN_PRESSED);
}
static MP_DEFINE_CONST_FUN_OBJ_1(gpio_btn_is_pressed_obj, gpio_btn_is_pressed);

static mp_obj_t gpio_btn_value(mp_obj_t self_in) {
    gpio_btn_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_int(Cy_GPIO_Read(btn_table[self->index].port,
                                       btn_table[self->index].pin));
}
static MP_DEFINE_CONST_FUN_OBJ_1(gpio_btn_value_obj, gpio_btn_value);

static mp_obj_t gpio_btn_name(mp_obj_t self_in) {
    gpio_btn_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_str(btn_table[self->index].name,
                          strlen(btn_table[self->index].name));
}
static MP_DEFINE_CONST_FUN_OBJ_1(gpio_btn_name_obj, gpio_btn_name);

static void gpio_btn_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    gpio_btn_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "Button(%d, name='%s')", self->index, btn_table[self->index].name);
}

static const mp_rom_map_elem_t gpio_btn_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_is_pressed), MP_ROM_PTR(&gpio_btn_is_pressed_obj) },
    { MP_ROM_QSTR(MP_QSTR_value),      MP_ROM_PTR(&gpio_btn_value_obj) },
    { MP_ROM_QSTR(MP_QSTR_name),       MP_ROM_PTR(&gpio_btn_name_obj) },
};
static MP_DEFINE_CONST_DICT(gpio_btn_locals_dict, gpio_btn_locals_table);

MP_DEFINE_CONST_OBJ_TYPE(
    gpio_btn_type,
    MP_QSTR_Button,
    MP_TYPE_FLAG_NONE,
    print, gpio_btn_print,
    locals_dict, &gpio_btn_locals_dict
);

/*******************************************************************************
 * Module-Level Functions
 *******************************************************************************/

/* Cached LED/Button objects (one per index, allocated on first access) */
static gpio_led_obj_t led_objs[NUM_LEDS];
static gpio_btn_obj_t btn_objs[NUM_BTNS];
static bool objs_initialized = false;

static void init_cached_objects(void) {
    if (objs_initialized) return;
    for (int i = 0; i < NUM_LEDS; i++) {
        led_objs[i].base.type = &gpio_led_type;
        led_objs[i].index = i;
    }
    for (int i = 0; i < NUM_BTNS; i++) {
        btn_objs[i].base.type = &gpio_btn_type;
        btn_objs[i].index = i;
    }
    objs_initialized = true;
}

/* gpio.led(n) -> LED object */
static mp_obj_t mod_gpio_led(mp_obj_t index_obj) {
    gpio_hw_init();
    init_cached_objects();
    int idx = mp_obj_get_int(index_obj);
    if (idx < 0 || idx >= NUM_LEDS) {
        mp_raise_msg_varg(&mp_type_ValueError,
            MP_ERROR_TEXT("LED index %d out of range (0-%d)"), idx, NUM_LEDS - 1);
    }
    return MP_OBJ_FROM_PTR(&led_objs[idx]);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_gpio_led_obj, mod_gpio_led);

/* gpio.button(n) -> Button object */
static mp_obj_t mod_gpio_button(mp_obj_t index_obj) {
    gpio_hw_init();
    init_cached_objects();
    int idx = mp_obj_get_int(index_obj);
    if (idx < 0 || idx >= NUM_BTNS) {
        mp_raise_msg_varg(&mp_type_ValueError,
            MP_ERROR_TEXT("Button index %d out of range (0-%d)"), idx, NUM_BTNS - 1);
    }
    return MP_OBJ_FROM_PTR(&btn_objs[idx]);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_gpio_button_obj, mod_gpio_button);

/* gpio.num_leds() -> int */
static mp_obj_t mod_gpio_num_leds(void) {
    return mp_obj_new_int(NUM_LEDS);
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_gpio_num_leds_obj, mod_gpio_num_leds);

/* gpio.num_buttons() -> int */
static mp_obj_t mod_gpio_num_buttons(void) {
    return mp_obj_new_int(NUM_BTNS);
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_gpio_num_buttons_obj, mod_gpio_num_buttons);

/* gpio.board_info() -> dict */
static mp_obj_t mod_gpio_board_info(void) {
    mp_obj_dict_t *dict = mp_obj_new_dict(3);
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_name),
                      mp_obj_new_str(BOARD_NAME, strlen(BOARD_NAME)));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_leds),
                      mp_obj_new_int(NUM_LEDS));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_buttons),
                      mp_obj_new_int(NUM_BTNS));

    /* Add LED names list */
    mp_obj_t led_names[NUM_LEDS];
    for (int i = 0; i < NUM_LEDS; i++) {
        led_names[i] = mp_obj_new_str(led_table[i].name,
                                       strlen(led_table[i].name));
    }
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_led_names),
                      mp_obj_new_list(NUM_LEDS, led_names));

    /* Add button names list */
    mp_obj_t btn_names[NUM_BTNS];
    for (int i = 0; i < NUM_BTNS; i++) {
        btn_names[i] = mp_obj_new_str(btn_table[i].name,
                                       strlen(btn_table[i].name));
    }
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_btn_names),
                      mp_obj_new_list(NUM_BTNS, btn_names));

    return MP_OBJ_FROM_PTR(dict);
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_gpio_board_info_obj, mod_gpio_board_info);

/*******************************************************************************
 * Module Definition
 *******************************************************************************/

static const mp_rom_map_elem_t gpio_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),    MP_ROM_QSTR(MP_QSTR_gpio) },
    { MP_ROM_QSTR(MP_QSTR_led),         MP_ROM_PTR(&mod_gpio_led_obj) },
    { MP_ROM_QSTR(MP_QSTR_button),      MP_ROM_PTR(&mod_gpio_button_obj) },
    { MP_ROM_QSTR(MP_QSTR_num_leds),    MP_ROM_PTR(&mod_gpio_num_leds_obj) },
    { MP_ROM_QSTR(MP_QSTR_num_buttons), MP_ROM_PTR(&mod_gpio_num_buttons_obj) },
    { MP_ROM_QSTR(MP_QSTR_board_info),  MP_ROM_PTR(&mod_gpio_board_info_obj) },
    /* Expose types for isinstance() checks */
    { MP_ROM_QSTR(MP_QSTR_LED),         MP_ROM_PTR(&gpio_led_type) },
    { MP_ROM_QSTR(MP_QSTR_Button),      MP_ROM_PTR(&gpio_btn_type) },
};
static MP_DEFINE_CONST_DICT(gpio_module_globals, gpio_module_globals_table);

const mp_obj_module_t mp_module_gpio = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&gpio_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_gpio, mp_module_gpio);
