/*******************************************************************************
* File Name        : bento_storage.h
* Description      : C-native LittleFS storage for the mtb-only variant.
*
* The mtb-mpy variant mounts LittleFS by executing a Python source string
* (mpy_main.c, vfs_mount_script) against psoc_edge.QSPI_Flash. This module is
* the same filesystem reached without the VM: same flash window, same geometry,
* same on-disk format, so the two variants read each other's volumes.
*
* Compiled ONLY when BENTO_HAS_MPY=0. Under mtb-mpy the VM owns the mount, and
* a second lfs2 instance on the same flash would corrupt it.
*
* One deliberate behavioural difference: the Python path formats the volume on
* ANY mount failure (bare except: → mkfs). Here a failed mount is an error and
* the volume is left alone — a wiped /main.py and config is a worse outcome
* than a boot with storage marked unavailable. bento_storage_format() exists
* for when erasing is what the caller actually means.
*******************************************************************************/
#ifndef BENTO_STORAGE_H
#define BENTO_STORAGE_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Bring up SMIF serial memory and mount the LittleFS volume.
 *  Call once from main() before anything reads config or credentials.
 *  Returns true when the volume is mounted. Safe to call twice. */
bool bento_storage_init(void);

/** Mounted and usable. */
bool bento_storage_ready(void);

/** mkfs + mount. Destroys every file on the volume. Never called by init. */
bool bento_storage_format(void);

/** Read a whole file into buf. Returns bytes read, or -1.
 *  Paths are as the Python side writes them, e.g. "/.tesaiot_config". */
int bento_storage_read_file(const char *path, void *buf, size_t max_len);

/** Atomically replace a file: write path.tmp, remove path, rename.
 *  The same sequence the Python path uses, so a power cut mid-save leaves
 *  either the old file or the new one, never a torn half. */
bool bento_storage_write_file(const char *path, const void *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* BENTO_STORAGE_H */
