/**
 * @file tesaiot_csr_workflow.c
 * @brief TESAIoT CSR Workflow State Machine Implementation
 * @version 1.0
 * @date 2026-01-12
 *
 * Part of TESAIoT Firmware SDK
 */

#include "tesaiot_csr.h"
#include "tesaiot_optiga.h"
#include "tesaiot_platform.h"
#include "tesaiot.h"
#include <stdio.h>
#include <string.h>

/* FreeRTOS includes */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "event_groups.h"

/* For factory UID reading - now part of tesaiot_optiga.h */

/* For device_cert_semaphore and device_cert_write_status */
#include "subscriber_task.h"

/* For mqtt_request_stop() and mqtt_is_connected() */
#include "mqtt_task.h"

/* External functions */
extern int publish_csr(uint8_t *csr, size_t csr_length, uint16_t target_oid,
                       uint16_t trust_anchor_oid, uint32_t payload_version);

/*----------------------------------------------------------------------------
 * Internal State
 *---------------------------------------------------------------------------*/

static tesaiot_csr_state_t current_state = TESAIOT_CSR_STATE_IDLE;
static tesaiot_csr_workflow_config_t saved_config = {0};
static int initialized = 0;

/* Workflow context structure definition */
typedef struct {
    uint8_t public_key_der[TESAIOT_OPTIGA_PUBKEY_MAX_LEN];
    uint16_t pubkey_len;
    char csr_pem[TESAIOT_OPTIGA_CSR_MAX_LEN];
} tesaiot_csr_context_t;

/* Global context pointer - points to stack-allocated context during workflow
 * This allows the 700-byte context to live on stack (not RAM) until needed
 */
static tesaiot_csr_context_t *g_workflow_context = NULL;

/*----------------------------------------------------------------------------
 * Public API Implementation
 *---------------------------------------------------------------------------*/

/**
 * Initialize CSR workflow
 */
int tesaiot_csr_workflow_init(const tesaiot_csr_workflow_config_t *config)
{
    /* License enforcement - CSR workflow disabled without valid license */
    TESAIOT_LICENSE_GATE_INT();

    if (!config) {
        return -1;  /* Invalid parameter */
    }

    if (!config->device_id) {
        return -2;  /* Missing device_id */
    }

    /* Save configuration */
    saved_config = *config;

    /* Reset state */
    current_state = TESAIOT_CSR_STATE_IDLE;
    initialized = 1;

    return 0;  /* Success */
}

/**
 * Get current workflow state
 */
tesaiot_csr_state_t tesaiot_csr_workflow_get_state(void)
{
    return current_state;
}

/**
 * Start CSR workflow
 */
int tesaiot_csr_workflow_start(void)
{
    if (!initialized) {
        return -1;  /* Not initialized */
    }

    if (current_state != TESAIOT_CSR_STATE_IDLE) {
        return -2;  /* Already started */
    }

    /* Transition to first state */
    current_state = TESAIOT_CSR_STATE_GENERATE_KEYPAIR;

    return 0;  /* Success */
}

/**
 * Run one iteration of state machine
 */
int tesaiot_csr_workflow_run(void)
{
    if (!initialized) {
        return -1;  /* Not initialized */
    }

    switch (current_state) {
        case TESAIOT_CSR_STATE_IDLE:
            printf("[CSR-SM] State: IDLE (waiting for start)\n");
            break;

        case TESAIOT_CSR_STATE_GENERATE_KEYPAIR:
            printf("[CSR-SM] State: GENERATE_KEYPAIR\n");

            /* Initialize public key buffer */
            g_workflow_context->pubkey_len = TESAIOT_OPTIGA_PUBKEY_MAX_LEN;

            /* Generate ECC P-256 keypair at OID 0xE0F1 */
            if (tesaiot_optiga_generate_keypair(
                    TESAIOT_OPTIGA_OID_SESSION_KEY,
                    g_workflow_context->public_key_der,
                    &g_workflow_context->pubkey_len))
            {
                printf("[CSR-SM] ✓ Keypair generated (%u bytes public key)\n",
                       (unsigned int)g_workflow_context->pubkey_len);
                current_state = TESAIOT_CSR_STATE_GENERATE_CSR;
            }
            else
            {
                printf("[CSR-SM] ✗ Keypair generation failed!\n");
                current_state = TESAIOT_CSR_STATE_ERROR;
            }
            break;

        case TESAIOT_CSR_STATE_GENERATE_CSR:
            printf("[CSR-SM] State: GENERATE_CSR\n");

            /* Build subject string from device_id */
            char subject[256];
            snprintf(subject, sizeof(subject), "CN=%s,O=TESAIoT", saved_config.device_id);

            /* Generate CSR signed by OPTIGA */
            if (tesaiot_optiga_generate_csr(
                    TESAIOT_OPTIGA_OID_SESSION_KEY,
                    g_workflow_context->public_key_der,
                    g_workflow_context->pubkey_len,
                    subject,
                    g_workflow_context->csr_pem,
                    sizeof(g_workflow_context->csr_pem)))
            {
                printf("[CSR-SM] ✓ CSR generated (%u bytes)\n", (unsigned int)strlen(g_workflow_context->csr_pem));
                current_state = TESAIOT_CSR_STATE_CONNECT_MQTT;
            }
            else
            {
                printf("[CSR-SM] ✗ CSR generation failed!\n");
                current_state = TESAIOT_CSR_STATE_ERROR;
            }
            break;

        case TESAIOT_CSR_STATE_CONNECT_MQTT:
            printf("[CSR-SM] State: CONNECT_MQTT\n");

            /* Connect to MQTT broker using Factory Certificate */
            if (tesaiot_mqtt_connect())
            {
                printf("[CSR-SM] ✓ MQTT connected\n");
                current_state = TESAIOT_CSR_STATE_PUBLISH_CSR;
            }
            else
            {
                printf("[CSR-SM] ✗ MQTT connection failed!\n");
                current_state = TESAIOT_CSR_STATE_ERROR;
            }
            break;

        case TESAIOT_CSR_STATE_PUBLISH_CSR:
            printf("[CSR-SM] State: PUBLISH_CSR\n");
            fflush(stdout);

            /* Publish CSR to TESAIoT platform */
            printf("[CSR-SM] DEBUG: About to call publish_csr()...\n");
            fflush(stdout);

            int publish_result = publish_csr(
                (uint8_t *)g_workflow_context->csr_pem,
                strlen(g_workflow_context->csr_pem),
                TESAIOT_OPTIGA_OID_SESSION_KEY,  /* target_oid: 0xE0F1 */
                0xE0E9,                           /* trust_anchor_oid */
                1                                 /* payload_version */
            );

            printf("[CSR-SM] DEBUG: publish_csr() returned %d\n", publish_result);
            fflush(stdout);

            if (publish_result == 0)
            {
                printf("[CSR-SM] ✓ CSR published successfully (queued to Publisher task)\n");
                fflush(stdout);

                /* No delay needed - WAIT_CERTIFICATE state keeps stack frame alive
                 * by looping forever (return 0), so Publisher task can safely
                 * access CSR buffer for async MQTT publish.
                 */
                current_state = TESAIOT_CSR_STATE_WAIT_CERTIFICATE;
            }
            else
            {
                printf("[CSR-SM] ✗ CSR publish failed (error %d)\n", publish_result);
                current_state = TESAIOT_CSR_STATE_ERROR;
            }
            break;

        case TESAIOT_CSR_STATE_WAIT_CERTIFICATE:
            printf("[CSR-SM] State: WAIT_CERTIFICATE\n");
            fflush(stdout);

            /* Wait for subscriber to create event group (created in subscriber_task entry) */
            for (uint32_t i = 0; i < 100 && data_received_event_group == NULL; ++i)
            {
                vTaskDelay(pdMS_TO_TICKS(100U));
            }

            if (data_received_event_group == NULL)
            {
                printf("[CSR-SM] ✗ Event group not created\n");
                fflush(stdout);
                current_state = TESAIOT_CSR_STATE_ERROR;
                break;
            }

            printf("[CSR-SM] Waiting for certificate from TESAIoT platform...\n");
            fflush(stdout);

            /* Wait for Protected Update completion via event bit
             * Server now responds with cmd=3 (Protected Update bundle) instead of cmd=2 (direct cert)
             * Subscriber sets PROTECTED_UPDATE_COMPLETE_BIT after STEP 4.2 success
             * Timeout: 5 minutes (300 seconds) - same as Protected Update workflow
             */
            if (data_received_event_group != NULL)
            {
                EventBits_t bits = xEventGroupWaitBits(
                    data_received_event_group,
                    PROTECTED_UPDATE_COMPLETE_BIT,
                    pdTRUE,   /* Clear bit after reading */
                    pdFALSE,  /* Don't wait for all bits */
                    pdMS_TO_TICKS(300000));

                if (bits & PROTECTED_UPDATE_COMPLETE_BIT)
                {
                    /* Certificate received via Protected Update - workflow complete!
                     * Jump directly to completion handling instead of relying on state machine loop
                     * to avoid race conditions with background tasks */
                    printf("[CSR-SM] ✓ Certificate received and written to OPTIGA Trust M!\n");
                    printf("[CSR-SM] Certificate installed to OID 0xE0E1 via Protected Update\n");
                    printf("[CSR-SM] State: DONE - Workflow completed!\n");
                    fflush(stdout);

                    /* Brief delay to let background tasks finish printing */
                    vTaskDelay(pdMS_TO_TICKS(300));

                    /* Success - return 1 to trigger completion in main loop */
                    return 1;
                }
                else
                {
                    printf("[CSR-SM] ✗ Timeout waiting for certificate (5 minutes)!\n");
                    fflush(stdout);
                    current_state = TESAIOT_CSR_STATE_ERROR;
                }
            }
            else
            {
                printf("[CSR-SM] ✗ data_received_event_group is NULL!\n");
                fflush(stdout);
                current_state = TESAIOT_CSR_STATE_ERROR;
            }
            break;

        case TESAIOT_CSR_STATE_VALIDATE_CERT:
            printf("[CSR-SM] State: VALIDATE_CERT\n");
            printf("[CSR-SM] TODO: Implement certificate validation\n");
            current_state = TESAIOT_CSR_STATE_WRITE_TO_OPTIGA;
            break;

        case TESAIOT_CSR_STATE_WRITE_TO_OPTIGA:
            printf("[CSR-SM] State: WRITE_TO_OPTIGA\n");
            printf("[CSR-SM] TODO: Implement OPTIGA certificate write\n");
            current_state = TESAIOT_CSR_STATE_DONE;
            break;

        case TESAIOT_CSR_STATE_DONE:
            printf("[CSR-SM] State: DONE - Workflow completed!\n");
            return 1;  /* Workflow complete */

        case TESAIOT_CSR_STATE_ERROR:
            printf("[CSR-SM] State: ERROR - Workflow failed!\n");
            return -2;  /* Workflow error */

        default:
            printf("[CSR-SM] ERROR: Unknown state %d\n", current_state);
            current_state = TESAIOT_CSR_STATE_ERROR;
            return -3;  /* Invalid state */
    }

    return 0;  /* Continue */
}

/*----------------------------------------------------------------------------
 * Legacy Wrapper Function
 *---------------------------------------------------------------------------*/

/**
 * Run complete CSR workflow (legacy wrapper for backward compatibility)
 */
int tesaiot_run_csr_workflow(void)
{
    char factory_uid[65];
    int ret;
    int iterations = 0;
    const int MAX_ITERATIONS = 100;  /* Safety limit */

    /* Stop existing MQTT session if running (prevents publish conflicts) */
    if (mqtt_is_connected())
    {
        printf("[CSR-SM] Existing MQTT session detected - stopping it first...\n");
        fflush(stdout);
        if (!mqtt_request_stop())
        {
            printf("[CSR-SM] WARNING: Failed to stop existing MQTT session\n");
            /* Continue anyway - may still work */
        }
        else
        {
            printf("[CSR-SM] ✓ Existing MQTT session stopped\n");
        }
        /* Give time for resources to be released */
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    /* Set CSR workflow flag to enable synchronization with subscriber_task */
    g_csr_workflow_active = true;

    /* CRITICAL: Force Factory Certificate mode for CSR workflow!
     *
     * The CSR workflow generates a NEW keypair at OID 0xE0F1.
     * Any existing Device Certificate at 0xE0E1 was issued for the OLD keypair.
     * Using 0xE0E1 + new 0xE0F1 = TLS handshake FAIL (key mismatch).
     *
     * Solution: Always use Factory Certificate (0xE0E0 + 0xE0F0) during CSR workflow.
     * After new certificate is received, it will match the new keypair at 0xE0F1.
     */
    tesaiot_set_force_factory_cert(true);

    /* CRITICAL: Allocate context on STACK (700 bytes)
     * This keeps RAM free during MQTT connection TLS handshake
     * Following old implementation pattern from backup code
     */
    tesaiot_csr_context_t stack_context;
    memset(&stack_context, 0, sizeof(tesaiot_csr_context_t));

    /* Set global pointer to stack-allocated context */
    g_workflow_context = &stack_context;

    printf("[CSR-SM] Starting CSR workflow...\n");

    /* Step 1: Read factory UID */
    if (!tesaiot_read_factory_uid(factory_uid, sizeof(factory_uid)))
    {
        printf("[CSR-SM] ✗ Failed to read factory UID!\n");
        g_csr_workflow_active = false;  /* Clear flag on error */
        g_workflow_context = NULL;
        return -1;
    }

    printf("[CSR-SM] Device UID: %s\n", factory_uid);

    /* Step 2: Initialize workflow */
    tesaiot_csr_workflow_config_t config = {
        .device_id = factory_uid
    };

    ret = tesaiot_csr_workflow_init(&config);
    if (ret != 0)
    {
        printf("[CSR-SM] ✗ Initialization failed! (error %d)\n", ret);
        g_csr_workflow_active = false;  /* Clear flag on error */
        g_workflow_context = NULL;
        return -2;
    }

    /* Step 3: Start workflow */
    ret = tesaiot_csr_workflow_start();
    if (ret != 0)
    {
        printf("[CSR-SM] ✗ Start failed! (error %d)\n", ret);
        g_csr_workflow_active = false;  /* Clear flag on error */
        g_workflow_context = NULL;
        return -3;
    }

    /* Step 4: Run workflow until completion */
    while (iterations < MAX_ITERATIONS)
    {
        ret = tesaiot_csr_workflow_run();
        iterations++;

        if (ret == 1)
        {
            /* Workflow complete */
            printf("[CSR-SM] Workflow completed successfully in %d iterations!\n", iterations);
            fflush(stdout);

            /* Brief delay to let background tasks (MQTT Publisher) finish printing */
            vTaskDelay(pdMS_TO_TICKS(500));

            g_workflow_context = NULL;

            /* User-controlled reset to use new Device Certificate on next boot */
            printf("\n");
            printf("=============================================================\n");
            printf("   CSR WORKFLOW COMPLETED - NEW CERTIFICATE INSTALLED!\n");
            printf("=============================================================\n");
            printf("Device Certificate written to OID 0xE0E1\n");
            printf("After reset, the new Device Certificate will be used.\n");
            printf("=============================================================\n\n");
            fflush(stdout);

            /* Stop MQTT session to prevent background output during reset prompt */
            if (mqtt_is_connected())
            {
                printf("[CSR-SM] Stopping MQTT session...\n");
                fflush(stdout);
                mqtt_request_stop();
                vTaskDelay(pdMS_TO_TICKS(1000));  /* Wait for cleanup */
            }

            printf("\nPlease press RESET button to reboot the board...\n");
            fflush(stdout);

            /* Wait indefinitely for user to press hardware RESET button */
            while (1)
            {
                vTaskDelay(pdMS_TO_TICKS(1000));
            }

            /* Code below will never execute, but kept for safety */
            return 0;
        }
        else if (ret < 0)
        {
            /* Error occurred */
            printf("[CSR-SM] ✗ Workflow failed at iteration %d (error %d)\n", iterations, ret);
            g_csr_workflow_active = false;  /* Clear flag on error */
            g_workflow_context = NULL;
            return ret;
        }

        /* ret == 0 means continue - small delay between iterations */
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    /* Safety limit reached */
    printf("[CSR-SM] ✗ Workflow did not complete in %d iterations!\n", MAX_ITERATIONS);
    g_csr_workflow_active = false;  /* Clear flag on error */
    g_workflow_context = NULL;
    return -4;
}
