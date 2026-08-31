/*******************************************************************************
 * File Name: claw_transport.h
 *
 * Description: BentoClaw transport abstraction — HTTPS POST to TESAIoT
 *              APISIX gateway. Uses cy_http_client over TLS.
 *
 *              API:
 *                claw_https_connect(host, port, api_key) -> bool
 *                claw_https_post(path, json, json_len, resp, resp_max) -> int
 *                claw_https_disconnect() -> void
 *                claw_https_connected() -> bool
 *
 *******************************************************************************/

#ifndef CLAW_TRANSPORT_H
#define CLAW_TRANSPORT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Connection state */
bool claw_https_connected(void);

/* Connect to TESAIoT APISIX gateway.
 * host: APISIX hostname (e.g., "api.tesaiot.com")
 * port: HTTPS port (typically 443)
 * api_key: device API key for key-auth plugin
 * Returns true on success. */
bool claw_https_connect(const char *host, uint16_t port, const char *api_key);

/* POST JSON to endpoint.
 * path: API path (e.g., "/ai/v1/chat")
 * json: request body (JSON string)
 * json_len: length of json
 * resp_buf: output buffer for response body
 * resp_max: max bytes to write to resp_buf
 * Returns response body length, or -1 on error. */
int claw_https_post(const char *path, const char *json, size_t json_len,
                    char *resp_buf, size_t resp_max);

/* Disconnect from server. */
void claw_https_disconnect(void);

#endif /* CLAW_TRANSPORT_H */
