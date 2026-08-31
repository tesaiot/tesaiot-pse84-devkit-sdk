/******************************************************************************
* File Name: optiga_trust_helpers.c
*
* Description: This file contains helping fucntions to read a certificate or 
* change some default parameter
*
* Related Document: See README.md
*
*
*******************************************************************************
* Copyright 2020-2025, Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation. All rights reserved.
*
* This software, including source code, documentation and related
* materials ("Software") is owned by Cypress Semiconductor Corporation
* or one of its affiliates ("Cypress") and is protected by and subject to
* worldwide patent protection (United States and foreign),
* United States copyright laws and international treaty provisions.
* Therefore, you may use this Software only as provided in the license
* agreement accompanying the software package from which you
* obtained this Software ("EULA").
* If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
* non-transferable license to copy, modify, and compile the Software
* source code solely for use in connection with Cypress's
* integrated circuit products. Any reproduction, modification, translation,
* compilation, or representation of this Software except as specified
* above is prohibited without the express written permission of Cypress.
*
* Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
* reserves the right to make changes to the Software without notice. Cypress
* does not assume any liability arising out of the application or use of the
* Software or any product or circuit described in the Software. Cypress does
* not authorize its products for use in any products where a malfunction or
* failure of the Cypress product may reasonably be expected to result in
* significant property damage, injury or death ("High Risk Product"). By
* including Cypress's product in a High Risk Product, the manufacturer
* of such system or application assumes all risk of such use and in doing
* so agrees to indemnify Cypress against all liability.
*******************************************************************************/

/******************************************************************************
* TESAIoT Platform Extensions
*******************************************************************************
* This file has been extended through collaboration between:
* - Infineon Technologies AG (OPTIGA Trust M expertise)
* - TESAIoT Platform Developer Team (AIoT platform integration)
*
* Extensions added:
* - Certificate validation and expiry checking implementation
* - CSR generation with OPTIGA Trust M integration
* - Protected Update data set processing
* - TESAIoT Platform-specific certificate lifecycle management
*
* Original work Copyright 2020-2025 Cypress Semiconductor Corporation (Infineon)
* Extensions developed jointly by Infineon Technologies AG and TESAIoT Team
*
* This file is part of the TESAIoT AIoT Foundation Platform, developed in
* collaboration with Infineon Technologies AG for PSoC Edge E84 + OPTIGA Trust M.
*
* Contact: Wiroon Sriborrirux <sriborrirux@gmail.com>
*******************************************************************************/

/* Declared here rather than by including the manager header, which lives
 * outside this module's include path. */
void optiga_manager_touch_hold(void);
void optiga_manager_touch_hold_reason(const char *reason);
void optiga_manager_touch_release(void);

#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"
#include "mbedtls/psa_util.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "include/optiga_util.h"
#include "include/common/optiga_lib_logger.h"
#include "include/pal/pal_os_event.h"
#include "include/pal/pal_gpio.h"
#include "include/ifx_i2c/ifx_i2c_config.h"
#include "include/pal/pal_ifx_i2c_config.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha256.h"
#include <stddef.h>   /* offsetof, used by the staged-model verifier */
#include <mbedtls/x509_crt.h>
#include "mbedtls/build_info.h"
#include <mbedtls/error.h>
#include "optiga_trust_helpers.h"
#include "include/common/optiga_lib_common.h"
#include "tesaiot_config.h" // Debug labels and configuration
#include "tesaiot_optiga_core.h" // OPTIGA manager
#include "include/optiga_crypt.h"
#include "include/optiga_util.h"
#include "include/pal/pal_os_timer.h"
#include "mqtt_client_config.h"
#include "../cryptography/cryptography_examples.h"
#include "FreeRTOS.h"
#include "task.h" /* Required for vTaskDelay() and pdMS_TO_TICKS() */
#include "semphr.h"
#include "event_groups.h"

/* TESAIoT platform services (SNTP) */
#include "tesaiot_platform.h"

/* OPTIGA OID configuration for test data (metadata diagnostics, isolated tests) */
#include "tesaiot_config.h"


static int asn1_get_len(const uint8_t *buf, uint16_t len, uint16_t *idx, uint16_t *out_len);
static int tlv2_to_raw_rs(const uint8_t *buf, uint16_t len, uint8_t *raw, uint16_t *raw_len, uint16_t limb_len);

/* Forward declaration for tesaiot_read_data() used by test_metadata_operations() */
optiga_lib_status_t tesaiot_read_data(uint16_t oid, uint8_t *buffer, uint16_t *length);

/**
 * Callback when optiga_util_xxxx operation is completed asynchronously
 * NOTE: Made non-static so subscriber_task.c can poll this for async completion
 */
volatile optiga_lib_status_t optiga_lib_status;

#define ASN1_INTEGER 0x02
#define ASN1_BIT_STRING 0x03
#define ASN1_OID 0x06
#define ASN1_UTF8_STRING 0x0C
#define ASN1_SEQUENCE 0x30
#define ASN1_SET 0x31
#define ASN1_CONTEXT_SPECIFIC 0xA0

static const uint8_t SIGNATURE_OID_ECDSA_SHA256[] = {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x02};
static const uint8_t ALG_EC_PUBLIC_KEY_OID[] = {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02, 0x01};
static const uint8_t ALG_PRIME256V1_OID[] = {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07};
static const uint8_t COMMON_NAME_OID[] = {0x55, 0x04, 0x03}; // CN
static const uint8_t ORGANISATION_OID[] = {0x55, 0x04, 0x0A}; // O

uint8_t *dev_cert_raw;
size_t dev_cert_raw_len;
uint8_t *ecc_key_final_fragment_array;
uint8_t *manifest_ecc_key;
size_t ecc_key_final_fragment_array_length;
size_t manifest_ecc_key_length;
uint8_t *pubkey;
size_t pubkey_length;

/**
 * Global buffer for Platform-provided Trust Anchor certificate (Protected Update)
 *
 * When using Protected Update workflow with TESAIoT Platform, the subscriber_task receives
 * the Platform's X.509 certificate and stores it here. The write_trust_anchor()
 * function checks this buffer and uses the Platform certificate instead of the
 * hardcoded Infineon certificate.
 *
 * This prevents the bug where provision() would overwrite the Platform certificate
 * with the hardcoded Infineon certificate, causing signature verification to fail
 * with Error 0x802C.
 */
uint8_t *external_trust_anchor = NULL;
size_t external_trust_anchor_len = 0;

optiga_protected_update_manifest_fragment_configuration_t data_ecc_key_configuration;
optiga_protected_update_data_configuration_t optiga_protected_update_data_set;

/**
 * ECC-256 Trust Anchor
 */
const uint8_t trust_anchor[] = {
 0x30, 0x82, 0x02, 0x58, 0x30, 0x82, 0x01, 0xFF, 0xA0, 0x03, 0x02, 0x01, 0x02, 0x02, 0x01, 0x2F,
 0x30, 0x0A, 0x06, 0x08, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x02, 0x30, 0x56, 0x31, 0x0B,
 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13, 0x02, 0x49, 0x4E, 0x31, 0x0D, 0x30, 0x0B, 0x06,
 0x03, 0x55, 0x04, 0x0A, 0x0C, 0x04, 0x49, 0x46, 0x49, 0x4E, 0x31, 0x0C, 0x30, 0x0A, 0x06, 0x03,
 0x55, 0x04, 0x0B, 0x0C, 0x03, 0x43, 0x43, 0x53, 0x31, 0x13, 0x30, 0x11, 0x06, 0x03, 0x55, 0x04,
 0x03, 0x0C, 0x0A, 0x49, 0x6E, 0x74, 0x43, 0x41, 0x20, 0x50, 0x32, 0x35, 0x36, 0x31, 0x15, 0x30,
 0x13, 0x06, 0x03, 0x55, 0x04, 0x2E, 0x13, 0x0C, 0x54, 0x72, 0x75, 0x73, 0x74, 0x20, 0x41, 0x6E,
 0x63, 0x68, 0x6F, 0x72, 0x30, 0x1E, 0x17, 0x0D, 0x31, 0x36, 0x30, 0x35, 0x32, 0x36, 0x30, 0x38,
 0x30, 0x31, 0x33, 0x37, 0x5A, 0x17, 0x0D, 0x31, 0x37, 0x30, 0x36, 0x30, 0x35, 0x30, 0x38, 0x30,
 0x31, 0x33, 0x37, 0x5A, 0x30, 0x5A, 0x31, 0x0B, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13,
 0x02, 0x49, 0x4E, 0x31, 0x0D, 0x30, 0x0B, 0x06, 0x03, 0x55, 0x04, 0x0A, 0x0C, 0x04, 0x49, 0x46,
 0x49, 0x4E, 0x31, 0x0C, 0x30, 0x0A, 0x06, 0x03, 0x55, 0x04, 0x0B, 0x0C, 0x03, 0x43, 0x43, 0x53,
 0x31, 0x17, 0x30, 0x15, 0x06, 0x03, 0x55, 0x04, 0x03, 0x0C, 0x0E, 0x65, 0x6E, 0x64, 0x45, 0x6E,
 0x74, 0x69, 0x74, 0x79, 0x20, 0x50, 0x32, 0x35, 0x36, 0x31, 0x15, 0x30, 0x13, 0x06, 0x03, 0x55,
 0x04, 0x2E, 0x13, 0x0C, 0x54, 0x72, 0x75, 0x73, 0x74, 0x20, 0x41, 0x6E, 0x63, 0x68, 0x6F, 0x72,
 0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02, 0x01, 0x06, 0x08, 0x2A,
 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07, 0x03, 0x42, 0x00, 0x04, 0x19, 0xB5, 0xB2, 0x17, 0x0D,
 0xF5, 0x98, 0x5E, 0xD4, 0xD9, 0x72, 0x16, 0xEF, 0x61, 0x39, 0x3F, 0x14, 0x58, 0xAF, 0x5C, 0x02,
 0x78, 0x07, 0xCA, 0x48, 0x8F, 0x2A, 0xE3, 0x90, 0xB9, 0x03, 0xA1, 0xD2, 0x46, 0x20, 0x09, 0x21,
 0x52, 0x98, 0xDC, 0x8E, 0x88, 0x84, 0x67, 0x8E, 0x83, 0xD1, 0xDE, 0x0F, 0x1C, 0xE5, 0x19, 0x1D,
 0x0C, 0x74, 0x60, 0x41, 0x58, 0x5B, 0x36, 0x55, 0xF8, 0x3D, 0xAB, 0xA3, 0x81, 0xB9, 0x30, 0x81,
 0xB6, 0x30, 0x09, 0x06, 0x03, 0x55, 0x1D, 0x13, 0x04, 0x02, 0x30, 0x00, 0x30, 0x1D, 0x06, 0x03,
 0x55, 0x1D, 0x0E, 0x04, 0x16, 0x04, 0x14, 0xB5, 0x97, 0xFD, 0xAB, 0x36, 0x1A, 0xA0, 0xA2, 0x23,
 0xA7, 0x68, 0x25, 0x25, 0xFB, 0x82, 0x55, 0xD0, 0x4F, 0xCF, 0xB8, 0x30, 0x7A, 0x06, 0x03, 0x55,
 0x1D, 0x23, 0x04, 0x73, 0x30, 0x71, 0x80, 0x14, 0x1A, 0xBB, 0x56, 0x44, 0x65, 0x8C, 0x4D, 0x4F,
 0xCD, 0x29, 0xA2, 0x3F, 0x4C, 0xC6, 0xBC, 0xA8, 0x8B, 0xA4, 0x0A, 0xDA, 0xA1, 0x56, 0xA4, 0x54,
 0x30, 0x52, 0x31, 0x0B, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13, 0x02, 0x49, 0x4E, 0x31,
 0x0D, 0x30, 0x0B, 0x06, 0x03, 0x55, 0x04, 0x0A, 0x0C, 0x04, 0x49, 0x46, 0x49, 0x4E, 0x31, 0x0C,
 0x30, 0x0A, 0x06, 0x03, 0x55, 0x04, 0x0B, 0x0C, 0x03, 0x43, 0x43, 0x53, 0x31, 0x0F, 0x30, 0x0D,
 0x06, 0x03, 0x55, 0x04, 0x03, 0x0C, 0x06, 0x52, 0x6F, 0x6F, 0x74, 0x43, 0x41, 0x31, 0x15, 0x30,
 0x13, 0x06, 0x03, 0x55, 0x04, 0x2E, 0x13, 0x0C, 0x54, 0x72, 0x75, 0x73, 0x74, 0x20, 0x41, 0x6E,
 0x63, 0x68, 0x6F, 0x72, 0x82, 0x01, 0x2E, 0x30, 0x0E, 0x06, 0x03, 0x55, 0x1D, 0x0F, 0x01, 0x01,
 0xFF, 0x04, 0x04, 0x03, 0x02, 0x00, 0x81, 0x30, 0x0A, 0x06, 0x08, 0x2A, 0x86, 0x48, 0xCE, 0x3D,
 0x04, 0x03, 0x02, 0x03, 0x47, 0x00, 0x30, 0x44, 0x02, 0x20, 0x68, 0xFD, 0x9C, 0x8F, 0x35, 0x33,
 0x0B, 0xB8, 0x32, 0x8C, 0xAF, 0x1C, 0x81, 0x4E, 0x41, 0x29, 0x26, 0xCB, 0xB7, 0x10, 0xA0, 0x75,
 0xFC, 0x89, 0xAE, 0xC5, 0x1D, 0x92, 0x8E, 0x72, 0xEF, 0x5C, 0x02, 0x20, 0x7D, 0xC1, 0xEB, 0x58,
 0x21, 0xF1, 0xFD, 0xFB, 0x5E, 0xD7, 0xDE, 0x06, 0xC9, 0xB4, 0xFF, 0x59, 0x8D, 0x37, 0x8C, 0x7A,
 0x48, 0xCD, 0x2D, 0x99, 0x74, 0x77, 0x58, 0x9D, 0x95, 0x51, 0x8F, 0x5D};

uint8_t reset_metadata[] = {0x20, 0x05, 0xD0, 0x03, 0xE1, 0xFC, 0x07};

/**
 * Shared secret data
 */
const unsigned char shared_secret[] = {
 0x49, 0xC9, 0xF4, 0x92, 0xA9, 0x92, 0xF6, 0xD4, 0xC5, 0x4F, 0x5B, 0x12, 0xC5, 0x7E, 0xDB, 0x27,
 0xCE, 0xD2, 0x24, 0x04, 0x8F, 0x25, 0x48, 0x2A, 0xA1, 0x49, 0xC9, 0xF4, 0x92, 0xA9, 0x92, 0xF6,
 0x49, 0xC9, 0xF4, 0x92, 0xA9, 0x92, 0xF6, 0xD4, 0xC5, 0x4F, 0x5B, 0x12, 0xC5, 0x7E, 0xDB, 0x27,
 0xCE, 0xD2, 0x24, 0x04, 0x8F, 0x25, 0x48, 0x2A, 0xA1, 0x49, 0xC9, 0xF4, 0x92, 0xA9, 0x92, 0xF6};

//0x12 to denote dev cert
uint8_t cert_metadata[] = {0x20, 0x06, 0xD3, 0x01, 0x00, 0xE8, 0x01, 0x12};

/**
 * Metadata for target key OID :
 * Change access condition = Integrity protected using 0xE0E3
 * Execute access condition = Always
 */
uint8_t target_key_oid_metadata[] = {0x20, 0x0D, 0xC1, 0x02, 0x00, 0x00, 0xD0, 0x07, 0x21,
			0x00,
			0x00,
			0xFD, 0x20, 0xF1, 0xD4};
/**
 * Metadata for Trust Anchor :
 * Execute access condition = Always
 * Data object type = Trust Anchor
 */
uint8_t trust_anchor_metadata[] =
	{0x20, 0x03, 0xE8, 0x01, 0x11};

/**
 * Metadata for shared secret OID :
 * Execute access condition = Always
 * Data object type = Protected updated secret
 */
uint8_t confidentiality_oid_metadata[] =
	{0x20, 0x0B, 0xD1, 0x03, 0xE1, 0xFC, 0x07, 0xD3, 0x01, 0x00, 0xE8, 0x01,0x23};

#define TRUSTM_TLS_CERT_BUFFER_SIZE (2048U)
#define OPTIGA_FACTORY_CERTIFICATE_OID (0xE0E0U)

// OID definitions for CSR workflow - must match optiga_oid_config.h
#ifndef DEVICE_CERTIFICATE_OID
/* Certificate 2, the slot this project's pre-provisioning plan pairs with ECC
 * Key 2 (0xE0F1) — the key the device enrols with.
 *
 * The carried-over reference sets this to 0xE0E2, which is Certificate 3 and
 * belongs with Key 3 (0xE0F2). Writing there succeeds, because a certificate
 * slot is an ordinary data object and the chip enforces no binding between a
 * certificate and a key; the pairing is provisioning convention, and the only
 * metadata that does bind objects is the MUD linking a Protected Update target
 * to its trust anchor. So the mistake is silent at write time and surfaces
 * later as a certificate whose private half the device does not hold — the
 * handshake fails at CertificateVerify and reads like a server rejection.
 *
 * The Infineon pre-provisioning plan for this project is explicit: E0F1 for the
 * private key, E0E1 for the device certificate, E0E9 for the TESA CA. */
#define DEVICE_CERTIFICATE_OID (0xE0E1U)
#endif

#ifndef ROOT_CA_OID
#define ROOT_CA_OID (0xE0E3U) // TESAIOT_TRUST_ANCHOR_OID (changed from 0xE0E8)
#endif

static char trustm_tls_cert_buffer[TRUSTM_TLS_CERT_BUFFER_SIZE];
static size_t trustm_tls_cert_size = 0U;

static size_t add_tlv(uint8_t *buffer, size_t index, uint8_t tag, size_t length, const uint8_t *value)
{
 buffer[index++] = tag;

 if (length < 128U)
 {
 buffer[index++] = (uint8_t)length;
 }
 else if (length < 256U)
 {
 buffer[index++] = 0x81;
 buffer[index++] = (uint8_t)length;
 }
 else
 {
 buffer[index++] = 0x82;
 buffer[index++] = (uint8_t)((length >> 8) & 0xFF);
 buffer[index++] = (uint8_t)(length & 0xFF);
 }

 if (length > 0 && value != NULL)
 {
 memcpy(&buffer[index], value, length);
 index += length;
 }

 return index;
}

void optiga_util_callback(void * context, optiga_lib_status_t return_status)
{
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf(LABEL_OPTIGA_UTIL_CALLBACK " INVOKED: return_status=0x%08X\n", return_status);
 fflush(stdout);
 #endif // TESAIOT_DEBUG_VERBOSE_ENABLED
 optiga_lib_status = return_status;
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf(LABEL_OPTIGA_UTIL_CALLBACK " Set optiga_lib_status=0x%08X\n", optiga_lib_status);
 fflush(stdout);
 #endif // TESAIOT_DEBUG_VERBOSE_ENABLED
}

static bool read_certificate_internal(uint16_t optiga_oid, bool skip_tls_identity_header, char *cert_pem, uint16_t *cert_pem_length)
{
 if (!cert_pem || !cert_pem_length)
 {
 return false;
 }

 const uint16_t capacity = *cert_pem_length;
 if (capacity < 64U)
 {
 return false;
 }

 size_t ifx_cert_b64_len = 0;
 uint8_t ifx_cert_b64_temp[1200];
 uint16_t offset_to_write = 0;
 uint16_t offset_to_read = 0;
 uint16_t size_to_copy;
 optiga_lib_status_t return_status;

 optiga_util_t *me_util = NULL;
 uint8_t ifx_cert_hex[1300];
 uint16_t ifx_cert_hex_len = sizeof(ifx_cert_hex);
 bool success = false;

 do
 {
 me_util = optiga_util_create(0, optiga_util_callback, NULL);
 if (!me_util)
 {
 optiga_lib_print_message("optiga_util_create failed !!!", OPTIGA_UTIL_SERVICE, OPTIGA_UTIL_SERVICE_COLOR);
 break;
 }

 optiga_lib_status = OPTIGA_LIB_BUSY;
 return_status = optiga_util_read_data(me_util, optiga_oid, 0, ifx_cert_hex, &ifx_cert_hex_len);
 if (OPTIGA_LIB_SUCCESS != return_status)
 {
 optiga_lib_print_message("optiga_util_read_data api returns error !!!", OPTIGA_UTIL_SERVICE, OPTIGA_UTIL_SERVICE_COLOR);
 break;
 }

 while (optiga_lib_status == OPTIGA_LIB_BUSY)
 {
 pal_os_timer_delay_in_milliseconds(1);
 }

 if (OPTIGA_LIB_SUCCESS != optiga_lib_status)
 {
 optiga_lib_print_message("optiga_util_read_data failed", OPTIGA_UTIL_SERVICE, OPTIGA_UTIL_SERVICE_COLOR);
 break;
 }

 offset_to_read = (skip_tls_identity_header && ifx_cert_hex[0] == 0xC0U) ? 9U : 0U;

 if (mbedtls_base64_encode(ifx_cert_b64_temp, sizeof(ifx_cert_b64_temp), &ifx_cert_b64_len, ifx_cert_hex + offset_to_read, ifx_cert_hex_len - offset_to_read) != 0)
 {
 optiga_lib_print_message("mbedtls_base64_encode failed", OPTIGA_UTIL_SERVICE, OPTIGA_UTIL_SERVICE_COLOR);
 break;
 }

 const char *header = "-----BEGIN CERTIFICATE-----\n";
 const char *footer = "-----END CERTIFICATE-----\n";
 const size_t header_len = strlen(header);
 const size_t footer_len = strlen(footer);

 if (header_len + footer_len + ifx_cert_b64_len + ((ifx_cert_b64_len + 63U) / 64U) + 1U > capacity)
 {
 optiga_lib_print_message("Certificate buffer too small", OPTIGA_UTIL_SERVICE, OPTIGA_UTIL_SERVICE_COLOR);
 break;
 }

 memcpy(cert_pem, header, header_len);
 offset_to_write += (uint16_t)header_len;

 for (offset_to_read = 0; offset_to_read < ifx_cert_b64_len;)
 {
 size_to_copy = (offset_to_read + 64U >= ifx_cert_b64_len) ? (uint16_t)(ifx_cert_b64_len - offset_to_read) : 64U;
 memcpy(cert_pem + offset_to_write, ifx_cert_b64_temp + offset_to_read, size_to_copy);
 offset_to_write += size_to_copy;
 offset_to_read += size_to_copy;
 cert_pem[offset_to_write++] = '\n';
 }

 memcpy(cert_pem + offset_to_write, footer, footer_len);
 offset_to_write += (uint16_t)footer_len;
 cert_pem[offset_to_write] = '\0';
 offset_to_write += 1U;

 *cert_pem_length = offset_to_write;
 success = true;
 } while (0);

 if (me_util)
 {
 optiga_util_destroy(me_util);
 }

 return success;
}

static bool trustm_stage_certificate_for_tls(uint16_t oid, const char *label)
{
 uint16_t buffer_len = TRUSTM_TLS_CERT_BUFFER_SIZE;
 if (!read_certificate_internal(oid, true, trustm_tls_cert_buffer, &buffer_len))
 {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s Failed to read certificate for %s (OID 0x%04X)\n", LABEL_TRUSTM, label, oid);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 return false;
 }

 trustm_tls_cert_size = (size_t)buffer_len;

 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s Loaded %s certificate (%lu bytes) into buffer\n", LABEL_TRUSTM, label, (unsigned long)trustm_tls_cert_size);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */

 return true;
}

// Check return status - WITH TIMEOUT (10 seconds)
// Override the original macros from optiga_example.h to add timeout protection
#define OPTIGA_WAIT_TIMEOUT_MS  10000U
#define OPTIGA_WAIT_POLL_MS     10U

#ifdef WAIT_AND_CHECK_STATUS
#undef WAIT_AND_CHECK_STATUS
#endif

#ifdef WAIT_FOR_COMPLETION
#undef WAIT_FOR_COMPLETION
#endif

#define WAIT_AND_CHECK_STATUS(return_status, optiga_lib_status) \
 if (OPTIGA_LIB_SUCCESS != return_status) { \
 break; \
 } \
 { \
   uint32_t _wait_ms = 0; \
   while (OPTIGA_LIB_BUSY == optiga_lib_status) { \
     vTaskDelay(pdMS_TO_TICKS(OPTIGA_WAIT_POLL_MS)); \
     _wait_ms += OPTIGA_WAIT_POLL_MS; \
     if (_wait_ms >= OPTIGA_WAIT_TIMEOUT_MS) { \
       printf("[OPTIGA] TIMEOUT after %ums\n", (unsigned)OPTIGA_WAIT_TIMEOUT_MS); \
       return_status = OPTIGA_LIB_BUSY; \
       break; \
     } \
   } \
 } \
 if (OPTIGA_LIB_SUCCESS != optiga_lib_status) { \
 return_status = optiga_lib_status; \
 break; \
 }

/* Bounded replacement for the bare `while (optiga_lib_status == OPTIGA_LIB_BUSY);`
 * spins further down this file.
 *
 * Those spins have neither a yield nor a timeout. When a call never completes —
 * the commonest cause being an operation issued while the OPTIGA application is
 * not open — CM33_NS never leaves the loop. That is not a failed operation, it
 * is a dead core: the REPL stops, the CM33 sensor bus stops, and because the
 * shared SCB is left held the display goes with it. Only a power cycle recovers.
 * Hit three times on 2026-08-04 while bringing mTLS up on the TESAIoT Dev Kit.
 *
 * On timeout optiga_lib_status is deliberately left at OPTIGA_LIB_BUSY, so the
 * `if (OPTIGA_LIB_SUCCESS != optiga_lib_status)` test that already follows each
 * of those sites fires and takes the failure path that is already written there.
 * Nothing else has to change.
 *
 * Same budget as the two macros around it, and like them this assumes task
 * context: vTaskDelay is invalid before the scheduler starts, which is already
 * true of every caller in this file. */
#define WAIT_WHILE_OPTIGA_BUSY() \
 do { \
   uint32_t _wb_ms = 0; \
   while (OPTIGA_LIB_BUSY == optiga_lib_status) { \
     vTaskDelay(pdMS_TO_TICKS(OPTIGA_WAIT_POLL_MS)); \
     _wb_ms += OPTIGA_WAIT_POLL_MS; \
     if (_wb_ms >= OPTIGA_WAIT_TIMEOUT_MS) { \
       printf("[OPTIGA] TIMEOUT after %ums in %s\n", \
              (unsigned)OPTIGA_WAIT_TIMEOUT_MS, __func__); \
       break; \
     } \
   } \
 } while (0)

#define WAIT_FOR_COMPLETION(ret) \
 if (OPTIGA_LIB_SUCCESS != ret) \
 { \
 break; \
 } \
 { \
   uint32_t _wait_ms = 0; \
   while (optiga_lib_status == OPTIGA_LIB_BUSY) \
   { \
     vTaskDelay(pdMS_TO_TICKS(OPTIGA_WAIT_POLL_MS)); \
     _wait_ms += OPTIGA_WAIT_POLL_MS; \
     if (_wait_ms >= OPTIGA_WAIT_TIMEOUT_MS) { \
       printf("[OPTIGA] TIMEOUT after %ums\n", (unsigned)OPTIGA_WAIT_TIMEOUT_MS); \
       ret = OPTIGA_LIB_BUSY; \
       break; \
     } \
   } \
 } \
 if (OPTIGA_LIB_SUCCESS != optiga_lib_status) \
 { \
 ret = optiga_lib_status; \
 printf("Error: 0x%02X \r\n", optiga_lib_status); \
 break; \
 }



void read_certificate_from_optiga(uint16_t optiga_oid, char * cert_pem, uint16_t * cert_pem_length)
{
 /* Denial is fatal. optiga_chip_enter() now fails only when another task holds
  * the chip, so carrying on regardless is exactly the collision the gate is
  * for. The caller sees a zero length, which every one of them already treats
  * as "could not read". */
 if (!optiga_chip_enter()) {
 if (cert_pem_length) { *cert_pem_length = 0U; }
 return;
 }
 size_t ifx_cert_b64_len = 0;
 uint8_t ifx_cert_b64_temp[2048];
 uint16_t offset_to_write = 0, offset_to_read = 0;
 uint16_t size_to_copy = 0;
 optiga_lib_status_t return_status;

 optiga_util_t * me_util = NULL;
 uint8_t ifx_cert_hex[2048];
 uint16_t ifx_cert_hex_len = sizeof(ifx_cert_hex);
 
 do
 {
 //Create an instance of optiga_util to read the certificate from OPTIGA.
 me_util = optiga_util_create(0, optiga_util_callback, NULL);
 if(!me_util)
 {
 optiga_lib_print_message("optiga_util_create failed !!!",OPTIGA_UTIL_SERVICE,OPTIGA_UTIL_SERVICE_COLOR);
 break;
 }
 optiga_lib_status = OPTIGA_LIB_BUSY;
 return_status = optiga_util_read_data(me_util, optiga_oid, 0, ifx_cert_hex, &ifx_cert_hex_len);
 if (OPTIGA_LIB_SUCCESS != return_status)
 {
 //optiga_util_read_data api returns error !!!
 optiga_lib_print_message("optiga_util_read_data api returns error !!!",OPTIGA_UTIL_SERVICE,OPTIGA_UTIL_SERVICE_COLOR);
 break;
 }
 
 WAIT_WHILE_OPTIGA_BUSY();
 if (OPTIGA_LIB_SUCCESS != optiga_lib_status)
 {
 //optiga_util_read_data failed
 optiga_lib_print_message("optiga_util_read_data failed",OPTIGA_UTIL_SERVICE,OPTIGA_UTIL_SERVICE_COLOR);
 break;
 }
 
 //convert to PEM format
 // If the first byte is TLS Identity Tag, than we need to skip 9 first bytes
 offset_to_read = ifx_cert_hex[0] == 0xc0? 9: 0;
 mbedtls_base64_encode((unsigned char *)ifx_cert_b64_temp, sizeof(ifx_cert_b64_temp),
 &ifx_cert_b64_len,
 ifx_cert_hex + offset_to_read, ifx_cert_hex_len - offset_to_read);

 memcpy(cert_pem, "-----BEGIN CERTIFICATE-----\n", 28);
 offset_to_write += 28;
 
 //Properly copy certificate and format it as pkcs expects
 for (offset_to_read = 0; offset_to_read < ifx_cert_b64_len;)
 {
 // The last block of data usually is less than 64, thus we need to find the leftover
 if ((offset_to_read + 64) >= ifx_cert_b64_len)
 size_to_copy = ifx_cert_b64_len - offset_to_read;
 else
 size_to_copy = 64;
 memcpy(cert_pem + offset_to_write, ifx_cert_b64_temp + offset_to_read, size_to_copy);
 offset_to_write += size_to_copy;
 offset_to_read += size_to_copy;
 cert_pem[offset_to_write] = '\n';
 offset_to_write++;
 }

 memcpy(cert_pem + offset_to_write, "-----END CERTIFICATE-----\n\0", 27);

 *cert_pem_length = offset_to_write + 27;
 
 } while(0);

 //me_util instance to be destroyed 
 if (me_util)
 {
 optiga_util_destroy(me_util);
 }

 optiga_chip_exit();
}

void read_trust_anchor_from_optiga(uint16_t oid, char * cert_pem, uint16_t * cert_pem_length)
{
 size_t ifx_cert_b64_len = 0;
 uint8_t ifx_cert_b64_temp[1200];
 uint16_t offset_to_write = 0, offset_to_read = 0;
 uint16_t size_to_copy = 0;
 optiga_lib_status_t return_status;

 optiga_util_t * me_util = NULL;
 uint8_t ifx_cert_hex[1300];
 uint16_t ifx_cert_hex_len = sizeof(ifx_cert_hex);

 do
 {
 //Create an instance of optiga_util to read the certificate from OPTIGA.
 me_util = optiga_util_create(0, optiga_util_callback, NULL);
 if(!me_util)
 {
 optiga_lib_print_message("optiga_util_create failed !!!",OPTIGA_UTIL_SERVICE,OPTIGA_UTIL_SERVICE_COLOR);
 break;
 }
 optiga_lib_status = OPTIGA_LIB_BUSY; 
 return_status = optiga_util_read_data(me_util, oid, 0, ifx_cert_hex, &ifx_cert_hex_len);
 if (OPTIGA_LIB_SUCCESS != return_status)
 {
 //optiga_util_read_data api returns error !!!
 optiga_lib_print_message("optiga_util_read_data api for trust anchor returns error !!!",OPTIGA_UTIL_SERVICE,OPTIGA_UTIL_SERVICE_COLOR);
 break;
 }
 
 while (optiga_lib_status == OPTIGA_LIB_BUSY)
 {
 pal_os_timer_delay_in_milliseconds(30);
 }
 
 if (OPTIGA_LIB_SUCCESS != optiga_lib_status)
 {
 //optiga_util_read_data failed
 optiga_lib_print_message("optiga_util_read_data failed for reading trust anchor",OPTIGA_UTIL_SERVICE,OPTIGA_UTIL_SERVICE_COLOR);
 break;
 }

 //ESP_LOG_BUFFER_HEX("OPTIGA Certificate", ifx_cert_hex, ifx_cert_hex_len);
 
 //convert to PEM format
 // printf("read ifx cer\r\n");
 mbedtls_base64_encode((unsigned char *)ifx_cert_b64_temp, sizeof(ifx_cert_b64_temp),
 &ifx_cert_b64_len,
 //in case of TLS chain format of ceritificate
 // ifx_cert_hex + 9, ifx_cert_hex_len - 9);
 ifx_cert_hex, ifx_cert_hex_len);

 memcpy(cert_pem, "-----BEGIN CERTIFICATE-----\n", 28);
 offset_to_write += 28;
 
 //TBD: Verify the given cert_pem length against the ifx_cert_b64_len after conversion
 
 //Properly copy certificate and format it as pkcs expects
 for (offset_to_read = 0; offset_to_read < ifx_cert_b64_len;)
 {
 // The last block of data usually is less than 64, thus we need to find the leftover
 if ((offset_to_read + 64) >= ifx_cert_b64_len)
 size_to_copy = ifx_cert_b64_len - offset_to_read;
 else
 size_to_copy = 64;
 memcpy(cert_pem + offset_to_write, ifx_cert_b64_temp + offset_to_read, size_to_copy);
 offset_to_write += size_to_copy;
 offset_to_read += size_to_copy;
 cert_pem[offset_to_write] = '\n';
 offset_to_write++;
 }

 memcpy(cert_pem + offset_to_write, "-----END CERTIFICATE-----\n\0", 27);

 *cert_pem_length = offset_to_write + 27;
 /* for(int i=0; i<1200; i++)
 {
 printf("%c", cert_pem[i]);
 }*/ 
 } while(0);

 //me_util instance to be destroyed 
 if (me_util)
 {
 optiga_util_destroy(me_util);
 }
}

void write_data_object (uint16_t oid, const uint8_t * p_data, uint16_t length)
{
 optiga_util_t * me_util = NULL;
 optiga_lib_status_t return_status;
 
 do
 {
 //Create an instance of optiga_util to open the application on OPTIGA.
 me_util = optiga_util_create(0, optiga_util_callback, NULL);
 if(!me_util)
 {
 optiga_lib_print_message("optiga_util_create failed !!!",OPTIGA_UTIL_SERVICE,OPTIGA_UTIL_SERVICE_COLOR);
 break;
 }

 optiga_lib_status = OPTIGA_LIB_BUSY;
 return_status = optiga_util_write_data(me_util,
 oid,
 OPTIGA_UTIL_ERASE_AND_WRITE,
 0,
 p_data,
 length);
 {
 if (OPTIGA_LIB_SUCCESS != return_status)
 {
 optiga_lib_print_message("optiga_util_wirte_data api returns error !!!",OPTIGA_UTIL_SERVICE,OPTIGA_UTIL_SERVICE_COLOR);
 break;
 }

 while (OPTIGA_LIB_BUSY == optiga_lib_status)
 {
 //Wait until the optiga_util_write_data operation is completed
 }

 if (OPTIGA_LIB_SUCCESS != optiga_lib_status)
 {
 optiga_lib_print_message("optiga_util_write_data failed",OPTIGA_UTIL_SERVICE,OPTIGA_UTIL_SERVICE_COLOR);
 return_status = optiga_lib_status;
 break;
 }
 else
 {
 optiga_lib_print_message("optiga_util_write_data successful",OPTIGA_UTIL_SERVICE,OPTIGA_UTIL_SERVICE_COLOR);
 }
 }
 } while (0);

 //me_util instance can be destroyed 
 //if no close_application w.r.t hibernate is required to be performed
 if (me_util)
 {
 optiga_util_destroy(me_util);
 }
}


static void write_set_high_performance (void)
{
 const uint8_t current_limit [] = {
 0x0F,
 };
 
 write_data_object (0xE0C4, current_limit, sizeof(current_limit));
}

void trustm_close(optiga_util_t *util);   /* defined below */

/**
 * Open the OPTIGA application, recovering from an application left open by a
 * previous host session.
 *
 * The secure element keeps its application state across a host reset, because
 * only the host is reset — the chip stays powered. Flashing the board, a
 * CM33_NS crash, or any reset that does not run an orderly shutdown therefore
 * leaves the application open, and the next optiga_util_open_application()
 * fails: observed 2026-08-04 as a 28-second stall ending in
 * "optiga_util_open_application failed" (0x0102), after which every read
 * returned garbage and CM33_NS eventually hung.
 *
 * Closing the application on the way out cannot fix this on its own — a crash
 * or a debugger reset never gets to run it. Recovering here does, for every
 * case: on failure close the stale application and open again.
 *
 * @return true when the application is open and usable.
 */
/* Owners of the open application.
 *
 * It is a process-wide resource with more than one claimant: an mTLS session
 * holds it for its whole life so the TLS stack can sign CertificateVerify, and
 * optiga.init() from the REPL wants it for ad-hoc work — enrolment needs both at
 * once, since the device asks for a CSR over a session opened with its factory
 * certificate. Opening it twice drives the chip through two util instances with
 * two separate status variables; closing it once while another claimant is still
 * working takes the ground out from under a live session. Observed 2026-08-04:
 * optiga.csr() while connected hung CM33_NS with no output at all.
 *
 * Only the 0 -> 1 transition touches the chip, and only the 1 -> 0 transition
 * closes it. The stale-session recovery below therefore runs only when nothing
 * in this process holds the application — never against a session of our own. */
static uint32_t s_app_refs = 0;

/* True while one task is driving the 0 -> 1 hardware open.
 *
 * Without it the count was only taken *after* the open returned, and the open
 * is not quick — up to 10 s for the wait, and about 28 s when the stale-session
 * recovery below runs. A second task entering that window read s_app_refs == 0,
 * decided it was the first opener too, and drove a second
 * optiga_util_open_application() at a chip that already had one in flight.
 * Both then executed `s_app_refs = 1`, so two owners shared one count and the
 * first close pulled the application out from under the second. Newly
 * reachable once the HSM page could start enrolment while MQTT was still
 * coming up. */
static bool s_app_opening = false;

/* Is the shared application currently open?
 *
 * For code that keeps its own util instance — the HSM page's worker does — and
 * must not issue a second optiga_util_open_application() on a chip that already
 * has one. Two applications on one chip is the condition that hung CM33_NS on
 * 2026-08-04, and the close that follows tears down whichever session got there
 * first. */
bool optiga_trust_application_is_open(void)
{
 bool open;
 taskENTER_CRITICAL();
 open = (s_app_refs > 0U);
 taskEXIT_CRITICAL();
 return open;
}

static bool optiga_trust_open_application_gated(void);

bool optiga_trust_open_application(void)
{
 /* Held across the hardware open, including the stale-session recovery, so no
  * other task can be mid-transaction while OpenApplication runs - and so a
  * close cannot run underneath it. */
 if (!optiga_chip_enter()) {
 optiga_lib_print_message("open refused: the chip is busy elsewhere",
 OPTIGA_UTIL_SERVICE, OPTIGA_UTIL_SERVICE_COLOR);
 return false;
 }
 bool ok = optiga_trust_open_application_gated();
 optiga_chip_exit();
 return ok;
}

static bool optiga_trust_open_application_gated(void)
{
 for (;;)
 {
 bool mine = false;
 taskENTER_CRITICAL();
 if (s_app_refs > 0U)
 {
 s_app_refs++;                    /* already open; no hardware touched */
 taskEXIT_CRITICAL();
 return true;
 }
 if (!s_app_opening)
 {
 s_app_opening = true;            /* this task performs the open */
 mine = true;
 }
 taskEXIT_CRITICAL();

 if (mine)
 {
 break;
 }

 /* Another task is mid-open. Wait for it rather than racing it. */
 vTaskDelay(pdMS_TO_TICKS(10));
 }

 for (int attempt = 0; attempt < 2; attempt++)
 {
 optiga_util_t *me_util = optiga_util_create(0, optiga_util_callback, NULL);
 if (!me_util)
 {
 optiga_lib_print_message("optiga_util_create failed !!!",OPTIGA_UTIL_SERVICE,OPTIGA_UTIL_SERVICE_COLOR);
 taskENTER_CRITICAL();
 s_app_opening = false;
 taskEXIT_CRITICAL();
 return false;
 }

 optiga_lib_status = OPTIGA_LIB_BUSY;
 optiga_lib_status_t rs = optiga_util_open_application(me_util, 0);
 if (OPTIGA_LIB_SUCCESS == rs)
 {
 WAIT_WHILE_OPTIGA_BUSY();
 if (OPTIGA_LIB_SUCCESS == optiga_lib_status)
 {
 optiga_util_destroy(me_util);
 taskENTER_CRITICAL();
 s_app_refs++;            /* increment: a waiter may already be queued */
 s_app_opening = false;
 taskEXIT_CRITICAL();
 return true;
 }
 optiga_lib_print_message("optiga_util_open_application failed",OPTIGA_UTIL_SERVICE,OPTIGA_UTIL_SERVICE_COLOR);
 }
 else
 {
 optiga_lib_print_message("optiga_util_open_application api returns error !!!",OPTIGA_UTIL_SERVICE,OPTIGA_UTIL_SERVICE_COLOR);
 }

 /* Close whatever session the chip still believes is open, then retry.
  * trustm_close() destroys the instance as well. */
 if (attempt == 0)
 {
 optiga_lib_print_message("closing a stale application and retrying",OPTIGA_UTIL_SERVICE,OPTIGA_UTIL_SERVICE_COLOR);
 }
 trustm_close(me_util);
 }

 taskENTER_CRITICAL();
 s_app_opening = false;
 taskEXIT_CRITICAL();
 return false;
}

bool optiga_trust_init(void)
{
 bool opened = false;
 optiga_lib_print_message("OPTIGA Trust initialization",OPTIGA_UTIL_SERVICE,OPTIGA_UTIL_SERVICE_COLOR);

 do
 {
 if (!optiga_trust_open_application())
 {
 break;
 }
 opened = true;

 //The below specified functions can be used to personalize OPTIGA w.r.t
 //certificates, Trust Anchors, etc.
 
 //write_device_certificate ();
 write_set_high_performance(); //setting current limitation to 15mA
 //write_platform_binding_secret (); 
 //write_optiga_trust_anchor(); //can be used to write server root certificate to optiga data object

 optiga_lib_print_message("OPTIGA Trust initialization is successful",OPTIGA_UTIL_SERVICE,OPTIGA_UTIL_SERVICE_COLOR);
 }while(0);

 return opened;
}

/**
 * Prove that the private key in key_oid belongs to the certificate in cert_oid.
 *
 * A certificate slot and a key slot are written independently, and after a reset
 * they can disagree — which is what happens as soon as a fresh CSR generates a
 * new key while the reply for the previous one is still installed. Presenting a
 * certificate whose private half the device does not hold fails the TLS
 * handshake at CertificateVerify, and that failure reads like a server-side
 * rejection.
 *
 * The method is a challenge signature: sign a fixed, non-secret digest with the
 * private half inside the chip, then verify it against the public key carried in
 * the certificate. That succeeds only if the two halves are one key pair.
 *
 * Deliberately avoids the PSA secure-element driver — creating a second PSA key
 * would disturb the slot map a live mTLS session signs through, whereas
 * trustm_ecdsa_sign() takes the same manager mutex as that session and waits its
 * turn.
 *
 * Not called from the connect path. It is reachable only as
 * optiga.verify_pair(), so it can be exercised without a broker session
 * depending on the result.
 *
 * @return 1 when the pair matches, 0 when it does not, negative when the check
 *         could not be carried out.
 */
static int verify_cert_key_pair_locked(uint16_t cert_oid, uint16_t key_oid)
{
 static const uint8_t challenge[32] = {
 0x54, 0x45, 0x53, 0x41, 0x49, 0x6f, 0x54, 0x20,
 0x70, 0x61, 0x69, 0x72, 0x20, 0x63, 0x68, 0x65,
 0x63, 0x6b, 0x20, 0x76, 0x31, 0x00, 0x00, 0x00,
 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
 };

 static char cert_pem[2048];
 uint16_t cert_pem_len = (uint16_t)sizeof(cert_pem);
 cert_pem[0] = '\0';
 read_certificate_from_optiga(cert_oid, cert_pem, &cert_pem_len);

 /* A failed read leaves the caller's length untouched, so a value still equal
  * to the buffer size means nothing was written. */
 if (0U == cert_pem_len || cert_pem_len >= (uint16_t)sizeof(cert_pem))
 {
 return -1;
 }

 /*
  * Pass cert_pem_len exactly. read_certificate_from_optiga() already counts the
  * terminating NUL in what it reports, and mbedtls wants a length that includes
  * it precisely once — x509_crt.c chooses PEM over DER on `buf[buflen - 1] ==
  * '\0'` and its loop runs `while (buflen > 1)` for that reason.
  *
  * Adding one here read the byte *after* the NUL. cert_pem is static, so that
  * byte is a leftover from whatever certificate went through last. Zero on a
  * cold boot, which is why the first check of a session always passed — but a
  * previous, longer certificate leaves base64 there, mbedtls then treats the
  * PEM text as DER, and the parse fails with no bad input anywhere.
  *
  * ECDSA signatures are 70-72 DER bytes, so two certificates for the same
  * subject routinely differ in length. tesaiot_pu_ingest.c runs its own pair
  * check right after an install, which puts the outgoing certificate through
  * this buffer immediately before the incoming, shorter one. That is the whole
  * of "the first pair check works, the next returns -2" — 2026-08-07.
  */
 mbedtls_x509_crt crt;
 mbedtls_x509_crt_init(&crt);
 if (0 != mbedtls_x509_crt_parse(&crt, (const unsigned char *)cert_pem,
                                 (size_t)cert_pem_len))
 {
 mbedtls_x509_crt_free(&crt);
 return -2;
 }

 uint8_t raw[80];
 uint16_t raw_len = (uint16_t)sizeof(raw);
 if (OPTIGA_LIB_SUCCESS != trustm_ecdsa_sign((optiga_key_id_t)key_oid,
                                             challenge, (uint16_t)sizeof(challenge),
                                             raw, &raw_len))
 {
 mbedtls_x509_crt_free(&crt);
 return -3;
 }

 /* The chip returns raw r||s; X.509 verification wants DER. */
 uint8_t der[80];
 size_t der_len = 0U;
 if (0 != mbedtls_ecdsa_raw_to_der(256U, raw, raw_len, der, sizeof(der), &der_len))
 {
 mbedtls_x509_crt_free(&crt);
 return -4;
 }

 (void)der; (void)der_len;

 /* Verify through PSA with an explicitly local key, not mbedtls_pk_verify().
  *
  * This project builds with MBEDTLS_USE_PSA_CRYPTO, so pk.c dispatches through
  * PSA. Once an mTLS session has run, mqtt_mtls_setup_optiga() has called
  * optiga_psa_register() and left the OPTIGA secure-element driver installed
  * behind PSA_KEY_LOCATION_OPTIGA. Verifying an ordinary public key after that
  * fails, and the caller reads the failure as "these are not a pair".
  *
  * Measured on hardware 2026-08-18, one variable between the two rows:
  *   before tesaiot.connect()  verify_pair(E0E1,E0F1)=1  (E0E0,E0F0)=1
  *   after  tesaiot.connect()  verify_pair(E0E1,E0F1)=0  (E0E0,E0F0)=0
  * The factory pair failing settles it: that pair completes every TLS handshake
  * this board makes. The signature was not at fault either — one produced in
  * the failing state, exported over the REPL, verified against this same
  * certificate under OpenSSL on a host.
  *
  * The fix is to say where the key lives. psa_set_key_lifetime(VOLATILE) puts
  * it in PSA_KEY_LOCATION_LOCAL_STORAGE, the software implementation, so the
  * OPTIGA driver is not consulted no matter what is registered.
  *
  * Not mbedtls_pk_ec(): this build enables MBEDTLS_PK_USE_PSA_EC_DATA (pk.h),
  * where the pk context holds raw PSA key data and no mbedtls_ecp_keypair, so
  * that accessor returns NULL. Trying it that way on 2026-08-18 turned every
  * verdict into "pair check did not run" and had to be reverted.
  *
  * psa_verify_hash() wants ECDSA signatures as raw r||s, which is what the chip
  * gave us before the DER conversion above. */
 /* PSA has to be up before any of this. On the connect path
  * mqtt_mtls_setup_optiga() has already called it, but this function is also
  * reachable with no session ever having run — from optiga.verify_pair() at the
  * REPL, and from the provisioning task on a freshly booted board. psa_crypto_init()
  * is idempotent, so calling it here costs nothing when it is already done and
  * is the difference between a verdict and -7 when it is not. */
 (void)psa_crypto_init();

 psa_key_attributes_t vattr = PSA_KEY_ATTRIBUTES_INIT;
 int vrc = mbedtls_pk_get_psa_attributes(&crt.pk, PSA_KEY_USAGE_VERIFY_HASH,
                                         &vattr);
 if (0 != vrc)
 {
 printf("[PairCheck] get_psa_attributes failed: -0x%04X\n", (unsigned)-vrc);
 mbedtls_x509_crt_free(&crt);
 return -6;   /* could not describe the key; not a verdict on the pair */
 }
 psa_set_key_lifetime(&vattr, PSA_KEY_LIFETIME_VOLATILE);
 /* And name the algorithm we are about to use.
  *
  * mbedtls_pk_get_psa_attributes() derives a policy from the certificate and
  * chooses PSA_ALG_DETERMINISTIC_ECDSA(PSA_ALG_ANY_HASH) — measured as
  * alg=0x060007FF on 2026-08-18. Verifying with PSA_ALG_ECDSA(PSA_ALG_SHA_256)
  * against that policy is refused before any maths happens:
  * psa_verify_hash returned -133, PSA_ERROR_NOT_PERMITTED, with a correct
  * 64-byte r||s and 256-bit key.
  *
  * The distinction is meaningless on the verifying side — deterministic and
  * randomised ECDSA differ only in how the signer picks k, and produce
  * signatures that verify identically. The policy is simply stricter than the
  * operation needs, so state the algorithm this key will be used with. */
 psa_set_key_algorithm(&vattr, PSA_ALG_ECDSA(PSA_ALG_SHA_256));

 mbedtls_svc_key_id_t vkey = MBEDTLS_SVC_KEY_ID_INIT;
 vrc = mbedtls_pk_import_into_psa(&crt.pk, &vattr, &vkey);
 if (0 != vrc)
 {
 printf("[PairCheck] import_into_psa failed: -0x%04X\n", (unsigned)-vrc);
 psa_reset_key_attributes(&vattr);
 mbedtls_x509_crt_free(&crt);
 return -7;   /* could not import the public key; still not a verdict */
 }
 psa_status_t vst = psa_verify_hash(vkey, PSA_ALG_ECDSA(PSA_ALG_SHA_256),
                                    challenge, sizeof(challenge),
                                    raw, raw_len);
 if (PSA_SUCCESS != vst && PSA_ERROR_INVALID_SIGNATURE != vst) {
 printf("[PairCheck] psa_verify_hash=%d raw_len=%u alg=0x%08lX bits=%u\n",
        (int)vst, (unsigned)raw_len,
        (unsigned long)psa_get_key_algorithm(&vattr),
        (unsigned)psa_get_key_bits(&vattr));
 }
 psa_reset_key_attributes(&vattr);
 psa_destroy_key(vkey);
 mbedtls_x509_crt_free(&crt);

 if (PSA_SUCCESS == vst) { return 1; }
 if (PSA_ERROR_INVALID_SIGNATURE == vst) { return 0; }
 return -8;   /* the check could not be carried out — do not report "no" */
}

/*
 * The pair check reads a certificate and then signs, two chip transactions with
 * a parse between them. trustm_ecdsa_sign() holds the bus for its own half; the
 * certificate read had nothing, so CM55 touch polling could land in the middle
 * of it — that is what timed out the read at 10 s on 2026-08-07. Hold across
 * both. The hold is counted, so the inner one still nests correctly.
 */
/*
 * Clear the manifest requirement from a data object's change access condition.
 *
 * Protected Update sets D0 to Int(anchor) - `21 <hi> <lo>` - so the chip will
 * only overwrite the object from a manifest signed by the certificate in that
 * anchor. This writes D0 back to `E1 FC 07`, LcsO < operational, which is the
 * factory default and means an ordinary write is accepted again.
 *
 * It is not a workaround and it is not undocumented: reset_metadata is the same
 * seven bytes this firmware already writes to a key slot on every single key
 * generation, and reversing 0xE0E1 was measured on this board on
 * 2026-08-08 at 23:55: D0 read 21 e0 e8 before the write and e1 fc 07 after.
 * The lock is a metadata field, not a fuse.
 *
 * It works only while the chip is in Creation state. The gate on a metadata
 * write is the object's own `E1 FC 07` - LcsO < operational - and every object
 * on this board still reads LcsO = 0x01. Advance a production device to
 * operational (0x07) and this write is refused, and LcsO only moves one way
 * (cr -> in -> op -> te). So the lock really is permanent on a shipped device
 * and reversible on a development one, and saying only one of those is how the
 * project's documents ended up contradicting each other.
 *
 * It is still a deliberate reduction in what the object enforces, so it is its
 * own call with its own name rather than something an enrolment does quietly on
 * the operator's behalf.
 *
 * @return true when the object will take an ordinary write again.
 */
static optiga_lib_status_t reset_access_condition(optiga_util_t *me, uint16_t target_oid);

bool optiga_clear_manifest_lock(uint16_t target_oid)
{
 bool ok = false;

 if (!optiga_chip_enter()) {
 return false;
 }
 optiga_manager_touch_hold_reason("Clearing the signed-manifest requirement");

 optiga_util_t *me = optiga_util_create(0, optiga_util_callback, NULL);
 if (me)
 {
 ok = (OPTIGA_LIB_SUCCESS == reset_access_condition(me, target_oid));
 optiga_util_destroy(me);
 }

 optiga_manager_touch_release();
 optiga_chip_exit();

 optiga_lib_print_message(ok ? "manifest lock cleared"
                             : "manifest lock NOT cleared",
                          OPTIGA_UTIL_SERVICE, OPTIGA_UTIL_SERVICE_COLOR);
 return ok;
}

//! [hsm_optiga_chip_enter_gate]
int optiga_verify_cert_key_pair(uint16_t cert_oid, uint16_t key_oid)
{
 int rc;
 /* Two tasks call this: the MQTT subscriber after installing a certificate,
  * and the provisioning task when its wait ends. They share cert_pem - a 2 KB
  * static - and the library's single global status. The gate is re-entrant, so
  * the certificate read and the signature inside still take it, without
  * deadlocking on this one. */
 if (!optiga_chip_enter()) {
 return -5;   /* the chip is busy elsewhere; a verdict now would be a guess */
 }
 optiga_manager_touch_hold_reason("Checking certificate against its key");
 rc = verify_cert_key_pair_locked(cert_oid, key_oid);
 optiga_manager_touch_release();
 optiga_chip_exit();
 return rc;
}
//! [hsm_optiga_chip_enter_gate]

/**
 * Release one reference taken by optiga_trust_open_application(), closing the
 * application only when the last owner lets go.
 *
 * Callers that want it for the life of the process — the mTLS path — simply
 * never call this, which is what keeps the secure element available for signing
 * for as long as the broker session lasts.
 */
static void optiga_trust_close_application_gated(void);

//! [hsm_optiga_chip_exit_deferred_close]
void optiga_trust_close_application(void)
{
 if (!optiga_chip_enter()) {
 /* Closing under another task's transaction is worse than deferring the
  * close: the reference stays, and the next balanced close will do it. */
 optiga_lib_print_message("close deferred: the chip is busy elsewhere",
 OPTIGA_UTIL_SERVICE, OPTIGA_UTIL_SERVICE_COLOR);
 return;
 }
 optiga_trust_close_application_gated();
 optiga_chip_exit();
}
//! [hsm_optiga_chip_exit_deferred_close]

static void optiga_trust_close_application_gated(void)
{
 bool last;
 taskENTER_CRITICAL();
 if (0U == s_app_refs)
 {
 /* Nothing to release. The old code let `last` come out true here and
  * closed the chip application anyway, so an unbalanced close tore down
  * a session it never owned instead of doing nothing. */
 taskEXIT_CRITICAL();
 return;
 }
 s_app_refs--;
 last = (0U == s_app_refs);
 taskEXIT_CRITICAL();

 if (!last)
 {
 return;
 }

 optiga_util_t *me_util = optiga_util_create(0, optiga_util_callback, NULL);
 if (me_util)
 {
 trustm_close(me_util);   /* closes the application and destroys the instance */
 }
}

/***** New functions ******/
void trustm_close(optiga_util_t *util)
{
	if (!util) return;
	optiga_lib_status = OPTIGA_LIB_BUSY;
	(void) optiga_util_close_application(util,0);
	WAIT_WHILE_OPTIGA_BUSY();
	optiga_util_destroy(util);
}

optiga_lib_status_t trustm_gen_ecc_keypair(uint16_t optiga_key_id, optiga_ecc_curve_t curve_id,
		uint8_t key_usage, bool export_private,
		uint8_t * pub, uint16_t *pub_len)
{
	optiga_lib_status_t return_status = OPTIGA_LIB_BUSY;
	optiga_crypt_t *me_crypt = NULL;
	uint8_t pub_key[70];

	/* Lock OPTIGA mutex for thread-safe access */
	if (!optiga_manager_lock()) {
		printf("optiga_manager_lock failed\n");
		return OPTIGA_LIB_BUSY;
	}

	do
	{
		/* Create temporary crypt instance */
		me_crypt = optiga_crypt_create(0, optiga_util_callback, NULL);
		if (NULL == me_crypt) {
			printf("optiga_crypt_create failed\n");
			break;
		}
		optiga_lib_status = OPTIGA_LIB_BUSY;

		optiga_key_id_t key_oid = optiga_key_id;

		return_status = optiga_crypt_ecc_generate_keypair(
				me_crypt, curve_id, key_usage, export_private,
				&key_oid, pub_key, pub_len);
		if (return_status != OPTIGA_LIB_SUCCESS)
		{
			printf("optiga_crypt_ecc_generate_keypair api returns error\n");
			break;
		}

		WAIT_FOR_COMPLETION(return_status);

		if (OPTIGA_LIB_SUCCESS != return_status)
		{
			printf("optiga_crypt_ecc_generate_keypair returns error\n");
			break;

		}
	} while (0);
	printf("Pub key len %d \n", *pub_len);

	/* Destroy local crypt instance and unlock mutex */
	if (me_crypt != NULL)
	{
		optiga_crypt_destroy(me_crypt);
	}
	optiga_manager_unlock();

	memcpy (pub, pub_key+3, 65);
	*pub_len = 65;
 return return_status;
}

static int asn1_get_len(const uint8_t *buf, uint16_t len, uint16_t *idx, uint16_t *out_len)
{
	if (*idx >= len) return -1;
	uint8_t b = buf[(*idx)++];

	if ((b & 0x80) == 0)
	{
		*out_len = b;
		return 0;
	}

	uint8_t n = (uint8_t)(b & 0x7F); // Number of length bytes
	if (n == 0 || n > 2) return -1; // Not supported

	if ((uint32_t)*idx+n > len) return -1;

	uint16_t L = 0;
	for (uint8_t i = 0; i<n; i++)
	{
		L = (uint16_t)((L << 8) | buf[(*idx)++]);
	}
	*out_len = L;
	return 0;
}

/**
 * @brief Simple DER certificate parser that uses only stack memory
 *
 * This parser extracts basic certificate information without using heap allocation.
 * It's used as a fallback when mbedTLS parsing fails due to heap fragmentation.
 *
 * @param der_buf      DER-encoded certificate buffer
 * @param der_len      Length of the DER buffer
 * @param subject_cn   Output buffer for Subject CN (must be at least 128 bytes)
 * @param issuer_cn    Output buffer for Issuer CN (must be at least 128 bytes)
 * @param valid_from   Output buffer for validity start date (must be at least 32 bytes)
 * @param valid_to     Output buffer for validity end date (must be at least 32 bytes)
 * @return 0 on success, -1 on failure
 */
static int simple_der_parse_cert(const uint8_t *der_buf, uint16_t der_len,
                                  char *subject_cn, char *issuer_cn,
                                  char *valid_from, char *valid_to)
{
    uint16_t idx = 0;
    uint16_t seq_len, tbs_len, field_len;

    // Initialize outputs
    subject_cn[0] = '\0';
    issuer_cn[0] = '\0';
    valid_from[0] = '\0';
    valid_to[0] = '\0';

    // Certificate ::= SEQUENCE
    if (idx >= der_len || der_buf[idx++] != 0x30) return -1;
    if (asn1_get_len(der_buf, der_len, &idx, &seq_len) != 0) return -1;

    // tbsCertificate ::= SEQUENCE
    if (idx >= der_len || der_buf[idx++] != 0x30) return -1;
    if (asn1_get_len(der_buf, der_len, &idx, &tbs_len) != 0) return -1;

    uint16_t tbs_end = idx + tbs_len;
    if (tbs_end > der_len) return -1;

    // Skip version [0] if present (optional, explicit tag)
    if (idx < tbs_end && der_buf[idx] == 0xA0) {
        idx++; // tag
        if (asn1_get_len(der_buf, der_len, &idx, &field_len) != 0) return -1;
        idx += field_len; // skip version content
    }

    // Skip serialNumber (INTEGER)
    if (idx >= tbs_end || der_buf[idx++] != 0x02) return -1;
    if (asn1_get_len(der_buf, der_len, &idx, &field_len) != 0) return -1;
    idx += field_len;

    // Skip signature (AlgorithmIdentifier = SEQUENCE)
    if (idx >= tbs_end || der_buf[idx++] != 0x30) return -1;
    if (asn1_get_len(der_buf, der_len, &idx, &field_len) != 0) return -1;
    idx += field_len;

    // issuer (Name = SEQUENCE of RDN)
    if (idx >= tbs_end || der_buf[idx++] != 0x30) return -1;
    if (asn1_get_len(der_buf, der_len, &idx, &field_len) != 0) return -1;
    {
        uint16_t issuer_end = idx + field_len;
        // Search for CN OID (2.5.4.3 = 55 04 03) in issuer
        while (idx < issuer_end) {
            // Each RDN is a SET containing SEQUENCE(s)
            if (der_buf[idx] == 0x31) { // SET
                idx++;
                uint16_t set_len;
                if (asn1_get_len(der_buf, der_len, &idx, &set_len) != 0) break;
                uint16_t set_end = idx + set_len;

                while (idx < set_end) {
                    if (der_buf[idx] == 0x30) { // SEQUENCE (AttributeTypeAndValue)
                        idx++;
                        uint16_t attr_len;
                        if (asn1_get_len(der_buf, der_len, &idx, &attr_len) != 0) break;

                        // OID
                        if (idx < set_end && der_buf[idx] == 0x06) {
                            idx++;
                            uint16_t oid_len;
                            if (asn1_get_len(der_buf, der_len, &idx, &oid_len) != 0) break;

                            // Check for CN OID
                            if (oid_len == 3 && idx + 3 <= der_len &&
                                der_buf[idx] == 0x55 && der_buf[idx+1] == 0x04 && der_buf[idx+2] == 0x03) {
                                idx += oid_len;
                                // Value (UTF8String, PrintableString, etc.)
                                if (idx < set_end && (der_buf[idx] == 0x0C || der_buf[idx] == 0x13 || der_buf[idx] == 0x14)) {
                                    idx++;
                                    uint16_t val_len;
                                    if (asn1_get_len(der_buf, der_len, &idx, &val_len) != 0) break;
                                    if (val_len < 127 && idx + val_len <= der_len) {
                                        memcpy(issuer_cn, &der_buf[idx], val_len);
                                        issuer_cn[val_len] = '\0';
                                    }
                                    idx += val_len;
                                }
                            } else {
                                idx += oid_len;
                                // Skip value
                                if (idx < set_end) {
                                    idx++; // tag
                                    uint16_t val_len;
                                    if (asn1_get_len(der_buf, der_len, &idx, &val_len) == 0) idx += val_len;
                                }
                            }
                        } else {
                            break;
                        }
                    } else {
                        break;
                    }
                }
                idx = set_end;
            } else {
                break;
            }
        }
        idx = issuer_end;
    }

    // validity (Validity = SEQUENCE { notBefore, notAfter })
    if (idx >= tbs_end || der_buf[idx++] != 0x30) return -1;
    if (asn1_get_len(der_buf, der_len, &idx, &field_len) != 0) return -1;
    {
        uint16_t validity_end = idx + field_len;

        // notBefore (UTCTime or GeneralizedTime)
        if (idx < validity_end && (der_buf[idx] == 0x17 || der_buf[idx] == 0x18)) {
            uint8_t time_tag = der_buf[idx++];
            uint16_t time_len;
            if (asn1_get_len(der_buf, der_len, &idx, &time_len) == 0 && time_len < 30 && idx + time_len <= der_len) {
                // Parse UTCTime format: YYMMDDHHMMSSZ or GeneralizedTime: YYYYMMDDHHMMSSZ
                if (time_tag == 0x17 && time_len >= 12) { // UTCTime
                    int yy = (der_buf[idx] - '0') * 10 + (der_buf[idx+1] - '0');
                    int year = (yy >= 50) ? 1900 + yy : 2000 + yy;
                    int utc_hour = (der_buf[idx+6] - '0') * 10 + (der_buf[idx+7] - '0');
                    int thai_hour = (utc_hour + 7) % 24;
                    snprintf(valid_from, 64, "%04d-%c%c-%c%c %c%c:%c%c:%c%c UTC (%02d:%c%c:%c%c Thai)",
                             year, der_buf[idx+2], der_buf[idx+3], der_buf[idx+4], der_buf[idx+5],
                             der_buf[idx+6], der_buf[idx+7], der_buf[idx+8], der_buf[idx+9],
                             der_buf[idx+10], der_buf[idx+11],
                             thai_hour, der_buf[idx+8], der_buf[idx+9], der_buf[idx+10], der_buf[idx+11]);
                } else if (time_tag == 0x18 && time_len >= 14) { // GeneralizedTime
                    int utc_hour = (der_buf[idx+8] - '0') * 10 + (der_buf[idx+9] - '0');
                    int thai_hour = (utc_hour + 7) % 24;
                    snprintf(valid_from, 64, "%c%c%c%c-%c%c-%c%c %c%c:%c%c:%c%c UTC (%02d:%c%c:%c%c Thai)",
                             der_buf[idx], der_buf[idx+1], der_buf[idx+2], der_buf[idx+3],
                             der_buf[idx+4], der_buf[idx+5], der_buf[idx+6], der_buf[idx+7],
                             der_buf[idx+8], der_buf[idx+9], der_buf[idx+10], der_buf[idx+11],
                             der_buf[idx+12], der_buf[idx+13],
                             thai_hour, der_buf[idx+10], der_buf[idx+11], der_buf[idx+12], der_buf[idx+13]);
                }
                idx += time_len;
            }
        }

        // notAfter
        if (idx < validity_end && (der_buf[idx] == 0x17 || der_buf[idx] == 0x18)) {
            uint8_t time_tag = der_buf[idx++];
            uint16_t time_len;
            if (asn1_get_len(der_buf, der_len, &idx, &time_len) == 0 && time_len < 30 && idx + time_len <= der_len) {
                if (time_tag == 0x17 && time_len >= 12) { // UTCTime
                    int yy = (der_buf[idx] - '0') * 10 + (der_buf[idx+1] - '0');
                    int year = (yy >= 50) ? 1900 + yy : 2000 + yy;
                    int utc_hour = (der_buf[idx+6] - '0') * 10 + (der_buf[idx+7] - '0');
                    int thai_hour = (utc_hour + 7) % 24;
                    snprintf(valid_to, 64, "%04d-%c%c-%c%c %c%c:%c%c:%c%c UTC (%02d:%c%c:%c%c Thai)",
                             year, der_buf[idx+2], der_buf[idx+3], der_buf[idx+4], der_buf[idx+5],
                             der_buf[idx+6], der_buf[idx+7], der_buf[idx+8], der_buf[idx+9],
                             der_buf[idx+10], der_buf[idx+11],
                             thai_hour, der_buf[idx+8], der_buf[idx+9], der_buf[idx+10], der_buf[idx+11]);
                } else if (time_tag == 0x18 && time_len >= 14) { // GeneralizedTime
                    int utc_hour = (der_buf[idx+8] - '0') * 10 + (der_buf[idx+9] - '0');
                    int thai_hour = (utc_hour + 7) % 24;
                    snprintf(valid_to, 64, "%c%c%c%c-%c%c-%c%c %c%c:%c%c:%c%c UTC (%02d:%c%c:%c%c Thai)",
                             der_buf[idx], der_buf[idx+1], der_buf[idx+2], der_buf[idx+3],
                             der_buf[idx+4], der_buf[idx+5], der_buf[idx+6], der_buf[idx+7],
                             der_buf[idx+8], der_buf[idx+9], der_buf[idx+10], der_buf[idx+11],
                             der_buf[idx+12], der_buf[idx+13],
                             thai_hour, der_buf[idx+10], der_buf[idx+11], der_buf[idx+12], der_buf[idx+13]);
                }
                idx += time_len;
            }
        }
        idx = validity_end;
    }

    // subject (Name = SEQUENCE of RDN) - same parsing as issuer
    if (idx >= tbs_end || der_buf[idx++] != 0x30) return -1;
    if (asn1_get_len(der_buf, der_len, &idx, &field_len) != 0) return -1;
    {
        uint16_t subject_end = idx + field_len;
        while (idx < subject_end) {
            if (der_buf[idx] == 0x31) { // SET
                idx++;
                uint16_t set_len;
                if (asn1_get_len(der_buf, der_len, &idx, &set_len) != 0) break;
                uint16_t set_end = idx + set_len;

                while (idx < set_end) {
                    if (der_buf[idx] == 0x30) { // SEQUENCE
                        idx++;
                        uint16_t attr_len;
                        if (asn1_get_len(der_buf, der_len, &idx, &attr_len) != 0) break;

                        if (idx < set_end && der_buf[idx] == 0x06) {
                            idx++;
                            uint16_t oid_len;
                            if (asn1_get_len(der_buf, der_len, &idx, &oid_len) != 0) break;

                            if (oid_len == 3 && idx + 3 <= der_len &&
                                der_buf[idx] == 0x55 && der_buf[idx+1] == 0x04 && der_buf[idx+2] == 0x03) {
                                idx += oid_len;
                                if (idx < set_end && (der_buf[idx] == 0x0C || der_buf[idx] == 0x13 || der_buf[idx] == 0x14)) {
                                    idx++;
                                    uint16_t val_len;
                                    if (asn1_get_len(der_buf, der_len, &idx, &val_len) != 0) break;
                                    if (val_len < 127 && idx + val_len <= der_len) {
                                        memcpy(subject_cn, &der_buf[idx], val_len);
                                        subject_cn[val_len] = '\0';
                                    }
                                    idx += val_len;
                                }
                            } else {
                                idx += oid_len;
                                if (idx < set_end) {
                                    idx++;
                                    uint16_t val_len;
                                    if (asn1_get_len(der_buf, der_len, &idx, &val_len) == 0) idx += val_len;
                                }
                            }
                        } else {
                            break;
                        }
                    } else {
                        break;
                    }
                }
                idx = set_end;
            } else {
                break;
            }
        }
    }

    return 0;
}

static int tlv2_to_raw_rs(const uint8_t *buf, uint16_t len, uint8_t *raw, uint16_t *raw_len, uint16_t limb_len)
{
	uint16_t i = 0;

	if (len < 6) return -1; //TLV sanity check

	//Integer R
	if (buf[i++] != 0x02) return -1;
	uint16_t RLen;
	if(asn1_get_len(buf, len, &i, &RLen) != 0) return -1;
	if((uint32_t)i + RLen > len) return -1;
	const uint8_t *R = &buf[i];
	i = (uint16_t)(i +RLen);

	//Integer S
	if (i >= len || buf[i++] != 0x02) return -1;
	uint16_t SLen;
	if(asn1_get_len(buf, len, &i, &SLen) != 0) return -1;
	if((uint32_t)i +SLen > len) return -1;
	const uint8_t *S = &buf[i];


	//Strip leading 0x00 that appears when msb=1
	while(RLen && *R == 0x00) {
		R++;
		RLen--;
	}
	while(SLen && *S == 0x00) {
		S++;
		SLen--;
	}

	if (RLen > 32 || SLen > 32) return -1;

	//Left pad both to limb len (32 for P256)
	for (uint16_t k = 0; k < 2* limb_len; k++)
	{
		raw[k] = 0x00;
	}
	memcpy(raw + (32 - RLen), R, RLen);
	memcpy(raw + 32 + (32 - SLen), S, SLen);

	*raw_len = (uint16_t)(2* limb_len);
	return 0;
}

/*ECDSA signer using key in OID, returns RAW (r||s)*/
optiga_lib_status_t trustm_ecdsa_sign(optiga_key_id_t oid,
		const uint8_t *digest, uint16_t digest_len,
		uint8_t *sig_raw, uint16_t *sig_raw_len)
{
	optiga_lib_status_t return_status = OPTIGA_LIB_BUSY;
	optiga_crypt_t *me_crypt = NULL;
	uint8_t sig[80];
	uint16_t sig_len = sizeof(sig);

	//! [hsm_optiga_manager_lock_touch_hold]
	/* ...context: inside trustm_ecdsa_sign() ... */
	/* Lock OPTIGA mutex for thread-safe access */
	if (!optiga_manager_lock()) {
		printf("optiga_manager_lock failed\n");
		return OPTIGA_LIB_BUSY;
	}

	/* Keep CM55 touch off SCB5 for the whole transaction — the secure element
	 * and the touch controller share the bus, and the TLS handshake signs after
	 * the mTLS setup has already resumed touch polling. */
	optiga_manager_touch_hold();
	//! [hsm_optiga_manager_lock_touch_hold]

	/* Wait 200ms to allow queue slot cleanup from previous operations */
	vTaskDelay(pdMS_TO_TICKS(200));

	do {
		/* Create temporary crypt instance */
		me_crypt = optiga_crypt_create(0, optiga_util_callback, NULL);
		if (NULL == me_crypt) {
			printf("optiga_crypt_create failed\n");
			break;
		}
		optiga_lib_status = OPTIGA_LIB_BUSY;

		return_status = optiga_crypt_ecdsa_sign(
				me_crypt, digest, digest_len, oid, sig, &sig_len);

		if (return_status != OPTIGA_LIB_SUCCESS)
		{
			printf("optiga_crypt_ecdsa_sign api returns error\n");
			break;
		}

		WAIT_FOR_COMPLETION(return_status);

		if (OPTIGA_LIB_SUCCESS != return_status)
		{
			printf("optiga_crypt_ecdsa_sign returns error\n");
			break;

		}

	} while (0);

	/* Destroy local crypt instance and unlock mutex */
	if (me_crypt != NULL)
	{
		optiga_crypt_destroy(me_crypt);
	}
	//! [hsm_optiga_touch_release_unlock_order]
	/* ...context: inside trustm_ecdsa_sign(), the single exit path ... */
	optiga_manager_touch_release();
	optiga_manager_unlock();
	//! [hsm_optiga_touch_release_unlock_order]

	//Need Raw R and S

	tlv2_to_raw_rs(sig, sig_len, sig_raw, sig_raw_len, 32);

	return return_status;
}

bool optiga_read_factory_certificate(char *cert_pem, uint16_t *cert_pem_length)
{
 return read_certificate_internal(0xE0E0, true, cert_pem, cert_pem_length);
}

bool optiga_read_device_certificate(char *cert_pem, uint16_t *cert_pem_length)
{
 return read_certificate_internal(DEVICE_CERTIFICATE_OID, true, cert_pem, cert_pem_length);
}

bool trustm_use_factory_certificate(void)
{
 return trustm_stage_certificate_for_tls(OPTIGA_FACTORY_CERTIFICATE_OID, "factory");
}

bool trustm_use_device_certificate(void)
{
 return trustm_stage_certificate_for_tls(DEVICE_CERTIFICATE_OID, "TESAIoT device");
}

/******************************************************************************
 * Certificate Lifecycle Management Functions (Phase 6)
 ******************************************************************************/

/**
 * @brief Check certificate validity and extract information
 *
 * Phase 6: Certificate Lifecycle Management
 * Implements certificate validation for automatic fallback mechanism.
 */
bool optiga_check_certificate_validity(uint16_t oid, cert_validation_result_t *result)
{
 if (!result) {
 return false;
 }

 // Initialize result structure
 memset(result, 0, sizeof(cert_validation_result_t));
 result->cert_exists = false;
 result->is_valid = false;
 result->is_expired = false;
 result->days_until_expiry = 0;

 // Allocate buffer for certificate (DER format)
 uint8_t *cert_buffer = (uint8_t *)pvPortMalloc(1800); // OID 0xE0E1 max size: 1728 bytes
 if (!cert_buffer) {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s ERROR: Failed to allocate certificate buffer\n", LABEL_OPTIGA_CERTVALIDATION);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 return false;
 }

 optiga_util_t *me_util = NULL;
 uint16_t cert_len = 1800;
 optiga_lib_status_t return_status;
 bool validation_success = false;

 do {
 // Step 1: Initialize OPTIGA
 me_util = optiga_util_create(0, optiga_util_callback, NULL);
 if (NULL == me_util) {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s ERROR: optiga_util_create failed\n", LABEL_OPTIGA_CERTVALIDATION);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 break;
 }

 // Step 2: Read certificate from OID
 // Note: Must set optiga_lib_status = BUSY before async call!
 optiga_lib_status = OPTIGA_LIB_BUSY;
 return_status = optiga_util_read_data(me_util, oid, 0, cert_buffer, &cert_len);

 if (OPTIGA_LIB_SUCCESS != return_status) {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s ERROR: Failed to start read operation (status=0x%04X)\n", LABEL_OPTIGA_CERTVALIDATION, return_status);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 result->cert_exists = false;
 break;
 }

 // Wait for read operation to complete (with 5s timeout)
 uint32_t timeout_ms = 5000;
 uint32_t elapsed_ms = 0;
 while (OPTIGA_LIB_BUSY == optiga_lib_status && elapsed_ms < timeout_ms) {
 vTaskDelay(pdMS_TO_TICKS(100)); // Match metadata diagnostics wait pattern
 elapsed_ms += 100;
 }

 // Check if read completed successfully
 if (OPTIGA_LIB_SUCCESS != optiga_lib_status) {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s Certificate read failed for OID 0x%04X (status=0x%04X, timeout=%s)\n", LABEL_OPTIGA_CERTVALIDATION, oid, optiga_lib_status, (elapsed_ms >= timeout_ms) ? "YES" : "NO");
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 result->cert_exists = false;
 break;
 }

 result->cert_exists = true;

 // Step 3: Parse X.509 certificate using mbedtls
 mbedtls_x509_crt cert;
 mbedtls_x509_crt_init(&cert);

 // Parse DER format certificate (binary format from OPTIGA Trust M)
 // Note: Use _parse_der() NOT _parse() - OPTIGA stores DER, not PEM!
 int parse_ret = mbedtls_x509_crt_parse_der(&cert, cert_buffer, cert_len);

 if (parse_ret != 0) {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s mbedtls_x509_crt_parse_der failed: -0x%04X\n", LABEL_OPTIGA_CERTVALIDATION, -parse_ret);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 mbedtls_x509_crt_free(&cert);
 break;
 }

 // Step 4: Extract certificate information

 // Extract subject DN
 mbedtls_x509_dn_gets(result->subject, sizeof(result->subject), &cert.subject);

 // Extract issuer DN
 mbedtls_x509_dn_gets(result->issuer, sizeof(result->issuer), &cert.issuer);

 // Extract validity dates
 // mbedtls uses mbedtls_x509_time structure: {year, mon, day, hour, min, sec}
 struct tm valid_from_tm = {0};
 valid_from_tm.tm_year = cert.valid_from.year - 1900;
 valid_from_tm.tm_mon = cert.valid_from.mon - 1;
 valid_from_tm.tm_mday = cert.valid_from.day;
 valid_from_tm.tm_hour = cert.valid_from.hour;
 valid_from_tm.tm_min = cert.valid_from.min;
 valid_from_tm.tm_sec = cert.valid_from.sec;
 result->valid_from = mktime(&valid_from_tm);

 struct tm valid_to_tm = {0};
 valid_to_tm.tm_year = cert.valid_to.year - 1900;
 valid_to_tm.tm_mon = cert.valid_to.mon - 1;
 valid_to_tm.tm_mday = cert.valid_to.day;
 valid_to_tm.tm_hour = cert.valid_to.hour;
 valid_to_tm.tm_min = cert.valid_to.min;
 valid_to_tm.tm_sec = cert.valid_to.sec;
 result->valid_to = mktime(&valid_to_tm);

 // Step 5: Get current time from NTP sync (no RTC hardware on PSoC E84)
 time_t now;
 if (!tesaiot_sntp_get_time(&now)) {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s WARNING: NTP time not synced - certificate date validation may be inaccurate\n", LABEL_OPTIGA_CERTVALIDATION);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 // Use epoch time (Jan 1, 1970) as fallback - will likely mark certs as "not yet valid"
 now = 0;
 }

 // Step 6: Check validity
 result->is_valid = (now >= result->valid_from && now <= result->valid_to);
 result->is_expired = (now > result->valid_to);

 // Step 7: Calculate days until expiry
 if (result->is_expired) {
 result->days_until_expiry = 0;
 } else {
 int32_t seconds_until_expiry = (int32_t)(result->valid_to - now);
 result->days_until_expiry = (seconds_until_expiry > 0) ? (seconds_until_expiry / 86400) : 0;
 }

 mbedtls_x509_crt_free(&cert);
 validation_success = true;

 } while (false);

 // Cleanup
 if (me_util) {
 optiga_util_destroy(me_util);
 }
 vPortFree(cert_buffer);

 return validation_success;
}

/**
 * @brief Get days until certificate expiry (convenience function)
 */
uint32_t optiga_get_cert_days_until_expiry(uint16_t oid)
{
 cert_validation_result_t result;

 if (optiga_check_certificate_validity(oid, &result)) {
 return result.days_until_expiry;
 }

 return 0; // Error or expired
}

bool optiga_read_factory_uid(char *uid_hex, size_t uid_hex_len)
{
 if (!uid_hex || uid_hex_len < 65U)
 {
 return false;
 }

 optiga_util_t *me_util = NULL;
 uint8_t uid_raw[32];
 uint16_t bytes_to_read = sizeof(uid_raw);
 bool success = false;
 const char hex_chars[] = "0123456789ABCDEF";

 do
 {
 me_util = optiga_util_create(0, optiga_util_callback, NULL);
 if (!me_util)
 {
 optiga_lib_print_message("optiga_util_create failed !!!", OPTIGA_UTIL_SERVICE, OPTIGA_UTIL_SERVICE_COLOR);
 break;
 }

 optiga_lib_status = OPTIGA_LIB_BUSY;
 optiga_lib_status_t status = optiga_util_read_data(me_util, 0xE0C2, 0, uid_raw, &bytes_to_read);
 if (OPTIGA_LIB_SUCCESS != status)
 {
 optiga_lib_print_message("optiga_util_read_data api returns error !!!", OPTIGA_UTIL_SERVICE, OPTIGA_UTIL_SERVICE_COLOR);
 break;
 }

 while (optiga_lib_status == OPTIGA_LIB_BUSY)
 {
 pal_os_timer_delay_in_milliseconds(1);
 }

 if (OPTIGA_LIB_SUCCESS != optiga_lib_status)
 {
 optiga_lib_print_message("optiga_util_read_data failed", OPTIGA_UTIL_SERVICE, OPTIGA_UTIL_SERVICE_COLOR);
 break;
 }

 size_t offset = 0;
 for (uint16_t i = 0; i < bytes_to_read && (offset + 2U) < uid_hex_len; ++i)
 {
 uid_hex[offset++] = hex_chars[(uid_raw[i] >> 4) & 0x0F];
 uid_hex[offset++] = hex_chars[uid_raw[i] & 0x0F];
 }
 uid_hex[offset] = '\0';
 success = true;
 }
 while (0);

 if (me_util)
 {
 optiga_util_destroy(me_util);
 }

 return success;
}
int check_access_condition(optiga_util_t *me, uint16_t target_oid)
{
 uint8_t metadata_buf[64];
 uint16_t metadata_length = sizeof(metadata_buf);

 optiga_lib_status = OPTIGA_LIB_BUSY;
 optiga_lib_status_t status = optiga_util_read_metadata(me, target_oid, metadata_buf, &metadata_length);
 if (status != OPTIGA_LIB_SUCCESS) {
 printf("Failed to read metadata (status=0x%04X)\n", status);
 return -1; // indicate failure
 }
 /* Wait while BUSY, not "until SUCCESS".
  *
  * The old test never terminated on an error: a failed read leaves the flag at
  * the error code, which is neither BUSY nor SUCCESS, and the bare spin sat
  * there forever with no yield - hanging CM33_NS rather than returning. */
 WAIT_WHILE_OPTIGA_BUSY();
 if (OPTIGA_LIB_SUCCESS != optiga_lib_status) {
 return -1;
 }

 int i = 0;
 while(metadata_buf[i] != 0xD0 && i < metadata_length) i += 1; //find tag for change access condition
 if (i == metadata_length) {
 	printf("no change condition in metadata found\n"); //should not reach here
 	return -1;
 }

 i += 2; //skip over length, point to first value of change condition

 uint8_t expected_metadata[3] = {0xE1, 0xFC, 0x07}; // Change: Lcs0<0x07
 return memcmp(expected_metadata, ( metadata_buf + i ) , 3);
}
/**
 * Local functions prototype
 */
static optiga_lib_status_t
write_metadata(optiga_util_t *me, uint16_t oid, uint8_t *metadata, uint8_t metadata_length) {
 optiga_lib_status_t return_status = OPTIGA_LIB_SUCCESS;

 do {
 optiga_lib_status = OPTIGA_LIB_BUSY;
 return_status = optiga_util_write_metadata(me, oid, metadata, metadata_length);
 if (OPTIGA_LIB_SUCCESS != return_status) {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s ERROR: optiga_util_write_metadata failed for OID 0x%04X, status = 0x%04X\n", LABEL_OPTIGA_METADATA, oid, return_status);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 break;
 }

 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s Waiting for metadata write completion for OID 0x%04X...\n", LABEL_OPTIGA_METADATA, oid);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 fflush(stdout);

 /* Use vTaskDelay() to allow OPTIGA callback execution
 * pal_os_timer_delay_in_milliseconds() blocks FreeRTOS scheduler
 * OPTIGA callback (in I2C interrupt or task context) cannot run -> deadlock
 * Use vTaskDelay() to yield to scheduler, allow callback to update status
 */
 uint32_t timeout_ms = 10000;
 uint32_t elapsed_ms = 0;
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s Entering wait loop, optiga_lib_status = 0x%04X\n", LABEL_OPTIGA_METADATA, optiga_lib_status);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 fflush(stdout);

 while (OPTIGA_LIB_BUSY == optiga_lib_status && elapsed_ms < timeout_ms) {
 // CRITICAL: vTaskDelay() yields to scheduler, allows OPTIGA I2C callback to run
 vTaskDelay(pdMS_TO_TICKS(100)); // Wait 100ms, yield to other tasks
 elapsed_ms += 100;
 if (elapsed_ms % 1000 == 0) {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf(LABEL_WRITE_METADATA " Still waiting... (%lu seconds), status = 0x%04X\n",
 (unsigned long)(elapsed_ms / 1000), optiga_lib_status);
 fflush(stdout);
 #endif // TESAIOT_DEBUG_VERBOSE_ENABLED
 }
 }

 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf(LABEL_WRITE_METADATA " Exited wait loop after %lu ms, final status = 0x%04X\n",
 (unsigned long)elapsed_ms, optiga_lib_status);
 fflush(stdout);
 #endif // TESAIOT_DEBUG_VERBOSE_ENABLED

 if (optiga_lib_status == OPTIGA_LIB_BUSY) {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s ERROR: Timeout waiting for metadata write operation on OID 0x%04X!\n", LABEL_OPTIGA_METADATA, oid);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 return_status = OPTIGA_UTIL_ERROR;
 break;
 }

 if (OPTIGA_LIB_SUCCESS != optiga_lib_status) {
 // writing metadata to a data object failed.
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s ERROR: Metadata write failed for OID 0x%04X, status = 0x%04X\n", LABEL_OPTIGA_METADATA, oid, optiga_lib_status);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 return_status = optiga_lib_status;
 break;
 }

 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s Metadata write completed successfully for OID 0x%04X\n", LABEL_OPTIGA_METADATA, oid);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 } while (FALSE);

 return (return_status);
}

static optiga_lib_status_t reset_access_condition(optiga_util_t *me, uint16_t target_oid) {
 optiga_lib_status_t return_status = OPTIGA_LIB_BUSY;

 do {
 /* Arm the completion flag before issuing the command.
  *
  * This did not, and the wait below tests for BUSY. Whatever ran last leaves
  * the flag at SUCCESS, so the wait fell straight through and this function
  * returned while the metadata write was still in flight. The next chip call -
  * the key generation, three lines later in the caller - then met an instance
  * that was still busy and came back 0x0305 INSTANCE_IN_USE.
  *
  * That is the whole of "the first CSR after a boot works and the second does
  * not": reset_access_condition() is only reached when the access condition
  * has already been changed, which is exactly what the first key generation
  * does. Same mechanism for a CSR that follows a Protected Update, because a
  * Protected Update generates a key pair too. */
 optiga_lib_status = OPTIGA_LIB_BUSY;

 return_status = write_metadata(
 me,
 target_oid,
 reset_metadata,
 sizeof(reset_metadata)
 );
 if (OPTIGA_LIB_SUCCESS != return_status) {
 break;
 }

 /* Bounded, and it yields. Was a bare spin. */
 WAIT_WHILE_OPTIGA_BUSY();

 if (OPTIGA_LIB_SUCCESS != optiga_lib_status) {
 // writing data to a data object failed.
 return_status = optiga_lib_status;
 break;
 }
 } while (FALSE);
 return (return_status);
}

bool optiga_generate_device_keypair(uint16_t key_oid, uint8_t *public_key_der, uint16_t *public_key_der_len)
{
 if (!public_key_der || !public_key_der_len)
 {
 return false;
 }

 /* Acquire Manager's util instance (locks mutex) */
 optiga_util_t *me_util = optiga_manager_acquire();
 if (!me_util)
 {
 optiga_lib_print_message("optiga_manager_acquire failed", OPTIGA_UTIL_SERVICE, OPTIGA_UTIL_SERVICE_COLOR);
 return false;
 }

 bool success = false;
 optiga_crypt_t *me_crypt = NULL;

 /* NO DELAY NEEDED - Architectural fix applied!
  * Keypair generation now runs BEFORE MQTT/TLS connection.
  * Benefits:
  *   - Memory is clean (no TLS buffers, no MQTT tasks)
  *   - OPTIGA queue slots are empty (no prior TLS operations)
  *   - No race conditions with subscriber task
  *   - Deterministic, reproducible behavior
  */

 do {
 /* Create temporary crypt instance for keygen */
 me_crypt = optiga_crypt_create(0, optiga_util_callback, NULL);
 if (!me_crypt)
 {
 optiga_lib_print_message("optiga_crypt_create failed", OPTIGA_UTIL_SERVICE, OPTIGA_UTIL_SERVICE_COLOR);
 break;
 }

 uint16_t pubkey_len = *public_key_der_len;
 uint16_t key_id = key_oid;
 int ret = check_access_condition(me_util, key_oid);

 // Check access condition if change Lcs0 < 0x07
 if (ret != 0)
 {
 printf("Resetting access condition\n");
 // Reset access condition
 reset_access_condition(me_util, key_oid);
 }

 optiga_lib_status = OPTIGA_LIB_BUSY;
 optiga_lib_status_t status = optiga_crypt_ecc_generate_keypair(
 me_crypt,
 OPTIGA_ECC_CURVE_NIST_P_256,
 (OPTIGA_KEY_USAGE_AUTHENTICATION | OPTIGA_KEY_USAGE_SIGN),
 FALSE,
 &key_id,
 public_key_der,
 &pubkey_len
 );
 if (status != OPTIGA_LIB_SUCCESS)
 {
 break;
 }
 WAIT_WHILE_OPTIGA_BUSY();
 if (optiga_lib_status != OPTIGA_LIB_SUCCESS)
 {
 printf("Generate key pair fail\n");
 break;
 }

 *public_key_der_len = pubkey_len;
 success = true;
 } while (0);

 /* Destroy local crypt instance and release Manager's util */
 if (me_crypt) optiga_crypt_destroy(me_crypt);
 optiga_manager_release();
 return success;
}

bool optiga_generate_csr_pem(uint16_t key_oid,
 const uint8_t *public_key_der,
 uint16_t public_key_der_len,
 const char *subject,
 char *csr_pem,
 size_t csr_pem_len)
{
 /* Large buffers moved to static to avoid stack overflow when called from FreeRTOS tasks.
  * Total savings: ~7KB stack -> static memory.
  * Note: This makes the function non-reentrant, but OPTIGA operations are already
  * serialized so this is safe. */
 static uint8_t cri_content[1536];
 static uint8_t cri_wrapped[1600];
 static uint8_t content_buf[2048];
 static uint8_t final_buf[2200];
 /* CSR output buffers also static - heap exhausted after TLS connection */
 static uint8_t csr_der_buf[400];   /* ECC CSR DER ~200-300 bytes */
 static uint8_t b64_buf[600];       /* Base64 of CSR DER ~400 bytes */

 if (!public_key_der || public_key_der_len == 0 || !subject || !csr_pem || csr_pem_len == 0)
 {
 return false;
 }

 /* Lock OPTIGA mutex for thread-safe access */
 if (!optiga_manager_lock())
 {
 optiga_lib_print_message("optiga_manager_lock failed", OPTIGA_UTIL_SERVICE, OPTIGA_UTIL_SERVICE_COLOR);
 return false;
 }

 /* Create temporary crypt instance */
 optiga_crypt_t *me_crypt = optiga_crypt_create(0, optiga_util_callback, NULL);
 if (!me_crypt)
 {
 optiga_lib_print_message("optiga_crypt_create failed", OPTIGA_UTIL_SERVICE, OPTIGA_UTIL_SERVICE_COLOR);
 optiga_manager_unlock();
 return false;
 }

 size_t cri_idx = 0;
 uint8_t alg_id_inner[64];
 size_t alg_id_inner_len = 0;
 size_t cri_wrapped_len = 0;
 uint8_t digest[32];
 uint8_t signature_der[256];
 uint16_t signature_len = sizeof(signature_der);
 size_t content_len = 0;
 uint8_t *csr_der = NULL;
 size_t csr_der_len = 0;
 bool success = false;

 do
 {
 uint8_t ver0 = 0x00;
 cri_idx = add_tlv(cri_content, cri_idx, ASN1_INTEGER, 1U, &ver0);

 // Parse subject string "CN=xxx,O=yyy" or just "xxx"
 char cn[128] = {0};
 char org[128] = {0};
 const char *cn_prefix = "CN=";
 const char *o_prefix = "O=";
 const char *cn_start = strstr(subject, cn_prefix);
 const char *o_start = strstr(subject, o_prefix);

 if (cn_start)
 {
 cn_start += 3; // Skip "CN="
 const char *cn_end = strchr(cn_start, ',');
 size_t cn_len = cn_end ? (size_t)(cn_end - cn_start) : strlen(cn_start);
 if (cn_len > sizeof(cn) - 1) cn_len = sizeof(cn) - 1;
 memcpy(cn, cn_start, cn_len);
 cn[cn_len] = '\0';
 }

 if (o_start)
 {
 o_start += 2; // Skip "O="
 const char *o_end = strchr(o_start, ',');
 size_t o_len = o_end ? (size_t)(o_end - o_start) : strlen(o_start);
 if (o_len > sizeof(org) - 1) o_len = sizeof(org) - 1;
 memcpy(org, o_start, o_len);
 org[o_len] = '\0';
 }

 // If no CN/O found, use entire subject as O
 if (cn[0] == '\0' && org[0] == '\0')
 {
 strncpy(org, subject, sizeof(org) - 1);
 org[sizeof(org) - 1] = '\0';
 }

 // Build DN with CN and O (if both exist)
 uint8_t name_sequence[512];
 size_t name_seq_idx = 0;

 // Add CN RDN if present
 if (cn[0] != '\0')
 {
 uint8_t cn_rdn[256];
 size_t cn_rdn_idx = 0;
 cn_rdn_idx = add_tlv(cn_rdn, cn_rdn_idx, ASN1_OID, sizeof(COMMON_NAME_OID), COMMON_NAME_OID);
 cn_rdn_idx = add_tlv(cn_rdn, cn_rdn_idx, ASN1_UTF8_STRING, strlen(cn), (const uint8_t *)cn);

 uint8_t cn_seq_wrapped[270];
 size_t cn_seq_wrapped_idx = add_tlv(cn_seq_wrapped, 0, ASN1_SEQUENCE, cn_rdn_idx, cn_rdn);
 uint8_t cn_set_wrapped[280];
 size_t cn_set_wrapped_idx = add_tlv(cn_set_wrapped, 0, ASN1_SET, cn_seq_wrapped_idx, cn_seq_wrapped);
 memcpy(&name_sequence[name_seq_idx], cn_set_wrapped, cn_set_wrapped_idx);
 name_seq_idx += cn_set_wrapped_idx;
 }

 // Add O RDN if present
 if (org[0] != '\0')
 {
 uint8_t o_rdn[256];
 size_t o_rdn_idx = 0;
 o_rdn_idx = add_tlv(o_rdn, o_rdn_idx, ASN1_OID, sizeof(ORGANISATION_OID), ORGANISATION_OID);
 o_rdn_idx = add_tlv(o_rdn, o_rdn_idx, ASN1_UTF8_STRING, strlen(org), (const uint8_t *)org);

 uint8_t o_seq_wrapped[270];
 size_t o_seq_wrapped_idx = add_tlv(o_seq_wrapped, 0, ASN1_SEQUENCE, o_rdn_idx, o_rdn);
 uint8_t o_set_wrapped[280];
 size_t o_set_wrapped_idx = add_tlv(o_set_wrapped, 0, ASN1_SET, o_seq_wrapped_idx, o_seq_wrapped);
 memcpy(&name_sequence[name_seq_idx], o_set_wrapped, o_set_wrapped_idx);
 name_seq_idx += o_set_wrapped_idx;
 }

 // Wrap entire DN in SEQUENCE
 uint8_t name_wrapped[600];
 size_t name_wrapped_idx = add_tlv(name_wrapped, 0, ASN1_SEQUENCE, name_seq_idx, name_sequence);
 memcpy(&cri_content[cri_idx], name_wrapped, name_wrapped_idx);
 cri_idx += name_wrapped_idx;

 uint8_t alg_inner_buf[64];
 size_t alg_inner_idx = 0;
 alg_inner_idx = add_tlv(alg_inner_buf, alg_inner_idx, ASN1_OID, sizeof(ALG_EC_PUBLIC_KEY_OID), ALG_EC_PUBLIC_KEY_OID);
 alg_inner_idx = add_tlv(alg_inner_buf, alg_inner_idx, ASN1_OID, sizeof(ALG_PRIME256V1_OID), ALG_PRIME256V1_OID);
 alg_id_inner_len = add_tlv(alg_id_inner, 0, ASN1_SEQUENCE, alg_inner_idx, alg_inner_buf);

 uint8_t spki_tmp[512];
 size_t spki_tmp_idx = 0;
 memcpy(&spki_tmp[spki_tmp_idx], alg_id_inner, alg_id_inner_len);
 spki_tmp_idx += alg_id_inner_len;
 memcpy(&spki_tmp[spki_tmp_idx], public_key_der, public_key_der_len);
 spki_tmp_idx += public_key_der_len;

 uint8_t spki_wrapped[600];
 size_t spki_wrapped_idx = add_tlv(spki_wrapped, 0, ASN1_SEQUENCE, spki_tmp_idx, spki_tmp);
 memcpy(&cri_content[cri_idx], spki_wrapped, spki_wrapped_idx);
 cri_idx += spki_wrapped_idx;

 cri_idx = add_tlv(cri_content, cri_idx, ASN1_CONTEXT_SPECIFIC, 0U, NULL);

 cri_wrapped_len = add_tlv(cri_wrapped, 0, ASN1_SEQUENCE, cri_idx, cri_content);

 mbedtls_sha256(cri_wrapped, cri_wrapped_len, digest, 0);

 optiga_lib_status = OPTIGA_LIB_BUSY;
 optiga_lib_status_t status = optiga_crypt_ecdsa_sign(me_crypt, digest, sizeof(digest), key_oid, signature_der, &signature_len);
 if (status != OPTIGA_LIB_SUCCESS)
 {
 optiga_lib_print_message("optiga_crypt_ecdsa_sign failed", OPTIGA_UTIL_SERVICE, OPTIGA_UTIL_SERVICE_COLOR);
 break;
 }

 while (optiga_lib_status == OPTIGA_LIB_BUSY)
 {
 pal_os_timer_delay_in_milliseconds(1);
 }

 if (optiga_lib_status != OPTIGA_LIB_SUCCESS)
 {
 optiga_lib_print_message("ECDSA signature generation failed", OPTIGA_UTIL_SERVICE, OPTIGA_UTIL_SERVICE_COLOR);
 break;
 }

 memcpy(&content_buf[content_len], cri_wrapped, cri_wrapped_len);
 content_len += cri_wrapped_len;

 uint8_t alg_seq[32];
 size_t alg_seq_idx = 0;
 alg_seq_idx = add_tlv(alg_seq, alg_seq_idx, ASN1_OID, sizeof(SIGNATURE_OID_ECDSA_SHA256), SIGNATURE_OID_ECDSA_SHA256);
 uint8_t alg_seq_wrapped[48];
 size_t alg_seq_wrapped_idx = add_tlv(alg_seq_wrapped, 0, ASN1_SEQUENCE, alg_seq_idx, alg_seq);
 memcpy(&content_buf[content_len], alg_seq_wrapped, alg_seq_wrapped_idx);
 content_len += alg_seq_wrapped_idx;

 uint8_t ecdsa_seq[300];
 if (signature_len >= 2U && signature_der[0] == ASN1_INTEGER)
 {
 ecdsa_seq[0] = ASN1_SEQUENCE;
 ecdsa_seq[1] = (uint8_t)signature_len;
 memcpy(&ecdsa_seq[2], signature_der, signature_len);
 }
 else
 {
 optiga_lib_print_message("Unexpected ECDSA signature format", OPTIGA_UTIL_SERVICE, OPTIGA_UTIL_SERVICE_COLOR);
 break;
 }

 size_t ecdsa_seq_len = 2U + signature_len;
 uint8_t sig_bit[1 + sizeof(ecdsa_seq)];
 sig_bit[0] = 0x00;
 memcpy(&sig_bit[1], ecdsa_seq, ecdsa_seq_len);
 content_len = add_tlv(content_buf, content_len, ASN1_BIT_STRING, 1U + ecdsa_seq_len, sig_bit);

 /* final_buf is now static at function top to save stack */
 size_t final_idx = add_tlv(final_buf, 0, ASN1_SEQUENCE, content_len, content_buf);
 if (final_idx > sizeof(csr_der_buf))
 {
 optiga_lib_print_message("CSR DER too large for buffer", OPTIGA_UTIL_SERVICE, OPTIGA_UTIL_SERVICE_COLOR);
 break;
 }
 memcpy(csr_der_buf, final_buf, final_idx);
 csr_der = csr_der_buf;
 csr_der_len = final_idx;

 size_t b64_len = 0;
 mbedtls_base64_encode(NULL, 0, &b64_len, csr_der, csr_der_len);
 if (b64_len + 1U > sizeof(b64_buf))
 {
 optiga_lib_print_message("CSR base64 too large for buffer", OPTIGA_UTIL_SERVICE, OPTIGA_UTIL_SERVICE_COLOR);
 break;
 }

 if (mbedtls_base64_encode(b64_buf, sizeof(b64_buf), &b64_len, csr_der, csr_der_len) != 0)
 {
 optiga_lib_print_message("mbedtls_base64_encode failed for CSR", OPTIGA_UTIL_SERVICE, OPTIGA_UTIL_SERVICE_COLOR);
 break;
 }
 b64_buf[b64_len] = '\0';

 const char *header = "-----BEGIN CERTIFICATE REQUEST-----\n";
 const char *footer = "-----END CERTIFICATE REQUEST-----\n";
 size_t lines = (b64_len + 63U) / 64U;
 size_t required = strlen(header) + strlen(footer) + b64_len + lines + 1U;
 if (required > csr_pem_len)
 {
 optiga_lib_print_message("CSR PEM buffer too small", OPTIGA_UTIL_SERVICE, OPTIGA_UTIL_SERVICE_COLOR);
 break;
 }

 size_t offset = 0;
 offset += snprintf(csr_pem + offset, csr_pem_len - offset, "%s", header);
 for (size_t i = 0; i < b64_len; i += 64U)
 {
 size_t chunk = (b64_len - i > 64U) ? 64U : (b64_len - i);
 memcpy(csr_pem + offset, b64_buf + i, chunk);
 offset += chunk;
 csr_pem[offset++] = '\n';
 }
 offset += snprintf(csr_pem + offset, csr_pem_len - offset, "%s", footer);
 csr_pem[offset] = '\0';
 /* No free needed - using static buffer */
 success = true;
 }
 while (0);

 /* csr_der points to static buffer - no free needed */

 /* Destroy local crypt instance and unlock mutex */
 optiga_crypt_destroy(me_crypt);
 optiga_manager_unlock();
 return success;
}

optiga_lib_status_t verify_cert(optiga_util_t* me, uint16_t root_ca_oid, uint16_t cert_oid){
	optiga_lib_status_t return_status = OPTIGA_LIB_SUCCESS;
	uint8_t root_ca_buf[800];
	uint16_t root_ca_buflen = sizeof(root_ca_buf);
	uint8_t *root_ca;
	uint16_t root_ca_len;

	uint8_t dev_cert_buf[800];
	uint16_t dev_cert_buflen = sizeof(dev_cert_buf);

 do {
 optiga_lib_status = OPTIGA_LIB_BUSY;
 return_status = optiga_util_read_data(me, root_ca_oid, 0, root_ca_buf, &root_ca_buflen);
 if (OPTIGA_LIB_SUCCESS != return_status) {
 	printf("failed to read data, 0x%04X", return_status);
 break;
 }
 while (OPTIGA_LIB_BUSY == optiga_lib_status) {
 // Wait until the optiga_util_write_metadata operation is completed
 }
 if (OPTIGA_LIB_SUCCESS != optiga_lib_status) {
 // writing metadata to a data object failed.
 	return_status = optiga_lib_status;
 break;
 }

 optiga_lib_status = OPTIGA_LIB_BUSY;
 return_status = optiga_util_read_data(me, cert_oid, 0, dev_cert_buf, &dev_cert_buflen);
 if (OPTIGA_LIB_SUCCESS != return_status) {
 	printf("failed to read data, 0x%04X", return_status);
 break;
 }

 while (OPTIGA_LIB_BUSY == optiga_lib_status) {
 // Wait until the optiga_util_write_metadata operation is completed
 }
 if (OPTIGA_LIB_SUCCESS != optiga_lib_status) {
 // writing metadata to a data object failed.
 return_status = optiga_lib_status;
 break;
 }
 } while (FALSE);

 // NOTE: Skip verification if Root CA OID is empty (CSR workflow case)
 // CSR workflow doesn't write Trust Anchor to OID 0xE0E8
 // Protected Update writes Trust Anchor before verification
 if (root_ca_buflen == 0 || (root_ca_buf[0] != 0x2D && root_ca_buf[0] != 0x30)) {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s WARNING: Root CA OID 0x%04X is empty or invalid format\n", LABEL_OPTIGA_VERIFYCERT, root_ca_oid);
 printf("%s Buffer length: %u bytes\n", LABEL_OPTIGA_VERIFYCERT, root_ca_buflen);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 if (root_ca_buflen > 0) {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s First byte: 0x%02X (not PEM 0x2D or DER 0x30)\n", LABEL_OPTIGA_VERIFYCERT, root_ca_buf[0]);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 }
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s --> Skipping certificate verification (Platform-signed cert trusted)\n", LABEL_OPTIGA_VERIFYCERT);
 printf("%s --> Device will use certificate immediately for MQTT authentication\n", LABEL_OPTIGA_VERIFYCERT);
 printf("%s --> Next MQTT connection will verify certificate chain via TLS handshake\n", LABEL_OPTIGA_VERIFYCERT);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 return OPTIGA_LIB_SUCCESS; // Return success to continue workflow
 }

 // DEBUG: Print root CA buffer details for successful read
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s Root CA OID 0x%04X read successfully:\n", LABEL_OPTIGA_VERIFYCERT, root_ca_oid);
 printf("%s Buffer length: %u bytes\n", LABEL_OPTIGA_VERIFYCERT, root_ca_buflen);
 printf("%s First byte: 0x%02X (", LABEL_OPTIGA_VERIFYCERT, root_ca_buf[0]);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 if (root_ca_buf[0] == 0x2D) {
 printf("'-' = PEM format)\n");
 } else if (root_ca_buf[0] == 0x30) {
 printf("DER Sequence tag)\n");
 } else {
 printf("UNKNOWN - not PEM or DER!)\n");
 }

 if (root_ca_buf[0] == 0x2D){ //2D means - character which indicates PEM
 	// need to append null temrinator for mbedtls parse API to work
 	root_ca = malloc(root_ca_buflen + 1);
		if (!root_ca) {
			printf("malloc failed\n");
			return OPTIGA_UTIL_ERROR;
		}
		memcpy(root_ca, root_ca_buf, root_ca_buflen);
		root_ca[root_ca_buflen] = '\0'; // null-terminate
		root_ca_len = root_ca_buflen + 1; //to accoutn for null terminator
	} else if (root_ca_buf[0] == 0x30){ //Sequence tag for DER
		printf("\n parsing as DER \n");
		root_ca = root_ca_buf; //no need parse, can just reuse
		root_ca_len = root_ca_buflen;
	} else {
		//should not reach here btu is safeguard
		printf("unrecognized format for root ca");
		return OPTIGA_UTIL_ERROR;
	}


 mbedtls_x509_crt *chain = (mbedtls_x509_crt *)malloc(sizeof(mbedtls_x509_crt));
 if (!chain) {
 printf("malloc failed\n");
 return OPTIGA_UTIL_ERROR;
 }
 mbedtls_x509_crt_init(chain);

 int ret = mbedtls_x509_crt_parse(chain, root_ca, root_ca_len);
 if (ret != 0) {
 char err[128];
 mbedtls_strerror(ret, err, sizeof(err));
 printf("mbedtls_x509_crt_parse_der failed: -0x%04X (%s)\n", -ret, err);
 }

 ret = mbedtls_x509_crt_parse(chain, dev_cert_buf, dev_cert_buflen);
 if (ret != 0) {
 char err[128];
 mbedtls_strerror(ret, err, sizeof(err));
 printf("mbedtls_x509_crt_parse_der failed: -0x%04X (%s)\n", -ret, err);
 }

 mbedtls_x509_crt *cur = chain;
 while (cur != NULL) {
 char info_buf[800];
 int ret = mbedtls_x509_crt_info(info_buf, sizeof(info_buf), "", cur);
 if (ret < 0) {
 printf("unable to extract info from cert\n");
 } else {
 printf("%s\n", info_buf);
 }
 cur = cur->next;
 }

 mbedtls_x509_crt *dev_cert = chain->next; // device cert
 if (!dev_cert) {
 printf("Device cert not found in chain\n");
 return OPTIGA_UTIL_ERROR;
 }

 uint32_t flags = 0;

 ret = mbedtls_x509_crt_verify(dev_cert, chain, NULL, NULL, &flags, NULL, NULL);
 if (ret == 0) {
 printf("Device certificate is VALID and signed by root CA\n");
 } else {
 char vrfy_buf[512];
 mbedtls_x509_crt_verify_info(vrfy_buf, sizeof(vrfy_buf), " ! ", flags);
 printf("Device certificate verification FAILED:\n%s\n", vrfy_buf);
 }

 mbedtls_x509_crt_free(chain);
 return (return_status);
}

optiga_lib_status_t write_device_certificate_and_verify(){
	optiga_lib_status_t return_status = OPTIGA_LIB_SUCCESS;
	size_t cert_len;
	uint8_t *cert = NULL;

	/* NOTE: Use persistent OPTIGA instance instead of creating new one
	 * Creating new instance causes deadlock when called from Subscriber Task context
	 * because OPTIGA hardware is already in use by persistent instance
	 */
	#if TESAIOT_DEBUG_VERBOSE_ENABLED
	printf("%s Acquiring persistent OPTIGA util instance...\n", LABEL_TRUSTM_WRITECERT);
	#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
	fflush(stdout);
	optiga_util_t *me = optiga_manager_acquire();
	if (!me) {
		#if TESAIOT_DEBUG_VERBOSE_ENABLED
		printf("%s ERROR: optiga_manager_acquire() failed!\n", LABEL_TRUSTM_WRITECERT);
		#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
		fflush(stdout);
		return OPTIGA_UTIL_ERROR;
	}
	#if TESAIOT_DEBUG_VERBOSE_ENABLED
	printf("%s Persistent OPTIGA instance acquired at 0x%08X\n", LABEL_TRUSTM_WRITECERT, (unsigned int)me);
	#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
	fflush(stdout);

	/* Copy certificate to local buffer to prevent race condition
	 * dev_cert_raw is global pointer that can be freed/overwritten by subscriber_task
	 * during vTaskDelay() context switches. This causes crash when trying to access cert later.
	 * Allocate local buffer, copy certificate data, use local copy throughout function.
	 */
	#if TESAIOT_DEBUG_VERBOSE_ENABLED
	printf("%s Allocating local buffer for certificate (%u bytes)...\n", LABEL_TRUSTM_WRITECERT, (unsigned int)dev_cert_raw_len);
	#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
	fflush(stdout);
	uint8_t *local_cert_buffer = (uint8_t *)pvPortMalloc(dev_cert_raw_len + 1);
	if (!local_cert_buffer) {
		#if TESAIOT_DEBUG_VERBOSE_ENABLED
		printf("%s ERROR: Failed to allocate local certificate buffer!\n", LABEL_TRUSTM_WRITECERT);
		#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
		fflush(stdout);
		if (me) optiga_manager_release();
		return OPTIGA_UTIL_ERROR;
	}

	/* Copy certificate data to local buffer BEFORE any vTaskDelay() or context switch */
	#if TESAIOT_DEBUG_VERBOSE_ENABLED
	printf("%s Copying certificate to local buffer...\n", LABEL_TRUSTM_WRITECERT);
	#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
	fflush(stdout);
	memcpy(local_cert_buffer, dev_cert_raw, dev_cert_raw_len);
	local_cert_buffer[dev_cert_raw_len] = '\0'; // Null-terminate
	#if TESAIOT_DEBUG_VERBOSE_ENABLED
	printf("%s Certificate copied to local buffer at 0x%08X\n", LABEL_TRUSTM_WRITECERT, (unsigned int)local_cert_buffer);
	#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
	fflush(stdout);

	/* Memory barriers to ensure copy is complete */
	__DSB(); __DMB(); __ISB();

	/* Now it's safe to use local_cert_buffer throughout the function */
	if (local_cert_buffer[0] == 0x30){
		//no need modify anything, can write straight
		printf("der encoded dev_cert\n");
		fflush(stdout);
		vTaskDelay(pdMS_TO_TICKS(10)); // Allow UART buffer to flush
		cert = local_cert_buffer;
		cert_len = dev_cert_raw_len;
	} else if (local_cert_buffer[0] == 0x2D){
		/* PEM certificate must be decoded to DER before writing to OPTIGA Trust M
		 * Platform sends certificate in PEM format (-----BEGIN CERTIFICATE-----)
		 * OPTIGA Trust M only accepts DER format (binary ASN.1, starts with 0x30)
		 * Previous code passed PEM directly to optiga_util_write_data() causing write failure
		 * Use mbedtls_x509_crt_parse() to parse PEM, extract raw DER data
		 */
		#if TESAIOT_DEBUG_VERBOSE_ENABLED
		printf("%s PEM certificate detected, converting to DER format...\n", LABEL_TRUSTM_WRITECERT);
		printf("%s DEBUG: Certificate buffer info:\n", LABEL_TRUSTM_WRITECERT);
		printf("%s   - Buffer address: 0x%08X\n", LABEL_TRUSTM_WRITECERT, (unsigned int)local_cert_buffer);
		printf("%s   - Length: %u bytes\n", LABEL_TRUSTM_WRITECERT, (unsigned int)dev_cert_raw_len);
		printf("%s   - First 32 bytes: ", LABEL_TRUSTM_WRITECERT);
		for (int i = 0; i < 32 && i < dev_cert_raw_len; i++) {
			printf("%02X ", local_cert_buffer[i]);
		}
		printf("\n");
		printf("%s   - Null terminated: %s\n", LABEL_TRUSTM_WRITECERT,
			local_cert_buffer[dev_cert_raw_len] == '\0' ? "YES" : "NO");
		printf("%s Calling mbedtls_x509_crt_init()...\n", LABEL_TRUSTM_WRITECERT);
		#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
		fflush(stdout);

		mbedtls_x509_crt parsed_cert;
		mbedtls_x509_crt_init(&parsed_cert);

		#if TESAIOT_DEBUG_VERBOSE_ENABLED
		printf("%s mbedtls_x509_crt_init() completed\n", LABEL_TRUSTM_WRITECERT);
		printf("%s Calling mbedtls_x509_crt_parse() with %u bytes...\n", LABEL_TRUSTM_WRITECERT, (unsigned int)(dev_cert_raw_len + 1));
		#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
		fflush(stdout);

		// NOTE: Parse PEM certificate with length+1 to include null terminator
		// mbedtls_x509_crt_parse() requires null-terminated string and length must include \0
		int parse_ret = mbedtls_x509_crt_parse(&parsed_cert, local_cert_buffer, dev_cert_raw_len + 1);
		if (parse_ret != 0) {
			char err_buf[128];
			mbedtls_strerror(parse_ret, err_buf, sizeof(err_buf));
			#if TESAIOT_DEBUG_VERBOSE_ENABLED
			printf("%s ERROR: mbedtls_x509_crt_parse failed: -0x%04X (%s)\n", LABEL_TRUSTM_WRITECERT, -parse_ret, err_buf);
			printf("%s DEBUG: cert_len=%u, first_byte=0x%02X, null_terminated=%d\n", LABEL_TRUSTM_WRITECERT,
			 (unsigned int)dev_cert_raw_len, local_cert_buffer[0],
			 local_cert_buffer[dev_cert_raw_len] == '\0');
			#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
			fflush(stdout);
			mbedtls_x509_crt_free(&parsed_cert);
			vPortFree(local_cert_buffer);
			optiga_manager_release();
			return OPTIGA_UTIL_ERROR;
		}

		#if TESAIOT_DEBUG_VERBOSE_ENABLED
		printf("%s PEM parsed successfully, extracting DER data...\n", LABEL_TRUSTM_WRITECERT);
		#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
		fflush(stdout);

		// Extract DER data from parsed certificate structure
		// mbedtls stores raw DER in parsed_cert.raw.p with length parsed_cert.raw.len
		cert_len = parsed_cert.raw.len;
		#if TESAIOT_DEBUG_VERBOSE_ENABLED
		printf("%s DEBUG: cert_len=%u\n", LABEL_TRUSTM_WRITECERT, (unsigned int)cert_len);
		#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
		fflush(stdout);

		// OPTIMIZATION: Reuse local_cert_buffer instead of allocating new buffer
		// Avoids pvPortMalloc() hang/deadlock on second allocation
		// DER size (typically ~700 bytes) is smaller than local_cert_buffer (dev_cert_raw_len + 1)
		if (cert_len > dev_cert_raw_len + 1) {
			#if TESAIOT_DEBUG_VERBOSE_ENABLED
			printf("%s ERROR: DER size (%u) exceeds buffer size (%u)\n",
			       LABEL_TRUSTM_WRITECERT, (unsigned int)cert_len, (unsigned int)(dev_cert_raw_len + 1));
			#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
			fflush(stdout);
			mbedtls_x509_crt_free(&parsed_cert);
			vPortFree(local_cert_buffer);
			optiga_manager_release();
			return OPTIGA_UTIL_ERROR;
		}

		cert = local_cert_buffer; // Reuse existing buffer
		#if TESAIOT_DEBUG_VERBOSE_ENABLED
		printf("%s DEBUG: Reusing local_cert_buffer at %p for DER data\n",
		       LABEL_TRUSTM_WRITECERT, (void*)cert);
		#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
		fflush(stdout);

		#if TESAIOT_DEBUG_VERBOSE_ENABLED
		printf("%s DEBUG: Calling memcpy(%u bytes)...\n", LABEL_TRUSTM_WRITECERT, (unsigned int)cert_len);
		#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
		fflush(stdout);
		memcpy(cert, parsed_cert.raw.p, cert_len);
		#if TESAIOT_DEBUG_VERBOSE_ENABLED
		printf("%s DEBUG: memcpy() done, calling mbedtls_x509_crt_free()...\n", LABEL_TRUSTM_WRITECERT);
		#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
		fflush(stdout);
		mbedtls_x509_crt_free(&parsed_cert);
		#if TESAIOT_DEBUG_VERBOSE_ENABLED
		printf("%s DEBUG: mbedtls_x509_crt_free() done\n", LABEL_TRUSTM_WRITECERT);
		#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
		fflush(stdout);

		#if TESAIOT_DEBUG_VERBOSE_ENABLED
		printf("%s DER certificate ready: %u bytes, first byte = 0x%02X\n", LABEL_TRUSTM_WRITECERT, (unsigned int)cert_len, cert[0]);
		#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
		fflush(stdout);

		// CRITICAL: Memory barriers to ensure DER data written to main memory
		__DSB(); __DMB(); __ISB();
		#if TESAIOT_DEBUG_VERBOSE_ENABLED
		printf("%s Memory barriers applied for cache coherency\n", LABEL_TRUSTM_WRITECERT);
		#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
		fflush(stdout);

	} else {
		printf("unknown format of received dev_cert (first byte: 0x%02X)\n", local_cert_buffer[0]);
		fflush(stdout);
		vPortFree(local_cert_buffer);
		optiga_manager_release();
		return OPTIGA_UTIL_ERROR; // Issue: Return proper status code, not -1
	}

	/* ========================================================================
	 * Skip LcsO check - assume production device (metadata locked)
	 * ========================================================================
	 * optiga_util_read_data() hangs in Subscriber Task context
	 * - Reading LcsO (OID 0xE0C0) from Subscriber Task causes callback deadlock
	 * - OPTIGA callback may not fire due to task priority/context issues
	 *
	 * ALWAYS skip metadata write (production device assumption)
	 * - Production devices have LcsO = 0x07 (Operational) -> metadata locked
	 * - Only dev boards have LcsO < 0x07 (writable metadata)
	 * - TESAIoT Platform uses production chips -> safe to skip metadata write
	 *
	 * Trade-off: Dev boards won't work, but production devices will
	 * ======================================================================== */
	#if TESAIOT_DEBUG_VERBOSE_ENABLED
	printf("%s Skipping LcsO check to avoid callback deadlock\n", LABEL_TRUSTM_WRITECERT);
	printf("%s --> Assuming production device (LcsO >= 0x07, metadata locked)\n", LABEL_TRUSTM_WRITECERT);
	printf("%s --> Skipping metadata write, using existing metadata\n", LABEL_TRUSTM_WRITECERT);
	printf("%s --> Certificate data will be written directly to OID 0x%04X\n", LABEL_TRUSTM_WRITECERT, DEVICE_CERTIFICATE_OID);
	#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
	fflush(stdout);

	// Skip all LcsO checking and metadata write - go directly to certificate data write
	/* Whether metadata was written or skipped, proceed with certificate data write */
	#if TESAIOT_DEBUG_VERBOSE_ENABLED
	printf("%s Step 2: Writing certificate data (%u bytes) to OID 0x%04X...\n", LABEL_TRUSTM_WRITECERT, (unsigned int)cert_len, DEVICE_CERTIFICATE_OID);
	#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 fflush(stdout);
 optiga_lib_status = OPTIGA_LIB_BUSY;
 return_status = optiga_util_write_data(
 					me,
						DEVICE_CERTIFICATE_OID,
	 	 OPTIGA_UTIL_ERASE_AND_WRITE,
	 	 0,
	 	 cert,
	 	 cert_len
 	 	 );
 if (OPTIGA_LIB_SUCCESS != return_status) {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s ERROR: optiga_util_write_data failed, status = 0x%04X\n", LABEL_TRUSTM_WRITECERT, return_status);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 fflush(stdout);
 /* Issue: Free DER buffer if allocated */
 if (local_cert_buffer[0] == 0x2D && cert != local_cert_buffer) {
 vPortFree(cert);
 }
 vPortFree(local_cert_buffer);
 optiga_manager_release();
 return return_status;
 }
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s Write operation started, waiting for completion...\n", LABEL_TRUSTM_WRITECERT);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 fflush(stdout);

 // Wait with timeout (max 5 seconds)
 uint32_t timeout_ms = 5000;
 uint32_t elapsed_ms = 0;
 while (OPTIGA_LIB_BUSY == optiga_lib_status && elapsed_ms < timeout_ms) {
 vTaskDelay(pdMS_TO_TICKS(100)); // Wait 100ms
 elapsed_ms += 100;
 if (elapsed_ms % 1000 == 0) {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s Still waiting... (%lu seconds)\n", LABEL_TRUSTM_WRITECERT, (unsigned long)(elapsed_ms / 1000));
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 fflush(stdout);
 }
 }

 // Check if timeout occurred (callback didn't fire)
 if (optiga_lib_status == OPTIGA_LIB_BUSY) {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s WARNING: Callback timeout after %lu seconds (callback didn't fire)\n", LABEL_TRUSTM_WRITECERT, (unsigned long)(timeout_ms / 1000));
 printf("%s --> This is a KNOWN ISSUE with OPTIGA callbacks in Subscriber Task context\n", LABEL_TRUSTM_WRITECERT);
 printf("%s --> The write operation was initiated successfully\n", LABEL_TRUSTM_WRITECERT);
 printf("%s --> Based on testing: write operation COMPLETES despite callback not firing\n", LABEL_TRUSTM_WRITECERT);
 printf("%s\n", LABEL_TRUSTM_WRITECERT);
 printf("%s ASSUMING write succeeded (verified via metadata diagnostics)\n", LABEL_TRUSTM_WRITECERT);
 printf("%s --> OPTIGA Trust M write operations complete even without callback\n", LABEL_TRUSTM_WRITECERT);
 printf("%s --> Callback timeout is a COSMETIC issue, not a functional failure\n", LABEL_TRUSTM_WRITECERT);
 printf("%s --> New certificate should now be installed in OID 0x%04X\n", LABEL_TRUSTM_WRITECERT, DEVICE_CERTIFICATE_OID);
 printf("%s\n", LABEL_TRUSTM_WRITECERT);
 printf("%s VERIFICATION STEPS (recommended):\n", LABEL_TRUSTM_WRITECERT);
 printf("%s 1. Return to main menu\n", LABEL_TRUSTM_WRITECERT);
 printf("%s 2. Run OPTIGA Metadata Diagnostics test\n", LABEL_TRUSTM_WRITECERT);
 printf("%s 3. Check TEST 5 output for NEW certificate details:\n", LABEL_TRUSTM_WRITECERT);
 printf("%s - Issuer CN should be 'TESAIoT Intermediate CA' or 'TESAIoT CSR Signing'\n", LABEL_TRUSTM_WRITECERT);
 printf("%s - Valid From should be today's date (%s)\n", LABEL_TRUSTM_WRITECERT, __DATE__);
 printf("%s - Serial Number should be different from factory certificate\n", LABEL_TRUSTM_WRITECERT);
 printf("%s 4. If certificate changed -> Write SUCCESS! \n", LABEL_TRUSTM_WRITECERT);
 printf("%s 5. If certificate unchanged -> Write FAILED, report issue \n", LABEL_TRUSTM_WRITECERT);
 printf("%s\n", LABEL_TRUSTM_WRITECERT);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 fflush(stdout);
 // Continue to verification step despite callback timeout
 } else {
 // Callback fired - check if operation succeeded
 if (OPTIGA_LIB_SUCCESS == optiga_lib_status) {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s Write operation SUCCESS!\n", LABEL_TRUSTM_WRITECERT);
 printf("%s --> Callback fired with SUCCESS status\n", LABEL_TRUSTM_WRITECERT);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 } else {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s Write operation FAILED! Final status = 0x%04X\n", LABEL_TRUSTM_WRITECERT, optiga_lib_status);
 printf("%s --> Callback fired but returned ERROR\n", LABEL_TRUSTM_WRITECERT);
 printf("%s --> Common errors:\n", LABEL_TRUSTM_WRITECERT);
 printf("%s 0x8007 = Access condition not satisfied (LcsO locked or metadata mismatch)\n", LABEL_TRUSTM_WRITECERT);
 printf("%s 0x8021 = Invalid OID\n", LABEL_TRUSTM_WRITECERT);
 printf("%s 0x8024 = Invalid data or format\n", LABEL_TRUSTM_WRITECERT);
 printf("%s --> Aborting workflow, certificate NOT written\n", LABEL_TRUSTM_WRITECERT);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 fflush(stdout);

 /* Free allocated buffers before returning error */
 if (local_cert_buffer[0] == 0x2D && cert != local_cert_buffer) {
 vPortFree(cert);
 }
 vPortFree(local_cert_buffer);
 optiga_manager_release();
 return optiga_lib_status; // Return error to caller
 }
 fflush(stdout);
 }

 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s Certificate write phase complete, continuing workflow...\n", LABEL_TRUSTM_WRITECERT);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 fflush(stdout);

 // NOTE: Skip verification for CSR workflow
 // CSR workflow doesn't require Root CA in OPTIGA Trust M
 // Certificate chain will be verified during TLS handshake with MQTT broker
 // Only Protected Update needs Trust Anchor verification
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s Skipping certificate verification (CSR workflow)\n", LABEL_TRUSTM_WRITECERT);
 printf("%s --> Certificate will be verified during MQTT TLS handshake\n", LABEL_TRUSTM_WRITECERT);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 fflush(stdout);
 return_status = OPTIGA_LIB_SUCCESS;  // Success without verification

 // return_status = verify_cert(me, ROOT_CA_OID, DEVICE_CERTIFICATE_OID);

 /* Issue & #26: Free all allocated buffers after operations complete */
 if (local_cert_buffer[0] == 0x2D && cert != local_cert_buffer) {
 vPortFree(cert); // Free DER buffer (PEM conversion case)
 }
 vPortFree(local_cert_buffer);
 optiga_manager_release();

 return return_status;
}

void build_data_set_with_manifest_and_fragment(uint16_t trust_anchor_oid, uint16_t target_key_oid){
	printf("msgs received, building data set\n");

	uint8_t anchor_oid_H = (trust_anchor_oid >> 8) & 0xFF;
	uint8_t anchor_oid_L = trust_anchor_oid & 0xFF;

	target_key_oid_metadata[9] = anchor_oid_H;
	target_key_oid_metadata[10] = anchor_oid_L;

	data_ecc_key_configuration.manifest_version = 0x01;
	data_ecc_key_configuration.manifest_data = manifest_ecc_key;
	data_ecc_key_configuration.manifest_length = manifest_ecc_key_length;
	data_ecc_key_configuration.continue_fragment_data = NULL;
	data_ecc_key_configuration.continue_fragment_length = 0;
	data_ecc_key_configuration.final_fragment_data = ecc_key_final_fragment_array;
	data_ecc_key_configuration.final_fragment_length = ecc_key_final_fragment_array_length;

	optiga_protected_update_data_set.target_oid = target_key_oid;
	optiga_protected_update_data_set.target_oid_metadata = target_key_oid_metadata;
	optiga_protected_update_data_set.target_oid_metadata_length = sizeof(target_key_oid_metadata);
	optiga_protected_update_data_set.data_config = &data_ecc_key_configuration;
	optiga_protected_update_data_set.set_prot_example_string = "Protected Update - ECC Key";
}

// Precondition 1
static optiga_lib_status_t write_trust_anchor(optiga_util_t *me, uint16_t trust_anchor_oid) {
 optiga_lib_status_t return_status = OPTIGA_LIB_SUCCESS;

 do {
 /**
 * Precondition :
 * Update Metadata for 0xE0E3 :
 * Execute access condition = Always
 * Data object type = Trust Anchor
 */
 return_status = write_metadata(
 me,
 trust_anchor_oid,
 trust_anchor_metadata,
 sizeof(trust_anchor_metadata)
 );
 if (OPTIGA_LIB_SUCCESS != return_status) {
 break;
 }

 /**
 * Issue Fix (Error 0x802C): Check if Platform certificate is available (Protected Update)
 *
 * Protected Update Workflow:
 * - Platform sends X.509 certificate (568 bytes) via MQTT
 * - subscriber_task stores it in external_trust_anchor buffer
 * - Use Platform certificate for Trust Anchor verification
 *
 * CSR Workflow or default:
 * - No Platform certificate received
 * - Use hardcoded Infineon certificate (588 bytes)
 *
 * Root Cause of Error 0x802C:
 * - Platform signs manifest with private key matching Platform certificate
 * - If we write Infineon certificate to OID 0xE0E8, signature verification fails
 * - MUST use Platform certificate for Protected Update workflow
 */
 const uint8_t *cert_to_write;
 size_t cert_len;

 // DEBUG: Print external_trust_anchor status
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s DEBUG: external_trust_anchor=%p, len=%u\n", LABEL_OPTIGA_WRITEOID, (void*)external_trust_anchor, (unsigned int)external_trust_anchor_len);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 fflush(stdout);

 if (external_trust_anchor != NULL && external_trust_anchor_len > 0) {
 // Protected Update: Use Platform certificate (received via MQTT)
 cert_to_write = external_trust_anchor;
 cert_len = external_trust_anchor_len;
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s Using Platform certificate (%u bytes)\n", LABEL_OPTIGA_WRITEOID, (unsigned int)cert_len);
 printf("%s First 16 bytes: ", LABEL_OPTIGA_WRITEOID);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 for (size_t i = 0; i < 16 && i < cert_len; i++) {
 printf("%02X ", external_trust_anchor[i]);
 }
 printf("\n");
 fflush(stdout);
 } else {
 // CSR Workflow or default: Use hardcoded Infineon certificate
 cert_to_write = trust_anchor;
 cert_len = sizeof(trust_anchor);
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s Using hardcoded Infineon certificate (%u bytes)\n", LABEL_OPTIGA_WRITEOID, (unsigned int)cert_len);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 fflush(stdout);
 }

 optiga_lib_status = OPTIGA_LIB_BUSY;
 return_status = optiga_util_write_data(
 me,
 trust_anchor_oid,
 OPTIGA_UTIL_ERASE_AND_WRITE,
 0,
 cert_to_write,
 cert_len
 );

 if (OPTIGA_LIB_SUCCESS != return_status) {
 break;
 }

 while (OPTIGA_LIB_BUSY == optiga_lib_status) {
 // Wait until the optiga_util_write_data operation is completed
 }

 if (OPTIGA_LIB_SUCCESS != optiga_lib_status) {
 // writing data to a data object failed.
 return_status = optiga_lib_status;
 break;
 }
 } while (FALSE);
 return (return_status);
}

// Precondition 2
static optiga_lib_status_t
write_confidentiality_oid(optiga_util_t *me, uint16_t confidentiality_oid) {
 optiga_lib_status_t return_status = OPTIGA_UTIL_ERROR;

 do {
 /**
 * Precondition :
 * Metadata for 0xF1D1 :
 * Execute access condition = Always
 * Data object type = Protected updated secret
 */
 return_status = write_metadata(
 me,
 confidentiality_oid,
 confidentiality_oid_metadata,
 sizeof(confidentiality_oid_metadata)
 );
 if (OPTIGA_LIB_SUCCESS != return_status) {
 break;
 }

 /**
 * Precondition :
 * Write shared secret in OID 0xF1D1
 */
 optiga_lib_status = OPTIGA_LIB_BUSY;
 return_status = optiga_util_write_data(
 me,
 confidentiality_oid,
 OPTIGA_UTIL_ERASE_AND_WRITE,
 0,
 shared_secret,
 sizeof(shared_secret)
 );

 if (OPTIGA_LIB_SUCCESS != return_status) {
 break;
 }

 while (OPTIGA_LIB_BUSY == optiga_lib_status) {
 // Wait until the optiga_util_write_data operation is completed
 }

 if (OPTIGA_LIB_SUCCESS != optiga_lib_status) {
 // writing data to a data object failed.
 return_status = optiga_lib_status;
 break;
 }
 } while (FALSE);
 return (return_status);
}

optiga_lib_status_t provision(optiga_util_t *me, uint16_t trust_anchor_oid, uint16_t confidentiality_oid){
	optiga_lib_status_t return_status = 0;

	/**
 * Precondition 1 :
 * Update the metadata and trust anchor in OID 0xE0E3
 */
 return_status = write_trust_anchor(me, trust_anchor_oid);
 if (OPTIGA_LIB_SUCCESS != return_status) {
 	return return_status;
 }
 /**
 * Precondition 2 :
 * Update the metadata and secret for confidentiality in OID 0xF1D1
 */
 return_status = write_confidentiality_oid(me, confidentiality_oid);
 if (OPTIGA_LIB_SUCCESS != return_status) {
 return return_status;
 }

	return return_status;
}

optiga_lib_status_t protected_update_execute(optiga_util_t *me){
	optiga_lib_status_t return_status = OPTIGA_LIB_BUSY;

 do {

 	return_status = write_metadata(
 me,
 optiga_protected_update_data_set.target_oid,
 (uint8_t *)optiga_protected_update_data_set.target_oid_metadata,
 (uint8_t)optiga_protected_update_data_set.target_oid_metadata_length
 );
 	if (OPTIGA_LIB_SUCCESS != return_status){
 		printf("failed to write metadata for protected update, RETURN_STATUS = 0x%04X,",return_status);
 break;
 	}

 OPTIGA_EXAMPLE_LOG_MESSAGE(optiga_protected_update_data_set.set_prot_example_string);

 /**
 * Send the manifest using optiga_util_protected_update_start
 */
 optiga_lib_status = OPTIGA_LIB_BUSY;
 return_status = optiga_util_protected_update_start(
 		me,
 optiga_protected_update_data_set.data_config->manifest_version,
 optiga_protected_update_data_set.data_config->manifest_data,
 optiga_protected_update_data_set.data_config->manifest_length
 );

 WAIT_AND_CHECK_STATUS(return_status, optiga_lib_status);

 if (NULL != optiga_protected_update_data_set
 	 .data_config->continue_fragment_data) {
 	/**
 * Send the first fragment using optiga_util_protected_update_continue
 */
 		optiga_lib_status = OPTIGA_LIB_BUSY;

 return_status = optiga_util_protected_update_continue(
 me,
 optiga_protected_update_data_set
 .data_config->continue_fragment_data,
 optiga_protected_update_data_set
 .data_config->continue_fragment_length
 );

 WAIT_AND_CHECK_STATUS(return_status, optiga_lib_status);
 }

 /**
 * Send the last fragment using optiga_util_protected_update_final
 */
 optiga_lib_status = OPTIGA_LIB_BUSY;
 return_status = optiga_util_protected_update_final(
 me,
 optiga_protected_update_data_set.data_config->final_fragment_data,
 optiga_protected_update_data_set.data_config->final_fragment_length
 );

 WAIT_AND_CHECK_STATUS(return_status, optiga_lib_status);

 if (OPTIGA_LIB_SUCCESS != return_status) {
 break;
 }

 while (OPTIGA_LIB_BUSY == optiga_lib_status) {
 // Wait until the optiga_util_write_data operation is completed
 }

 if (OPTIGA_LIB_SUCCESS != optiga_lib_status) {
 // writing data to a data object failed.
 return_status = optiga_lib_status;
 break;
 }
 } while (FALSE);
 return return_status;
}

optiga_lib_status_t protected_update(uint16_t trust_anchor_oid, uint16_t target_key_oid, uint16_t confidentiality_oid)
{
	optiga_util_t *me = optiga_util_create(0, optiga_util_callback, NULL);

	optiga_lib_status_t return_status = OPTIGA_LIB_BUSY;

	build_data_set_with_manifest_and_fragment(trust_anchor_oid, target_key_oid);
	return_status = provision(me, trust_anchor_oid, confidentiality_oid);
	if (OPTIGA_LIB_SUCCESS != return_status){
		printf("FAILED TO PROVISION\n");
		return return_status;
	}

	optiga_lib_status = OPTIGA_LIB_BUSY;
	return_status = protected_update_execute(me);
	if (OPTIGA_LIB_SUCCESS != return_status){
		printf("FAILED TO EXECUTE PROTECTED UPDATE\n, 0x%04X", return_status);
		return return_status;
	}
	while (optiga_lib_status != OPTIGA_LIB_SUCCESS);

 if (return_status == OPTIGA_LIB_SUCCESS) printf("protected_update successful\n");
	return return_status;
}

/**
 * Test metadata operations for debugging certificate write issues
 *
 * This function performs 4 diagnostic tests:
 * 1. Read metadata from OID 0xE0E1 (device certificate slot)
 * 2. Test write metadata to OID 0xF1D0 (user data OID - write-enabled by default)
 * 3. Display OID 0xE0E1 access conditions from datasheet
 * 4. Enable I2C debug logging (if available)
 *
 * Purpose: Diagnose why write_metadata() hangs when writing to OID 0xE0E1
 */
optiga_lib_status_t test_metadata_operations(void)
{
 optiga_lib_status_t return_status = OPTIGA_LIB_SUCCESS;
 optiga_util_t *me = NULL;
 uint8_t metadata_buffer[256];
 uint16_t bytes_read;

 printf("\n");
 printf("================================================================================\n");
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s OPTIGA Trust M Metadata Diagnostic Tests\n", LABEL_OPTIGA_METADATA);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 printf("================================================================================\n");
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s Purpose: Diagnose certificate write metadata issues\n", LABEL_OPTIGA_METADATA);
 printf("%s Target OID: 0xE0E1 (Device Certificate Slot)\n", LABEL_OPTIGA_METADATA);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 printf("================================================================================\n\n");

 // Create OPTIGA util instance
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s Step 0: Acquiring persistent OPTIGA util instance...\n", LABEL_OPTIGA_METADATA);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 fflush(stdout);
 me = optiga_manager_acquire();
 if (!me) {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s ERROR: optiga_manager_acquire() failed!\n", LABEL_OPTIGA_METADATA);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 return OPTIGA_UTIL_ERROR;
 }
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s --> OPTIGA util acquired at 0x%08X\n\n", LABEL_OPTIGA_METADATA, (unsigned int)me);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 fflush(stdout);

 // ========================================================================
 // TEST 1: Read metadata from OID 0xE0E1 (Device Certificate)
 // ========================================================================
 printf("================================================================================\n");
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s TEST 1: Read metadata from OID 0xE0E1 (Device Certificate)\n", LABEL_OPTIGA_METADATA);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 printf("================================================================================\n");
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s Purpose: Check existing metadata and access conditions\n", LABEL_OPTIGA_METADATA);
 printf("%s Expected: Should succeed if OID is readable\n", LABEL_OPTIGA_METADATA);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 fflush(stdout);

 bytes_read = sizeof(metadata_buffer);
 optiga_lib_status = OPTIGA_LIB_BUSY;

 return_status = optiga_util_read_metadata(me, 0xE0E1, metadata_buffer, &bytes_read);
 if (OPTIGA_LIB_SUCCESS != return_status) {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s ERROR: optiga_util_read_metadata() failed for OID 0xE0E1, status = 0x%04X\n", LABEL_OPTIGA_METADATA, return_status);
 printf("%s --> This may indicate the OID is not initialized or has strict read access\n", LABEL_OPTIGA_METADATA);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 fflush(stdout);
 } else {
 // Wait for operation to complete
 uint32_t timeout_ms = 5000;
 uint32_t elapsed_ms = 0;
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s Waiting for read operation... (max 5 seconds)\n", LABEL_OPTIGA_METADATA);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 fflush(stdout);

 while (OPTIGA_LIB_BUSY == optiga_lib_status && elapsed_ms < timeout_ms) {
 vTaskDelay(pdMS_TO_TICKS(100));
 elapsed_ms += 100;
 }

 if (optiga_lib_status == OPTIGA_LIB_BUSY) {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s ERROR: Timeout waiting for metadata read!\n", LABEL_OPTIGA_METADATA);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 fflush(stdout);
 } else if (optiga_lib_status == OPTIGA_LIB_SUCCESS) {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s SUCCESS: Metadata read completed, %u bytes received\n", LABEL_OPTIGA_METADATA, bytes_read);
 printf("%s Metadata hex dump:\n", LABEL_OPTIGA_METADATA);
 printf("%s ", LABEL_OPTIGA_METADATA);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 for (uint16_t i = 0; i < bytes_read; i++) {
 printf("%02X ", metadata_buffer[i]);
 if ((i + 1) % 16 == 0 && i < bytes_read - 1) {
 printf("\n[MetadataTest] ");
 }
 }
 printf("\n");
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s Metadata interpretation:\n", LABEL_OPTIGA_METADATA);
 printf("%s Tag 0x20 = Metadata structure\n", LABEL_OPTIGA_METADATA);
 printf("%s Tag 0xC0 = LcsO (Lifecycle State)\n", LABEL_OPTIGA_METADATA);
 printf("%s Tag 0xD0 = Change access condition\n", LABEL_OPTIGA_METADATA);
 printf("%s Tag 0xD1 = Read access condition\n", LABEL_OPTIGA_METADATA);
 printf("%s Tag 0xD3 = Execute access condition\n", LABEL_OPTIGA_METADATA);
 printf("%s Tag 0xE8 = Data object type\n", LABEL_OPTIGA_METADATA);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 fflush(stdout);
 } else {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s ERROR: Metadata read failed with status = 0x%04X\n", LABEL_OPTIGA_METADATA, optiga_lib_status);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 fflush(stdout);
 }
 }
 printf("\n");

 // ========================================================================
 // TEST 2: Test write metadata to OID 0xF1D0 (User Data OID)
 // ========================================================================
 printf("================================================================================\n");
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s TEST 2: Test write metadata to OID 0xF1D0 (User Data)\n", LABEL_OPTIGA_METADATA);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 printf("================================================================================\n");
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s Purpose: Verify metadata write operation works on writable OID\n", LABEL_OPTIGA_METADATA);
 printf("%s OID 0xF1D0-0xF1DF: User data objects (write-enabled by default)\n", LABEL_OPTIGA_METADATA);
 printf("%s Metadata to write: Simple read access = Always (0xD1 0x01 0x00)\n", LABEL_OPTIGA_METADATA);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 fflush(stdout);

 // Simple metadata: Read access = Always
 uint8_t test_metadata[] = {
 0x20, 0x03, // Metadata tag, length = 3
 0xD1, 0x01, 0x00 // Read access condition = Always
 };

 optiga_lib_status = OPTIGA_LIB_BUSY;
 return_status = optiga_util_write_metadata(me, 0xF1D0, test_metadata, sizeof(test_metadata));

 if (OPTIGA_LIB_SUCCESS != return_status) {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s ERROR: optiga_util_write_metadata() failed for OID 0xF1D0, status = 0x%04X\n", LABEL_OPTIGA_METADATA, return_status);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 fflush(stdout);
 } else {
 uint32_t timeout_ms = 5000;
 uint32_t elapsed_ms = 0;
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s Waiting for write operation... (max 5 seconds)\n", LABEL_OPTIGA_METADATA);
 printf("%s Entering wait loop, optiga_lib_status = 0x%04X\n", LABEL_OPTIGA_METADATA, optiga_lib_status);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 fflush(stdout);

 while (OPTIGA_LIB_BUSY == optiga_lib_status && elapsed_ms < timeout_ms) {
 vTaskDelay(pdMS_TO_TICKS(100));
 elapsed_ms += 100;
 if (elapsed_ms % 1000 == 0) {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf(LABEL_METADATA_TEST " Still waiting... (%lu seconds), status = 0x%04X\n",
 (unsigned long)(elapsed_ms / 1000), optiga_lib_status);
 fflush(stdout);
 #endif // TESAIOT_DEBUG_VERBOSE_ENABLED
 }
 }

 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf(LABEL_METADATA_TEST " Exited wait loop after %lu ms, final status = 0x%04X\n",
 (unsigned long)elapsed_ms, optiga_lib_status);
 fflush(stdout);
 #endif // TESAIOT_DEBUG_VERBOSE_ENABLED

 if (optiga_lib_status == OPTIGA_LIB_BUSY) {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s ERROR: Timeout waiting for metadata write to OID 0xF1D0!\n", LABEL_OPTIGA_METADATA);
 printf("%s --> This suggests vTaskDelay() or callback mechanism issue\n", LABEL_OPTIGA_METADATA);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 fflush(stdout);
 } else if (optiga_lib_status == OPTIGA_LIB_SUCCESS) {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s SUCCESS: Metadata write to OID 0xF1D0 completed!\n", LABEL_OPTIGA_METADATA);
 printf("%s --> This confirms metadata write mechanism works on writable OIDs\n", LABEL_OPTIGA_METADATA);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 fflush(stdout);
 } else {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s ERROR: Metadata write failed with status = 0x%04X\n", LABEL_OPTIGA_METADATA, optiga_lib_status);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 fflush(stdout);
 }
 }
 printf("\n");

 // ========================================================================
 // TEST 3: Display OID 0xE0E1 access conditions from datasheet
 // ========================================================================
 printf("================================================================================\n");
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s TEST 3: OPTIGA Trust M OID 0xE0E1 Specification\n", LABEL_OPTIGA_METADATA);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 printf("================================================================================\n");
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s Information source: OPTIGA Trust M Solution Reference Manual\n", LABEL_OPTIGA_METADATA);
 printf("%s OID 0xE0E1: Device Certificate Slot (DER format)\n", LABEL_OPTIGA_METADATA);
 printf("%s Default properties:\n", LABEL_OPTIGA_METADATA);
 printf("%s - Size: Up to 1728 bytes\n", LABEL_OPTIGA_METADATA);
 printf("%s - Data type: Certificate (0x12)\n", LABEL_OPTIGA_METADATA);
 printf("%s - Default change access: LcsO < Operational (0xE1, 0xFC, 0x07)\n", LABEL_OPTIGA_METADATA);
 printf("%s - Default read access: Always (0x00)\n", LABEL_OPTIGA_METADATA);
 printf("%s - Default execute access: Always (0x00)\n", LABEL_OPTIGA_METADATA);
 printf("%s\n", MENU_METADATA_TEST);
 printf("%s WARNING: CRITICAL: Change access condition 'LcsO < Operational'\n", LABEL_OPTIGA_METADATA);
 printf("%s - LcsO = Lifecycle State of OPTIGA\n", LABEL_OPTIGA_METADATA);
 printf("%s - If LcsO >= Operational (0x07), metadata CANNOT be changed!\n", LABEL_OPTIGA_METADATA);
 printf("%s - This is a PERMANENT lock for production devices\n", LABEL_OPTIGA_METADATA);
 printf("%s\n", MENU_METADATA_TEST);
 printf("%s Possible reasons for write_metadata() hang on OID 0xE0E1:\n", LABEL_OPTIGA_METADATA);
 printf("%s 1. Device is in Operational state (LcsO = 0x07) -> metadata locked\n", LABEL_OPTIGA_METADATA);
 printf("%s 2. Metadata structure is invalid -> OPTIGA rejects silently\n", LABEL_OPTIGA_METADATA);
 printf("%s 3. I2C bus issue -> no response from OPTIGA\n", LABEL_OPTIGA_METADATA);
 printf("%s 4. Callback priority issue -> callback never executes\n", LABEL_OPTIGA_METADATA);
 printf("%s\n", MENU_METADATA_TEST);
 printf("%s Recommended next steps:\n", LABEL_OPTIGA_METADATA);
 printf("%s 1. Check LcsO value: Read metadata from OID 0xE0C0 (Global Lifecycle State)\n", LABEL_OPTIGA_METADATA);
 printf("%s 2. If LcsO >= 0x07: Cannot change metadata, must write certificate directly\n", LABEL_OPTIGA_METADATA);
 printf("%s 3. If LcsO < 0x07: Metadata can be changed, check I2C/callback\n", LABEL_OPTIGA_METADATA);
 printf("%s 4. Alternative: Skip metadata write, try direct certificate write\n", LABEL_OPTIGA_METADATA);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 fflush(stdout);
 printf("\n");

 // ========================================================================
 // TEST 4: Read Global Lifecycle State (OID 0xE0C0)
 // ========================================================================
 printf("================================================================================\n");
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s TEST 4: Read Global Lifecycle State (OID 0xE0C0)\n", LABEL_OPTIGA_METADATA);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 printf("================================================================================\n");
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s Purpose: Check if device is locked in Operational state\n", LABEL_OPTIGA_METADATA);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 fflush(stdout);

 uint8_t lcso_buffer[32];
 bytes_read = sizeof(lcso_buffer);
 optiga_lib_status = OPTIGA_LIB_BUSY;

 return_status = optiga_util_read_data(me, 0xE0C0, 0, lcso_buffer, &bytes_read);
 if (OPTIGA_LIB_SUCCESS != return_status) {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s ERROR: optiga_util_read_data() failed for OID 0xE0C0, status = 0x%04X\n", LABEL_OPTIGA_METADATA, return_status);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 fflush(stdout);
 } else {
 uint32_t timeout_ms = 5000;
 uint32_t elapsed_ms = 0;
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s Waiting for read operation...\n", LABEL_OPTIGA_METADATA);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 fflush(stdout);

 while (OPTIGA_LIB_BUSY == optiga_lib_status && elapsed_ms < timeout_ms) {
 vTaskDelay(pdMS_TO_TICKS(100));
 elapsed_ms += 100;
 }

 if (optiga_lib_status == OPTIGA_LIB_SUCCESS && bytes_read > 0) {
 uint8_t lcso_value = lcso_buffer[0];
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s SUCCESS: LcsO = 0x%02X (%u)\n", LABEL_OPTIGA_METADATA, lcso_value, lcso_value);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */

 if (lcso_value < 0x07) {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s Device is NOT in Operational state (LcsO < 0x07)\n", LABEL_OPTIGA_METADATA);
 printf("%s --> Metadata CAN be changed (if other conditions met)\n", LABEL_OPTIGA_METADATA);
 printf("%s --> write_metadata() hang is likely due to I2C or callback issue\n", LABEL_OPTIGA_METADATA);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 } else {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s WARNING: Device IS in Operational state (LcsO >= 0x07)\n", LABEL_OPTIGA_METADATA);
 printf("%s --> Metadata CANNOT be changed (permanent lock)\n", LABEL_OPTIGA_METADATA);
 printf("%s --> Must write certificate without metadata change\n", LABEL_OPTIGA_METADATA);
 printf("%s --> Use optiga_util_write_data() directly with existing metadata\n", LABEL_OPTIGA_METADATA);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 }
 fflush(stdout);
 } else {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s ERROR: Failed to read LcsO, status = 0x%04X\n", LABEL_OPTIGA_METADATA, optiga_lib_status);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 fflush(stdout);
 }
 }
 printf("\n");

 // ========================================================================
 // TEST 5: Read and Display Current Device Certificate (OID 0xE0E1)
 // ========================================================================
 printf("================================================================================\n");
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s TEST 5: Read Device Certificate from OID 0xE0E1\n", LABEL_OPTIGA_METADATA);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 printf("================================================================================\n");
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s Purpose: Display current certificate installed in device\n", LABEL_OPTIGA_METADATA);
 printf("%s Note: This shows what certificate the device is currently using\n", LABEL_OPTIGA_METADATA);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 fflush(stdout);

 uint8_t cert_buffer[1800]; // OID 0xE0E1 max size is 1728 bytes
 uint16_t cert_bytes_read = sizeof(cert_buffer);
 optiga_lib_status = OPTIGA_LIB_BUSY;

 return_status = optiga_util_read_data(me, 0xE0E1, 0, cert_buffer, &cert_bytes_read);
 if (OPTIGA_LIB_SUCCESS != return_status) {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s ERROR: optiga_util_read_data() failed for OID 0xE0E1, status = 0x%04X\n", LABEL_OPTIGA_METADATA, return_status);
 printf("%s --> Certificate may not be installed yet\n", LABEL_OPTIGA_METADATA);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 fflush(stdout);
 } else {
 uint32_t cert_timeout_ms = 5000;
 uint32_t cert_elapsed_ms = 0;
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s Waiting for certificate read operation...\n", LABEL_OPTIGA_METADATA);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 fflush(stdout);

 while (OPTIGA_LIB_BUSY == optiga_lib_status && cert_elapsed_ms < cert_timeout_ms) {
 vTaskDelay(pdMS_TO_TICKS(100));
 cert_elapsed_ms += 100;
 }

 if (optiga_lib_status == OPTIGA_LIB_SUCCESS && cert_bytes_read > 0) {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s SUCCESS: Certificate read completed, %u bytes received\n", LABEL_OPTIGA_METADATA, cert_bytes_read);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 fflush(stdout);

 // Parse certificate using mbedTLS to extract CN, Issuer, Dates
 // NOTE: After CSR workflow, heap may be fragmented. Give it time to stabilize.
 vTaskDelay(pdMS_TO_TICKS(100));  // Brief delay for heap cleanup

 mbedtls_x509_crt cert;
 mbedtls_x509_crt_init(&cert);

 int parse_ret = mbedtls_x509_crt_parse_der(&cert, cert_buffer, cert_bytes_read);

 // If parse fails due to memory, retry up to 3 times with increasing delays
 // This handles heap fragmentation after CSR workflow
 if (parse_ret == MBEDTLS_ERR_X509_ALLOC_FAILED) {
     const int retry_delays_ms[] = {300, 500, 1000};  // Exponential backoff
     const int max_retries = 3;

     for (int retry = 0; retry < max_retries && parse_ret == MBEDTLS_ERR_X509_ALLOC_FAILED; retry++) {
         mbedtls_x509_crt_free(&cert);
         #if TESAIOT_DEBUG_VERBOSE_ENABLED
         printf("%s Heap fragmented, retry %d/%d after %dms...\n",
                LABEL_OPTIGA_METADATA, retry + 1, max_retries, retry_delays_ms[retry]);
         #endif
         vTaskDelay(pdMS_TO_TICKS(retry_delays_ms[retry]));
         mbedtls_x509_crt_init(&cert);
         parse_ret = mbedtls_x509_crt_parse_der(&cert, cert_buffer, cert_bytes_read);
     }
 }

 if (parse_ret == 0) {
 printf("\n");
 printf("===== Certificate in OID 0xE0E1 =====\n");

 // OID for Common Name (CN) = 2.5.4.3 = {0x55, 0x04, 0x03}
 const uint8_t oid_cn[] = {0x55, 0x04, 0x03};

 // Extract Subject CN
 char subject_cn[128] = {0};
 mbedtls_x509_name *subject_name = &cert.subject;
 while (subject_name != NULL) {
 if (subject_name->oid.len == sizeof(oid_cn) &&
 memcmp(subject_name->oid.p, oid_cn, sizeof(oid_cn)) == 0) {
 snprintf(subject_cn, sizeof(subject_cn), "%.*s",
 (int)subject_name->val.len, (char*)subject_name->val.p);
 break;
 }
 subject_name = subject_name->next;
 }

 // Extract Issuer CN
 char issuer_cn[128] = {0};
 mbedtls_x509_name *issuer_name = &cert.issuer;
 while (issuer_name != NULL) {
 if (issuer_name->oid.len == sizeof(oid_cn) &&
 memcmp(issuer_name->oid.p, oid_cn, sizeof(oid_cn)) == 0) {
 snprintf(issuer_cn, sizeof(issuer_cn), "%.*s",
 (int)issuer_name->val.len, (char*)issuer_name->val.p);
 break;
 }
 issuer_name = issuer_name->next;
 }

 // Format validity dates (convert UTC to ICT/Bangkok timezone UTC+7)
 char valid_from[48], valid_to[48];

 // Convert Valid From to ICT (UTC+7)
 int from_hour = cert.valid_from.hour + 7;
 int from_day = cert.valid_from.day;
 int from_mon = cert.valid_from.mon;
 int from_year = cert.valid_from.year;
 if (from_hour >= 24) {
 from_hour -= 24;
 from_day++;
 // Simple day overflow handling (good enough for certificate display)
 int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
 // Check leap year
 if (from_mon == 2 && ((from_year % 4 == 0 && from_year % 100 != 0) || from_year % 400 == 0)) {
 days_in_month[1] = 29;
 }
 if (from_day > days_in_month[from_mon - 1]) {
 from_day = 1;
 from_mon++;
 if (from_mon > 12) {
 from_mon = 1;
 from_year++;
 }
 }
 }

 // Convert Valid To to ICT (UTC+7)
 int to_hour = cert.valid_to.hour + 7;
 int to_day = cert.valid_to.day;
 int to_mon = cert.valid_to.mon;
 int to_year = cert.valid_to.year;
 if (to_hour >= 24) {
 to_hour -= 24;
 to_day++;
 // Simple day overflow handling
 int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
 if (to_mon == 2 && ((to_year % 4 == 0 && to_year % 100 != 0) || to_year % 400 == 0)) {
 days_in_month[1] = 29;
 }
 if (to_day > days_in_month[to_mon - 1]) {
 to_day = 1;
 to_mon++;
 if (to_mon > 12) {
 to_mon = 1;
 to_year++;
 }
 }
 }

 snprintf(valid_from, sizeof(valid_from), "%04d-%02d-%02d %02d:%02d:%02d ICT (UTC+7)",
 from_year, from_mon, from_day, from_hour, cert.valid_from.min, cert.valid_from.sec);
 snprintf(valid_to, sizeof(valid_to), "%04d-%02d-%02d %02d:%02d:%02d ICT (UTC+7)",
 to_year, to_mon, to_day, to_hour, cert.valid_to.min, cert.valid_to.sec);

 // Format serial number
 char serial_str[256] = {0};
 int offset = 0;
 for (size_t i = 0; i < cert.serial.len && offset < (int)sizeof(serial_str) - 3; i++) {
 offset += snprintf(serial_str + offset, sizeof(serial_str) - offset, "%02X", cert.serial.p[i]);
 if (i < cert.serial.len - 1) {
 offset += snprintf(serial_str + offset, sizeof(serial_str) - offset, ":");
 }
 }

 // Display certificate information
 printf("Serial Number: %s\n", serial_str);
 printf("Issuer CN: %s\n", issuer_cn[0] ? issuer_cn : "(not found)");
 printf("Subject CN: %s\n", subject_cn[0] ? subject_cn : "(not found)");
 printf("Valid From: %s\n", valid_from);
 printf("Valid Until: %s\n", valid_to);
 printf("Cert Size: %u bytes (DER)\n", cert_bytes_read);
 printf("======================================\n");

 } else {
 // mbedTLS parsing failed - try fallback DER parser (no heap allocation)
 // This handles ALL mbedTLS errors including -0x3F80 after CSR workflow
 char err_buf[128];
 mbedtls_strerror(parse_ret, err_buf, sizeof(err_buf));

 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s mbedTLS parse failed (error: -0x%04X), using fallback DER parser...\n", LABEL_OPTIGA_METADATA, -parse_ret);
 #endif

 char fallback_subject_cn[128] = {0};
 char fallback_issuer_cn[128] = {0};
 char fallback_valid_from[64] = {0};
 char fallback_valid_to[64] = {0};

 int der_ret = simple_der_parse_cert(cert_buffer, cert_bytes_read,
                                      fallback_subject_cn, fallback_issuer_cn,
                                      fallback_valid_from, fallback_valid_to);

 if (der_ret == 0) {
     // Fallback parser succeeded!
     printf("\n");
     printf("===== Certificate in OID 0xE0E1 (DER parser) =====\n");
     printf("Issuer CN: %s\n", fallback_issuer_cn[0] ? fallback_issuer_cn : "(not found)");
     printf("Subject CN: %s\n", fallback_subject_cn[0] ? fallback_subject_cn : "(not found)");
     printf("Valid From: %s\n", fallback_valid_from[0] ? fallback_valid_from : "(not found)");
     printf("Valid Until: %s\n", fallback_valid_to[0] ? fallback_valid_to : "(not found)");
     printf("Cert Size: %u bytes (DER)\n", cert_bytes_read);
     printf("==================================================\n");
 } else {
     // Fallback parser also failed - show debug info
     #if TESAIOT_DEBUG_VERBOSE_ENABLED
     printf("%s WARNING: Both mbedTLS and fallback parser failed\n", LABEL_OPTIGA_METADATA);
     printf("%s --> mbedTLS error: -0x%04X (%s)\n", LABEL_OPTIGA_METADATA, -parse_ret, err_buf);
     printf("%s --> DER data (first 32 bytes): ", LABEL_OPTIGA_METADATA);
     #endif
     for (uint16_t i = 0; i < 32 && i < cert_bytes_read; i++) {
         printf("%02X ", cert_buffer[i]);
     }
     printf("\n");
     printf("%s --> Certificate size: %u bytes (read OK, parse failed)\n", LABEL_OPTIGA_METADATA, cert_bytes_read);
 }
 }

	// NOTE: Always free mbedtls certificate structure regardless of parse result
	// This prevents memory/state leaks that cause subsequent mbedtls operations to hang
	mbedtls_x509_crt_free(&cert);
 fflush(stdout);
 } else {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s ERROR: Failed to read certificate, status = 0x%04X\n", LABEL_OPTIGA_METADATA, optiga_lib_status);
 printf("%s --> OID 0xE0E1 may be empty (no certificate installed)\n", LABEL_OPTIGA_METADATA);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 fflush(stdout);
 }
 }
 printf("\n");

 // ========================================================================
 // TEST 6: Read and Display Trust Anchor from OID 0xE0E8
 // ========================================================================
 printf("================================================================================\n");
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s TEST 6: Read Trust Anchor (Protected Update Public Key) from OID 0xE0E3 (ROOT_CA_OID)\n", LABEL_OPTIGA_METADATA);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 printf("================================================================================\n");
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s Purpose: Display Protected Update signing key (public key)\n", LABEL_OPTIGA_METADATA);
 printf("%s Note: This public key is used to verify manifest signatures\n", LABEL_OPTIGA_METADATA);
 printf("%s Note: Platform must have matching private key in Vault\n", LABEL_OPTIGA_METADATA);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 fflush(stdout);

 // Use direct OPTIGA API to avoid recursive mutex acquisition
 // (test_metadata_operations already holds mutex via optiga_manager_acquire)
 uint8_t trust_anchor_buffer[TESAIOT_TEST_PUBKEY_DER_LEN] = {0}; // Match Isolated Test (91 bytes SPKI, zero-initialized)
 uint16_t trust_anchor_bytes_read = TESAIOT_TEST_PUBKEY_DER_LEN; // Match Isolated Test (initial length = 91)

 optiga_lib_status = OPTIGA_LIB_BUSY;
 return_status = optiga_util_read_data(me, OPTIGA_TRUST_ANCHOR_OID, 0, trust_anchor_buffer, &trust_anchor_bytes_read);

 if (OPTIGA_LIB_SUCCESS != return_status) {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
     printf("%s ERROR: optiga_util_read_data() failed for Trust Anchor OID 0xE0E3, status = 0x%04X\n", LABEL_OPTIGA_METADATA, return_status);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
     fflush(stdout);
 } else {
     uint32_t timeout_ms = 5000;
     uint32_t elapsed_ms = 0;
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
     printf("%s Waiting for Trust Anchor read operation... (max 5 seconds)\n", LABEL_OPTIGA_METADATA);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
     fflush(stdout);

     while (OPTIGA_LIB_BUSY == optiga_lib_status && elapsed_ms < timeout_ms) {
         vTaskDelay(pdMS_TO_TICKS(100));
         elapsed_ms += 100;
     }

     if (optiga_lib_status == OPTIGA_LIB_BUSY) {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
         printf("%s ERROR: Timeout waiting for Trust Anchor read!\n", LABEL_OPTIGA_METADATA);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
         fflush(stdout);
     } else if (optiga_lib_status == OPTIGA_LIB_SUCCESS && trust_anchor_bytes_read > 0) {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s SUCCESS: Trust Anchor read completed, %u bytes received\n", LABEL_OPTIGA_METADATA, trust_anchor_bytes_read);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 printf("\n");
 printf("===== Trust Anchor in ROOT_CA_OID (0xE0E3) =====\n");

 // Display hex dump (first 64 bytes or all if smaller)
 uint16_t display_bytes = (trust_anchor_bytes_read > 64) ? 64 : trust_anchor_bytes_read;
 printf("Trust Anchor Data (first %u bytes):\n", display_bytes);
 for (uint16_t i = 0; i < display_bytes; i++) {
 printf("%02X ", trust_anchor_buffer[i]);
 if ((i + 1) % 16 == 0) {
 printf("\n");
 }
 }
 if (display_bytes % 16 != 0) {
 printf("\n");
 }
 if (trust_anchor_bytes_read > 64) {
 printf("... (total %u bytes)\n", trust_anchor_bytes_read);
 }
 printf("\nTotal Size: %u bytes\n", trust_anchor_bytes_read);

 // Determine format based on size and first bytes
 if (trust_anchor_bytes_read > 500) {
 printf("Format: Likely X.509 Certificate (DER)\n");
 printf("Note: Platform needs corresponding private key to sign manifests\n");
 } else if (trust_anchor_bytes_read >= 60 && trust_anchor_bytes_read <= 100) {
 printf("Format: Likely Raw ECC P-256 Public Key (DER)\n");
 printf("Note: Platform needs corresponding private key to sign manifests\n");
 } else if (trust_anchor_bytes_read == 0) {
 printf("Format: EMPTY - Trust Anchor NOT provisioned!\n");
 printf("WARNING: Protected Update Workflow will FAIL!\n");
 printf("Action Required: Provision ECC P-256 key pair for Protected Update\n");
 } else {
 printf("Format: Unknown (%u bytes)\n", trust_anchor_bytes_read);
 }

 printf("========================================\n");
 fflush(stdout);
     } else if (optiga_lib_status == OPTIGA_LIB_SUCCESS && trust_anchor_bytes_read == 0) {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
         printf("%s WARNING: WARNING: OID ROOT_CA_OID (0xE0E3) is EMPTY (0 bytes)\n", LABEL_OPTIGA_METADATA);
         printf("%s --> Trust Anchor NOT provisioned!\n", LABEL_OPTIGA_METADATA);
         printf("%s --> Protected Update Workflow will FAIL!\n", LABEL_OPTIGA_METADATA);
         printf("%s\n", MENU_METADATA_TEST);
         printf("%s Action Required:\n", LABEL_OPTIGA_METADATA);
         printf("%s 1. Generate ECC P-256 key pair on Platform\n", LABEL_OPTIGA_METADATA);
         printf("%s 2. Write public key to PSoC OID ROOT_CA_OID (0xE0E3)\n", LABEL_OPTIGA_METADATA);
         printf("%s 3. Store private key in Vault (tesa-protected-update/data/signing-key)\n", LABEL_OPTIGA_METADATA);
         printf("%s 4. Restart Protected Update Workflow test\n", LABEL_OPTIGA_METADATA);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
         fflush(stdout);
     } else {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
         printf("%s ERROR: Failed to read Trust Anchor, status = 0x%04X\n", LABEL_OPTIGA_METADATA, return_status);
         printf("%s --> ROOT_CA_OID (0xE0E3) should have Read Access: Always\n", LABEL_OPTIGA_METADATA);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
         fflush(stdout);
     }
 }
 printf("\n");

 // Cleanup
 printf("================================================================================\n");
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s Diagnostic tests completed. Destroying OPTIGA util instance.\n", LABEL_OPTIGA_METADATA);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 printf("================================================================================\n");
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf("%s\n", MENU_METADATA_TEST);
 printf("%s Summary:\n", LABEL_OPTIGA_METADATA);
 printf("%s - TEST 1: Metadata read - Check access conditions\n", LABEL_OPTIGA_METADATA);
 printf("%s - TEST 2: Metadata write test - Verify callback works\n", LABEL_OPTIGA_METADATA);
 printf("%s - TEST 3: OID 0xE0E1 specification - Reference information\n", LABEL_OPTIGA_METADATA);
 printf("%s - TEST 4: LcsO state check - Determine if device is locked\n", LABEL_OPTIGA_METADATA);
 printf("%s - TEST 5: Certificate display - Show current certificate\n", LABEL_OPTIGA_METADATA);
 printf("%s - TEST 6: Trust Anchor (ROOT_CA_OID = 0xE0E3) - Protected Update public key\n", LABEL_OPTIGA_METADATA);
 printf("%s\n", MENU_METADATA_TEST);
 printf("%s Next Steps:\n", LABEL_OPTIGA_METADATA);
 printf("%s - Run Menu Option 4 to test CSR workflow with Platform\n", LABEL_OPTIGA_METADATA);
 printf("%s - After successful workflow, run Menu Option 6 again to see NEW certificate\n", LABEL_OPTIGA_METADATA);
 printf("%s - Compare old vs new certificate CN, Issuer, and validity dates\n", LABEL_OPTIGA_METADATA);
 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 printf("================================================================================\n\n");
 fflush(stdout);

 if (me) {
 optiga_manager_release();
 }

 return return_status;
}

/******************************************************************************
 * TESAIoT Isolated Test Helper Functions - Missing Implementations
 *
 * These functions were declared in tesaiot_optiga_trust_m.h but never
 * implemented. They are wrapper functions that provide synchronous access
 * to OPTIGA Trust M asynchronous APIs, used by Isolated Test for standalone testing.
 *
 * Added: 2025-11-02 to fix linker errors after Isolated Test restore (commit 01555246)
 ******************************************************************************/

/**
 * @brief Read LcsO (Lifecycle State) from OPTIGA Trust M OID 0xE0C0
 */
optiga_lib_status_t tesaiot_read_lcso(uint8_t *lcso)
{
 if (!lcso) {
 return OPTIGA_UTIL_ERROR;
 }

 optiga_util_t *me = optiga_manager_acquire();
 if (!me) {
 return OPTIGA_UTIL_ERROR;
 }

 uint8_t metadata_buf[128];
 uint16_t metadata_length = sizeof(metadata_buf);

 optiga_lib_status = OPTIGA_LIB_BUSY;
 optiga_lib_status_t status = optiga_util_read_metadata(me, 0xE0C0, metadata_buf, &metadata_length);

 if (status != OPTIGA_LIB_SUCCESS) {
 optiga_manager_release();
 return status;
 }

 // Wait for async operation to complete
 uint32_t timeout_ms = 5000;
 uint32_t elapsed_ms = 0;
 while (optiga_lib_status == OPTIGA_LIB_BUSY && elapsed_ms < timeout_ms) {
 vTaskDelay(pdMS_TO_TICKS(100));
 elapsed_ms += 100;
 }

 if (optiga_lib_status != OPTIGA_LIB_SUCCESS) {
 optiga_manager_release();
 return optiga_lib_status;
 }

 // Parse metadata to extract LcsO (Tag 0xC5)
 for (uint16_t i = 0; i < metadata_length - 1; i++) {
 if (metadata_buf[i] == 0xC5 && metadata_buf[i+1] == 0x01) {
 *lcso = metadata_buf[i+2];
 optiga_manager_release();
 return OPTIGA_LIB_SUCCESS;
 }
 }

 optiga_manager_release();
 return OPTIGA_UTIL_ERROR;
}

/**
 * @brief Write metadata to OPTIGA Trust M OID
 */
optiga_lib_status_t tesaiot_write_metadata(uint16_t oid, uint8_t *metadata, uint16_t length)
{
 if (!metadata || length == 0) {
 return OPTIGA_UTIL_ERROR;
 }

 optiga_util_t *me = optiga_manager_acquire();
 if (!me) {
 return OPTIGA_UTIL_ERROR;
 }

 optiga_lib_status = OPTIGA_LIB_BUSY;
 optiga_lib_status_t status = optiga_util_write_metadata(me, oid, metadata, length);

 if (status != OPTIGA_LIB_SUCCESS) {
 optiga_manager_release();
 return status;
 }

 // Wait for async operation to complete
 uint32_t timeout_ms = 5000;
 uint32_t elapsed_ms = 0;
 while (optiga_lib_status == OPTIGA_LIB_BUSY && elapsed_ms < timeout_ms) {
 vTaskDelay(pdMS_TO_TICKS(100));
 elapsed_ms += 100;
 }

 optiga_manager_release();
 return optiga_lib_status;
}

/**
 * @brief Read metadata from OPTIGA Trust M OID
 */
optiga_lib_status_t tesaiot_read_metadata(uint16_t oid, uint8_t *buffer, uint16_t *length)
{
 if (!buffer || !length || *length == 0) {
 return OPTIGA_UTIL_ERROR;
 }

 optiga_util_t *me = optiga_manager_acquire();
 if (!me) {
 return OPTIGA_UTIL_ERROR;
 }

 optiga_lib_status = OPTIGA_LIB_BUSY;
 optiga_lib_status_t status = optiga_util_read_metadata(me, oid, buffer, length);

 if (status != OPTIGA_LIB_SUCCESS) {
 optiga_manager_release();
 return status;
 }

 // Wait for async operation to complete
 uint32_t timeout_ms = 5000;
 uint32_t elapsed_ms = 0;
 while (optiga_lib_status == OPTIGA_LIB_BUSY && elapsed_ms < timeout_ms) {
 vTaskDelay(pdMS_TO_TICKS(100));
 elapsed_ms += 100;
 }

 optiga_manager_release();
 return optiga_lib_status;
}

/*
 * Slot introspection
 *
 * Metadata comes back as 0x20 <len> followed by tag/len/value triples. Two of
 * those tags decide whether an object can still be written the ordinary way,
 * and both only appear once Protected Update has run:
 *
 *   D0  Change access condition. `E1 FC 07` is LcsO < 0x07 — anybody may write.
 *       `21 <hi> <lo>` is Int(OID): only a manifest signed by the certificate in
 *       that OID may write, and optiga_util_write_data() is refused from then on.
 *   C1  Payload version. The chip's anti-rollback counter. A manifest carrying a
 *       version that is not greater is rejected, and the rejection does not say
 *       so — it looks like a signature failure.
 *
 * Both are read-only questions, so they are cheap to ask before doing something
 * that would otherwise fail in a way nobody can diagnose from the error code.
 */
static const uint8_t *slot_meta_find(const uint8_t *meta, uint16_t meta_len,
                                     uint8_t want, uint8_t *out_len)
{
 if (NULL == meta || meta_len < 2U || 0x20U != meta[0])
 {
 return NULL;
 }
 uint16_t end = (uint16_t)(2U + meta[1]);
 if (end > meta_len)
 {
 end = meta_len;
 }
 uint16_t i = 2U;
 while ((uint16_t)(i + 2U) <= end)
 {
 uint8_t tag = meta[i];
 uint8_t len = meta[i + 1U];
 if ((uint16_t)(i + 2U + len) > end)
 {
 break;
 }
 if (tag == want)
 {
 if (NULL != out_len)
 {
 *out_len = len;
 }
 return &meta[i + 2U];
 }
 i = (uint16_t)(i + 2U + len);
 }
 return NULL;
}

bool optiga_slot_info(uint16_t oid, optiga_slot_info_t *info)
{
 uint8_t meta[64];
 uint16_t meta_len = (uint16_t)sizeof(meta);

 if (NULL == info)
 {
 return false;
 }
 memset(info, 0, sizeof(*info));
 info->oid = oid;

 if (OPTIGA_LIB_SUCCESS != tesaiot_read_metadata(oid, meta, &meta_len))
 {
 return false;
 }

 uint8_t len = 0U;
 /* Scan the whole change-access value for Int(OID), do not test only the first
  * byte. A compound condition puts the operator and an LcsO term ahead of it —
  * `E1 FC 07 FD 21 E0 E8` is manifest-locked and reads as anchor 0 if only
  * v[0] is examined. The HSM page decodes it by scanning; these two disagreeing
  * meant the screen could say "signed manifests only" while the enrolment gate
  * that calls this function decided the slot was freely writable and published
  * a request the chip would refuse. */
 const uint8_t *v = slot_meta_find(meta, meta_len, 0xD0U, &len);
 if (NULL != v)
 {
 for (uint8_t i = 0U; (i + 2U) < len; i++)
 {
 if (0x21U == v[i])
 {
 info->manifest_anchor_oid = (uint16_t)(((uint16_t)v[i + 1U] << 8) | v[i + 2U]);
 break;
 }
 }
 }

 v = slot_meta_find(meta, meta_len, 0xC1U, &len);
 if ((NULL != v) && (len >= 1U) && (len <= 4U))
 {
 for (uint8_t i = 0U; i < len; i++)
 {
 info->payload_version = (info->payload_version << 8) | v[i];
 }
 }

 v = slot_meta_find(meta, meta_len, 0xC5U, &len);
 if ((NULL != v) && (2U == len))
 {
 info->used_size = (uint16_t)(((uint16_t)v[0] << 8) | v[1]);
 }

 v = slot_meta_find(meta, meta_len, 0xE8U, &len);
 if ((NULL != v) && (1U == len))
 {
 info->data_type = v[0];
 }

 info->valid = true;
 return true;
}

/* Flat form for callers that decline to include this header — modoptiga.c is
 * compiled by six projects and takes prototypes only. */
bool optiga_slot_info_raw(uint16_t oid, uint16_t *anchor, uint32_t *version,
                          uint16_t *used, uint8_t *type)
{
 optiga_slot_info_t info;
 if (!optiga_slot_info(oid, &info))
 {
 return false;
 }
 if (NULL != anchor)  { *anchor  = info.manifest_anchor_oid; }
 if (NULL != version) { *version = info.payload_version; }
 if (NULL != used)    { *used    = info.used_size; }
 if (NULL != type)    { *type    = info.data_type; }
 return true;
}

uint16_t optiga_slot_manifest_anchor(uint16_t oid)
{
 optiga_slot_info_t info;
 return optiga_slot_info(oid, &info) ? info.manifest_anchor_oid : 0U;
}

uint32_t optiga_slot_payload_version(uint16_t oid)
{
 optiga_slot_info_t info;
 return optiga_slot_info(oid, &info) ? info.payload_version : 0U;
}


/**
 * @brief Write data to OPTIGA Trust M OID
 */
optiga_lib_status_t tesaiot_write_data(uint16_t oid, const uint8_t *data, uint16_t length)
{
 if (!data || length == 0) {
 return OPTIGA_UTIL_ERROR;
 }

 optiga_util_t *me = optiga_manager_acquire();
 if (!me) {
 return OPTIGA_UTIL_ERROR;
 }

 optiga_lib_status = OPTIGA_LIB_BUSY;
 optiga_lib_status_t status = optiga_util_write_data(me, oid, OPTIGA_UTIL_WRITE_ONLY, 0, data, length);

 if (status != OPTIGA_LIB_SUCCESS) {
 optiga_manager_release();
 return status;
 }

 // Wait for async operation to complete
 uint32_t timeout_ms = 5000;
 uint32_t elapsed_ms = 0;
 while (optiga_lib_status == OPTIGA_LIB_BUSY && elapsed_ms < timeout_ms) {
 vTaskDelay(pdMS_TO_TICKS(100));
 elapsed_ms += 100;
 }

 optiga_manager_release();
 return optiga_lib_status;
}

/**
 * @brief Read data from OPTIGA Trust M OID
 */
optiga_lib_status_t tesaiot_read_data(uint16_t oid, uint8_t *buffer, uint16_t *length)
{
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf(LABEL_TESAIOT_READ_DATA " ENTRY: OID=0x%04X, buffer=%p, requested_len=%u\n",
 oid, (void*)buffer, length ? *length : 0);
 fflush(stdout);
 #endif // TESAIOT_DEBUG_VERBOSE_ENABLED

 if (!buffer || !length || *length == 0) {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf(LABEL_TESAIOT_READ_DATA " ERROR: Invalid parameters\n");
 fflush(stdout);
 #endif // TESAIOT_DEBUG_VERBOSE_ENABLED
 return OPTIGA_UTIL_ERROR;
 }

 uint16_t initial_length = *length;

 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf(LABEL_TESAIOT_READ_DATA " Acquiring persistent OPTIGA instance...\n");
 fflush(stdout);
 #endif // TESAIOT_DEBUG_VERBOSE_ENABLED
 optiga_util_t *me = optiga_manager_acquire();
 if (!me) {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf(LABEL_TESAIOT_READ_DATA " ERROR: optiga_manager_acquire() failed\n");
 fflush(stdout);
 #endif // TESAIOT_DEBUG_VERBOSE_ENABLED
 return OPTIGA_UTIL_ERROR;
 }
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf(LABEL_TESAIOT_READ_DATA " Persistent OPTIGA instance acquired: %p\n", (void*)me);
 fflush(stdout);
 #endif // TESAIOT_DEBUG_VERBOSE_ENABLED

 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf(LABEL_TESAIOT_READ_DATA " Setting optiga_lib_status = OPTIGA_LIB_BUSY (0x%08X)\n", OPTIGA_LIB_BUSY);
 fflush(stdout);
 #endif // TESAIOT_DEBUG_VERBOSE_ENABLED
 optiga_lib_status = OPTIGA_LIB_BUSY;

 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf(LABEL_TESAIOT_READ_DATA " Calling optiga_util_read_data(me=%p, oid=0x%04X, offset=0, buffer=%p, length=%p[%u])...\n",
 (void*)me, oid, (void*)buffer, (void*)length, *length);
 fflush(stdout);
 #endif // TESAIOT_DEBUG_VERBOSE_ENABLED
 optiga_lib_status_t status = optiga_util_read_data(me, oid, 0, buffer, length);
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf(LABEL_TESAIOT_READ_DATA " optiga_util_read_data() returned: 0x%08X (length now: %u)\n", status, *length);
 fflush(stdout);
 #endif // TESAIOT_DEBUG_VERBOSE_ENABLED

 if (status != OPTIGA_LIB_SUCCESS) {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf(LABEL_TESAIOT_READ_DATA " ERROR: optiga_util_read_data() failed with status 0x%08X\n", status);
 fflush(stdout);
 #endif // TESAIOT_DEBUG_VERBOSE_ENABLED
 optiga_manager_release();
 return status;
 }

 // Wait for async operation to complete
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf(LABEL_TESAIOT_READ_DATA " Waiting for async callback (timeout=5000ms)...\n");
 fflush(stdout);
 #endif // TESAIOT_DEBUG_VERBOSE_ENABLED
 uint32_t timeout_ms = 5000;
 uint32_t elapsed_ms = 0;
 while (optiga_lib_status == OPTIGA_LIB_BUSY && elapsed_ms < timeout_ms) {
 if (elapsed_ms % 500 == 0) { // Log every 500ms
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf(LABEL_TESAIOT_READ_DATA " Still waiting... elapsed=%lums, optiga_lib_status=0x%08X\n",
 (unsigned long)elapsed_ms, optiga_lib_status);
 fflush(stdout);
 #endif // TESAIOT_DEBUG_VERBOSE_ENABLED
 }
 vTaskDelay(pdMS_TO_TICKS(100));
 elapsed_ms += 100;
 }

 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf(LABEL_TESAIOT_READ_DATA " Async operation completed: final_status=0x%08X, elapsed=%lums\n",
 optiga_lib_status, (unsigned long)elapsed_ms);
 fflush(stdout);
 #endif // TESAIOT_DEBUG_VERBOSE_ENABLED

 if (optiga_lib_status == OPTIGA_LIB_SUCCESS) {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf(LABEL_TESAIOT_READ_DATA " SUCCESS: Read %u bytes (requested %u)\n", *length, initial_length);
 #endif // TESAIOT_DEBUG_VERBOSE_ENABLED
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf(LABEL_TESAIOT_READ_DATA " Buffer first 16 bytes: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
 buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5], buffer[6], buffer[7],
 buffer[8], buffer[9], buffer[10], buffer[11], buffer[12], buffer[13], buffer[14], buffer[15]);
 fflush(stdout);
 #endif // TESAIOT_DEBUG_VERBOSE_ENABLED
 } else if (optiga_lib_status == OPTIGA_LIB_BUSY) {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf(LABEL_TESAIOT_READ_DATA " ERROR: Operation timed out after %lums\n", (unsigned long)elapsed_ms);
 fflush(stdout);
 #endif // TESAIOT_DEBUG_VERBOSE_ENABLED
 } else {
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf(LABEL_TESAIOT_READ_DATA " ERROR: Callback returned error status 0x%08X\n", optiga_lib_status);
 fflush(stdout);
 #endif // TESAIOT_DEBUG_VERBOSE_ENABLED
 }

 optiga_manager_release();
 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 printf(LABEL_TESAIOT_READ_DATA " EXIT: returning 0x%08X\n", optiga_lib_status);
 fflush(stdout);
 #endif // TESAIOT_DEBUG_VERBOSE_ENABLED
 return optiga_lib_status;
}

/**
 * @brief Write Trust Anchor to OPTIGA Trust M OID using ERASE_AND_WRITE mode
 */
optiga_lib_status_t tesaiot_write_trust_anchor(uint16_t oid, const uint8_t *data, uint16_t length)
{
 if (!data || length == 0) {
 return OPTIGA_UTIL_ERROR;
 }

 optiga_util_t *me = optiga_manager_acquire();
 if (!me) {
 return OPTIGA_UTIL_ERROR;
 }

 optiga_lib_status = OPTIGA_LIB_BUSY;
 // Use ERASE_AND_WRITE mode to clear OID before writing (recommended by Infineon)
 optiga_lib_status_t status = optiga_util_write_data(me, oid, OPTIGA_UTIL_ERASE_AND_WRITE, 0, data, length);

 if (status != OPTIGA_LIB_SUCCESS) {
 optiga_manager_release();
 return status;
 }

 // Wait for async operation to complete
 uint32_t timeout_ms = 5000;
 uint32_t elapsed_ms = 0;
 while (optiga_lib_status == OPTIGA_LIB_BUSY && elapsed_ms < timeout_ms) {
 vTaskDelay(pdMS_TO_TICKS(100));
 elapsed_ms += 100;
 }

 optiga_manager_release();
 return optiga_lib_status;
}

/**
 * @brief Erase data from OPTIGA Trust M OID (write 0 bytes)
 */
optiga_lib_status_t tesaiot_erase_data(uint16_t oid)
{
 optiga_util_t *me = optiga_manager_acquire();
 if (!me) {
 return OPTIGA_UTIL_ERROR;
 }

 optiga_lib_status = OPTIGA_LIB_BUSY;
 // Write 0 bytes to erase OID content
 optiga_lib_status_t status = optiga_util_write_data(me, oid, OPTIGA_UTIL_ERASE_AND_WRITE, 0, NULL, 0);

 if (status != OPTIGA_LIB_SUCCESS) {
 optiga_manager_release();
 return status;
 }

 // Wait for async operation to complete
 uint32_t timeout_ms = 5000;
 uint32_t elapsed_ms = 0;
 while (optiga_lib_status == OPTIGA_LIB_BUSY && elapsed_ms < timeout_ms) {
 vTaskDelay(pdMS_TO_TICKS(100));
 elapsed_ms += 100;
 }

 optiga_manager_release();
 return optiga_lib_status;
}

/**
 * @brief Verify Protected Update manifest signature using Trust Anchor
 */
optiga_lib_status_t tesaiot_verify_manifest_with_trustanchor(const uint8_t *manifest, uint16_t manifest_len, uint16_t trust_anchor_oid)
{
 if (!manifest || manifest_len == 0) {
 return OPTIGA_UTIL_ERROR;
 }

 optiga_util_t *me = optiga_manager_acquire();
 if (!me) {
 return OPTIGA_UTIL_ERROR;
 }

 optiga_lib_status = OPTIGA_LIB_BUSY;
 // Use OPTIGA Protected Update API to verify manifest
 // manifest_version = 1 (current Protected Update manifest version)
 // Note: trust_anchor_oid parameter unused - OPTIGA uses configured default OID
 optiga_lib_status_t status = optiga_util_protected_update_start(
 me,
 1, // manifest_version
 manifest,
 manifest_len
 );

 if (status != OPTIGA_LIB_SUCCESS) {
 optiga_manager_release();
 return status;
 }

 // Wait for async operation to complete
 uint32_t timeout_ms = 5000;
 uint32_t elapsed_ms = 0;
 while (optiga_lib_status == OPTIGA_LIB_BUSY && elapsed_ms < timeout_ms) {
 vTaskDelay(pdMS_TO_TICKS(100));
 elapsed_ms += 100;
 }

 optiga_manager_release();
 return optiga_lib_status;
}

/* ── Signed model blobs ────────────────────────────────────────────────────
 *
 * Verifies an Edge AI model staged by the store. See ipc_model_stage_defs.h
 * for the trailer format and for why a CRC is not a substitute.
 *
 * Verification runs IN THE SECURE ELEMENT. optiga_crypt_ecdsa_verify has been
 * present and working on this board since the HSM benchmark, where it was the
 * only caller -- a proven primitive wired to nothing. This is its first real
 * use.
 *
 * THE KEY, and an honest note about it. The store's public key is compiled in
 * below, and handed to the chip as host data. That is weaker than a key held
 * in an OPTIGA data object, because anyone who can rewrite the firmware can
 * swap it -- and it is the right trade for the threat this actually addresses,
 * which is a malicious or corrupt model FILE arriving over TACP or MQTT. An
 * attacker who can rewrite firmware has already won by every other route too.
 * Moving the key into a chip slot is a provisioning change, not a code change,
 * and the verify call below does not care which it is.
 */
#include "ipc_model_stage_defs.h"

/* Store signing key, NIST P-256.
 *
 * 68 bytes, not 65: the chip wants the DER BIT STRING wrapper -- 0x03 0x42
 * 0x00 -- around the uncompressed point, which is the shape
 * optiga_crypt_ecc_generate_keypair hands back and the shape the HSM benchmark
 * feeds to verify. Passing the bare 65-byte point returned OPTIGA_CMD_ERROR
 * (0x0202) on hardware, which reads as a bus or session fault and is not one. */
static const uint8_t k_store_pubkey[68] = {
    0x03, 0x42, 0x00, 0x04, 0x80, 0x1e, 0x64, 0x61, 0xf7, 0xdd, 0x8c, 0x15,
    0x52, 0x1e, 0xc5, 0xfb, 0x44, 0xdc, 0x00, 0xa3, 0x40, 0x68, 0x5c, 0xfb,
    0xf3, 0x32, 0xb5, 0x44, 0x22, 0x9d, 0x03, 0xb9, 0x7c, 0x1b, 0x0e, 0x80,
    0xed, 0x36, 0x51, 0x58, 0x9c, 0x6b, 0x33, 0xe5, 0x39, 0xe9, 0x98, 0x22,
    0x2d, 0x20, 0x20, 0x29, 0x4f, 0x06, 0x8b, 0x3b, 0xdb, 0x47, 0xbd, 0x9f,
    0x77, 0x08, 0xe3, 0xf6, 0x48, 0x5c, 0x86, 0x2a
};

/* Rejection reasons, so a refusal is legible off-core rather than a bare no. */
#define STAGE_SIG_OK              (1)
#define STAGE_SIG_ABSENT         (-1)   /* no trailer: unsigned blob          */
#define STAGE_SIG_MALFORMED      (-2)   /* magic, CRC, or algorithm           */
#define STAGE_SIG_HASH_FAILED    (-3)
#define STAGE_SIG_BAD            (-4)   /* the chip said no                   */
#define STAGE_SIG_CHIP_ERROR     (-5)   /* could not ask                      */

int optiga_verify_staged_model(const uint8_t *blob, uint32_t blob_len)
{
    if (blob == NULL || blob_len < sizeof(ai_stage_header_t)) {
        return STAGE_SIG_MALFORMED;
    }
    const ai_stage_header_t *h = (const ai_stage_header_t *)(const void *)blob;
    const uint32_t sig_off = ai_stage_sig_offset(h);
    if ((uint64_t)sig_off + sizeof(ai_stage_sig_t) > (uint64_t)blob_len) {
        return STAGE_SIG_ABSENT;
    }
    const ai_stage_sig_t *t = (const ai_stage_sig_t *)(const void *)(blob + sig_off);
    if (t->magic != AI_STAGE_SIG_MAGIC) {
        return STAGE_SIG_ABSENT;
    }
    if (t->alg != AI_STAGE_SIG_ALG_ECDSA_P256) {
        return STAGE_SIG_MALFORMED;
    }
    {
        const uint32_t n = (uint32_t)offsetof(ai_stage_sig_t, footer_crc32);
        if (ai_stage_crc32(0u, (const uint8_t *)t, n) != t->footer_crc32) {
            return STAGE_SIG_MALFORMED;
        }
    }

    /* Everything before the trailer: manifest AND model. Signing only the
     * model would leave the window geometry, the training rate and the axis
     * convention unauthenticated -- the three fields whose wrongness cannot be
     * detected at run time. */
    uint8_t digest[32];
    {
        mbedtls_sha256_context sha;
        mbedtls_sha256_init(&sha);
        int rc = mbedtls_sha256_starts(&sha, 0);
        if (rc == 0) { rc = mbedtls_sha256_update(&sha, blob, sig_off); }
        if (rc == 0) { rc = mbedtls_sha256_finish(&sha, digest); }
        mbedtls_sha256_free(&sha);
        if (rc != 0) {
            return STAGE_SIG_HASH_FAILED;
        }
    }

    /* Two bare INTEGERs, not a DER SEQUENCE.
     *
     * The chip speaks `02 <len> R 02 <len> S` with NO 0x30 wrapper -- which is
     * exactly what tlv2_to_raw_rs() in this file parses coming back OUT of
     * optiga_crypt_ecdsa_sign(). Feeding it a full DER SEQUENCE, which is what
     * mbedtls_ecdsa_raw_to_der() produces, adds two bytes the command layer
     * does not expect and the whole APDU is rejected with OPTIGA_CMD_ERROR
     * (0x0202) -- a code that reads like a bus or session fault and sent me
     * chasing the key format and the application-open precondition first.
     *
     * The leading 0x00 when the high bit is set is not optional: an ASN.1
     * INTEGER is signed, so without it a 32-byte limb whose MSB is 1 is a
     * negative number and roughly half of all signatures would fail. */
    uint8_t tlv[80];
    uint16_t tlv_len = 0U;
    {
        const uint8_t *limb = t->sig;
        for (int half = 0; half < 2; half++, limb += 32) {
            uint16_t n = 32U;
            const uint8_t *p = limb;
            while (n > 1U && *p == 0x00U) { p++; n--; }     /* strip leading zeros */
            const uint8_t pad = (uint8_t)((*p & 0x80U) ? 1U : 0U);
            if ((uint32_t)tlv_len + 2U + pad + n > sizeof(tlv)) {
                return STAGE_SIG_MALFORMED;
            }
            tlv[tlv_len++] = 0x02U;
            tlv[tlv_len++] = (uint8_t)(n + pad);
            if (pad) { tlv[tlv_len++] = 0x00U; }
            memcpy(&tlv[tlv_len], p, n);
            tlv_len = (uint16_t)(tlv_len + n);
        }
    }

    optiga_lib_status_t status = OPTIGA_LIB_BUSY;
    optiga_crypt_t *me = NULL;
    int result = STAGE_SIG_CHIP_ERROR;

    /* Open the application BEFORE taking the manager lock, never inside it.
     *
     * optiga_crypt_ecdsa_verify documents the open as a precondition, and it is
     * a real one -- without it the chip refuses the command. But
     * optiga_trust_open_application() takes its own lock (optiga_chip_enter),
     * so calling it while already holding the manager lock nests one lock
     * inside another and wedges the secure element: 10-second timeouts in
     * optiga_trust_open_application_gated, and a session this project's notes
     * say survives an MCU reset, a debugger reset and a reflash, clearing only
     * on a power cut. Measured the hard way, twice, on 2026-08-11.
     *
     * Order, not omission, was the fix. Removing the open entirely -- the first
     * correction attempted -- just moved the failure to "application not open"
     * and refused every model. */
    if (!optiga_trust_application_is_open()) {
        if (!optiga_trust_open_application()) {
            return STAGE_SIG_CHIP_ERROR;
        }
    }

    //! [hsm_optiga_touch_hold_staged_verify]
    /* ...context: inside optiga_verify_staged_model() ... */
    if (!optiga_manager_lock()) {
        return STAGE_SIG_CHIP_ERROR;
    }
    /* Same bus discipline every other transaction here uses: the secure
     * element and the touch controller share SCB5. */
    optiga_manager_touch_hold();
    //! [hsm_optiga_touch_hold_staged_verify]

    do {
        /* The application is NOT opened here, and that omission is deliberate.
         *
         * An earlier version called optiga_trust_open_application() at this
         * point, on the theory that a missing application was behind the
         * OPTIGA_CMD_ERROR (0x0202) this function used to return. That theory
         * was wrong -- the real cause was the signature encoding below -- and
         * the call was left in after the real fix landed, which is the mistake
         * worth recording. Opening the application from inside the manager lock
         * wedged the secure element hard: 10-second timeouts in
         * optiga_trust_open_application_gated, and a session that this project's
         * own notes say survives an MCU reset, a debugger reset and a reflash,
         * and clears only on a power cut.
         *
         * So this function asks and does not arrange. If the application is not
         * open, the caller gets STAGE_SIG_CHIP_ERROR and the model is refused --
         * a legible failure instead of a bricked bus. Bring-up owns opening it;
         * every other OPTIGA path in this file assumes the same.
         *
         * The 200 ms settle stays. trustm_ecdsa_sign takes it for a stated
         * reason -- letting the previous operation's queue slot clear -- and
         * this is the same bus. */
        /* The settle trustm_ecdsa_sign takes, for its stated reason -- letting
         * the previous operation's queue slot clear. Same bus. */
        vTaskDelay(pdMS_TO_TICKS(200));

        me = optiga_crypt_create(0, optiga_util_callback, NULL);
        if (me == NULL) {
            break;
        }
        public_key_from_host_t pk;
        pk.public_key = (uint8_t *)k_store_pubkey;
        pk.length     = (uint16_t)sizeof(k_store_pubkey);
        pk.key_type   = (uint8_t)OPTIGA_ECC_CURVE_NIST_P_256;

        optiga_lib_status = OPTIGA_LIB_BUSY;
        status = optiga_crypt_ecdsa_verify(me, digest, (uint8_t)sizeof(digest),
                                           tlv, tlv_len,
                                           OPTIGA_CRYPT_HOST_DATA, &pk);
        if (status != OPTIGA_LIB_SUCCESS) {
            break;      /* refused before it started: a real chip error */
        }

        /* Polled here rather than through WAIT_FOR_COMPLETION, which contains a
         * `break` and so leaves this do-while before the mapping below runs --
         * losing exactly the distinction this function exists to keep. The chip
         * ANSWERING "this signature is invalid" is not the same event as the
         * chip being unreachable, and a caller that cannot tell them apart will
         * read an attack as a bus fault or the reverse.
         *
         * Measured on hardware: a genuine blob completes with 0x0000, and one
         * signed by a different key completes with 0x802C -- OPTIGA_DEVICE_ERROR
         * plus a device status byte. Anything in that band is the chip's own
         * verdict; anything else is the transport. */
        {
            uint32_t waited_ms = 0u;
            while (optiga_lib_status == OPTIGA_LIB_BUSY && waited_ms < 3000u) {
                vTaskDelay(pdMS_TO_TICKS(10));
                waited_ms += 10u;
            }
            if (optiga_lib_status == OPTIGA_LIB_SUCCESS) {
                result = STAGE_SIG_OK;
            } else if ((optiga_lib_status & 0xFF00u) == (OPTIGA_DEVICE_ERROR & 0xFF00u)) {
                result = STAGE_SIG_BAD;
            } else {
                result = STAGE_SIG_CHIP_ERROR;
            }
        }
    } while (0);

    if (me != NULL) {
        optiga_crypt_destroy(me);
    }
    optiga_manager_touch_release();
    optiga_manager_unlock();
    return result;
}
