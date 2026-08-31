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
* same chip select, same HAL config block, same block config.
*
* "Above every XIP region" keeps the volume from overwriting code. It does not
* keep an erase or program from stalling code: the volume is on the SAME chip
* that CM33_S, CM33_NS and CM55 execute from, and while the chip erases a
* 256 KB sector (typ. 0.8 s, max 5.9 s) it answers no reads. The PDL covers the
* calling core only (interrupts off, driver in SRAM). CM55 is covered by the
* XIP guard: every erase and program below first asks CM55 to park in ITCM and
* waits for it, and refuses the operation if CM55 does not answer. See
* ipc_xip_guard.h.
*******************************************************************************/
#include "bento_storage.h"

#include "littlefs/lfs2.h"
#include "mtb_serial_memory.h"
#include "cycfg_peripherals.h"     /* CYBSP_SMIF_CORE_0_XSPI_FLASH_hal_config */
#include "cycfg_qspi_memslot.h"    /* smif0BlockConfig                        */

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "ipc_xip_guard.h"

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

/* ---- XIP guard, CM33 half ------------------------------------------------
 *
 * Off until bento_storage_guard_arm(), which main() calls just before it starts
 * CM55: until then nothing but CM33 executes from the chip, and CM33's own
 * flash driver runs from SRAM with interrupts off. From then on every erase and
 * program is bracketed by the handshake in ipc_xip_guard.h. */
static volatile bool s_guard_armed;
static bool          s_guard_magic_timed_out;
static uint32_t      s_guard_token;      /* last token sent; never 0 */

static uint32_t bs_now_ms(void)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    }
    return 0u;   /* caller falls back to counting its own delays */
}

/* Wait up to `ms` for cond() to hold. Sleeps when the scheduler runs, so a
 * waiting writer does not starve the tasks CM55 depends on. */
static bool bs_wait(bool (*cond)(void), uint32_t ms)
{
    if (cond()) return true;
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        uint32_t t0 = bs_now_ms();
        while ((bs_now_ms() - t0) < ms) {
            vTaskDelay(1);
            if (cond()) return true;
        }
    } else {
        for (uint32_t t = 0u; t < ms; ++t) {
            Cy_SysLib_Delay(1u);
            if (cond()) return true;
        }
    }
    return false;
}

static bool bs_guard_published(void) { return ipc_xip_guard_get()->magic == IPC_XIP_GUARD_MAGIC; }
static bool bs_guard_parked(void)    { return ipc_xip_guard_get()->parked == s_guard_token; }

/* True when CM55 is parked and the chip may be erased or programmed. */
static bool bs_guard_acquire(void)
{
    if (!s_guard_armed) return true;
    ipc_xip_guard_t *g = ipc_xip_guard_get();

    /* Wait for CM55 to publish only once. If it never does -- no panel, CM55
     * running headless -- it cannot be parked, and it is still executing from
     * this chip: refuse at once from then on rather than stall every write. */
    if (!bs_guard_published()) {
        if (s_guard_magic_timed_out ||
            !bs_wait(bs_guard_published, IPC_XIP_GUARD_MAGIC_WAIT_MS)) {
            if (!s_guard_magic_timed_out) {
                printf("storage: CM55 never answered the XIP guard; "
                       "flash writes are refused\r\n");
            }
            s_guard_magic_timed_out = true;
            return false;
        }
    }

    /* A fresh token per request, so a late park answering an earlier request
     * that timed out cannot be taken as the answer to this one. */
    if (++s_guard_token == 0u) s_guard_token = 1u;
    g->request = s_guard_token;
    __DMB();
    if (bs_wait(bs_guard_parked, IPC_XIP_GUARD_ACK_MS)) {
        return true;
    }
    g->request = 0u;              /* give up cleanly, and tell the caller */
    __DMB();
    return false;
}

static void bs_guard_release(void)
{
    if (!s_guard_armed) return;
    ipc_xip_guard_get()->request = 0u;
    __DMB();
}

void bento_storage_guard_arm(void)
{
    /* The block is ordinary RAM that a warm reset does not clear. A magic left
     * by the previous run would read as "ready" about a CM55 that has not even
     * started, so wipe it before CM55 can publish its own. */
    ipc_xip_guard_t *g = ipc_xip_guard_get();
    g->magic   = 0u;
    g->request = 0u;
    g->parked  = 0u;
    __DMB();
    s_guard_magic_timed_out = false;
    s_guard_armed = true;
}

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
    if (!bs_guard_acquire()) return LFS2_ERR_IO;
    cy_rslt_t r = mtb_serial_memory_write(&s_serial_mem, addr, size,
                                          (const uint8_t *)buffer);
    bs_guard_release();
    return (r == CY_RSLT_SUCCESS) ? LFS2_ERR_OK : LFS2_ERR_IO;
}

static int bs_erase(const struct lfs2_config *c, lfs2_block_t block) {
    (void)c;
    uint32_t addr = BS_FLASH_BASE + block * BS_BLOCK_SIZE;
    if (!bs_guard_acquire()) return LFS2_ERR_IO;
    cy_rslt_t r = mtb_serial_memory_erase(&s_serial_mem, addr, BS_BLOCK_SIZE);
    bs_guard_release();
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

#if defined(BENTO_TEST_BENCH) && BENTO_TEST_BENCH
/* Bench image only: remove the scratch file the XIP-guard exercise writes. */
bool bento_storage_remove_file(const char *path)
{
    if (!s_mounted || path == NULL) return false;
    bs_lock();
    int err = lfs2_remove(&s_lfs, path);
    bs_unlock();
    return (err == LFS2_ERR_OK) || (err == LFS2_ERR_NOENT);
}
#endif

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
            /* lfs2_rename() replaces an existing destination atomically. No
             * remove first: if the rename then failed -- and a write refused by
             * the XIP guard is a normal failure now -- the old file would be
             * gone and the new one deleted below, leaving nothing. */
            ok = (lfs2_rename(&s_lfs, tmp, path) == LFS2_ERR_OK);
        }
    }
    if (!ok) lfs2_remove(&s_lfs, tmp);
    bs_unlock();
    return ok;
    //! [j8_storage_atomic_write]
}
