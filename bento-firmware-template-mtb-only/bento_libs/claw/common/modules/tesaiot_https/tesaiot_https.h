/******************************************************************************
* File Name:   tesaiot_https.h
*
* Description: TESAIoT HTTPS POST Client for BENTO BentoClaw.
*              Raw cy_secure_sockets TLS + HTTP/1.1 POST.
*              Supports Scenario 3 (Bearer/API-key) and Scenario 4 (APISIX).
*
* Author:      Wiroon Sriborrirux (BDH)
******************************************************************************/

#ifndef TESAIOT_HTTPS_H
#define TESAIOT_HTTPS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief HTTPS POST result
 */
typedef struct {
    int     status_code;    /* HTTP status code (200, 401, etc.) or -1 on error */
    char    body[256];      /* Response body (truncated) */
    size_t  body_len;       /* Actual body length received */
} tesaiot_https_response_t;

/**
 * @brief Send HTTPS POST with JSON payload to TESAIoT API.
 *
 * Uses config store for: api_host, api_port, api_endpoint, api_key/apisix_api_key.
 * Auth header is determined by tls_mode:
 *   - Mode 2 (serverTLS): Authorization: Bearer {api_key}
 *   - Mode 3 (HTTPS_API): apikey: {apisix_api_key}
 *
 * @param payload     JSON string to POST
 * @param payload_len Length of payload
 * @param response    Output: HTTP response (may be NULL if caller doesn't care)
 * @return true if HTTP 2xx received, false on error
 */
bool tesaiot_https_post(const char *payload, size_t payload_len,
                        tesaiot_https_response_t *response);

#ifdef __cplusplus
}
#endif

#endif /* TESAIOT_HTTPS_H */
