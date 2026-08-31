/*******************************************************************************
 * File Name: gfxss_config.h
 *
 * Description: GFXSS peripheral configuration for KIT_PSE84_AI.
 *              Provides IRQ defines and extern declarations that the BSP's
 *              auto-generated cycfg_peripherals.h does not include.
 *              Based on Face ID Demo BSP (TARGET_APP_KIT_PSE84_AI).
 *
 *******************************************************************************/

#ifndef GFXSS_CONFIG_H
#define GFXSS_CONFIG_H

#include "cy_graphics.h"
#include "cy_mipidsi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* GFXSS IRQ definitions */
#define gfxss_0_ENABLED 1U
#define GFXSS_HW GFXSS
#define GFXSS_GPU_IRQ gfxss_interrupt_gpu_IRQn
#define GFXSS_DC_IRQ gfxss_interrupt_dc_IRQn
#define GFXSS_MIPIDSI_IRQ gfxss_interrupt_mipidsi_IRQn

/* GFXSS configuration structs */
extern cy_stc_gfx_layer_config_t GFXSS_graphics_layer;
extern cy_stc_gfx_layer_config_t GFXSS_overlay0_layer;
extern cy_stc_gfx_layer_config_t GFXSS_overlay1_layer;
extern cy_stc_gfx_dc_config_t GFXSS_dc_config;
extern cy_stc_gfx_gpu_cfg_t GFXSS_gpu_config;
extern cy_stc_mipidsi_display_params_t GFXSS_mipidsi_display_params;
extern cy_stc_mipidsi_config_t GFXSS_mipi_dsi_config;
extern cy_stc_gfx_config_t GFXSS_config;

/* Enable GFXSS peripheral clocks (GPU, DC, MIPI DSI) */
void gfxss_clock_init(void);

#ifdef __cplusplus
}
#endif

#endif /* GFXSS_CONFIG_H */
