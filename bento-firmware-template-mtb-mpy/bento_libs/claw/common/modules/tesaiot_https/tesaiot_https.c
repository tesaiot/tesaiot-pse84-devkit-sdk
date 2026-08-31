/******************************************************************************
* File Name:   tesaiot_https.c
*
* Description: TESAIoT HTTPS POST Client for BENTO BentoClaw.
*              Single-shot HTTPS POST using cy_secure_sockets (TLS 1.2).
*              No chunked encoding, no redirects — minimal HTTP/1.1 POST.
*
*              Memory budget:
*              - Static request buffer: 512 bytes (header)
*              - Static response buffer: 1024 bytes
*              - No heap allocation (all stack/static)
*              - TLS handshake: reuses existing mbedTLS buffers (IN=6KB, OUT=2KB)
*
* Author:      Wiroon Sriborrirux (BDH)
******************************************************************************/

#include "tesaiot_https.h"
#include "tesaiot_config_store.h"
#include "tesaiot_https_root_ca.h"
#include "cy_secure_sockets.h"
#include "cy_tls.h"
#include <stdio.h>
#include <string.h>

/******************************************************************************
* Macros
******************************************************************************/
#define HTTPS_HEADER_BUF_SIZE    512
#define HTTPS_RESPONSE_BUF_SIZE  1024
#define HTTPS_CONNECT_TIMEOUT_MS 15000
#define HTTPS_SEND_TIMEOUT_MS    10000
#define HTTPS_RECV_TIMEOUT_MS    10000

/******************************************************************************
* Static Buffers (avoid heap allocation)
******************************************************************************/
static char s_header_buf[HTTPS_HEADER_BUF_SIZE];
static char s_recv_buf[HTTPS_RESPONSE_BUF_SIZE];

/******************************************************************************
* Internal: parse HTTP status code from response
******************************************************************************/
static int parse_status_code(const char *response, size_t len)
{
    /* HTTP/1.1 200 OK\r\n */
    if (len < 12) return -1;
    if (strncmp(response, "HTTP/1.", 7) != 0) return -1;

    const char *code_start = response + 9;
    int code = 0;
    for (int i = 0; i < 3 && code_start[i] >= '0' && code_start[i] <= '9'; i++) {
        code = code * 10 + (code_start[i] - '0');
    }
    return (code >= 100 && code <= 599) ? code : -1;
}

/******************************************************************************
* Internal: find body start (\r\n\r\n) in HTTP response
******************************************************************************/
static const char *find_body(const char *response, size_t len)
{
    for (size_t i = 0; i + 3 < len; i++) {
        if (response[i] == '\r' && response[i+1] == '\n' &&
            response[i+2] == '\r' && response[i+3] == '\n') {
            return &response[i + 4];
        }
    }
    return NULL;
}

/******************************************************************************
* Public: HTTPS POST
******************************************************************************/
bool tesaiot_https_post(const char *payload, size_t payload_len,
                        tesaiot_https_response_t *response)
{
    cy_rslt_t result;
    cy_socket_t socket_handle = NULL;
    bool success = false;
    static bool s_socket_initialized = false;

    /* Initialize response */
    if (response) {
        memset(response, 0, sizeof(*response));
        response->status_code = -1;
    }

    /* Initialize secure sockets library (once) */
    if (!s_socket_initialized) {
        result = cy_socket_init();
        if (result != CY_RSLT_SUCCESS) {
            printf("[HTTPS] cy_socket_init failed: 0x%08X\n", (unsigned)result);
            return false;
        }
        s_socket_initialized = true;
    }

    /* Read config */
    tesaiot_config_t cfg;
    tesaiot_config_get(&cfg);

    if (cfg.api_host[0] == '\0') {
        printf("[HTTPS] No api_host configured\n");
        return false;
    }

    /* Auth headers:
     * Port 443 (APISIX public): needs BOTH "apikey" (gateway) + "X-API-Key" (backend)
     * Port 9444 (direct nginx): needs only "X-API-Key" (backend)
     */
    bool has_apisix_key = (cfg.apisix_api_key[0] != '\0');
    bool has_api_key    = (cfg.api_key[0] != '\0');

    if (!has_apisix_key && !has_api_key) {
        printf("[HTTPS] No API key configured\n");
        return false;
    }

    printf("[HTTPS] POST https://%s:%u%s (%u bytes)\n",
           cfg.api_host, cfg.api_port, cfg.api_endpoint, (unsigned)payload_len);

    /* DNS resolve */
    cy_socket_ip_address_t ip_addr;
    result = cy_socket_gethostbyname(cfg.api_host, CY_SOCKET_IP_VER_V4, &ip_addr);
    if (result != CY_RSLT_SUCCESS) {
        printf("[HTTPS] DNS resolve failed: 0x%08X\n", (unsigned)result);
        return false;
    }

    /* Create TLS socket */
    result = cy_socket_create(CY_SOCKET_DOMAIN_AF_INET, CY_SOCKET_TYPE_STREAM,
                              CY_SOCKET_IPPROTO_TLS, &socket_handle);
    if (result != CY_RSLT_SUCCESS) {
        printf("[HTTPS] Socket create failed: 0x%08X\n", (unsigned)result);
        return false;
    }

    /* Set Root CA for server verification (ISRG Root X1 for Let's Encrypt) */
    result = cy_socket_setsockopt(socket_handle, CY_SOCKET_SOL_TLS,
                                  CY_SOCKET_SO_TRUSTED_ROOTCA_CERTIFICATE,
                                  TESAIOT_HTTPS_ROOT_CA,
                                  sizeof(TESAIOT_HTTPS_ROOT_CA));
    if (result != CY_RSLT_SUCCESS) {
        printf("[HTTPS] Set Root CA failed: 0x%08X\n", (unsigned)result);
        goto cleanup;
    }

    /* Set SNI hostname */
    result = cy_socket_setsockopt(socket_handle, CY_SOCKET_SOL_TLS,
                                  CY_SOCKET_SO_SERVER_NAME_INDICATION,
                                  cfg.api_host, strlen(cfg.api_host) + 1);
    if (result != CY_RSLT_SUCCESS) {
        printf("[HTTPS] Set SNI failed: 0x%08X\n", (unsigned)result);
        goto cleanup;
    }

    /* Set TLS auth mode: verify server only */
    int auth_mode = CY_SOCKET_TLS_VERIFY_REQUIRED;
    result = cy_socket_setsockopt(socket_handle, CY_SOCKET_SOL_TLS,
                                  CY_SOCKET_SO_TLS_AUTH_MODE,
                                  &auth_mode, sizeof(auth_mode));
    if (result != CY_RSLT_SUCCESS) {
        printf("[HTTPS] Set auth mode failed: 0x%08X\n", (unsigned)result);
        goto cleanup;
    }

    /* Set send/recv timeouts BEFORE connect — TLS handshake uses recv internally */
    uint32_t send_timeout = cfg.http_send_timeout_ms > 0 ?
                            cfg.http_send_timeout_ms : HTTPS_SEND_TIMEOUT_MS;
    uint32_t recv_timeout = cfg.http_recv_timeout_ms > 0 ?
                            cfg.http_recv_timeout_ms : HTTPS_RECV_TIMEOUT_MS;

    cy_socket_setsockopt(socket_handle, CY_SOCKET_SOL_SOCKET,
                         CY_SOCKET_SO_SNDTIMEO, &send_timeout, sizeof(send_timeout));
    cy_socket_setsockopt(socket_handle, CY_SOCKET_SOL_SOCKET,
                         CY_SOCKET_SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout));

    /* TCP user timeout — limits how long connect/handshake can block */
    uint32_t tcp_user_timeout = HTTPS_CONNECT_TIMEOUT_MS;
    cy_socket_setsockopt(socket_handle, CY_SOCKET_SOL_SOCKET,
                         CY_SOCKET_SO_TCP_USER_TIMEOUT,
                         &tcp_user_timeout, sizeof(tcp_user_timeout));

    /* Connect (TLS handshake happens here) */
    cy_socket_sockaddr_t addr;
    memset(&addr, 0, sizeof(addr));
    addr.ip_address = ip_addr;
    addr.port = cfg.api_port;

    printf("[HTTPS] Connecting to %s:%u...\n", cfg.api_host, cfg.api_port);
    result = cy_socket_connect(socket_handle, &addr, sizeof(addr));
    if (result != CY_RSLT_SUCCESS) {
        printf("[HTTPS] Connect failed: 0x%08X\n", (unsigned)result);
        goto cleanup;
    }

    /* Build HTTP POST request */
    int header_len = snprintf(s_header_buf, sizeof(s_header_buf),
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n",
        cfg.api_endpoint,
        cfg.api_host);

    /* Append auth headers */
    if (has_apisix_key) {
        header_len += snprintf(s_header_buf + header_len,
            sizeof(s_header_buf) - header_len,
            "apikey: %s\r\n", cfg.apisix_api_key);
    }
    if (has_api_key) {
        header_len += snprintf(s_header_buf + header_len,
            sizeof(s_header_buf) - header_len,
            "%s: %s\r\n", cfg.api_key_header, cfg.api_key);
    }

    header_len += snprintf(s_header_buf + header_len,
        sizeof(s_header_buf) - header_len,
        "Content-Type: application/json\r\n"
        "Content-Length: %u\r\n"
        "Connection: close\r\n"
        "\r\n",
        (unsigned)payload_len);

    if (header_len < 0 || (size_t)header_len >= sizeof(s_header_buf)) {
        printf("[HTTPS] Header too large\n");
        goto cleanup;
    }

    /* Send header */
    uint32_t bytes_sent = 0;
    result = cy_socket_send(socket_handle, s_header_buf, (uint32_t)header_len,
                            0, &bytes_sent);
    if (result != CY_RSLT_SUCCESS) {
        printf("[HTTPS] Send header failed: 0x%08X\n", (unsigned)result);
        goto cleanup;
    }

    /* Send payload */
    if (payload && payload_len > 0) {
        bytes_sent = 0;
        result = cy_socket_send(socket_handle, payload, (uint32_t)payload_len,
                                0, &bytes_sent);
        if (result != CY_RSLT_SUCCESS) {
            printf("[HTTPS] Send payload failed: 0x%08X\n", (unsigned)result);
            goto cleanup;
        }
    }

    /* Receive response */
    uint32_t bytes_received = 0;
    memset(s_recv_buf, 0, sizeof(s_recv_buf));
    result = cy_socket_recv(socket_handle, s_recv_buf, sizeof(s_recv_buf) - 1,
                            0, &bytes_received);
    if (result != CY_RSLT_SUCCESS && bytes_received == 0) {
        printf("[HTTPS] Recv failed: 0x%08X\n", (unsigned)result);
        goto cleanup;
    }

    s_recv_buf[bytes_received] = '\0';

    /* Parse status code */
    int status_code = parse_status_code(s_recv_buf, bytes_received);
    printf("[HTTPS] Response: %d (%u bytes)\n", status_code, (unsigned)bytes_received);

    if (response) {
        response->status_code = status_code;
        const char *body = find_body(s_recv_buf, bytes_received);
        if (body) {
            size_t body_len = bytes_received - (size_t)(body - s_recv_buf);
            size_t copy_len = (body_len < sizeof(response->body) - 1) ?
                              body_len : sizeof(response->body) - 1;
            memcpy(response->body, body, copy_len);
            response->body[copy_len] = '\0';
            response->body_len = copy_len;
        }
    }

    success = (status_code >= 200 && status_code < 300);

cleanup:
    if (socket_handle) {
        cy_socket_disconnect(socket_handle, 0);
        cy_socket_delete(socket_handle);
    }
    return success;
}
