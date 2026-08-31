/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Wiroon Sriborrirux (BDH) / Advance Innovation Centre
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 */

// Minimal, BSP-adaptive hardware machine.Timer for PSoC Edge.
//
// Runs on the free TCPWM0 Group-0 (32-bit) counters of WHATEVER board this is
// built for: at start-up it builds a "reserved" mask from the BSP's peripheral
// macros (the time base, LED PWM, dead-time, camera PWM, ...) and exposes every
// remaining Group-0 counter as a Timer id (0, 1, 2, ...). That way the same code
// never collides with a board's own peripherals (Eva: time=0,LED=5/6/7,dead=7 ->
// free 1/3/4; AI: time=2,LED=0,cam=4,dead=7 -> free 1/3/5/6).
//
// Clock: the shared 16-bit divider #3 @ 1 MHz (1 tick = 1 us). Callbacks are
// soft-scheduled (mp_sched_schedule) which is FreeRTOS-safe; no hard IRQ.

#include "py/mpconfig.h"

#if ENABLE_MACHINE_TIMER

#include "py/obj.h"
#include "py/runtime.h"
#include "py/mphal.h"
#include "modmachine.h"

#include "cybsp.h"
#include "cy_tcpwm_counter.h"
#include "cy_tcpwm.h"
#include "cy_sysclk.h"
#include "cy_sysint.h"
#include "mtb_hal.h"

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

#define TIMER_MAX_INSTANCES   (6)          // most free Group-0 counters we expose
#define TIMER_DIV_TYPE        CY_SYSCLK_DIV_16_BIT
#define TIMER_DIV_NUM         (3U)         // shared 16-bit divider #3 (1 MHz)
#define TIMER_CLK_HZ          (1000000UL)  // 1 MHz -> 1 tick = 1 us
#define TIMER_IN_DIS          (0x3U)       // input-trigger "disabled" mode field

#define TIMER_MODE_ONE_SHOT   (1)
#define TIMER_MODE_PERIODIC   (2)

// ---------------------------------------------------------------------------
// Per-counter (0..7) constant lookups — indexed by the physical counter number.
// ---------------------------------------------------------------------------

static const en_clk_dst_t pclk_by_cnt[8] = {
    PCLK_TCPWM0_CLOCK_COUNTER_EN0, PCLK_TCPWM0_CLOCK_COUNTER_EN1,
    PCLK_TCPWM0_CLOCK_COUNTER_EN2, PCLK_TCPWM0_CLOCK_COUNTER_EN3,
    PCLK_TCPWM0_CLOCK_COUNTER_EN4, PCLK_TCPWM0_CLOCK_COUNTER_EN5,
    PCLK_TCPWM0_CLOCK_COUNTER_EN6, PCLK_TCPWM0_CLOCK_COUNTER_EN7,
};
static const IRQn_Type irqn_by_cnt[8] = {
    tcpwm_0_interrupts_0_IRQn, tcpwm_0_interrupts_1_IRQn,
    tcpwm_0_interrupts_2_IRQn, tcpwm_0_interrupts_3_IRQn,
    tcpwm_0_interrupts_4_IRQn, tcpwm_0_interrupts_5_IRQn,
    tcpwm_0_interrupts_6_IRQn, tcpwm_0_interrupts_7_IRQn,
};

// Group-0 counters this BSP reserves for its own peripherals + the time base.
// Built from the CYBSP_* macros that exist on the current board, so Timer never
// grabs a counter another driver owns.
static uint32_t timer_reserved_mask(void) {
    uint32_t m = (1u << 2);   // modtime routes its clock via EN2 on some BSPs -> avoid
    #ifdef CYBSP_GENERAL_PURPOSE_TIMER_NUM
    if (CYBSP_GENERAL_PURPOSE_TIMER_NUM < 8) m |= 1u << CYBSP_GENERAL_PURPOSE_TIMER_NUM;
    #endif
    #ifdef CYBSP_DEAD_TIME_PWM_NUM
    if (CYBSP_DEAD_TIME_PWM_NUM < 8) m |= 1u << CYBSP_DEAD_TIME_PWM_NUM;
    #endif
    #ifdef CYBSP_PWM_LED_CTRL_NUM
    if (CYBSP_PWM_LED_CTRL_NUM < 8) m |= 1u << CYBSP_PWM_LED_CTRL_NUM;
    #endif
    #ifdef CYBSP_PWM_DVP_CAM_CTRL_NUM
    if (CYBSP_PWM_DVP_CAM_CTRL_NUM < 8) m |= 1u << CYBSP_PWM_DVP_CAM_CTRL_NUM;
    #endif
    #ifdef ENABLE_LED_PWM
    m |= (1u << 5) | (1u << 6) | (1u << 7);  // gpio.led().brightness() LED counters (Eva)
    #endif
    return m;
}

// Free-counter pool, built once on first use.
static uint8_t timer_free_cnt[TIMER_MAX_INSTANCES];
static int timer_num_free = -1;

static void timer_build_pool(void) {
    if (timer_num_free >= 0) {
        return;
    }
    uint32_t reserved = timer_reserved_mask();
    timer_num_free = 0;
    for (uint32_t c = 0; c < 8 && timer_num_free < TIMER_MAX_INSTANCES; c++) {
        if (!(reserved & (1u << c))) {
            timer_free_cnt[timer_num_free++] = (uint8_t)c;
        }
    }
}

// ---------------------------------------------------------------------------
// Object type
// ---------------------------------------------------------------------------

typedef struct _machine_timer_obj_t {
    mp_obj_base_t base;
    uint8_t id;              // 0..timer_num_free-1
    uint8_t counter_num;     // physical TCPWM0 counter (0..7)
    uint16_t mode;
    mp_obj_t callback;
    bool active;
} machine_timer_obj_t;

extern const mp_obj_type_t machine_timer_type;  // forward decl

static machine_timer_obj_t *timer_obj[TIMER_MAX_INSTANCES] = { NULL };
static bool timer_clock_configured = false;

// ---------------------------------------------------------------------------
// IRQ handling
// ---------------------------------------------------------------------------

static void machine_timer_isr(machine_timer_obj_t *self) {
    if (self == NULL) {
        return;
    }
    if (Cy_TCPWM_GetInterruptStatusMasked(TCPWM0, self->counter_num) & CY_TCPWM_INT_ON_TC) {
        Cy_TCPWM_ClearInterrupt(TCPWM0, self->counter_num, CY_TCPWM_INT_ON_TC);
        if (self->mode == TIMER_MODE_ONE_SHOT) {
            Cy_TCPWM_Counter_Disable(TCPWM0, self->counter_num);
            self->active = false;
        }
        if (self->callback != mp_const_none) {
            mp_sched_schedule(self->callback, MP_OBJ_FROM_PTR(self));
        }
    }
}

// One trampoline per id (NVIC vector is per-counter).
static void machine_timer_irq_0(void) { machine_timer_isr(timer_obj[0]); }
static void machine_timer_irq_1(void) { machine_timer_isr(timer_obj[1]); }
static void machine_timer_irq_2(void) { machine_timer_isr(timer_obj[2]); }
static void machine_timer_irq_3(void) { machine_timer_isr(timer_obj[3]); }
static void machine_timer_irq_4(void) { machine_timer_isr(timer_obj[4]); }
static void machine_timer_irq_5(void) { machine_timer_isr(timer_obj[5]); }
static const cy_israddress machine_timer_irq_handlers[TIMER_MAX_INSTANCES] = {
    machine_timer_irq_0, machine_timer_irq_1, machine_timer_irq_2,
    machine_timer_irq_3, machine_timer_irq_4, machine_timer_irq_5,
};

// ---------------------------------------------------------------------------
// Clock + counter setup
// ---------------------------------------------------------------------------

static void machine_timer_configure_clock(en_clk_dst_t pclk) {
    Cy_SysClk_PeriPclkAssignDivider(pclk, TIMER_DIV_TYPE, TIMER_DIV_NUM);
    if (!timer_clock_configured) {
        mtb_hal_peri_div_t cref = {
            .clk_dst = pclk,
            .div_type = TIMER_DIV_TYPE,
            .div_num = TIMER_DIV_NUM,
        };
        mtb_hal_clock_set_peri_clock_freq(&cref, TIMER_CLK_HZ, 1000u);
        timer_clock_configured = true;
    }
}

static void machine_timer_start(machine_timer_obj_t *self, uint32_t period_ticks) {
    uint32_t cnt = self->counter_num;
    en_clk_dst_t pclk = pclk_by_cnt[cnt];
    IRQn_Type irqn = irqn_by_cnt[cnt];

    Cy_TCPWM_Counter_Disable(TCPWM0, cnt);
    NVIC_DisableIRQ(irqn);

    machine_timer_configure_clock(pclk);

    // Counter counts 0 -> period (inclusive) then fires TC; so period = ticks-1.
    cy_stc_tcpwm_counter_config_t cfg = { 0 };
    cfg.period = period_ticks - 1U;
    cfg.clockPrescaler = CY_TCPWM_COUNTER_PRESCALER_DIVBY_1;
    cfg.runMode = (self->mode == TIMER_MODE_ONE_SHOT)
        ? CY_TCPWM_COUNTER_ONESHOT : CY_TCPWM_COUNTER_CONTINUOUS;
    cfg.countDirection = CY_TCPWM_COUNTER_COUNT_UP;
    cfg.compareOrCapture = CY_TCPWM_COUNTER_MODE_COMPARE;
    cfg.interruptSources = CY_TCPWM_INT_ON_TC;
    cfg.captureInputMode = TIMER_IN_DIS;  cfg.captureInput = CY_TCPWM_INPUT_0;
    cfg.reloadInputMode = TIMER_IN_DIS;   cfg.reloadInput = CY_TCPWM_INPUT_0;
    cfg.startInputMode = TIMER_IN_DIS;    cfg.startInput = CY_TCPWM_INPUT_0;
    cfg.stopInputMode = TIMER_IN_DIS;     cfg.stopInput = CY_TCPWM_INPUT_0;
    cfg.countInputMode = TIMER_IN_DIS;    cfg.countInput = CY_TCPWM_INPUT_1;

    if (Cy_TCPWM_Counter_Init(TCPWM0, cnt, &cfg) != CY_TCPWM_SUCCESS) {
        mp_raise_msg_varg(&mp_type_ValueError,
            MP_ERROR_TEXT("Timer(%u) init failed"), (unsigned)self->id);
    }

    cy_stc_sysint_t intr_cfg = { irqn, 3 };
    if (Cy_SysInt_Init(&intr_cfg, machine_timer_irq_handlers[self->id]) != CY_SYSINT_SUCCESS) {
        mp_raise_msg_varg(&mp_type_ValueError,
            MP_ERROR_TEXT("Timer(%u) IRQ init failed"), (unsigned)self->id);
    }
    NVIC_ClearPendingIRQ(irqn);
    NVIC_EnableIRQ(irqn);

    self->active = true;
    Cy_TCPWM_Counter_Enable(TCPWM0, cnt);
    Cy_TCPWM_TriggerReloadOrIndex_Single(TCPWM0, cnt);
}

// ---------------------------------------------------------------------------
// init helper (shared by make_new + .init())
// ---------------------------------------------------------------------------

static mp_obj_t machine_timer_init_helper(machine_timer_obj_t *self,
    size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    enum { ARG_mode, ARG_callback, ARG_period, ARG_freq };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_mode,     MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = TIMER_MODE_PERIODIC} },
        { MP_QSTR_callback, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_period,   MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_freq,     MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args,
        MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    mp_int_t mode = args[ARG_mode].u_int;
    if (mode != TIMER_MODE_ONE_SHOT && mode != TIMER_MODE_PERIODIC) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid mode"));
    }
    self->mode = (uint16_t)mode;

    mp_obj_t callback = args[ARG_callback].u_obj;
    if (callback != mp_const_none && !mp_obj_is_callable(callback)) {
        mp_raise_ValueError(MP_ERROR_TEXT("callback must be callable"));
    }
    self->callback = callback;

    uint64_t ticks;
    if (args[ARG_freq].u_obj != mp_const_none) {
        mp_int_t freq = mp_obj_get_int(args[ARG_freq].u_obj);
        if (freq <= 0) {
            mp_raise_ValueError(MP_ERROR_TEXT("freq must be > 0"));
        }
        ticks = (uint64_t)TIMER_CLK_HZ / (uint64_t)(uint32_t)freq;
    } else if (args[ARG_period].u_int > 0) {
        ticks = (uint64_t)TIMER_CLK_HZ * (uint64_t)(uint32_t)args[ARG_period].u_int / 1000UL;
    } else {
        mp_raise_ValueError(MP_ERROR_TEXT("either freq or period must be specified"));
    }
    if (ticks == 0U || ticks > UINT32_MAX) {
        mp_raise_ValueError(MP_ERROR_TEXT("period out of range"));
    }

    machine_timer_start(self, (uint32_t)ticks);
    return mp_const_none;
}

// ---------------------------------------------------------------------------
// machine.Timer(id [, mode=, freq=, period=, callback=])
// ---------------------------------------------------------------------------

static mp_obj_t machine_timer_make_new(const mp_obj_type_t *type,
    size_t n_args, size_t n_kw, const mp_obj_t *args) {

    mp_arg_check_num(n_args, n_kw, 1, MP_OBJ_FUN_ARGS_MAX, true);
    timer_build_pool();

    mp_int_t id = mp_obj_get_int(args[0]);
    if (id < 0 || id >= timer_num_free) {
        mp_raise_msg_varg(&mp_type_ValueError,
            MP_ERROR_TEXT("Timer id must be 0-%d on this board"), timer_num_free - 1);
    }

    machine_timer_obj_t *self = timer_obj[id];
    if (self == NULL) {
        self = mp_obj_malloc(machine_timer_obj_t, &machine_timer_type);
        self->id = (uint8_t)id;
        self->counter_num = timer_free_cnt[id];
        self->callback = mp_const_none;
        self->mode = TIMER_MODE_PERIODIC;
        self->active = false;
        timer_obj[id] = self;
    }

    if (n_args > 1 || n_kw > 0) {
        mp_map_t kw_map;
        mp_map_init_fixed_table(&kw_map, n_kw, args + n_args);
        machine_timer_init_helper(self, n_args - 1, args + 1, &kw_map);
    }
    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t machine_timer_init(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    return machine_timer_init_helper(MP_OBJ_TO_PTR(args[0]), n_args - 1, args + 1, kw_args);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(machine_timer_init_obj, 1, machine_timer_init);

static mp_obj_t machine_timer_deinit(mp_obj_t self_in) {
    machine_timer_obj_t *self = MP_OBJ_TO_PTR(self_in);
    IRQn_Type irqn = irqn_by_cnt[self->counter_num];
    Cy_TCPWM_Counter_Disable(TCPWM0, self->counter_num);
    NVIC_DisableIRQ(irqn);
    NVIC_ClearPendingIRQ(irqn);
    self->callback = mp_const_none;
    self->active = false;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_timer_deinit_obj, machine_timer_deinit);

static void machine_timer_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    machine_timer_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint32_t period_ticks = Cy_TCPWM_Counter_GetPeriod(TCPWM0, self->counter_num) + 1U;
    uint32_t freq = period_ticks != 0U ? (uint32_t)(TIMER_CLK_HZ / period_ticks) : 0U;
    mp_printf(print, "Timer(id=%u, mode=Timer.%s, freq=%uHz)",
        (unsigned)self->id,
        self->mode == TIMER_MODE_ONE_SHOT ? "ONE_SHOT" : "PERIODIC",
        (unsigned)freq);
}

static const mp_rom_map_elem_t machine_timer_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_init),     MP_ROM_PTR(&machine_timer_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit),   MP_ROM_PTR(&machine_timer_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_ONE_SHOT), MP_ROM_INT(TIMER_MODE_ONE_SHOT) },
    { MP_ROM_QSTR(MP_QSTR_PERIODIC), MP_ROM_INT(TIMER_MODE_PERIODIC) },
};
static MP_DEFINE_CONST_DICT(machine_timer_locals_dict, machine_timer_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    machine_timer_type,
    MP_QSTR_Timer,
    MP_TYPE_FLAG_NONE,
    make_new, machine_timer_make_new,
    print, machine_timer_print,
    locals_dict, &machine_timer_locals_dict
    );

#endif // ENABLE_MACHINE_TIMER
