/******************************************************************************
 * \file mtb_dvp_camera_ov7675_config.h
 *
 * \brief
 *     Master configuration file for OV7675 DVP camera.
 *     All user-tunable camera parameters are consolidated here:
 *       - Resolution & output format
 *       - Frame rate / clock
 *       - Image quality (brightness, contrast, color matrix, gamma)
 *       - Auto functions (AGC, AEC, AWB)
 *       - Banding filter, edge enhancement, de-noise
 *
 *     Edit this file to adjust camera behavior.
 *     See DVP_Camera/OV7675_REGISTER_TUNING.md for detailed guide.
 *
 ********************************************************************************
 * SPDX-FileCopyrightText: 2025-2026 TESAIoT Foundation Platform
 *
 * \author TESA Developer Team
 *
 * This file was developed by the TESA Developer Team as part of the DVP camera
 * integration for the PSoC Edge AI Face ID application, in collaboration with
 * Infineon Technologies AG for PSoC Edge E84.
 *
 * Link: https://tesaiot.github.io/developer-hub/
 *******************************************************************************/

#ifndef MTB_DVP_CAMERA_OV7675_CONFIG_H
#define MTB_DVP_CAMERA_OV7675_CONFIG_H

/*******************************************************************************
 *
 *  SECTION 1: CAMERA RESOLUTION & OUTPUT FORMAT
 *
 ******************************************************************************/

/*******************************************************************************
 * Resolution (OV7675_FRAME_WIDTH / OV7675_FRAME_HEIGHT)
 *
 * Supported modes:
 *   VGA  = 640 x 480  (current default, higher detail)
 *   QVGA = 320 x 240  (faster DMA, lower detail)
 *
 * IMPORTANT: Changing resolution also requires:
 *   1. Update CAMERA_WIDTH / CAMERA_HEIGHT in proj_cm55/Makefile
 *   2. Update DVP_CAMERA_WIDTH / DVP_CAMERA_HEIGHT in dvp_camera_task.h
 *   3. Update DMA descriptors in bsps/.../cycfg_dmas.c:
 *      - DW0:     xCount = WIDTH*2/256 (VGA=5, QVGA=3)
 *      - AXIDMAC: mCount = WIDTH*2, yCount = HEIGHT/2
 *   4. Adjust CLKRC for DMA timing (see Frame Rate section)
 ******************************************************************************/
#define OV7675_FRAME_WIDTH              (640u)
#define OV7675_FRAME_HEIGHT             (480u)

/*******************************************************************************
 * Output Format
 *
 * RGB565: 16-bit color (5R + 6G + 5B), 2 bytes per pixel
 *   - Used by display pipeline (vg_lite) and inference (converted to RGB888)
 *   - COM7 = 0x04, COM15 = 0xD0
 *
 * RGB555: 15-bit color (5R + 5G + 5B), 2 bytes per pixel
 *   - Not used in this project
 *   - COM7 = 0x04, COM15 = 0xF0
 ******************************************************************************/
#define OV7675_BYTES_PER_PIXEL          (2u)


/*******************************************************************************
 *
 *  SECTION 2: FRAME RATE / CLOCK
 *
 ******************************************************************************/

/*******************************************************************************
 * CLKRC (Register 0x11) - Clock prescaler
 *
 * Formula: PCLK = (XCLK * PLL) / (CLKRC + 1)
 *          With XCLK=24MHz, PLL=4x: PCLK = 96 / (CLKRC + 1) MHz
 *
 * DMA constraint: DW0 transfers 1 byte/PCLK at ~75ns each (200MHz clk_slow).
 * VGA needs 1280 bytes/line → 96us minimum per line.
 * Line time = (WIDTH*2) / PCLK. Must be >= 96us for stable capture.
 *
 * | CLKRC | PCLK (MHz) | Line Time (us) | VGA Status | Approx FPS |
 * |-------|-----------|----------------|------------|------------|
 * | 0x09  | 9.6       | 133 (w/blank)  | STABLE     | ~5         |
 * | 0x08  | 10.67     | 120            | ARTIFACTS  | ~6.5       |
 * | 0x07  | 12.0      | 107            | GARBLED    | ~7.5       |
 * | 0x04  | 19.2      | 67             | GARBLED    | ~12        |
 * | 0x01  | 48.0      | 27             | GARBLED    | ~15        |
 *
 * Current: 0x09 = 9.6 MHz PCLK → ~5 FPS (stable, DMA-safe)
 * Note: 0x08 garbles even with ADVFL=50 dummy V-lines (DMA margin too thin)
 ******************************************************************************/
#define OV7675_CFG_CLKRC                0x09

/*******************************************************************************
 * DBLV (Register 0x6B) - PLL multiplier
 *
 * Bits 7:6 control PLL multiplier for internal clock:
 *   0x0A = bypass (1x)
 *   0x4A = 4x (default, recommended)
 *   0x8A = 6x
 *   0xCA = 8x
 *
 * Current: 0x4A = PLL 4x → internal clock = 24MHz * 4 = 96MHz
 ******************************************************************************/
#define OV7675_CFG_DBLV                 0x4A

/*******************************************************************************
 * EXHCH/EXHCL (Registers 0x2A/0x2B) - Extra horizontal blanking
 *
 * Inserts dummy PCLK periods during horizontal blanking to give the DMA
 * ISR more time between lines. Value is a 16-bit count split across two
 * registers: EXHCH (high byte) and EXHCL (low byte).
 *
 * Extra H-blank time = value × PCLK_period
 * Example: With CLKRC=0x08 (10.67 MHz, 93.8ns):
 *   200 dummy PCLKs × 93.8ns = 18.8 us extra per line
 *
 * Current: 0 (disabled)
 ******************************************************************************/
#define OV7675_CFG_EXHCH                0x00
#define OV7675_CFG_EXHCL                0x00

/*******************************************************************************
 * ADVFL/ADVFH (Registers 0x2D/0x2E) - Extra vertical blanking (dummy lines)
 *
 * Inserts dummy lines during vertical blanking to give the VSYNC ISR more
 * time to set up AXIDMAC before the first HREF arrives.
 * 16-bit value: ADVFH (high byte) and ADVFL (low byte).
 *
 * Each dummy line adds one full line period of delay.
 * With CLKRC=0x08 (10.67 MHz, ~120 us/line):
 *   50 dummy lines × 120 us = 6 ms extra VSYNC blanking
 *
 * Without ADVFL, CLKRC=0x08 shows color artifacts at top of frame
 * because VSYNC ISR doesn't finish before first HREF.
 *
 * Current: 0 (disabled)
 ******************************************************************************/
#define OV7675_CFG_ADVFL                0x00
#define OV7675_CFG_ADVFH                0x00


/*******************************************************************************
 *
 *  SECTION 3: BRIGHTNESS & CONTRAST
 *
 ******************************************************************************/

/*******************************************************************************
 * Brightness (Register 0x55)
 *
 * Format: Signed magnitude with bit7 as sign bit.
 *   0x00 = 0   (neutral / factory default)
 *   0x20 = +32 (slightly brighter)
 *   0x40 = +64 (moderately brighter)
 *   0x7F = +127 (maximum brightness)
 *   0x80 = -0  (starts going dark, sign bit set)
 *   0x90 = -16 (slightly darker)
 *   0xFF = -127 (maximum darkness)
 *
 * Current: 0x22 = +34 (slightly above neutral for indoor lighting)
 ******************************************************************************/
#define OV7675_CFG_BRIGHT               0x22

/*******************************************************************************
 * Contrast (Register 0x56)
 *
 * Format: Unsigned linear multiplier applied to luminance.
 *   0x20 = low  (flat image, no depth / washed out)
 *   0x40 = factory default (balanced)
 *   0x56 = +34% above default (more punch, good for face recognition)
 *   0x60 = high (vivid blacks/whites)
 *   0x80 = very high (may clip highlights/shadows)
 *
 * CONTRAS_CENTER (Register 0x57): pivot point for contrast.
 *   0x80 = mid-gray (default, usually no need to change)
 *
 * Current: 0x40 = factory default
 ******************************************************************************/
#define OV7675_CFG_CONTRAS              0x40
#define OV7675_CFG_CONTRAS_CENTER       0x80


/*******************************************************************************
 *
 *  SECTION 4: COLOR MATRIX (Registers 0x4F-0x54, sign: 0x58)
 *
 ******************************************************************************/

/*******************************************************************************
 * Color Matrix
 *
 * Converts sensor internal YUV to RGB565 output.
 * Values sourced from Linux kernel ov7670 driver (proven-good for RGB565).
 *
 * These also control color SATURATION:
 *   Higher values = more saturated/vivid colors
 *   Lower values  = faded/washed-out colors
 *   Zero matrix   = grayscale output
 *
 * MTX1 (0x4F) = 0xB3  MTX2 (0x50) = 0xB3  MTX3 (0x51) = 0x00
 * MTX4 (0x52) = 0x3D  MTX5 (0x53) = 0xA7  MTX6 (0x54) = 0xE4
 * MTXS (0x58) = 0x9E  (sign bits for MTX1-MTX6)
 *
 * WARNING: Changing these requires understanding the 3x2 color matrix math.
 * Incorrect values cause color cast or complete color inversion.
 ******************************************************************************/
#define OV7675_CFG_MTX1                 0xB3
#define OV7675_CFG_MTX2                 0xB3
#define OV7675_CFG_MTX3                 0x00
#define OV7675_CFG_MTX4                 0x3D
#define OV7675_CFG_MTX5                 0xA7
#define OV7675_CFG_MTX6                 0xE4
#define OV7675_CFG_MTXS                 0x9E


/*******************************************************************************
 *
 *  SECTION 5: GAMMA CURVE (Registers 0x7A-0x89)
 *
 ******************************************************************************/

/*******************************************************************************
 * Gamma Curve - Approximate gamma 2.2 for LCD
 *
 * Controls the tone mapping curve (similar to gamma 2.2 on monitors).
 * This is a 15-point piecewise linear curve from black (0) to white (255).
 *
 * SLOP (0x7A) = slope of the highest segment
 * GAM1-5   (0x7B-0x7F) = shadow region
 *   -> Increase values to brighten dark areas (lift shadows)
 *   -> Decrease values to deepen blacks
 * GAM6-10  (0x80-0x84) = midtone region
 *   -> Controls overall image "feel" (lighter vs moodier)
 * GAM11-15 (0x85-0x89) = highlight region
 *   -> Decrease values to darken bright areas (recover highlights)
 *   -> Increase values to make highlights more open/airy
 *
 * Tip: Adjust shadow/midtone/highlight independently for "S-curve" contrast.
 ******************************************************************************/
#define OV7675_CFG_SLOP                 0x20
#define OV7675_CFG_GAM1                 0x10
#define OV7675_CFG_GAM2                 0x1E
#define OV7675_CFG_GAM3                 0x35
#define OV7675_CFG_GAM4                 0x5A
#define OV7675_CFG_GAM5                 0x69
#define OV7675_CFG_GAM6                 0x76
#define OV7675_CFG_GAM7                 0x80
#define OV7675_CFG_GAM8                 0x88
#define OV7675_CFG_GAM9                 0x8F
#define OV7675_CFG_GAM10                0x96
#define OV7675_CFG_GAM11                0xA3
#define OV7675_CFG_GAM12                0xAF
#define OV7675_CFG_GAM13                0xC4
#define OV7675_CFG_GAM14                0xD7
#define OV7675_CFG_GAM15                0xE8


/*******************************************************************************
 *
 *  SECTION 6: AUTO GAIN / AUTO EXPOSURE / AUTO WHITE BALANCE
 *
 ******************************************************************************/

/*******************************************************************************
 * COM8 (Register 0x13) - Auto function master enable
 *
 * Controls which auto functions are enabled.
 * Bit 2: AGC enable    Bit 1: AWB enable    Bit 0: AEC enable
 *   0xE0 = all auto OFF (used during register configuration)
 *   0xE7 = all auto ON  (used after configuration is complete)
 ******************************************************************************/
#define OV7675_CFG_COM8_INIT            0xE0
#define OV7675_CFG_COM8_FINAL           0xE7

/*******************************************************************************
 * COM13 (Register 0x3D) - Color processing enables
 *
 * Bit 7: Gamma enable (1=on, 0=off)
 * Bit 6: UV saturation auto-adjust (1=on, 0=off)
 * Recommended: 0xC0 = both on
 ******************************************************************************/
#define OV7675_CFG_COM13                0xC0

/*******************************************************************************
 * AGC/AEC Configuration
 *
 * GAIN (0x00):  Reset gain to 0
 * AECH (0x10):  Reset AEC to 0
 * COM4 (0x0D):  Window for AEC (0x40 = default)
 * COM9 (0x14):  AGC gain ceiling (bits 6:4)
 *               0x18 = 4x, 0x28 = 8x, 0x38 = 16x (good for indoor)
 * AEW  (0x24):  AGC/AEC upper limit
 * AEB  (0x25):  AGC/AEC lower limit
 * VPT  (0x26):  AGC/AEC fast mode operating region
 ******************************************************************************/
#define OV7675_CFG_GAIN                 0x00
#define OV7675_CFG_AECH                 0x00
#define OV7675_CFG_COM4                 0x40
#define OV7675_CFG_COM9                 0x38
#define OV7675_CFG_AEW                  0x95
#define OV7675_CFG_AEB                  0x33
#define OV7675_CFG_VPT                  0xE3

/*******************************************************************************
 * AWB Configuration (Auto White Balance)
 *
 * AWBC1-6:   Color channel gain limits for AWB
 * AWBCTR0-3: AWB control parameters
 * AWBCTR0 = 0x9F: Simple AWB mode
 ******************************************************************************/
#define OV7675_CFG_AWBC1                0x0A
#define OV7675_CFG_AWBC2                0xF0
#define OV7675_CFG_AWBC3                0x34
#define OV7675_CFG_AWBC4                0x58
#define OV7675_CFG_AWBC5                0x28
#define OV7675_CFG_AWBC6                0x3A
#define OV7675_CFG_AWBCTR3              0x0A
#define OV7675_CFG_AWBCTR2              0x55
#define OV7675_CFG_AWBCTR1              0x11
#define OV7675_CFG_AWBCTR0              0x9F


/*******************************************************************************
 *
 *  SECTION 7: BANDING FILTER (anti-flicker for 50/60 Hz lighting)
 *
 ******************************************************************************/

#define OV7675_CFG_BD50MAX              0x05
#define OV7675_CFG_BD60MAX              0x07


/*******************************************************************************
 *
 *  SECTION 8: AEC HISTOGRAM WINDOWS
 *
 ******************************************************************************/

#define OV7675_CFG_HAECC1               0x78
#define OV7675_CFG_HAECC2               0x68
#define OV7675_CFG_HAECC3               0xD8
#define OV7675_CFG_HAECC4               0xD8
#define OV7675_CFG_HAECC5               0xF0
#define OV7675_CFG_HAECC6               0x90
#define OV7675_CFG_HAECC7               0x94


/*******************************************************************************
 *
 *  SECTION 9: EDGE ENHANCEMENT, DE-NOISE & PIXEL CORRECTION
 *
 ******************************************************************************/

/*******************************************************************************
 * Edge Enhancement
 *
 * EDGE  (0x3F): 0x00 = no edge enhancement (clean for face ID)
 * REG75 (0x75): Edge enhancement lower limit
 * REG76 (0x76): Pixel correction enable + edge enhancement upper limit
 * REG77 (0x77): De-noise offset
 ******************************************************************************/
#define OV7675_CFG_EDGE                 0x00
#define OV7675_CFG_REG75                0x05
#define OV7675_CFG_REG76                0xE1
#define OV7675_CFG_REG77                0x01

/*******************************************************************************
 * De-noise
 *
 * DNSTH (0x4C): De-noise strength threshold
 *   0x00 = no de-noise (sharpest, may show sensor noise)
 *   0x04 = light de-noise
 *   0x08 = moderate de-noise (good balance)
 ******************************************************************************/
#define OV7675_CFG_DNSTH                0x00

/*******************************************************************************
 * Additional Quality Registers
 *
 * COM16  (0x41): Edge enhancement / de-noise enable bits
 * REG_B0 (0xB0): Reserved - improves color rendering
 * ABLC1  (0xB1): Auto black level compensation
 * REG_B2 (0xB2): Reserved
 * THL_ST (0xB3): Black level compensation threshold
 ******************************************************************************/
#define OV7675_CFG_COM16                0x08
#define OV7675_CFG_REG_B0               0x84
#define OV7675_CFG_ABLC1                0x0C
#define OV7675_CFG_REG_B2               0x0E
#define OV7675_CFG_THL_ST               0x80


/*******************************************************************************
 *
 *  BACKWARD COMPATIBILITY ALIASES
 *  (maps old OV7675_TUNE_* names to new OV7675_CFG_* names)
 *
 *  TODO: Remove after updating all references to use OV7675_CFG_* directly
 *
 ******************************************************************************/
#define OV7675_TUNE_BRIGHT              OV7675_CFG_BRIGHT
#define OV7675_TUNE_CONTRAS             OV7675_CFG_CONTRAS
#define OV7675_TUNE_CONTRAS_CENTER      OV7675_CFG_CONTRAS_CENTER
#define OV7675_TUNE_COM13               OV7675_CFG_COM13
#define OV7675_TUNE_MTX1                OV7675_CFG_MTX1
#define OV7675_TUNE_MTX2                OV7675_CFG_MTX2
#define OV7675_TUNE_MTX3                OV7675_CFG_MTX3
#define OV7675_TUNE_MTX4                OV7675_CFG_MTX4
#define OV7675_TUNE_MTX5                OV7675_CFG_MTX5
#define OV7675_TUNE_MTX6                OV7675_CFG_MTX6
#define OV7675_TUNE_MTXS                OV7675_CFG_MTXS
#define OV7675_TUNE_SLOP                OV7675_CFG_SLOP
#define OV7675_TUNE_GAM1                OV7675_CFG_GAM1
#define OV7675_TUNE_GAM2                OV7675_CFG_GAM2
#define OV7675_TUNE_GAM3                OV7675_CFG_GAM3
#define OV7675_TUNE_GAM4                OV7675_CFG_GAM4
#define OV7675_TUNE_GAM5                OV7675_CFG_GAM5
#define OV7675_TUNE_GAM6                OV7675_CFG_GAM6
#define OV7675_TUNE_GAM7                OV7675_CFG_GAM7
#define OV7675_TUNE_GAM8                OV7675_CFG_GAM8
#define OV7675_TUNE_GAM9                OV7675_CFG_GAM9
#define OV7675_TUNE_GAM10               OV7675_CFG_GAM10
#define OV7675_TUNE_GAM11               OV7675_CFG_GAM11
#define OV7675_TUNE_GAM12               OV7675_CFG_GAM12
#define OV7675_TUNE_GAM13               OV7675_CFG_GAM13
#define OV7675_TUNE_GAM14               OV7675_CFG_GAM14
#define OV7675_TUNE_GAM15               OV7675_CFG_GAM15
#define OV7675_TUNE_COM8_INIT           OV7675_CFG_COM8_INIT
#define OV7675_TUNE_COM8_FINAL          OV7675_CFG_COM8_FINAL
#define OV7675_TUNE_GAIN                OV7675_CFG_GAIN
#define OV7675_TUNE_AECH                OV7675_CFG_AECH
#define OV7675_TUNE_COM4                OV7675_CFG_COM4
#define OV7675_TUNE_COM9                OV7675_CFG_COM9
#define OV7675_TUNE_AEW                 OV7675_CFG_AEW
#define OV7675_TUNE_AEB                 OV7675_CFG_AEB
#define OV7675_TUNE_VPT                 OV7675_CFG_VPT
#define OV7675_TUNE_BD50MAX             OV7675_CFG_BD50MAX
#define OV7675_TUNE_BD60MAX             OV7675_CFG_BD60MAX
#define OV7675_TUNE_HAECC1              OV7675_CFG_HAECC1
#define OV7675_TUNE_HAECC2              OV7675_CFG_HAECC2
#define OV7675_TUNE_HAECC3              OV7675_CFG_HAECC3
#define OV7675_TUNE_HAECC4              OV7675_CFG_HAECC4
#define OV7675_TUNE_HAECC5              OV7675_CFG_HAECC5
#define OV7675_TUNE_HAECC6              OV7675_CFG_HAECC6
#define OV7675_TUNE_HAECC7              OV7675_CFG_HAECC7
#define OV7675_TUNE_AWBC1               OV7675_CFG_AWBC1
#define OV7675_TUNE_AWBC2               OV7675_CFG_AWBC2
#define OV7675_TUNE_AWBC3               OV7675_CFG_AWBC3
#define OV7675_TUNE_AWBC4               OV7675_CFG_AWBC4
#define OV7675_TUNE_AWBC5               OV7675_CFG_AWBC5
#define OV7675_TUNE_AWBC6               OV7675_CFG_AWBC6
#define OV7675_TUNE_AWBCTR3             OV7675_CFG_AWBCTR3
#define OV7675_TUNE_AWBCTR2             OV7675_CFG_AWBCTR2
#define OV7675_TUNE_AWBCTR1             OV7675_CFG_AWBCTR1
#define OV7675_TUNE_AWBCTR0             OV7675_CFG_AWBCTR0
#define OV7675_TUNE_REG_B0              OV7675_CFG_REG_B0
#define OV7675_TUNE_ABLC1               OV7675_CFG_ABLC1
#define OV7675_TUNE_REG_B2              OV7675_CFG_REG_B2
#define OV7675_TUNE_THL_ST              OV7675_CFG_THL_ST
#define OV7675_TUNE_DNSTH               OV7675_CFG_DNSTH
#define OV7675_TUNE_REG76               OV7675_CFG_REG76
#define OV7675_TUNE_COM16               OV7675_CFG_COM16
#define OV7675_TUNE_EDGE                OV7675_CFG_EDGE
#define OV7675_TUNE_REG75               OV7675_CFG_REG75
#define OV7675_TUNE_REG77               OV7675_CFG_REG77
#define OV7675_TUNE_CLKRC_VGA           OV7675_CFG_CLKRC
#define OV7675_TUNE_DBLV                OV7675_CFG_DBLV

#endif /* MTB_DVP_CAMERA_OV7675_CONFIG_H */
