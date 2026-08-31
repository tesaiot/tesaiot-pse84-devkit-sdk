/**
 * SPDX-FileCopyrightText: 2024-2025 Assoc. Prof. Wiroon Sriborrirux (TESAIoT Platform Creator)
 *
 * @file tesaiot_config.h
 * @brief Build settings used by the OPTIGA helper sources in this template.
 *
 * Holds the log verbosity switch, the bracketed prefixes those sources put
 * in front of their log lines, and the two constants they read. Every name
 * defined here is also named by a source in this template.
 */

#ifndef TESAIOT_CONFIG_H_
#define TESAIOT_CONFIG_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Kept so that code including it still builds; it defines nothing that this
 * template reads. */
#include "tesaiot_license_config.h"

/*----------------------------------------------------------------------------
 * Log verbosity
 *
 * Pick one level for TESAIOT_DEBUG_LEVEL (a -D on the compiler command line
 * wins over the default below). Each level adds to the one before it.
 *--------------------------------------------------------------------------*/
#define TESAIOT_DEBUG_LEVEL_NONE    0  /**< silent */
#define TESAIOT_DEBUG_LEVEL_ERROR   1  /**< failures only */
#define TESAIOT_DEBUG_LEVEL_WARNING 2  /**< failures and warnings */
#define TESAIOT_DEBUG_LEVEL_INFO    3  /**< adds progress messages */
#define TESAIOT_DEBUG_LEVEL_VERBOSE 4  /**< adds step-by-step traces */

/* Default when the build does not choose a level. */
#ifndef TESAIOT_DEBUG_LEVEL
#define TESAIOT_DEBUG_LEVEL TESAIOT_DEBUG_LEVEL_VERBOSE
#endif

/* One on/off flag per level, derived from TESAIOT_DEBUG_LEVEL. The first two
 * may also be forced from the command line. */
#ifndef TESAIOT_DEBUG_ERROR_ENABLED
#if (TESAIOT_DEBUG_LEVEL >= TESAIOT_DEBUG_LEVEL_ERROR)
#define TESAIOT_DEBUG_ERROR_ENABLED 1
#else
#define TESAIOT_DEBUG_ERROR_ENABLED 0
#endif
#endif

#ifndef TESAIOT_DEBUG_WARNING_ENABLED
#if (TESAIOT_DEBUG_LEVEL >= TESAIOT_DEBUG_LEVEL_WARNING)
#define TESAIOT_DEBUG_WARNING_ENABLED 1
#else
#define TESAIOT_DEBUG_WARNING_ENABLED 0
#endif
#endif

#if (TESAIOT_DEBUG_LEVEL >= TESAIOT_DEBUG_LEVEL_INFO)
#define TESAIOT_DEBUG_INFO_ENABLED 1
#else
#define TESAIOT_DEBUG_INFO_ENABLED 0
#endif

#if (TESAIOT_DEBUG_LEVEL >= TESAIOT_DEBUG_LEVEL_VERBOSE)
#define TESAIOT_DEBUG_VERBOSE_ENABLED 1  /* traces on */
#else
#define TESAIOT_DEBUG_VERBOSE_ENABLED 0  /* traces off */
#endif

/*----------------------------------------------------------------------------
 * Log line prefixes
 *
 * Printed at the start of a log line so its origin can be told apart on a
 * shared console.
 *--------------------------------------------------------------------------*/
#define MENU_METADATA_TEST "[Metadata]"
#define LABEL_SUBSCRIBER "[Sub]"
#define LABEL_TRUSTM "[TrustM]"
#define LABEL_OPTIGA_WRITEOID "[OPTIGA:WriteOID]"
#define LABEL_OPTIGA_METADATA "[OPTIGA:Metadata]"
#define LABEL_OPTIGA_VERIFYCERT "[OPTIGA:VerifyCert]"
#define LABEL_OPTIGA_CERTVALIDATION "[OPTIGA:CertValidation]"
#define LABEL_TESAIOT_READ_DATA "[tesaiot_read_data]"
#define LABEL_OPTIGA_UTIL_CALLBACK "[optiga_util_callback]"
#define LABEL_WRITE_METADATA "[WriteMetadata]"
#define LABEL_METADATA_TEST "[MetadataTest]"
#define LABEL_TRUSTM_WRITECERT "[TrustM:WriteCert]"

/*----------------------------------------------------------------------------
 * Shared values
 *--------------------------------------------------------------------------*/

/** OPTIGA Trust M data object that holds the trust anchor (0xE0E8 is the
 *  chip's first trust-anchor object). */
#define OPTIGA_TRUST_ANCHOR_OID 0xE0E8

/** Size, in bytes, of the buffer used when reading that trust anchor back. */
#define TESAIOT_TEST_PUBKEY_DER_LEN 569

#ifdef __cplusplus
}
#endif

#endif /* TESAIOT_CONFIG_H_ */
