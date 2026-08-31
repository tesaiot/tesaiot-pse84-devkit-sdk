/*******************************************************************************
 * File Name: mod_qwa309_pots.c
 *
 * Description: MicroPython 'pots' module for CM33_NS (QWA309 base board).
 *
 *              Reads the four potentiometers VR1-4 (P15.4-7) straight from the
 *              free-running AutAnalog SAR result registers — no IPC, no CM55
 *              round-trip, microsecond latency. This is the game-rate input
 *              path: a 60 fps Pong paddle reads pots.read(0) every frame.
 *
 *              CM33_NS main.c starts the SAR (Cy_AutAnalog_Init +
 *              StartAutonomousControl) at boot when BSP_HAS_POTENTIOMETER=1;
 *              result-register reads are read-only and concurrent with the
 *              CM55 sensor-poll page (same registers, 200 ms tick), so both
 *              consumers coexist without arbitration.
 *
 *              SAR channel mapping (design.modus): VR1-4 = P15.4-7 = SAR GPIO
 *              channels 4-7, 12-bit, 0-1.8 V. A 0 count alone is NOT proof of
 *              life — an unconfigured channel also reads 0 — so pots.ready()
 *              exposes the per-channel result-status gate for bring-up checks.
 *
 * Usage:
 *   import pots
 *   pots.read(0)      # -> VR1 raw count 0..4095
 *   pots.read_all()   # -> (vr1, vr2, vr3, vr4)
 *   pots.norm(0)      # -> 0.0..1.0 (float)
 *   pots.ready()      # -> True once all 4 channels have live results
 *   pots.count()      # -> 4
 *
 * Gated by BSP_HAS_QWA309_BASEBOARD (+ BSP_HAS_POTENTIOMETER for the SAR boot
 * init). Smoothing/hysteresis for game feel lives in bentogame.game.pot() —
 * this module returns the raw hardware truth.
 *******************************************************************************/

#include "bsp_feature_flags.h"

#if BSP_HAS_QWA309_BASEBOARD

#include "py/runtime.h"
#include "py/obj.h"

#include "cy_pdl.h"

#define POTS_COUNT          (4U)
#define POTS_SAR_IDX        (0U)     /* SAR ADC index 0 */
#define POTS_ADC_MAX        (4095)   /* 12-bit SAR */

/* Silkscreen knob -> SAR GPIO channel. NOT identity: the QWA309 PCB routes
 * P15.4 (ch 4) to the VR2 knob and P15.5 (ch 5) to VR1 — hardware-verified
 * 2026-07-31 and long compensated by the CM55 sensor-poll page
 * (cm55_sensor_poll.c s_pot_adc_ch). This module shipped uncompensated, so
 * pots.read(0) read the knob silkscreened VR2; fixed 2026-08-20 so indices
 * finally mean what bentogame's docstring always claimed ("0..3 = VR1..VR4").
 * Keep this table in lockstep with cm55_sensor_poll.c. */
static const uint8_t s_pots_knob_ch[POTS_COUNT] = { 5U, 4U, 6U, 7U };
/* All four pot channels must have produced a result in the last scan. */
#define POTS_ADC_READY_MASK ((uint8_t)(CY_AUTANALOG_SAR_CHAN_MASK_GPIO4 | \
                                       CY_AUTANALOG_SAR_CHAN_MASK_GPIO5 | \
                                       CY_AUTANALOG_SAR_CHAN_MASK_GPIO6 | \
                                       CY_AUTANALOG_SAR_CHAN_MASK_GPIO7))

/* Spike filter state — the SAR occasionally glitches a sample to near-zero;
 * same reject rule as the CM55 sensor-poll page so both paths agree. */
static int32_t s_pots_prev[POTS_COUNT] = {-1, -1, -1, -1};
static uint8_t s_pots_rej[POTS_COUNT]  = {0, 0, 0, 0};

static int32_t pots_read_channel(uint32_t idx)
{
    int32_t adc_count = Cy_AutAnalog_SAR_ReadResult(
        POTS_SAR_IDX, CY_AUTANALOG_SAR_INPUT_GPIO,
        s_pots_knob_ch[idx]);

    if (adc_count < 0) adc_count = 0;
    if (adc_count > POTS_ADC_MAX) adc_count = POTS_ADC_MAX;

    if (s_pots_prev[idx] >= 0) {
        int32_t delta = adc_count - s_pots_prev[idx];
        if (delta < 0) delta = -delta;
        /* Reject a glitch-to-zero ONCE; a second consecutive low reading is
         * the player really slamming the knob to the end stop — accept it,
         * or the filter latches the stale value until the pot moves back. */
        if (delta > 200 && adc_count < 50 && s_pots_rej[idx] == 0U) {
            s_pots_rej[idx] = 1U;
            adc_count = s_pots_prev[idx];
        } else {
            s_pots_rej[idx] = 0U;
        }
    }
    s_pots_prev[idx] = adc_count;
    return adc_count;
}

static uint32_t pots_check_index(mp_obj_t n_obj)
{
    mp_int_t n = mp_obj_get_int(n_obj);
    if (n < 0 || n >= (mp_int_t)POTS_COUNT) {
        mp_raise_ValueError(MP_ERROR_TEXT("pots: index must be 0..3 (VR1..VR4)"));
    }
    return (uint32_t)n;
}

/* pots.read(n) -> raw ADC count 0..4095 */
static mp_obj_t pots_read(mp_obj_t n_obj)
{
    return mp_obj_new_int(pots_read_channel(pots_check_index(n_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(pots_read_obj, pots_read);

/* pots.read_all() -> (vr1, vr2, vr3, vr4) */
static mp_obj_t pots_read_all(void)
{
    mp_obj_t items[POTS_COUNT];
    for (uint32_t i = 0; i < POTS_COUNT; i++) {
        items[i] = mp_obj_new_int(pots_read_channel(i));
    }
    return mp_obj_new_tuple(POTS_COUNT, items);
}
static MP_DEFINE_CONST_FUN_OBJ_0(pots_read_all_obj, pots_read_all);

/* pots.norm(n) -> 0.0..1.0 */
static mp_obj_t pots_norm(mp_obj_t n_obj)
{
    int32_t raw = pots_read_channel(pots_check_index(n_obj));
    return mp_obj_new_float((mp_float_t)raw / (mp_float_t)POTS_ADC_MAX);
}
static MP_DEFINE_CONST_FUN_OBJ_1(pots_norm_obj, pots_norm);

/* pots.ready() -> True when all 4 channels report live results */
static mp_obj_t pots_ready(void)
{
    uint8_t st = Cy_AutAnalog_SAR_GetHSchanResultStatus(POTS_SAR_IDX);
    return mp_obj_new_bool((st & POTS_ADC_READY_MASK) == POTS_ADC_READY_MASK);
}
static MP_DEFINE_CONST_FUN_OBJ_0(pots_ready_obj, pots_ready);

/* pots.count() -> 4 */
static mp_obj_t pots_count(void)
{
    return MP_OBJ_NEW_SMALL_INT(POTS_COUNT);
}
static MP_DEFINE_CONST_FUN_OBJ_0(pots_count_obj, pots_count);

static const mp_rom_map_elem_t pots_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pots) },
    { MP_ROM_QSTR(MP_QSTR_read),     MP_ROM_PTR(&pots_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_read_all), MP_ROM_PTR(&pots_read_all_obj) },
    { MP_ROM_QSTR(MP_QSTR_norm),     MP_ROM_PTR(&pots_norm_obj) },
    { MP_ROM_QSTR(MP_QSTR_ready),    MP_ROM_PTR(&pots_ready_obj) },
    { MP_ROM_QSTR(MP_QSTR_count),    MP_ROM_PTR(&pots_count_obj) },
};
static MP_DEFINE_CONST_DICT(pots_module_globals, pots_module_globals_table);

const mp_obj_module_t mp_module_pots = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&pots_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_pots, mp_module_pots);

#endif /* BSP_HAS_QWA309_BASEBOARD */
