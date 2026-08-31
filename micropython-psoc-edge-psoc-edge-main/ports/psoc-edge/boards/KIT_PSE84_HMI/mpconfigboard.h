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
#define MICROPY_HW_BOARD_NAME                   "BENTO PSoC Edge HMI Kit"
#endif

#ifndef MICROPY_HW_BOARD_SKU
#define MICROPY_HW_BOARD_SKU                    "KIT_PSE84_AI_001"
#endif

#ifndef MICROPY_PY_NETWORK_HOSTNAME_DEFAULT
#define MICROPY_PY_NETWORK_HOSTNAME_DEFAULT     "KIT_PSE84_HMI"
#endif

// External QSPI Flash Configuration
// These sizes are determined by the physical flash chip specifications
// and memory map layout for the KIT_PSE84_AI board
/* ---------------------------------------------------------------------------
 * KIT_PSE84_HMI — PSOC(TM) Edge E84 HMI Kit
 *
 * Derived from the KIT_PSE84_AI board. Only three things actually differ, and
 * every one of them is silent if left wrong:
 *
 *  1. The Octal NOR sits on SMIF slave select 0 here (S28HS01GT), not slave
 *     select 1 as on the AI Kit (S25HS512T). With the wrong chip select no
 *     entry in smif0MemConfigs matches, so _Cy_SMIF_GetConfigNumber() returns
 *     its 0xFF "not found" sentinel unchanged — it has no error path — and
 *     cy_smif_memnum.c then indexes memConfig[0xFF] on a one-element array.
 *     That reads ~1 KB past the end, yields NULL, and the ->deviceCfg member
 *     at offset 0x18 of cy_stc_smif_mem_config_t is dereferenced: precise
 *     BusFault with BFAR = 0x18, escalated to HardFault, board parked in
 *     Cy_SysLib_ProcessingFault before MicroPython printed its banner.
 *     The missing memConfigNumber == 0xFF guard is a PDL defect, not ours.
 *  2. The general-purpose I2C is SCB11 on P17[2]/P17[3] (the shared 3V3 bus
 *     that also carries the display touch and the SHT40). The AI Kit's SCB5 on
 *     P17[0]/P17[1] is not even configured in this BSP.
 *  3. The flash is 1 Gbit (memSize 0x08000000) rather than 512 Mbit, but the
 *     XIP window this port addresses stays 64 MB, so EXT_FLASH_SIZE is left at
 *     the 64 MB figure deliberately. Under-using the chip is safe; addressing
 *     past the mapped window is not.
 * ------------------------------------------------------------------------- */
/* QSPI FILESYSTEM IS OFF ON THIS BOARD — KNOWN OPEN DEFECT, 2026-08-28.
 *
 * With the chip select corrected to 0 the NULL dereference is gone, but
 * mtb_serial_memory_setup() then spins forever inside
 * Cy_SMIF_ReceiveDataBlocking_Ext: sampled PC stays in that one polling loop
 * (+0x6c, +0x84, +0x170 across three halts) and never returns, so MicroPython
 * never reaches its banner. The working hypothesis is that this Octal NOR is
 * already in octal mode — the firmware XIPs out of it — while the driver
 * issues single-SPI MMIO commands, so the RX FIFO never fills. Proving that
 * needs a scope or a driver-level experiment, not a guess.
 *
 * Turning the block device off lets psoc_edge.QSPI_Flash() fail as a Python
 * AttributeError inside the frozen vfs_lfs2.py, which prints a traceback and
 * lets the boot continue to the REPL instead of hanging the board. Everything
 * except the on-device filesystem then works.
 *
 * Set back to 1 once the octal-mode handshake is sorted. */
/* One second, in 1 us units. This board is the reason the knob exists: the
 * S28HS01GT is driven Octal-DDR and the driver hung forever inside
 * Cy_SMIF_ReceiveDataBlocking_Ext() when the flash did not answer. With a live
 * timeout the same failure returns CY_SMIF_EXCEED_TIMEOUT and names its call
 * site instead of parking the core. */
#define EXT_FLASH_SMIF_TIMEOUT_UNITS (1000000u)

/* This board's CM55 image runs in place from this very chip - readelf puts
 * .app_code_main at run address 0x60580400, 3.4 MB inside the XIP window - so
 * every erase and program has to park CM55 first. */
#define EXT_FLASH_XIP_GUARD (1)

/* Bring-up state, measured on hardware 2026-08-28/29.
 *
 * WORKS, with the XIP guard in serial-memory.patch:
 *   - the board boots to a REPL
 *   - psoc_edge.QSPI_Flash() constructs, no hang
 *   - geometry reads back 208 blocks x 262144 B = 53 MB
 *   - readblocks() returns real data
 *
 * DOES NOT WORK yet:
 *   - os.VfsLfs2.mkfs() hangs the core. Reads are served by the memory-mapped
 *     XIP path; erase and program go through the command path, whose octal-DDR
 *     WIP status poll is the remaining suspect. EXT_FLASH_SMIF_TIMEOUT_UNITS is
 *     armed at one second and did NOT catch it, so that spin is in a loop other
 *     than Cy_SMIF_ReceiveDataBlocking_Ext.
 *
 * Therefore: driver ON, so the failure stays reproducible from the REPL, and
 * boot-time mount OFF via BENTO_VFS_AUTOMOUNT=0 in Makefile.micropython, so a
 * mkfs/mount failure can never take the boot down again. */
#define MICROPY_PY_EXT_FLASH (1)

// Flash chip on THIS board is the S28HS01GT (128 MB). The 64 MB figure below
// is the mapped XIP window (memMappedSize 0x4000000), not the chip size —
// deliberately conservative. The S25HS512T map notes below are the AI Kit's.
// S25HS512T Memory Map:
//   Region 0: 0x000000-0x01FFFF (32 x 4KB sectors)
//   Region 1: 0x020000-0x03FFFF (1 x 128KB sector)
//   Region 2: 0x040000-0x3FFFFFF (255 x 256KB sectors) <- We use this region
//
// !!! The firmware itself EXECUTES-IN-PLACE from this same chip. Per the
// !!! linker maps the declared XIP regions run to 11.75MB (m55_nvm ends
// !!! 0x60B80000, m55_trailer 0x60BC0000) and the linker will place code up
// !!! to there WITHOUT any warning. The old FS base of 9MB sat 2.75MB inside
// !!! that window — safe only while the m55 image stayed under 9MB (it had
// !!! grown to 8.69MB by 2026-07-02, i.e. 0.31MB from silent firmware
// !!! corruption). Same failure class that bricked Eva Kit boards. The FS
// !!! base MUST stay above the declared XIP regions, not just above today's
// !!! image size.
// CRITICAL: Base address MUST be aligned to 256KB (0x40000) sector boundary!
// NOTE: changing this base reformats the filesystem on already-deployed
// boards (LittleFS superblocks move) — warn users that on-board files are
// wiped on first boot after the update.
/* The flash chip this board carries is the S28HS01GT: 128 Mbyte, hybrid
 * sectors, 256 KB uniform sectors in the main region (eraseSize 0x40000 in the
 * BSP's generated memslot config — identical to the AI Kit's, so the sector
 * arithmetic below carries over unchanged). */
#define EXT_FLASH_CHIP_SELECT       MTB_SERIAL_MEMORY_CHIP_SELECT_0
#define EXT_FLASH_BASE              (0x00C00000)  // 12MB — above all XIP regions (sector 48)

// Usable filesystem space: 64MB - 12MB = 52MB (0x03400000 bytes)
#define EXT_FLASH_SIZE              (0x04000000 - EXT_FLASH_BASE)

// erase sector size : 256KB, fixed by flash chip hardware in Region 2.
#define EXT_FLASH_SECTOR_SIZE        (0x40000)       /** 256KB*/

// Block device block size: Must match erase sector size for proper filesystem operation
#define EXT_FLASH_BLOCK_SIZE_BYTES  (EXT_FLASH_SECTOR_SIZE)

// Program page size: Fixed by flash chip hardware (minimum writable unit). Matches LittleFS write_size.
#define EXT_FLASH_PAGE_SIZE         (0x200) /** 512 Bytes */

// I2C Configuration
/* SCB11 on P17[2]/P17[3] — the 3V3 bus, per cycfg_routing.h:
 *   ioss_0_port_17_pin_2_HSIOM P17_2_SCB11_I2C_SCL
 *   ioss_0_port_17_pin_3_HSIOM P17_3_SCB11_I2C_SDA
 * Shared with the FT5446 touch controller and the SHT40, so machine.I2C
 * traffic competes with the UI. Keep transactions short. */
#define MICROPY_HW_I2C0_SCB                     (SCB11)
#define MICROPY_HW_I2C0_SCL_PORT                GPIO_PRT17
#define MICROPY_HW_I2C0_SCL_PIN                 P17_2_NUM
#define MICROPY_HW_I2C0_SCL_HSIOM               P17_2_SCB11_I2C_SCL
#define MICROPY_HW_I2C0_SDA_PORT                GPIO_PRT17
#define MICROPY_HW_I2C0_SDA_PIN                 P17_3_NUM
#define MICROPY_HW_I2C0_SDA_HSIOM               P17_3_SCB11_I2C_SDA
#define MICROPY_HW_I2C0_SCL                     (P17_2_NUM)
#define MICROPY_HW_I2C0_SDA                     (P17_3_NUM)
#define MAX_I2C                                 1
#define MICROPY_HW_I2C_INTR_PRIORITY            (7UL)
#define MICROPY_HW_I2C_PCLK                     PCLK_SCB5_CLOCK_SCB_EN
#define MICROPY_HW_I2C_IRQn                     scb_5_interrupt_IRQn
