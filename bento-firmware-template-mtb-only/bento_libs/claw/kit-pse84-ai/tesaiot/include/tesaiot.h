/**
 * @file tesaiot.h
 * @brief One include for every TESAIoT OPTIGA Trust M header.
 * @copyright (c) 2025-2026 TESAIoT AIoT Foundation Platform
 *
 * Including this is the same as including each header in this directory.
 * What each one declares, and where it is implemented, is listed in the
 * README.md beside them.
 */

#ifndef TESAIOT_H
#define TESAIOT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Log switches, log prefixes and shared constants */
#include "tesaiot_config.h"

/* Serialised access to the chip */
#include "tesaiot_optiga_core.h"

/* Offline Protected Update self-test */
#include "tesaiot_protected_update.h"

/* Enrolment requests and data-object helpers */
#include "tesaiot_optiga.h"

/* MQTT link and wall clock */
#include "tesaiot_platform.h"

/* Declares nothing today; kept for code that includes it */
#include "tesaiot_crypto.h"

#ifdef __cplusplus
}
#endif

#endif /* TESAIOT_H */
