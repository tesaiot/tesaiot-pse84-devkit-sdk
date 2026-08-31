/*******************************************************************************
 * File Name: gfxss_config.c
 *
 * Description: GFXSS peripheral configuration structs for KIT_PSE84_AI.
 *              Based on Face ID Demo BSP (TARGET_APP_KIT_PSE84_AI).
 *              Provides GFXSS_config, DC config, GPU config, layer configs,
 *              and MIPI DSI configuration for Waveshare 4.3" DSI (832x480).
 *
 *******************************************************************************/

#include "gfxss_config.h"
#include "cy_sysclk.h"
#include "cybsp.h"

/*******************************************************************************
 * Enable GFXSS peripheral clocks.
 * The BSP's init_cycfg_peripherals() does not include these calls because
 * GFXSS was not enabled in design.modus. We add them manually.
 *******************************************************************************/
void gfxss_clock_init(void)
{
    Cy_SysClk_PeriGroupSlaveInit(CY_MMIO_GFXSS_GPU_PERI_NR,
                                 CY_MMIO_GFXSS_GPU_GROUP_NR,
                                 CY_MMIO_GFXSS_GPU_SLAVE_NR,
                                 CY_MMIO_GFXSS_GPU_CLK_HF_NR);

    Cy_SysClk_PeriGroupSlaveInit(CY_MMIO_GFXSS_DC_PERI_NR,
                                 CY_MMIO_GFXSS_DC_GROUP_NR,
                                 CY_MMIO_GFXSS_DC_SLAVE_NR,
                                 CY_MMIO_GFXSS_DC_CLK_HF_NR);

    Cy_SysClk_PeriGroupSlaveInit(CY_MMIO_GFXSS_MIPIDSI_PERI_NR,
                                 CY_MMIO_GFXSS_MIPIDSI_GROUP_NR,
                                 CY_MMIO_GFXSS_MIPIDSI_SLAVE_NR,
                                 CY_MMIO_GFXSS_MIPIDSI_CLK_HF_NR);
}

/* Graphics layer (primary framebuffer) */
cy_stc_gfx_layer_config_t GFXSS_graphics_layer =
{
    .layer_type = GFX_LAYER_GRAPHICS,
    .buffer_address = (gctADDRESS *)CY_SOCMEM_RAM_BASE,
    .uv_buffer_address = (gctADDRESS *)CY_SOCMEM_RAM_BASE,
    .input_format_type = vivRGB565,
    .tiling_type = vivLINEAR,
    .pos_x = 0,
    .pos_y = 0,
    .width = 832,
    .height = 480,
    .zorder = 0,
    .layer_enable = true,
    .visibility = true,
};

/* Overlay 0 layer (disabled) */
cy_stc_gfx_layer_config_t GFXSS_overlay0_layer =
{
    .layer_type = GFX_LAYER_OVERLAY0,
    .buffer_address = (gctADDRESS *)CY_SOCMEM_RAM_BASE,
    .uv_buffer_address = (gctADDRESS *)CY_SOCMEM_RAM_BASE,
    .input_format_type = vivRGB565,
    .tiling_type = vivLINEAR,
    .pos_x = 0,
    .pos_y = 0,
    .width = 1,
    .height = 1,
    .zorder = 0,
    .layer_enable = false,
    .visibility = false,
};

/* Overlay 1 layer (disabled) */
cy_stc_gfx_layer_config_t GFXSS_overlay1_layer =
{
    .layer_type = GFX_LAYER_OVERLAY1,
    .buffer_address = (gctADDRESS *)CY_SOCMEM_RAM_BASE,
    .uv_buffer_address = (gctADDRESS *)CY_SOCMEM_RAM_BASE,
    .input_format_type = vivRGB565,
    .tiling_type = vivLINEAR,
    .pos_x = 0,
    .pos_y = 0,
    .width = 1,
    .height = 1,
    .zorder = 0,
    .layer_enable = false,
    .visibility = false,
};

/* Display controller configuration */
cy_stc_gfx_dc_config_t GFXSS_dc_config =
{
    .gfx_layer_config = &GFXSS_graphics_layer,
    .ovl0_layer_config = &GFXSS_overlay0_layer,
    .ovl1_layer_config = &GFXSS_overlay1_layer,
    .display_type = GFX_DISP_TYPE_DSI_DPI,
    .display_format = vivD24,
    .display_size = vivDISPLAY_CUSTOMIZED,
    .display_width = 832,
    .display_height = 480,
};

/* GPU configuration */
cy_stc_gfx_gpu_cfg_t GFXSS_gpu_config =
{
    .enable = true,
};

/* MIPI DSI display timing parameters for 4.3" (832x480) */
cy_stc_mipidsi_display_params_t GFXSS_mipidsi_display_params =
{
    .pixel_clock = 33768,
    .hdisplay = 832,
    .hsync_width = 10,
    .hfp = 210,
    .hbp = 20,
    .vdisplay = 480,
    .vsync_width = 5,
    .vfp = 20,
    .vbp = 20,
    .polarity_flags = 0,
};

/* MIPI DSI configuration */
cy_stc_mipidsi_config_t GFXSS_mipi_dsi_config =
{
    .virtual_ch = 0,
    .num_of_lanes = 1,
    .per_lane_mbps = 850,
    .dpi_fmt = CY_MIPIDSI_FMT_RGB888,
    .dsi_mode = DSI_VIDEO_MODE,
    .max_phy_clk = 2500000000,
    .mode_flags = VID_MODE_TYPE_BURST | ENABLE_LOW_POWER_CMD | ENABLE_LOW_POWER,
    .display_params = &GFXSS_mipidsi_display_params,
};

/* Main GFXSS configuration */
cy_stc_gfx_config_t GFXSS_config =
{
    .dc_cfg = &GFXSS_dc_config,
    .gpu_cfg = &GFXSS_gpu_config,
    .mipi_dsi_cfg = &GFXSS_mipi_dsi_config,
    .display_update_type = GFX_DOUBLE_BUFFER,
    .clockHz = 399999999U,
};
