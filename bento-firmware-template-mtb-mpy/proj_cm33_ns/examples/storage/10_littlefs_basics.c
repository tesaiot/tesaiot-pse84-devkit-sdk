/* sdk-example: core=cm33 variant=both group=storage
 * id:      cm33/storage/10_littlefs_basics
 * title:   Mount LittleFS in C, write a file, read it back
 * teaches: the geometry contract the two variants share, why the C mount only
 *          exists under mtb-only, and what the API deliberately does NOT give
 *          you — no directory read, no unmount, no remove
 * apis:    bento_storage_init, bento_storage_ready, bento_storage_read_file,
 *          bento_storage_write_file, bento_storage_format
 * entry:   example_storage_littlefs_basics
 */

/*******************************************************************************
 * ONE FILESYSTEM, TWO WAYS IN — AND ONLY ONE OF THEM IS COMPILED
 *
 * The LittleFS volume on the external QSPI flash is the same volume in both
 * shipped variants. What differs is who mounts it:
 *
 *   mtb-mpy  (BENTO_HAS_MPY=1)  MicroPython mounts it. mpy_main.c runs a Python
 *                               source string that builds a VfsLfs2 over
 *                               psoc_edge.QSPI_Flash. The VM owns the lfs2_t.
 *   mtb-only (BENTO_HAS_MPY=0)  bento_storage.c mounts it with the C lfs2
 *                               directly over mtb_serial_memory.
 *
 * proj_cm33_ns/Makefile compiles storage_c/ ONLY in the mtb-only branch, and
 * that is not a packaging convenience. Two lfs2 instances over one flash window
 * corrupt each other: each keeps its own block cache and its own free-block
 * lookahead, and neither can see the other's writes. So this whole example is
 * inside `#if BENTO_HAS_MPY == 0`. Under mtb-mpy it compiles to a refusal that
 * tells you to go through the VM instead — it does not quietly do nothing.
 *
 * THE GEOMETRY IS A CONTRACT, NOT A TUNING KNOB
 * ---------------------------------------------
 * These numbers must equal what the MicroPython port passes to VfsLfs2, or the
 * two variants stop reading each other's volumes. They are restated here so you
 * can check a port against them; the authority is bento_storage.c's header:
 *
 *     read size / prog size   0x200      512 B
 *     block size              0x40000    256 KiB (EXT_FLASH_SECTOR_SIZE)
 *     block count             208        (0x04000000 - 0x00C00000) / 0x40000
 *     block_cycles            100        MicroPython extmod/vfs_lfsx.c
 *     cache size              2048       MIN(block, 4*MAX(read,prog))
 *     lookahead               32
 *     filesystem base         0x00C00000 device offset, NOT a CPU address
 *
 * WHERE THE BASE IS, AND WHAT HAPPENS IF IT OVERLAPS THE FIRMWARE
 * ---------------------------------------------------------------
 * Everything on this device below 0x00C00000 is executable image. From
 * bsps/TARGET_KIT_PSE84_AI/config/GeneratedSource/cymem_gnu_CM33_0.ld and
 * cymem_gnu_CM55_0.ld, as device offsets (the _OFFSET symbols; the same bytes
 * appear at 0x60000000 as the XIP window and at 0x08000000 cached):
 *
 *     0x00100000 .. 0x00300000   m33s_nvm       CM33 secure image
 *     0x00300000 .. 0x00340000   m33s_trailer
 *     0x00340000 .. 0x00540000   m33_nvm        CM33 non-secure image
 *     0x00540000 .. 0x00580000   m33_trailer
 *     0x00580000 .. 0x00B80000   m55_nvm        CM55 image  (6 MiB)
 *     0x00B80000 .. 0x00BC0000   m55_trailer
 *     0x00C00000 .. 0x04000000   THIS FILESYSTEM            (52 MiB, 208 blocks)
 *
 * The filesystem starts exactly one erase block above the last firmware byte.
 * That margin is the whole safety story, because LittleFS does not write bytes
 * — it ERASES 256 KiB BLOCKS, on mkfs, on garbage collection, and on any write
 * that needs a fresh block. Move the base down by one block and block 0 lands
 * on m55_trailer; by two, on the top of the CM55 image itself.
 *
 * The failure is not a mount error. The erase succeeds, the filesystem works,
 * and CM55 keeps running from its instruction cache until the next fetch from
 * the erased region — then a hard fault, minutes or hours later, with nothing
 * in the log connecting it to a file write. Only a reflash recovers it. If you
 * re-carve the memory map (moving m55_nvm is the usual reason), move this base
 * with it, in BOTH the C mount and the Python mount kwargs.
 *
 * WHAT THIS API DOES NOT DO — SAY IT OUT LOUD BEFORE YOU DESIGN AROUND IT
 * -----------------------------------------------------------------------
 *   no directory read   bento_storage keeps its lfs2_t static and private, and
 *                       exposes no handle. lfs2_dir_open/lfs2_dir_read/
 *                       lfs2_dir_close would need it. The "list" below is
 *                       therefore a PROBE OF KNOWN PATHS, and it is labelled as
 *                       one — it cannot discover a file you did not name.
 *   no unmount          there is no bento_storage_deinit(). lfs2_unmount() is
 *                       called in exactly one place: inside
 *                       bento_storage_format(), which then re-mounts. The
 *                       volume stays mounted for the life of the boot, which is
 *                       what you want on a device with no eject button.
 *   no remove           a file written here stays until MicroPython's
 *                       os.remove() deletes it or the volume is formatted. That
 *                       is the main reason the write below is opt-in.
 *
 * WHAT IT DOES GIVE YOU: an ATOMIC write. bento_storage_write_file() writes
 * path.tmp, removes path, renames. A power cut leaves either the old file or
 * the new one, never a torn half. It is the same sequence the Python side uses,
 * which is why a config saved by one variant is readable by the other.
 ******************************************************************************/

#include <stdio.h>
#include <string.h>

#include "../sdk_examples_cm33.h"

#ifndef BENTO_HAS_MPY
#define BENTO_HAS_MPY 1
#endif

#if BENTO_HAS_MPY == 0
/* storage_c/ is added to INCLUDES by the mtb-only branch of
 * proj_cm33_ns/Makefile only, so the path is written relative to this file.
 * In an mtb-only project `#include "bento_storage.h"` also resolves. */
#include "../../../bento_libs/claw/common/storage_c/bento_storage.h"
#endif

/* Both off by default, and they are separate on purpose: writing adds a file
 * that nothing here can delete, formatting destroys every file there is.
 *
 *     DEFINES+=EXAMPLE_LFS_ALLOW_WRITE=1
 *     DEFINES+=EXAMPLE_LFS_ALLOW_FORMAT=1
 *
 * volatile so the branches survive to the object file and the calls are really
 * linked rather than folded away. */
#ifndef EXAMPLE_LFS_ALLOW_WRITE
#define EXAMPLE_LFS_ALLOW_WRITE   0
#endif
#ifndef EXAMPLE_LFS_ALLOW_FORMAT
#define EXAMPLE_LFS_ALLOW_FORMAT  0
#endif


int example_storage_littlefs_basics(void);

#if BENTO_HAS_MPY == 0

static volatile int s_allow_write  = EXAMPLE_LFS_ALLOW_WRITE;
static volatile int s_allow_format = EXAMPLE_LFS_ALLOW_FORMAT;

/* The file this example writes. Leading dot so it does not show up as a
 * MicroPython module, and a name nobody will mistake for product data. */
#define PROBE_PATH   "/.sdk_example_probe"
#define PROBE_BODY   "bento sdk example 10_littlefs_basics\n"

/* Paths the shipped firmware and the IDE actually use. This is the closest
 * thing to `ls` the C API offers, and it is a fixed list, not a directory
 * read — see the header block. */
static const char *const k_known_paths[] = {
    "/main.py",              /* autorun script                                */
    "/boot.py",              /* run before main.py, if present                */
    "/.tesaiot_config",      /* tesaiot_config_store — see 11_config_kv.c     */
    "/.wifi_creds",          /* credential store — see 03_wifi_creds_lfs.c    */
    PROBE_PATH,              /* whatever this example last left behind        */
};

/* 512 B: one read unit, and large enough to identify every file above without
 * being large enough to matter on the runner task's stack. Static anyway —
 * the runner task's stack is not the place for it. */
static char s_buf[512];

int example_storage_littlefs_basics(void)
{
    printf("\r\n--- cm33/storage/10_littlefs_basics ---\r\n");

    printf("  geometry: base 0x%08lX  block 0x%lX x %lu  read/prog %lu  "
           "cache %lu  lookahead %lu\r\n",
           0x00C00000UL, 0x40000UL, 208UL, 512UL, 2048UL, 32UL);
    printf("    these MUST match the MicroPython VfsLfs2 kwargs or the two "
           "variants stop reading each other's volume\r\n");

    /* ── 1. MOUNT ────────────────────────────────────────────────────────
     * bento_storage_init() is idempotent and it does NOT format on failure.
     * That is a deliberate difference from the Python mount path, which wraps
     * its mount in a bare `except:` and calls mkfs — losing /main.py and the
     * config on any transient SMIF error. Here a failed mount is an error and
     * the volume is left alone.
     *
     * main() already calls this during boot, so the usual answer below is
     * "already mounted". Calling it again is free. */
    const bool was_ready = bento_storage_ready();
    printf("  bento_storage_ready() on entry = %s\r\n", was_ready ? "true" : "false");

    if (!was_ready) {
        printf("  calling bento_storage_init() ...\r\n");
        if (!bento_storage_init()) {
            /* Two causes, and init() prints which on the console: SMIF setup
             * failed (the flash did not answer), or lfs2_mount() rejected the
             * superblock. The second one means the volume was never formatted
             * or the geometry does not match what formatted it. Do NOT reach
             * for format() to "fix" that until you have checked the geometry —
             * formatting a volume whose geometry you got wrong destroys the
             * data and then mounts cleanly, which looks like success. */
            printf("  bento_storage_init() = false — volume NOT mounted and "
                   "NOT touched\r\n");
            return SDK_EX_UNAVAILABLE;
        }
        printf("  bento_storage_init() = true, ready() = %s\r\n",
               bento_storage_ready() ? "true" : "false");
    }

    /* ── 2. "LIST" — a probe of known paths, not a directory read ───────── */
    printf("  known-path probe (this is NOT ls; it cannot find unnamed "
           "files):\r\n");
    int present = 0;
    for (unsigned i = 0; i < (sizeof(k_known_paths) / sizeof(k_known_paths[0])); i++) {
        /* read_file() returns bytes read, or -1. -1 covers "no such file",
         * "not mounted" and "the read failed" — it does not distinguish them,
         * so do not report a missing file as a fault. A short read is not an
         * error either: it means the file is shorter than the buffer. */
        int n = bento_storage_read_file(k_known_paths[i], s_buf, sizeof(s_buf));
        if (n < 0) {
            printf("    %-20s  absent (or unreadable)\r\n", k_known_paths[i]);
        } else {
            present++;
            printf("    %-20s  %d byte(s) read into a %u-byte buffer%s\r\n",
                   k_known_paths[i], n, (unsigned)sizeof(s_buf),
                   (n == (int)sizeof(s_buf)) ? "  (TRUNCATED — file is larger)"
                                             : "");
        }
    }
    printf("  %d of %u known path(s) present\r\n",
           present, (unsigned)(sizeof(k_known_paths) / sizeof(k_known_paths[0])));

    /* ── 3. WRITE + READ BACK ───────────────────────────────────────────── */
    if (s_allow_write == 0) {
        printf("  SKIPPED bento_storage_write_file(): this API has no remove(),"
               " so the file it creates stays on the volume until MicroPython's"
               " os.remove() or a format takes it away. Rebuild with\r\n"
               "    DEFINES+=EXAMPLE_LFS_ALLOW_WRITE=1\r\n"
               "  Nothing was written.\r\n");
        return SDK_EX_OK;
    }

    printf("  bento_storage_write_file(\"%s\", %u bytes) ...\r\n",
           PROBE_PATH, (unsigned)(sizeof(PROBE_BODY) - 1u));

    /* sizeof - 1: the NUL is not part of the file. Writing it would make the
     * file one byte longer than strlen() and every reader would have to know
     * that. */
    if (!bento_storage_write_file(PROBE_PATH, PROBE_BODY, sizeof(PROBE_BODY) - 1u)) {
        /* false means: not mounted, a NULL argument, the path plus ".tmp" did
         * not fit the 64-byte temp-name buffer, or the lfs2 write failed. The
         * temp file is removed on every failure path, so a failed write leaves
         * no debris and the previous contents of PROBE_PATH survive. */
        printf("  bento_storage_write_file() = false — the volume is unchanged"
               " and no .tmp was left behind\r\n");
        return SDK_EX_UNAVAILABLE;
    }
    printf("  write_file() = true (atomic: tmp -> remove -> rename)\r\n");

    memset(s_buf, 0, sizeof(s_buf));
    int n = bento_storage_read_file(PROBE_PATH, s_buf, sizeof(s_buf));
    if (n != (int)(sizeof(PROBE_BODY) - 1u)) {
        printf("  read back %d byte(s), expected %u — the write reported "
               "success but the volume disagrees\r\n",
               n, (unsigned)(sizeof(PROBE_BODY) - 1u));
        return SDK_EX_UNAVAILABLE;
    }
    if (memcmp(s_buf, PROBE_BODY, (size_t)n) != 0) {
        printf("  read back %d byte(s) but the CONTENT differs\r\n", n);
        return SDK_EX_UNAVAILABLE;
    }
    /* %.*s and not %s: these bytes came off flash and the terminator is
     * exactly what a corrupt file is missing. */
    printf("  read back %d byte(s), content matches: \"%.*s\"\r\n",
           n, n - 1, s_buf);            /* n-1 drops the trailing newline */

    /* ── 4. FORMAT — the only call here that unmounts anything ───────────── */
    if (s_allow_format == 0) {
        printf("  SKIPPED bento_storage_format(): it is lfs2_format() followed "
               "by lfs2_mount(), and it DESTROYS EVERY FILE — /main.py, "
               "/.tesaiot_config and /.wifi_creds included. Saved WiFi networks"
               " and the broker configuration do not survive it, and no reflash"
               " brings them back. Rebuild with\r\n"
               "    DEFINES+=EXAMPLE_LFS_ALLOW_FORMAT=1\r\n"
               "  only when erasing the volume is what you actually mean.\r\n");
        return SDK_EX_OK;
    }

    printf("  bento_storage_format() — erasing the whole volume ...\r\n");
    if (!bento_storage_format()) {
        /* format() unmounts FIRST. A false return therefore means the volume is
         * now neither formatted nor mounted, and ready() is false: the erase or
         * the re-mount failed with the old filesystem already gone. Every later
         * read and write returns -1 / false until a successful init(). */
        printf("  bento_storage_format() = false — the volume was unmounted "
               "before the attempt, so storage is DOWN for this boot. "
               "ready() = %s\r\n", bento_storage_ready() ? "true" : "false");
        return SDK_EX_UNAVAILABLE;
    }
    printf("  format() = true — empty volume, mounted. ready() = %s\r\n",
           bento_storage_ready() ? "true" : "false");
    printf("  every known path is gone now; re-run this example to see the "
           "probe come back empty\r\n");

    return SDK_EX_OK;
}

#else  /* BENTO_HAS_MPY == 1 */

int example_storage_littlefs_basics(void)
{
    printf("\r\n--- cm33/storage/10_littlefs_basics ---\r\n");
    printf("  NOT AVAILABLE in the mtb-mpy variant, and not by omission.\r\n");
    printf("    MicroPython owns the lfs2_t for this flash window. A second\r\n"
           "    C mount over the same window keeps its own block cache and its\r\n"
           "    own free-block lookahead, sees none of the VM's writes, and\r\n"
           "    corrupts the volume the first time either side allocates.\r\n");
    printf("    Reach the same files from Python — open()/os.listdir()/\r\n"
           "    os.remove() — or build the mtb-only variant, where\r\n"
           "    proj_cm33_ns/Makefile compiles storage_c/ and this example\r\n"
           "    runs in full.\r\n");
    printf("  Geometry, identical in both variants, so a volume written by one\r\n"
           "    is readable by the other: base 0x00C00000, block 0x40000 x 208,"
           "\r\n    read/prog 512, cache 2048, lookahead 32, block_cycles 100."
           "\r\n");
    return SDK_EX_UNAVAILABLE;
}

#endif /* BENTO_HAS_MPY */
