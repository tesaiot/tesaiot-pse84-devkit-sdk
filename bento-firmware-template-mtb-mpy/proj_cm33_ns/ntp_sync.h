#pragma once

#include <stdbool.h>

/**
 * Perform NTP time sync and write result to hardware RTC.
 * Uses time.google.com (216.239.35.0), UTC+7 timezone.
 * Returns true on success, false on failure.
 * Safe to call from any FreeRTOS task (uses blocking LwIP socket).
 */
bool ntp_sync_rtc(void);
