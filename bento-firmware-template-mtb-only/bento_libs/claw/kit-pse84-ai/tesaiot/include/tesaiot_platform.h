/**
 * SPDX-FileCopyrightText: 2024-2025 Assoc. Prof. Wiroon Sriborrirux (TESAIoT Platform Creator)
 *
 * @file tesaiot_platform.h
 * @brief MQTT link and wall-clock queries used by the OPTIGA helper sources.
 */

#ifndef TESAIOT_PLATFORM_H_
#define TESAIOT_PLATFORM_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/*----------------------------------------------------------------------------
 * MQTT link (implemented by the MQTT client under
 * bento_libs/claw/common/modules/tesaiot_mqtt/, whose tesaiot_mqtt.h declares
 * the rest of that client)
 *--------------------------------------------------------------------------*/

/** Bring the MQTT client up if it is not already connected, using whichever
 *  device certificate is currently selected. True once connected. */
bool tesaiot_mqtt_connect(void);

/** True while the client holds a broker connection. */
bool tesaiot_mqtt_is_connected(void);

/*----------------------------------------------------------------------------
 * Wall clock
 *--------------------------------------------------------------------------*/

/** Current UTC time once the clock has been set from the network: stores it
 *  in `*now` (when `now` is not NULL) and returns true; returns false while
 *  the clock is still unset. The OPTIGA helpers call this; this template does
 *  not ship a definition of it. */
bool tesaiot_sntp_get_time(time_t *now);

#ifdef __cplusplus
}
#endif

#endif /* TESAIOT_PLATFORM_H_ */
