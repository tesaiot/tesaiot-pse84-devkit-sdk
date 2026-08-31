/*******************************************************************************
 * File: qspi_wifi_creds.c
 *
 * Description:
 *   QSPI flash persistent storage for WiFi credentials.
 *   Sole credential store — no LFS or OPTIGA at boot.
 *
 *   Storage address: 0x8C0000 (reserved sector in the gap between
 *   firmware region ~8MB and LittleFS partition at 9MB).
 *   Sector size: 256KB (0x40000) — only first 1KB used.
 *
 *   Uses mtb_serial_memory driver via the global serial_memory_obj
 *   from psoc_edge_qspi_flash.c (MicroPython QSPI_Flash block device).
 *   The driver is initialized during MicroPython VFS mount; this module
 *   waits up to 3 seconds for init before giving up.
 *
 *   Thread safety: Protected by FreeRTOS mutex (s_qspi_mutex).
 *   The static s_flash_buf and all mtb_serial_memory calls are guarded.
 *
 ******************************************************************************/

#include "qspi_wifi_creds.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "mtb_serial_memory.h"
#include <string.h>
#include <stdio.h>

/*******************************************************************************
 * Flash layout constants
 ******************************************************************************/
#define QSPI_WIFI_SECTOR_ADDR   0x8C0000U   /* Reserved sector in firmware-LFS gap */
#define QSPI_WIFI_SECTOR_SIZE   0x40000U    /* 256KB sector */

/*******************************************************************************
 * On-flash binary format (packed, little-endian)
 *
 * Offset  Size   Field
 * 0       4      magic    (0x57494649 = "WIFI")
 * 4       2      version  (1)
 * 6       2      count    (0..6)
 * 8       600    entries  (6 x 100 bytes)
 * 608     4      checksum (CRC32 of bytes 0..607)
 ******************************************************************************/
#define QSPI_WIFI_HEADER_SIZE   8
#define QSPI_WIFI_ENTRIES_SIZE  (QSPI_WIFI_CREDS_MAX * sizeof(qspi_wifi_entry_t))
#define QSPI_WIFI_TOTAL_SIZE    (QSPI_WIFI_HEADER_SIZE + QSPI_WIFI_ENTRIES_SIZE + 4)

/* Static buffer for flash read/write (avoids large stack allocation) */
static uint8_t s_flash_buf[QSPI_WIFI_TOTAL_SIZE];

/* FreeRTOS mutex protecting s_flash_buf and mtb_serial_memory calls */
static SemaphoreHandle_t s_qspi_mutex = NULL;

/* QSPI ready wait timeout (ms) — kept short to avoid blocking WiFi IPC worker */
#define QSPI_READY_TIMEOUT_MS   3000

/*******************************************************************************
 * External: serial_memory_obj from psoc_edge_qspi_flash.c
 * Initialized when MicroPython runs psoc_edge.QSPI_Flash() at VFS mount.
 ******************************************************************************/
extern mtb_serial_memory_t serial_memory_obj;
extern volatile uint8_t qspi_flash_init;

/*******************************************************************************
 * CRC32 (ISO 3309 / ITU-T V.42 polynomial 0xEDB88320, reflected)
 *
 * Replaces XOR32 which has high false-positive risk on power-loss corruption.
 * CRC32 provides 1-in-4-billion collision probability.
 ******************************************************************************/
static uint32_t calc_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320U;
            else
                crc >>= 1;
        }
    }
    return crc ^ 0xFFFFFFFF;
}

/*******************************************************************************
 * Mutex helpers
 ******************************************************************************/
static bool qspi_mutex_take(uint32_t timeout_ms)
{
    if (s_qspi_mutex == NULL) {
        s_qspi_mutex = xSemaphoreCreateMutex();
        if (s_qspi_mutex == NULL) {
            printf("[QSPI_CREDS] FATAL: mutex create failed\r\n");
            return false;
        }
    }
    return (xSemaphoreTake(s_qspi_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE);
}

static void qspi_mutex_give(void)
{
    if (s_qspi_mutex != NULL) {
        xSemaphoreGive(s_qspi_mutex);
    }
}

/*******************************************************************************
 * Wait for QSPI driver init (up to QSPI_READY_TIMEOUT_MS)
 ******************************************************************************/
static bool wait_qspi_ready(void)
{
    uint32_t elapsed = 0;
    while (!qspi_flash_init && elapsed < QSPI_READY_TIMEOUT_MS) {
        vTaskDelay(pdMS_TO_TICKS(100));
        elapsed += 100;
    }
    return (qspi_flash_init != 0);
}

/*******************************************************************************
 * Public API
 ******************************************************************************/

int qspi_wifi_creds_read(qspi_wifi_entry_t *entries, int max_entries)
{
    if (!entries || max_entries <= 0) return 0;

    /* Wait for MicroPython to initialize QSPI driver */
    if (!wait_qspi_ready()) {
        printf("[QSPI_CREDS] QSPI not ready after %dms\r\n", QSPI_READY_TIMEOUT_MS);
        return 0;
    }

    /* Take mutex to protect s_flash_buf and serial_memory calls */
    if (!qspi_mutex_take(5000)) {
        printf("[QSPI_CREDS] read: mutex timeout\r\n");
        return 0;
    }

    int result = 0;
    cy_rslt_t rc;
    uint32_t magic, stored_csum, calc_csum;
    uint16_t version, count;
    int n, i;

    /* Read from flash */
    rc = mtb_serial_memory_read(&serial_memory_obj,
                                QSPI_WIFI_SECTOR_ADDR,
                                QSPI_WIFI_TOTAL_SIZE,
                                s_flash_buf);
    if (rc != CY_RSLT_SUCCESS) {
        printf("[QSPI_CREDS] read: flash read failed (0x%08lx)\r\n", (unsigned long)rc);
        goto done;
    }

    /* Validate magic */
    memcpy(&magic, &s_flash_buf[0], 4);
    if (magic != QSPI_WIFI_CREDS_MAGIC) goto done;

    /* Validate version */
    memcpy(&version, &s_flash_buf[4], 2);
    if (version != QSPI_WIFI_CREDS_VERSION) goto done;

    /* Validate count */
    memcpy(&count, &s_flash_buf[6], 2);
    if (count == 0 || count > QSPI_WIFI_CREDS_MAX) goto done;

    /* Validate CRC32 checksum */
    memcpy(&stored_csum, &s_flash_buf[QSPI_WIFI_HEADER_SIZE + QSPI_WIFI_ENTRIES_SIZE], 4);
    calc_csum = calc_crc32(s_flash_buf,
                           QSPI_WIFI_HEADER_SIZE + QSPI_WIFI_ENTRIES_SIZE);
    if (stored_csum != calc_csum) {
        printf("[QSPI_CREDS] read: CRC32 mismatch (stored=0x%08lx calc=0x%08lx)\r\n",
               (unsigned long)stored_csum, (unsigned long)calc_csum);
        goto done;
    }

    /* Copy entries */
    n = (count > (uint16_t)max_entries) ? max_entries : (int)count;
    memcpy(entries, &s_flash_buf[QSPI_WIFI_HEADER_SIZE], n * sizeof(qspi_wifi_entry_t));

    /* Null-terminate strings (safety) */
    for (i = 0; i < n; i++) {
        entries[i].ssid[32] = '\0';
        entries[i].password[64] = '\0';
    }

    result = n;

done:
    qspi_mutex_give();
    return result;
}

bool qspi_wifi_creds_write(const qspi_wifi_entry_t *entries, int count)
{
    if (!entries || count <= 0 || count > QSPI_WIFI_CREDS_MAX) return false;

    if (!wait_qspi_ready()) {
        printf("[QSPI_CREDS] write: QSPI not ready\r\n");
        return false;
    }

    if (!qspi_mutex_take(5000)) {
        printf("[QSPI_CREDS] write: mutex timeout\r\n");
        return false;
    }

    bool success = false;
    cy_rslt_t rc;
    uint32_t magic = QSPI_WIFI_CREDS_MAGIC;
    uint16_t version = QSPI_WIFI_CREDS_VERSION;
    uint16_t cnt = (uint16_t)count;
    uint32_t csum;

    /* Build flash buffer */
    memset(s_flash_buf, 0, sizeof(s_flash_buf));

    memcpy(&s_flash_buf[0], &magic, 4);
    memcpy(&s_flash_buf[4], &version, 2);
    memcpy(&s_flash_buf[6], &cnt, 2);
    memcpy(&s_flash_buf[QSPI_WIFI_HEADER_SIZE], entries, count * sizeof(qspi_wifi_entry_t));

    /* Calculate and store CRC32 checksum */
    csum = calc_crc32(s_flash_buf,
                      QSPI_WIFI_HEADER_SIZE + QSPI_WIFI_ENTRIES_SIZE);
    memcpy(&s_flash_buf[QSPI_WIFI_HEADER_SIZE + QSPI_WIFI_ENTRIES_SIZE], &csum, 4);

    /* Erase sector first */
    rc = mtb_serial_memory_erase(&serial_memory_obj,
                                 QSPI_WIFI_SECTOR_ADDR,
                                 QSPI_WIFI_SECTOR_SIZE);
    if (rc != CY_RSLT_SUCCESS) {
        printf("[QSPI_CREDS] write: erase failed (0x%08lx)\r\n", (unsigned long)rc);
        goto done;
    }

    /* Write data */
    rc = mtb_serial_memory_write(&serial_memory_obj,
                                  QSPI_WIFI_SECTOR_ADDR,
                                  QSPI_WIFI_TOTAL_SIZE,
                                  s_flash_buf);
    if (rc != CY_RSLT_SUCCESS) {
        printf("[QSPI_CREDS] write: write failed (0x%08lx)\r\n", (unsigned long)rc);
        goto done;
    }

    printf("[QSPI_CREDS] wrote %d entries, CRC32=0x%08lx\r\n", count, (unsigned long)csum);
    success = true;

done:
    qspi_mutex_give();
    return success;
}

bool qspi_wifi_creds_erase(void)
{
    if (!wait_qspi_ready()) return false;

    if (!qspi_mutex_take(5000)) {
        printf("[QSPI_CREDS] erase: mutex timeout\r\n");
        return false;
    }

    cy_rslt_t rc = mtb_serial_memory_erase(&serial_memory_obj,
                                            QSPI_WIFI_SECTOR_ADDR,
                                            QSPI_WIFI_SECTOR_SIZE);
    qspi_mutex_give();

    if (rc != CY_RSLT_SUCCESS) {
        printf("[QSPI_CREDS] erase failed (0x%08lx)\r\n", (unsigned long)rc);
        return false;
    }

    printf("[QSPI_CREDS] sector erased\r\n");
    return true;
}
