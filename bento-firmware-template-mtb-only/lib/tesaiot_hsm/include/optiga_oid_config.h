/**
 * SPDX-FileCopyrightText: 2024-2025 Assoc. Prof. Wiroon Sriborrirux (TESAIoT Platform Creator)
 *
 * \file optiga_oid_config.h
 *
 * \brief DEPRECATED - Wrapper for backward compatibility
 *
 * It declares nothing of its own: it includes tesaiot_config.h and, with
 * GCC or Clang, emits a deprecation warning.
 *
 * MIGRATION GUIDE:
 * Replace: #include "optiga_oid_config.h"
 * With:    #include "optiga_trust_helpers.h"  (DEVICE_CERTIFICATE_OID), or
 *          #include "tesaiot_config.h"        (OPTIGA_TRUST_ANCHOR_OID)
 *
 * This wrapper will be removed in v1.0
 *
 * \ingroup TESAIoT
 */

#ifndef OPTIGA_OID_CONFIG_H_
#define OPTIGA_OID_CONFIG_H_

/* Deprecation warning - shown once per compilation unit */
#if defined(__GNUC__) || defined(__clang__)
#warning "optiga_oid_config.h is deprecated. Use tesaiot_oid_config.h instead. This wrapper will be removed in v1.0"
#elif defined(_MSC_VER)
#pragma message("Warning: optiga_oid_config.h is deprecated. Use tesaiot_oid_config.h instead. This wrapper will be removed in v1.0")
#endif

/* Kept for existing includes. tesaiot_config.h holds log settings and the
 * trust-anchor OID only; the device certificate OID is DEVICE_CERTIFICATE_OID
 * in optiga_trust_helpers.h. */
#include "tesaiot_config.h"

#endif /* OPTIGA_OID_CONFIG_H_ */
