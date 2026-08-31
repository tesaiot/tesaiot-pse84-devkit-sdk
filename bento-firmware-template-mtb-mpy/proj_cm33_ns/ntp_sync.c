/*******************************************************************************
 * File: ntp_sync.c
 *
 * Description:
 *   Simple NTP client that syncs hardware RTC via UDP to time.google.com.
 *   Compiled by ModusToolbox (NOT MicroPython) so LwIP headers are available.
 *
 ******************************************************************************/

#include "ntp_sync.h"
#include "cy_rtc.h"
#include "lwip/sockets.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

#define NTP_SERVER_IP    "216.239.35.0"  /* time.google.com */
#define NTP_PORT         123
#define NTP_EPOCH_DIFF   2208988800UL   /* NTP epoch (1900) to Unix epoch (1970) */
#define NTP_TIMEZONE_SEC (7 * 3600)     /* UTC+7 Bangkok */

bool ntp_sync_rtc(void)
{
    int sock = lwip_socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        printf("[NTP] socket failed\r\n");
        return false;
    }

    struct sockaddr_in server;
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = lwip_htons(NTP_PORT);
    server.sin_addr.s_addr = ipaddr_addr(NTP_SERVER_IP);

    uint8_t pkt[48];
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 0x1B; /* LI=0, VN=3, Mode=3 (client) */

    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    lwip_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    int sent = lwip_sendto(sock, pkt, 48, 0,
                           (struct sockaddr *)&server, sizeof(server));
    if (sent < 48) {
        printf("[NTP] sendto failed\r\n");
        lwip_close(sock);
        return false;
    }

    int n = lwip_recv(sock, pkt, 48, 0);
    lwip_close(sock);

    if (n < 48) {
        printf("[NTP] recv failed (%d)\r\n", n);
        return false;
    }

    /* Extract transmit timestamp (bytes 40-43) */
    uint32_t ntp_secs = ((uint32_t)pkt[40] << 24) | ((uint32_t)pkt[41] << 16) |
                        ((uint32_t)pkt[42] << 8)  | (uint32_t)pkt[43];
    uint32_t unix_secs = ntp_secs - NTP_EPOCH_DIFF;
    unix_secs += NTP_TIMEZONE_SEC;

    time_t ts = (time_t)unix_secs;
    struct tm *t = gmtime(&ts);  /* gmtime — UTC+7 already added manually */
    if (t == NULL || t->tm_year < 125) {  /* year < 2025 → invalid */
        printf("[NTP] gmtime failed or invalid year\r\n");
        return false;
    }

    cy_stc_rtc_config_t rtc;
    memset(&rtc, 0, sizeof(rtc));
    rtc.sec       = (uint32_t)t->tm_sec;
    rtc.min       = (uint32_t)t->tm_min;
    rtc.hour      = (uint32_t)t->tm_hour;
    rtc.hrFormat  = CY_RTC_24_HOURS;
    rtc.dayOfWeek = (uint32_t)(t->tm_wday + 1); /* CY_RTC: 1=Sun */
    rtc.date      = (uint32_t)t->tm_mday;
    rtc.month     = (uint32_t)(t->tm_mon + 1);
    rtc.year      = (uint32_t)(t->tm_year + 1900 - 2000);

    cy_en_rtc_status_t r = Cy_RTC_SetDateAndTime(&rtc);
    if (r != CY_RTC_SUCCESS) {
        printf("[NTP] RTC set failed: 0x%02X\r\n", (unsigned)r);
        return false;
    }

#ifdef BOOT_VERBOSE
    printf("[NTP] Synced: %04d-%02d-%02d %02d:%02d:%02d (UTC+7)\r\n",
           (int)(rtc.year + 2000), (int)rtc.month, (int)rtc.date,
           (int)rtc.hour, (int)rtc.min, (int)rtc.sec);
#endif
    return true;
}
