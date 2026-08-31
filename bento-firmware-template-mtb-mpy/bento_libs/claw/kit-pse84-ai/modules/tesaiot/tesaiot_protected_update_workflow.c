/**
 * SPDX-FileCopyrightText: 2024-2025 TESAIoT Platform
 *
 * \file tesaiot_protected_update_workflow.c
 * \brief Protected Update workflow for OPTIGA Trust M provisioning with TESAIoT Platform.
 *
 * \ingroup TESAIoT
 */

#include "tesaiot_protected_update.h"
#include "tesaiot.h"
#include <stdio.h>
#include <string.h>
#include "cybsp.h"
#include "FreeRTOS.h"
#include "task.h"
#include "event_groups.h"
#include "cy_wcm.h"
#include "mqtt_task.h"
#include "subscriber_task.h"
#include "tesaiot_optiga.h"
#include "tesaiot_config.h"

void tesaiot_run_protected_update_workflow(void)
{
    /* License enforcement */
    TESAIOT_LICENSE_GATE_VOID();

    printf("\n================ Protected Update Workflow ================\n");
    printf("Purpose: Certificate Renewal using existing keypair\n");

#if TESAIOT_DEBUG_VERBOSE_ENABLED
    printf("%s Workflow: read UID -> publish renewal request -> receive manifest+fragment\n", MENU_CSR_PROTUPD);
    printf("%s Note: Uses EXISTING keypair (OID 0xE0F1) for Protected Update\n\n", MENU_CSR_PROTUPD);
#endif

    /* Verify Wi-Fi connectivity */
#if TESAIOT_DEBUG_VERBOSE_ENABLED
    printf("%s Checking Wi-Fi...\n", MENU_CSR_PROTUPD);
#endif
    if (cy_wcm_is_connected_to_ap() == 0)
    {
        printf("%s ERROR: Wi-Fi not connected\n", MENU_CSR_PROTUPD);
        return;
    }
#if TESAIOT_DEBUG_VERBOSE_ENABLED
    printf("%s Wi-Fi OK\n", MENU_CSR_PROTUPD);
#endif

    /* Use FULL MQTT mode from start (Multi-Topic Protocol - 2026.02)
     * - Full mode: connect with subscriber task ready to receive response
     * - Eliminates disconnect/reconnect overhead
     * - Subscriber is ready before publish (prevents retained message issues)
     * - Recommended by Server Team for multi-topic workflow
     */
#if TESAIOT_DEBUG_VERBOSE_ENABLED
    printf("%s Checking MQTT...\n", MENU_CSR_PROTUPD);
#endif

    /* Stop any existing MQTT connection first */
    if (mqtt_is_connected())
    {
        printf("%s Stopping existing MQTT connection...\n", MENU_CSR_PROTUPD);
        mqtt_request_stop();
        vTaskDelay(pdMS_TO_TICKS(1000U));
    }

    /* Set state BEFORE starting MQTT to avoid race condition
     * Subscriber may process messages faster than workflow reaches publish
     */
    trustm_update_state(TRUSTM_STATE_WAITING_FOR_JSON_BUNDLE, NULL, NULL);

    /* Start full MQTT mode with subscriber task */
    if (!mqtt_request_start())
    {
        printf("%s ERROR: MQTT start failed\n", MENU_CSR_PROTUPD);
        return;
    }

    /* Wait for connection with timeout */
    const uint32_t max_attempts = 60;
    for (uint32_t i = 0; i < max_attempts; ++i)
    {
        if (mqtt_is_connected())
            break;
        vTaskDelay(pdMS_TO_TICKS(500U));
    }

    if (!mqtt_is_connected())
    {
        printf("%s ERROR: MQTT connection timeout\n", MENU_CSR_PROTUPD);
        return;
    }

    /* Wait for subscriber to create event group (created in subscriber_task entry) */
    for (uint32_t i = 0; i < 100 && data_received_event_group == NULL; ++i)
    {
        vTaskDelay(pdMS_TO_TICKS(100U));
    }

    if (data_received_event_group == NULL)
    {
        printf("%s ERROR: Event group not created\n", MENU_CSR_PROTUPD);
        return;
    }

#if TESAIOT_DEBUG_VERBOSE_ENABLED
    printf("%s MQTT OK (full mode with subscriber ready)\n\n", MENU_CSR_PROTUPD);
#endif

    /* Option A: Certificate Renewal - NO new keypair generation!
     *
     * This workflow renews the certificate for the EXISTING keypair in 0xE0F1
     * that was created by CSR Workflow.
     *
     * Per KNOWLEDGE.md best practices:
     * - On-device key generation (CSR Workflow) is more secure
     * - Protected Update is for secure certificate delivery, not key generation
     * - Same private key in 0xE0F1 is reused, only certificate in 0xE0E1 is renewed
     */
#if TESAIOT_DEBUG_VERBOSE_ENABLED
    printf("%s Using existing keypair from CSR Workflow (OID 0x%04X)\n", MENU_CSR_PROTUPD, TESAIOT_TARGET_KEY_OID);
    printf("%s Certificate will be renewed in OID 0x%04X\n", MENU_CSR_PROTUPD, TESAIOT_DEVICE_CERT_OID);
#endif

    /* Read factory UID */
    char factory_uid[65] = {0};
    if (!tesaiot_read_factory_uid(factory_uid, sizeof(factory_uid)))
    {
        printf("%s ERROR: Failed to read UID\n", MENU_CSR_PROTUPD);
        return;
    }
#if TESAIOT_DEBUG_VERBOSE_ENABLED
    printf("%s UID: %s\n\n", MENU_CSR_PROTUPD, factory_uid);
#endif

    /* Publish Protected Update request for Certificate Renewal (Option A)
     *
     * Target OID: 0xE0E1 (Certificate slot - same as CSR Workflow)
     * Trust Anchor: 0xE0E9 (Platform signing key certificate)
     * Payload Type: "data" (certificate, not key)
     *
     * The Platform will:
     * 1. Generate a new certificate for the EXISTING public key (from CSR Workflow)
     * 2. Create Protected Update manifest + fragment
     * 3. Send via MQTT to device
     * 4. Device writes new certificate to 0xE0E1
     */
    const char *target_oid = "E0E1";  // Certificate slot (Option A: renewal)
    const char *trust_anchor_oid = "E0E8";  // Per CSR_PU_GUIDELINE: Trust Anchor 1 is REQUIRED for PU
    uint32_t payload_version = 1;  // Increment for anti-rollback protection

#if TESAIOT_DEBUG_VERBOSE_ENABLED
    printf("%s Publishing certificate renewal request...\n", MENU_CSR_PROTUPD);
    printf("%s   Target OID: 0x%s (certificate slot)\n", MENU_CSR_PROTUPD, target_oid);
    printf("%s   Trust Anchor: 0x%s\n", MENU_CSR_PROTUPD, trust_anchor_oid);
    printf("%s   Payload Version: %u (anti-rollback)\n", MENU_CSR_PROTUPD, (unsigned)payload_version);
#endif

    int result = tesaiot_publish_protected_update(target_oid, trust_anchor_oid, payload_version);
    if (result != 0)
    {
        printf("%s ERROR: Publish failed\n", MENU_CSR_PROTUPD);
        mqtt_request_stop();
        return;
    }

    /* Request published successfully - message already printed by tesaiot_publish_protected_update()
     * Subscriber is already running and ready to receive single-topic JSON bundle:
     * - signing_certificate (X.509 cert in base64)
     * - manifest (CBOR manifest in base64)
     * - fragment_0, fragment_1, ... (encrypted fragments in base64)
     * - fragment_count (total ~1881 bytes for 1-fragment cert)
     */

#if TESAIOT_DEBUG_VERBOSE_ENABLED
    printf("%s Waiting for platform response (single-topic JSON bundle)...\n\n", MENU_CSR_PROTUPD);
#endif

    /* Wait for Protected Update completion via event bit (simple and reliable)
     * Subscriber sets PROTECTED_UPDATE_COMPLETE_BIT after STEP 4.2 success
     * Timeout: 5 minutes (300 seconds)
     */
    EventBits_t bits = xEventGroupWaitBits(
        data_received_event_group,
        PROTECTED_UPDATE_COMPLETE_BIT,
        pdTRUE,   /* Clear bit after reading */
        pdFALSE,  /* Don't wait for all bits */
        pdMS_TO_TICKS(300000));

    if (bits & PROTECTED_UPDATE_COMPLETE_BIT)
    {
        printf("\n%s SUCCESS - Certificate installed to OID 0xE0E1\n", MENU_CSR_PROTUPD);
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    else
    {
        printf("\n%s TIMEOUT - No response from platform\n", MENU_CSR_PROTUPD);
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    /* Stop MQTT connection before returning to menu
     * This prevents unnecessary Publisher task creation
     * User can reconnect via Menu 2 if needed
     */
    printf("%s Stopping MQTT connection...\n", MENU_CSR_PROTUPD);
    fflush(stdout);
    mqtt_request_stop();
    vTaskDelay(pdMS_TO_TICKS(1000));  /* Increase delay to ensure cleanup completes */
    printf("%s Done. Returning to menu.\n\n", MENU_CSR_PROTUPD);
    fflush(stdout);
}

void vProtectedUpdate(void *pvParameters)
{
    (void)pvParameters;

    const EventBits_t required = MANIFEST_RECEIVED_BIT | FRAGMENT_RECEIVED_BIT;

#if TESAIOT_DEBUG_VERBOSE_ENABLED
    printf("%s Waiting for manifest + fragment (timeout: 5min)...\n", LABEL_PROTUPD_TASK);
#endif

    EventBits_t bits = xEventGroupWaitBits(
        data_received_event_group,
        required,
        pdFALSE,
        pdTRUE,
        pdMS_TO_TICKS(300000));

    if ((bits & required) != required)
    {
        printf("%s Timeout: manifest=%s, fragment=%s\n",
               LABEL_PROTUPD_TASK,
               (bits & MANIFEST_RECEIVED_BIT) ? "OK" : "MISSING",
               (bits & FRAGMENT_RECEIVED_BIT) ? "OK" : "MISSING");
        vTaskDelete(NULL);
        return;
    }

#if TESAIOT_DEBUG_VERBOSE_ENABLED
    printf("%s Data received, executing Protected Update...\n", LABEL_PROTUPD_TASK);
#endif

    optiga_lib_status_t status = tesaiot_protected_update(
        TESAIOT_TRUST_ANCHOR_OID,
        TESAIOT_DEVICE_CERT_OID,
        TESAIOT_CONFIDENTIALITY_OID);

    if (status == OPTIGA_LIB_SUCCESS)
    {
        printf("%s Protected Update completed\n", LABEL_PROTUPD_TASK);
    }
    else
    {
        printf("%s Protected Update failed (0x%04X)\n", LABEL_PROTUPD_TASK, status);
    }

    vTaskDelete(NULL);
}
