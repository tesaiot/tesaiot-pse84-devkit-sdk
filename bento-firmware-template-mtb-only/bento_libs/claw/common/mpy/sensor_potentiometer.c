/*******************************************************************************
 * File Name: sensor_potentiometer.c
 *
 * Description: Potentiometer ADC driver (requires base board with SAR ADC).
 *              Uses MTB HAL ADC API with BSP-generated CYBSP_SAR_ADC config.
 *              SAR ADC channel GPIO0 on P15[1] (CYBSP_ADC_6_POT).
 *
 *              Reference voltage: VDDA (3.3V typical).
 *              Resolution: 12-bit native, scaled to 16-bit by HAL.
 *
 *******************************************************************************/

#include "sensor_potentiometer.h"
#include "mtb_hal.h"
#include "cybsp.h"
#include <stdio.h>

/* VDDA voltage for scaling */
#define POTENTIOMETER_VDDA_V    (3.3f)

/* ADC HAL objects */
static mtb_hal_adc_t adc_obj;
static mtb_hal_adc_channel_t *adc_channels[1] = { NULL };
static bool pot_initialized = false;

bool potentiometer_init(void)
{
    if (pot_initialized) {
        return true;
    }

    /* Initialize ADC using BSP-generated configurator */
    cy_rslt_t result = mtb_hal_adc_setup(
        &adc_obj,
        &CYBSP_SAR_ADC_hal_config,
        NULL,   /* Use default clock */
        adc_channels
    );
    if (result != CY_RSLT_SUCCESS) {
        printf("[POT] ADC setup failed: 0x%08lx\r\n", (unsigned long)result);
        return false;
    }

    /* Enable ADC */
    result = mtb_hal_adc_enable(&adc_obj, true);
    if (result != CY_RSLT_SUCCESS) {
        printf("[POT] ADC enable failed: 0x%08lx\r\n", (unsigned long)result);
        return false;
    }

    if (adc_channels[0] == NULL) {
        printf("[POT] ADC channel 0 not configured\r\n");
        return false;
    }

    pot_initialized = true;
    printf("[POT] ADC initialized OK (P15[1], VDDA ref)\r\n");
    return true;
}

bool potentiometer_read_raw(uint16_t *value)
{
    if (!pot_initialized && !potentiometer_init()) {
        return false;
    }

    /* Read 16-bit scaled value (0x0000 = 0V, 0xFFFF = VDDA) */
    *value = mtb_hal_adc_read_u16(adc_channels[0]);
    return true;
}

bool potentiometer_read_percent(float *percent)
{
    uint16_t raw;
    if (!potentiometer_read_raw(&raw)) {
        return false;
    }
    *percent = ((float)raw / 65535.0f) * 100.0f;
    return true;
}

bool potentiometer_read_voltage(float *voltage)
{
    uint16_t raw;
    if (!potentiometer_read_raw(&raw)) {
        return false;
    }
    *voltage = ((float)raw / 65535.0f) * POTENTIOMETER_VDDA_V;
    return true;
}
