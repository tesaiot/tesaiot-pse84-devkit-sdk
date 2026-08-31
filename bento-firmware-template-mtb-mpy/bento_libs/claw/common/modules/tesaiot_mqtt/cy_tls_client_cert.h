/******************************************************************************
* File Name:   cy_tls_client_cert.h
*
* Description: Header file for TLS client certificate functions.
*              Used by mqtt_task.c to set client cert for mTLS mode.
*
* Based on: pse84_tesaiot_client reference implementation
******************************************************************************/

#ifndef CY_TLS_CLIENT_CERT_H
#define CY_TLS_CLIENT_CERT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Set the client certificate for TLS connections
 *
 * @param buf      Buffer containing the certificate (PEM or DER format)
 * @param len      Length of the buffer
 * @param is_pem   1 if certificate is in PEM format, 0 for DER format
 *
 * @return int 0 on success, negative error code otherwise
 */
int cy_tls_set_client_cert(const uint8_t *buf, size_t len, int is_pem);

#ifdef __cplusplus
}
#endif

#endif /* CY_TLS_CLIENT_CERT_H */
