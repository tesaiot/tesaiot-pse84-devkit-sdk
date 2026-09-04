/* sdk-example: core=cm33 variant=mtb-mpy group=storage
 * id:      cm33/storage/03_wifi_creds_lfs
 * title:   Read and write saved WiFi networks on LittleFS
 * teaches: the credential store's readiness contract, the checksum-migration
 *          flag, and why every call but two must run on the MicroPython task
 * apis:    lfs_wifi_creds_ready, lfs_wifi_creds_needs_resave,
 *          lfs_wifi_creds_init, lfs_wifi_creds_read, lfs_wifi_creds_write,
 *          lfs_wifi_creds_deinit
 * entry:   example_mpy_secure_wifi_creds
 */

/*******************************************************************************
 * Up to QSPI_WIFI_CREDS_MAX (6) networks in /.wifi_creds on the MicroPython
 * LittleFS partition. This is the PRIMARY store — it survives a reflash, which
 * is exactly why writing to it carelessly is not a small mistake.
 *
 * WHO MAY CALL WHAT
 * -----------------
 *   ready(), needs_resave()      plain flag reads. Any task, any time.
 *   init(), deinit(),
 *   read(), write()              these go through the MicroPython VFS bridge —
 *                                they compile and run Python source. MicroPython
 *                                task only, interpreter up, VFS mounted.
 *
 * The SDK example runner is not that task, so those four are behind a switch
 * that is off by default:
 *
 *     DEFINES+=EXAMPLE_CREDS_ON_MPY_TASK=1
 *
 * WHAT init() ACTUALLY IS — AND THE TRAP IN IT
 * --------------------------------------------
 * lfs_wifi_creds_init() sets a ready flag and nothing else. It does not mount
 * anything and it does not check that anything is mounted. Call it before the
 * VFS mount succeeds and ready() will answer true while every read returns
 * nothing and every write goes nowhere. Its one correct call site is
 * immediately AFTER a successful mount, on the MicroPython task.
 *
 * lfs_wifi_creds_deinit() clears that flag. Its one correct call site is before
 * a MicroPython soft reset (mp_deinit()), so nothing touches a VFS that is
 * about to disappear. Calling it anywhere else disables the credential store
 * for the rest of the boot — auto-connect included.
 *
 * needs_resave() — the store's checksum moved from XOR-32 to CRC-32. A file
 * still carrying the old checksum reads fine but is flagged. The fix is a
 * read() followed by a write() of the same entries; do it once after boot
 * auto-connect, not in a loop.
 *
 * NEVER PRINT A PASSWORD. The entry struct carries one in cleartext because the
 * radio needs it in cleartext. The console is not the radio. This example
 * prints SSID, security and flags, and never the password field.
 ******************************************************************************/

#include <stdio.h>
#include <string.h>

#include "lfs_wifi_creds.h"

#include "../sdk_examples_cm33.h"

/* Both off by default. volatile so the branches survive to the object file and
 * the calls are really linked, not folded away. */
#ifndef EXAMPLE_CREDS_ON_MPY_TASK
#define EXAMPLE_CREDS_ON_MPY_TASK  0
#endif
#ifndef EXAMPLE_CREDS_ALLOW_WRITE
#define EXAMPLE_CREDS_ALLOW_WRITE  0    /* overwrites the saved networks */
#endif
#ifndef EXAMPLE_CREDS_ALLOW_DEINIT
#define EXAMPLE_CREDS_ALLOW_DEINIT 0    /* only on the MPY soft-reset path */
#endif

static volatile int s_on_mpy_task  = EXAMPLE_CREDS_ON_MPY_TASK;
static volatile int s_allow_write  = EXAMPLE_CREDS_ALLOW_WRITE;
static volatile int s_allow_deinit = EXAMPLE_CREDS_ALLOW_DEINIT;

/* 6 x 100 bytes. Static: too big for the runner task's stack. */
static qspi_wifi_entry_t s_entries[QSPI_WIFI_CREDS_MAX];

static const char *security_str(uint8_t sec)
{
    switch (sec) {
        case 0u:  return "open";
        case 2u:  return "WPA";
        case 6u:  return "WPA2-AES-PSK";
        default:  return "other";
    }
}

int example_mpy_secure_wifi_creds(void);

int example_mpy_secure_wifi_creds(void)
{
    printf("\r\n--- mpy_secure/08_wifi_creds_lfs ---\r\n");

    /* Safe from here, always. ready() false means the MicroPython VFS has not
     * been mounted yet (or deinit() has run) — not that the file is missing. */
    const bool ready = lfs_wifi_creds_ready();
    printf("  lfs_wifi_creds_ready()        = %s\r\n", ready ? "true" : "false");
    printf("  lfs_wifi_creds_needs_resave() = %s%s\r\n",
           lfs_wifi_creds_needs_resave() ? "true" : "false",
           lfs_wifi_creds_needs_resave()
               ? "  (old XOR-32 checksum: read then write once to migrate)"
               : "");

    if (s_on_mpy_task == 0) {
        printf("  SKIPPED init/read/write/deinit: they run MicroPython source\r\n"
               "    against the VFS and are valid only on the MicroPython task.\r\n"
               "    Move this call there and rebuild with\r\n"
               "    DEFINES+=EXAMPLE_CREDS_ON_MPY_TASK=1. Nothing was read or\r\n"
               "    written.\r\n");
        return SDK_EX_REFUSED;
    }

    /* ── From here down: MicroPython task only ───────────────────────────── */

    /* Only call init() if the store is not already up. The shipped boot path
     * calls it right after the mount; a second call is harmless but a FIRST
     * call from the wrong place is the trap described above. */
    if (!ready) {
        printf("  store not ready — calling lfs_wifi_creds_init(). This is only\r\n"
               "    correct if the VFS mount has already succeeded.\r\n");
        lfs_wifi_creds_init();
        if (!lfs_wifi_creds_ready()) {
            printf("  still not ready after init()\r\n");
            return SDK_EX_UNAVAILABLE;
        }
    }

    /* read() waits up to ten seconds for the store to come up, validates magic,
     * version and checksum, and returns the number of entries it accepted.
     * 0 is a valid answer: no file yet, or a file that failed validation. It
     * does not distinguish those. */
    memset(s_entries, 0, sizeof(s_entries));
    int n = lfs_wifi_creds_read(s_entries, (int)QSPI_WIFI_CREDS_MAX);
    printf("  lfs_wifi_creds_read() = %d entr%s\r\n", n, (n == 1) ? "y" : "ies");

    for (int i = 0; i < n; i++) {
        /* Bound the SSID print: the field is a fixed 33 bytes and a corrupt
         * file is exactly the case where the terminator is missing. Password
         * is deliberately not printed. */
        printf("    [%d] ssid=\"%.*s\" security=%s(%u) auto_connect=%s\r\n",
               i, (int)sizeof(s_entries[i].ssid), s_entries[i].ssid,
               security_str(s_entries[i].security),
               (unsigned)s_entries[i].security,
               (s_entries[i].flags & 0x01u) ? "yes" : "no");
    }

    if (s_allow_write == 0) {
        printf("  SKIPPED lfs_wifi_creds_write(): it REPLACES the whole saved\r\n"
               "    set, which survives a reflash. Rebuild with\r\n"
               "    DEFINES+=EXAMPLE_CREDS_ALLOW_WRITE=1 if that is what you\r\n"
               "    want.\r\n");
        return SDK_EX_OK;
    }

    /* write() takes the FULL set and rewrites the file. There is no append and
     * no per-entry update: read everything, change what you mean to change,
     * write everything back. Passing count=1 leaves exactly one network saved
     * and drops the other five. */
    if (n <= 0) {
        printf("  nothing was read, so there is nothing to write back — refusing\r\n"
               "    to replace the store with fabricated entries\r\n");
        return SDK_EX_NO_DATA;
    }

    /* The migration case: identical entries, written back so the file picks up
     * the CRC-32 checksum. Nothing about the networks changes. */
    if (!lfs_wifi_creds_write(s_entries, n)) {
        printf("  lfs_wifi_creds_write() = false — store not ready, bad count, "
               "or the VFS write failed\r\n");
        return SDK_EX_UNAVAILABLE;
    }
    printf("  lfs_wifi_creds_write(%d) = true; needs_resave() is now %s\r\n",
           n, lfs_wifi_creds_needs_resave() ? "true" : "false");

    /* lfs_wifi_creds_deinit() has exactly one correct caller: the MicroPython
     * soft-reset path, immediately before mp_deinit(), so nothing touches a VFS
     * that is about to go away. From anywhere else it turns the credential
     * store off for the rest of the boot — WiFi auto-connect included — and
     * only another init() after a fresh mount brings it back.
     *
     * So it is behind its own switch, and the switch is not the write switch:
     *     DEFINES+=EXAMPLE_CREDS_ALLOW_DEINIT=1
     */
    if (s_allow_deinit != 0) {
        lfs_wifi_creds_deinit();
        printf("  lfs_wifi_creds_deinit() called; ready() is now %s. The store\r\n"
               "    stays off until the next mount + init().\r\n",
               lfs_wifi_creds_ready() ? "true" : "false");
    } else {
        printf("  SKIPPED lfs_wifi_creds_deinit() — it belongs on the MPY\r\n"
               "    soft-reset path, not here.\r\n");
    }

    return SDK_EX_OK;
}
