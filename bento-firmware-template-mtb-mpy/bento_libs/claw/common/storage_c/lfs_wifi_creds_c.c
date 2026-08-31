/*******************************************************************************
* File Name        : lfs_wifi_creds_c.c
* Description      : lfs_wifi_creds_* for the mtb-only variant.
*
* wifi_init.c links against lfs_wifi_creds_ready/read/write. Under mtb-mpy
* those come from lfs_wifi_creds.c inside libbento_mpy.a, which does its file
* I/O by executing Python and therefore cannot exist without the VM. This is
* the same store reached through bento_storage instead.
*
* The file is byte-compatible on purpose — same /.wifi_creds, same layout:
*
*   0    4   magic    0x57494649 "WIFI"
*   4    2   version  1
*   6    2   count    0..6
*   8  600   entries  6 x sizeof(qspi_wifi_entry_t) = 100
*   608  4   CRC32 (poly 0xEDB88320) of bytes 0..607
*
* so a board can move between variants and keep its saved networks.
*******************************************************************************/
#include "lfs_wifi_creds.h"
#include "bento_storage.h"

#include <string.h>

#define CREDS_FILE          "/.wifi_creds"
#define CREDS_HEADER_SIZE   8
#define CREDS_ENTRIES_SIZE  (QSPI_WIFI_CREDS_MAX * (int)sizeof(qspi_wifi_entry_t))
#define CREDS_TOTAL_SIZE    (CREDS_HEADER_SIZE + CREDS_ENTRIES_SIZE + 4)

static uint8_t s_buf[CREDS_TOTAL_SIZE];

/* Identical to calc_xor32() in the mtb-mpy implementation: the checksum the
 * store used before it moved to CRC32. Accepted on read so a board whose
 * /.wifi_creds predates the migration boots with its networks instead of
 * silently boots with none; the next write re-saves as CRC32. */
static uint32_t calc_xor32(const uint8_t *data, size_t len) {
    uint32_t csum = 0;
    for (size_t i = 0; i < len; i += 4) {
        uint32_t word = 0;
        size_t remain = len - i;
        if (remain > 4) remain = 4;
        memcpy(&word, &data[i], remain);
        csum ^= word;
    }
    return csum;
}

/* Identical to calc_crc32() in the mtb-mpy implementation. */
static uint32_t calc_crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320U : crc >> 1;
        }
    }
    return crc ^ 0xFFFFFFFF;
}

//! [j8_creds_c_store_byte_compat]
void lfs_wifi_creds_init(void)   { /* bento_storage_init() owns bring-up */ }
void lfs_wifi_creds_deinit(void) { }

bool lfs_wifi_creds_ready(void)  { return bento_storage_ready(); }

/* The mtb-mpy store can inherit a pre-LFS sector image that wants rewriting;
 * this store writes only the current format, so never. */
bool lfs_wifi_creds_needs_resave(void) { return false; }

int lfs_wifi_creds_read(qspi_wifi_entry_t *entries, int max_entries) {
    if (entries == NULL || max_entries <= 0) return 0;
    if (!bento_storage_ready()) return 0;

    int n = bento_storage_read_file(CREDS_FILE, s_buf, sizeof(s_buf));
    if (n < CREDS_TOTAL_SIZE) return 0;

    uint32_t magic;   memcpy(&magic,   &s_buf[0], 4);
    uint16_t version; memcpy(&version, &s_buf[4], 2);
    uint16_t count;   memcpy(&count,   &s_buf[6], 2);
    if (magic != QSPI_WIFI_CREDS_MAGIC)     return 0;
    if (version != QSPI_WIFI_CREDS_VERSION) return 0;
    if (count > QSPI_WIFI_CREDS_MAX)        return 0;

    uint32_t stored; memcpy(&stored, &s_buf[CREDS_HEADER_SIZE + CREDS_ENTRIES_SIZE], 4);
    if (stored != calc_crc32(s_buf, CREDS_HEADER_SIZE + CREDS_ENTRIES_SIZE)) {
        /* Legacy XOR-32 fallback, exactly as the mtb-mpy reader does. */
        if (stored != calc_xor32(s_buf, CREDS_HEADER_SIZE + CREDS_ENTRIES_SIZE)) return 0;
    }

    int out = (count < (uint16_t)max_entries) ? count : max_entries;
    memcpy(entries, &s_buf[CREDS_HEADER_SIZE], (size_t)out * sizeof(qspi_wifi_entry_t));
    /* Defensive termination, same as the mtb-mpy reader. */
    for (int i = 0; i < out; i++) {
        entries[i].ssid[32]     = '\0';
        entries[i].password[64] = '\0';
    }
    return out;
}

bool lfs_wifi_creds_write(const qspi_wifi_entry_t *entries, int count) {
    if (count < 0 || count > QSPI_WIFI_CREDS_MAX) return false;
    if (count > 0 && entries == NULL)             return false;
    if (!bento_storage_ready())                   return false;

    memset(s_buf, 0, sizeof(s_buf));
    uint32_t magic   = QSPI_WIFI_CREDS_MAGIC;
    uint16_t version = QSPI_WIFI_CREDS_VERSION;
    uint16_t cnt     = (uint16_t)count;
    memcpy(&s_buf[0], &magic,   4);
    memcpy(&s_buf[4], &version, 2);
    memcpy(&s_buf[6], &cnt,     2);
    if (count > 0) {
        memcpy(&s_buf[CREDS_HEADER_SIZE], entries,
               (size_t)count * sizeof(qspi_wifi_entry_t));
    }
    uint32_t crc = calc_crc32(s_buf, CREDS_HEADER_SIZE + CREDS_ENTRIES_SIZE);
    memcpy(&s_buf[CREDS_HEADER_SIZE + CREDS_ENTRIES_SIZE], &crc, 4);

    return bento_storage_write_file(CREDS_FILE, s_buf, sizeof(s_buf));
}
//! [j8_creds_c_store_byte_compat]
