/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2022-2025 Infineon Technologies AG
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
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// Board and hardware specific configuration
#ifndef MICROPY_HW_MCU_NAME
#define MICROPY_HW_MCU_NAME                     "PSoC-Edge-E84"
#endif
#ifndef MICROPY_HW_BOARD_NAME
#define MICROPY_HW_BOARD_NAME                   "BENTO KIT_PSE84_EVAL_EPC2"
#endif

#ifndef MICROPY_HW_BOARD_SKU
#define MICROPY_HW_BOARD_SKU                    "KIT_PSE84_EVAL_EPC2_001"
#endif

#ifndef MICROPY_PY_NETWORK_HOSTNAME_DEFAULT
#define MICROPY_PY_NETWORK_HOSTNAME_DEFAULT     "KIT_PSE84_EVAL_EPC2"
#endif

// External QSPI Flash Configuration
// These sizes are determined by the physical flash chip specifications
// and memory map layout for the KIT_PSE84_EVAL_EPC2 (Eva Kit) board.
//
// !!! The Eva Kit NOR flash is a DIFFERENT, SMALLER chip than the AI Kit's
// !!! S25HS512T (64MB / 256KB sectors). Building Eva firmware with the AI
// !!! board's geometry makes LittleFS address blocks beyond the physical die,
// !!! and every erase past ~16MB fails with CY_SMIF_BAD_PARAM (0x00b20004) —
// !!! boards then permanently refuse Program-to-Device once the allocator
// !!! walks past the real end of flash (field incident 2026-07-02).
#define MICROPY_PY_EXT_FLASH (1)

// Flash memory map: Total 16MB (0x01000000) QSPI flash (S25FS128S hybrid flash)
// S25FS128S Memory Map (per BSP cycfg_qspi_memslot.c, hybridRegionCount=3):
//   Region 0: 0x000000-0x007FFF (8 x 4KB sectors)
//   Region 1: 0x008000-0x00FFFF (1 x 32KB sector)
//   Region 2: 0x010000-0xFFFFFF (uniform 64KB sectors) <- We use this region
//
// !!! The firmware itself EXECUTES-IN-PLACE from this same chip. Per the
// !!! linker maps the XIP images + signed trailers occupy 0x100000-0x980000
// !!! (m33s/m33/m55 nvm + trailers, QSPI offsets 1MB-9.5MB). The filesystem
// !!! MUST start above them — an FS base inside that window lets LittleFS
// !!! erase the running firmware (bricked a board on 2026-07-02: the block
// !!! allocator walked into the m33s trailer at 3.0MB during repeated
// !!! Program-to-Device cycles).
// CRITICAL: Base address MUST be aligned to a 64KB (0x10000) sector boundary!
#define EXT_FLASH_BASE              (0x00A00000)  // 10MB — above all XIP regions

// Usable filesystem space: 16MB - 10MB = 6MB (0x00600000 bytes) = 96 blocks
#define EXT_FLASH_SIZE              (0x01000000 - EXT_FLASH_BASE)

// erase sector size : 64KB, fixed by flash chip hardware in Region 2.
#define EXT_FLASH_SECTOR_SIZE        (0x10000)       /** 64KB */

// Block device block size: Must match erase sector size for proper filesystem operation
#define EXT_FLASH_BLOCK_SIZE_BYTES  (EXT_FLASH_SECTOR_SIZE)

// Program page size: Fixed by flash chip hardware (minimum writable unit). Matches LittleFS write_size.
#define EXT_FLASH_PAGE_SIZE         (0x200) /** 512 Bytes */

// I2C Configuration
#define MICROPY_HW_I2C0_SCB                     (SCB5)
#define MICROPY_HW_I2C0_SCL_PORT                GPIO_PRT17
#define MICROPY_HW_I2C0_SCL_PIN                 P17_0_NUM
#define MICROPY_HW_I2C0_SCL_HSIOM               P17_0_SCB5_I2C_SCL
#define MICROPY_HW_I2C0_SDA_PORT                GPIO_PRT17
#define MICROPY_HW_I2C0_SDA_PIN                 P17_1_NUM
#define MICROPY_HW_I2C0_SDA_HSIOM               P17_1_SCB5_I2C_SDA
#define MICROPY_HW_I2C0_SCL                     (P17_0_NUM)
#define MICROPY_HW_I2C0_SDA                     (P17_1_NUM)
#define MAX_I2C                                 1
#define MICROPY_HW_I2C_INTR_PRIORITY            (7UL)
#define MICROPY_HW_I2C_PCLK                     PCLK_SCB5_CLOCK_SCB_EN
#define MICROPY_HW_I2C_IRQn                     scb_5_interrupt_IRQn
