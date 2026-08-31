/******************************************************************************
* File Name:   mqtt_mtls_setup.h
*
* Description: mTLS + OPTIGA Trust M setup orchestrator for MQTT connections.
*              Reads device certificate from OPTIGA, registers PSA SE driver,
*              and binds the hardware key to the TLS stack.
*
*              Private key NEVER leaves the OPTIGA secure element.
******************************************************************************/

#ifndef MQTT_MTLS_SETUP_H
#define MQTT_MTLS_SETUP_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Setup OPTIGA Trust M for mTLS connection.
 *
 * Steps:
 *  1. Pause CM55 touch (shared I2C bus)
 *  2. Read client certificate from OPTIGA OID 0xE0E1 (device) or 0xE0E0 (factory fallback)
 *  3. Parse cert into mbedTLS via cy_tls_set_client_cert()
 *  4. Register OPTIGA as PSA Secure Element driver
 *  5. Bind signing key OID and create PSA key handle
 *  6. Set PSA key ID for TLS stack (opaque key — OPTIGA signs internally)
 *  7. Resume CM55 touch
 *
 * Idempotent: returns true immediately if already initialized.
 *
 * @return true on success, false on error (cert not found, PSA failure, etc.)
 */
bool mqtt_mtls_setup_optiga(void);

/**
 * @brief Cleanup mTLS resources after MQTT disconnect.
 *
 * Destroys PSA key handle, clears SE slots, resets state.
 * Safe to call even if setup was never called.
 */
void mqtt_mtls_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_MTLS_SETUP_H */
