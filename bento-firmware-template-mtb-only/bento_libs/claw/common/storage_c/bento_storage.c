/*******************************************************************************
* File Name        : bento_storage.c
* Description      : C-native LittleFS mount over MTB serial memory.
*
* Geometry is the contract. These numbers must equal what the MicroPython port
* passes to VfsLfs2, or the two variants stop reading each other's volumes:
*
*   read/prog size   0x200      vfs_mount_script kwargs
*   block size       0x40000    EXT_FLASH_SECTOR_SIZE (KIT_PSE84_AI)
*   block count      208        EXT_FLASH_SIZE / block size
*   block_cycles     100        MicroPython extmod/vfs_lfsx.c
*   cache size       2048       MIN(block, 4*MAX(read,prog)) — same file
*   lookahead        32         MicroPython extmod/vfs_lfs.c default
*   flash base       0x00C00000 EXT_FLASH_BASE — above every XIP region
*
* The SMIF bring-up is copied from the port's psoc_edge_qspi_flash.c verbatim:
* same chip select, same HAL config block, same block config. WiFi and this
* code both live on CM33_NS, and CM55 must never touch SMIF0 while it XIPs.
*******************************************************************************/
#include "bento_storage.h"

#include "littlefs/lfs2.h"
#include "mtb_serial_memory.h"
#include "cycfg_peripherals.h"     /* CYBSP_SMIF_CORE_0_XSPI_FLASH_hal_config */
#include "cycfg_qspi_memslot.h"    /* smif0BlockConfig                        */

#include "FreeRTOS.h"
#include "semphr.h"

#include <stdio.h>
#include <string.h>

/* ---- geometry (see header comment for provenance) ------------------------ */
#define BS_FLASH_BASE    (0x00C00000u)
#define BS_FLASH_END     (0x04000000u)
#define BS_BLOCK_SIZE    (0x40000u)
#define BS_BLOCK_COUNT   ((BS_FLASH_END - BS_FLASH_BASE) / BS_BLOCK_SIZE)
#define BS_RW_SIZE       (0x200u)
#define BS_CACHE_SIZE    (2048u)
#define BS_LOOKAHEAD     (32u)
#define BS_BLOCK_CYCLES  (100)

/* ---- state --------------------------------------------------------------- */
static mtb_serial_memory_t      s_serial_mem;
static cy_stc_smif_mem_context_t s_smif_ctx;
static cy_stc_smif_mem_info_t    s_smif_info;

static lfs2_t            s_lfs;
static struct lfs2_config s_cfg;
static bool              s_mounted;
static SemaphoreHandle_t s_mutex;

/* lfs2 asks for these buffers when the config supplies them; giving static
 * ones keeps the module off the heap entirely. */
static uint8_t s_read_buf[BS_CACHE_SIZE];
static uint8_t s_prog_buf[BS_CACHE_SIZE];
static uint8_t s_lookahead_buf[BS_LOOKAHEAD] __attribute__((aligned(8)));

/* One shared file buffer: file ops here are boot-time and mutex-serialised. */
static uint8_t s_file_cache[BS_CACHE_SIZE];

static int bs_read(const struct lfs2_config *c, lfs2_block_t block,
                   lfs2_off_t off, void *buffer, lfs2_size_t size) {
    (void)c;
    uint32_t addr = BS_FLASH_BASE + block * BS_BLOCK_SIZE + off;
    cy_rslt_t r = mtb_serial_memory_read(&s_serial_mem, addr, size, buffer);
    return (r == CY_RSLT_SUCCESS) ? LFS2_ERR_OK : LFS2_ERR_IO;
}

static int bs_prog(const struct lfs2_config *c, lfs2_block_t block,
                   lfs2_off_t off, const void *buffer, lfs2_size_t size) {
    (void)c;
    uint32_t addr = BS_FLASH_BASE + block * BS_BLOCK_SIZE + off;
    cy_rslt_t r = mtb_serial_memory_write(&s_serial_mem, addr, size,
                                          (const uint8_t *)buffer);
    return (r == CY_RSLT_SUCCESS) ? LFS2_ERR_OK : LFS2_ERR_IO;
}

static int bs_erase(const struct lfs2_config *c, lfs2_block_t block) {
    (void)c;
    uint32_t addr = BS_FLASH_BASE + block * BS_BLOCK_SIZE;
    cy_rslt_t r = mtb_serial_memory_erase(&s_serial_mem, addr, BS_BLOCK_SIZE);
    return (r == CY_RSLT_SUCCESS) ? LFS2_ERR_OK : LFS2_ERR_IO;
}

static int bs_sync(const struct lfs2_config *c) {
    (void)c;   /* mtb_serial_memory writes are synchronous */
    return LFS2_ERR_OK;
}

static void bs_lock(void)   { if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY); }
static void bs_unlock(void) { if (s_mutex) xSemaphoreGive(s_mutex); }

bool bento_storage_init(void) {
    if (s_mounted) return true;

    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
        if (s_mutex == NULL) return false;
    }

    //! [j8_storage_mount_no_format]
    /* ...context: inside bento_storage_init() ... */
    cy_rslt_t r = mtb_serial_memory_setup(&s_serial_mem,
        MTB_SERIAL_MEMORY_CHIP_SELECT_1,
        CYBSP_SMIF_CORE_0_XSPI_FLASH_hal_config.base,
        CYBSP_SMIF_CORE_0_XSPI_FLASH_hal_config.clock,
        &s_smif_ctx, &s_smif_info, &smif0BlockConfig);
    if (r != CY_RSLT_SUCCESS) {
        printf("storage: SMIF setup failed 0x%08lx\r\n", (unsigned long)r);
        return false;
    }

    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.read            = bs_read;
    s_cfg.prog            = bs_prog;
    s_cfg.erase           = bs_erase;
    s_cfg.sync            = bs_sync;
    s_cfg.read_size       = BS_RW_SIZE;
    s_cfg.prog_size       = BS_RW_SIZE;
    s_cfg.block_size      = BS_BLOCK_SIZE;
    s_cfg.block_count     = BS_BLOCK_COUNT;
    s_cfg.block_cycles    = BS_BLOCK_CYCLES;
    s_cfg.cache_size      = BS_CACHE_SIZE;
    s_cfg.lookahead_size  = BS_LOOKAHEAD;
    s_cfg.read_buffer     = s_read_buf;
    s_cfg.prog_buffer     = s_prog_buf;
    s_cfg.lookahead_buffer = s_lookahead_buf;

    int err = lfs2_mount(&s_lfs, &s_cfg);
    if (err != LFS2_ERR_OK) {
        /* NOT formatted here on purpose — see the header. */
        printf("storage: mount failed (%d); volume left untouched\r\n", err);
        return false;
    }
    s_mounted = true;
    return true;
}
//! [j8_storage_mount_no_format]

bool bento_storage_ready(void) { return s_mounted; }

bool bento_storage_format(void) {
    bs_lock();
    if (s_mounted) { lfs2_unmount(&s_lfs); s_mounted = false; }
    int err = lfs2_format(&s_lfs, &s_cfg);
    if (err == LFS2_ERR_OK) {
        err = lfs2_mount(&s_lfs, &s_cfg);
        s_mounted = (err == LFS2_ERR_OK);
    }
    bs_unlock();
    return s_mounted;
}

int bento_storage_read_file(const char *path, void *buf, size_t max_len) {
    if (!s_mounted || path == NULL || buf == NULL) return -1;
    bs_lock();
    lfs2_file_t f;
    struct lfs2_file_config fcfg = { .buffer = s_file_cache };
    int err = lfs2_file_opencfg(&s_lfs, &f, path, LFS2_O_RDONLY, &fcfg);
    if (err != LFS2_ERR_OK) { bs_unlock(); return -1; }
    lfs2_ssize_t n = lfs2_file_read(&s_lfs, &f, buf, max_len);
    lfs2_file_close(&s_lfs, &f);
    bs_unlock();
    return (n < 0) ? -1 : (int)n;
}

bool bento_storage_write_file(const char *path, const void *buf, size_t len) {
    if (!s_mounted || path == NULL || (buf == NULL && len > 0)) return false;

    char tmp[64];
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) {
        return false;
    }

    //! [j8_storage_atomic_write]
    /* ...context: inside bento_storage_write_file() ... */
    bs_lock();
    bool ok = false;
    lfs2_file_t f;
    struct lfs2_file_config fcfg = { .buffer = s_file_cache };
    int err = lfs2_file_opencfg(&s_lfs, &f, tmp,
                                LFS2_O_WRONLY | LFS2_O_CREAT | LFS2_O_TRUNC,
                                &fcfg);
    if (err == LFS2_ERR_OK) {
        lfs2_ssize_t n = lfs2_file_write(&s_lfs, &f, buf, len);
        err = lfs2_file_close(&s_lfs, &f);
        if (n == (lfs2_ssize_t)len && err == LFS2_ERR_OK) {
            lfs2_remove(&s_lfs, path);            /* may not exist; ignore */
            ok = (lfs2_rename(&s_lfs, tmp, path) == LFS2_ERR_OK);
        }
    }
    if (!ok) lfs2_remove(&s_lfs, tmp);
    bs_unlock();
    return ok;
    //! [j8_storage_atomic_write]
}
