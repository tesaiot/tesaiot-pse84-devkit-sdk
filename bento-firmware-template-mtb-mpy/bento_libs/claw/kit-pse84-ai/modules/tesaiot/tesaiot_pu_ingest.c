/*******************************************************************************
* File: tesaiot_pu_ingest.c
*
* Protected Update bundle ingest — carried over from
* official_pse84_trustm_mTLS_tesaiot/proj_cm33_ns/subscriber_task.c, where this
* logic lives inside the subscriber task's UPDATE_PROTECTED_UPDATE_BUNDLE case.
* This firmware has its own, much smaller subscriber task, so the case body is
* carried here as a function and the surrounding pieces it depends on —
* the JSON parse context and callback, the certificate-installation ACK
* publishers and the timestamp helper — come with it.
*
* The body is verbatim from the reference except for two mechanical
* adaptations, made so the code itself did not have to change:
*   - the reference reads the inbound message from its queue element
*     `subscriber_q_data`; here a local struct of the same name mirrors the
*     function arguments, with need_free=false so the reference's conditional
*     frees become no-ops (the caller owns the payload buffer);
*   - the reference's case-level `break;` / `continue;` end processing of one
*     message; at function level they become `goto pu_done;`.
*
* What it does (reference STEP numbering preserved in the body):
*   parse single_bundle JSON -> validate correlation id -> decode the platform
*   signing certificate -> write + read back the trust anchor (0xE0E8) ->
*   decode manifest + fragments into the optiga_trust_helpers globals -> set
*   the MUD metadata on the target (0xE0E1) -> protected_update() -> ACK.
*******************************************************************************/
#include "cybsp.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "task.h"
#include "event_groups.h"
#include "semphr.h"

#include "cy_mqtt_api.h"
#include "cy_json_parser.h"
#include "mbedtls/base64.h"

#include "mqtt_task.h"           /* mqtt_connection */
#include "mqtt_client_config.h"  /* identity accessors + topic formats */
#include "tesaiot_optiga.h"      /* trustm_update_state, TRUSTM_STATE_*, correlation */
#include "optiga_trust_helpers.h"
#include "optiga_util.h"
#include "optiga_crypt.h"
#include "ipc_communication.h"   /* TESAIOT_PU_* progress events, shared with CM33's UI handler */

#ifndef LABEL_SUBSCRIBER
#define LABEL_SUBSCRIBER "[PU-Ingest]"
#endif
#ifndef TESAIOT_DEBUG_SUBSCRIBER_ENABLED
#define TESAIOT_DEBUG_SUBSCRIBER_ENABLED 1
#endif
#ifndef TESAIOT_DEBUG_VERBOSE_ENABLED
#define TESAIOT_DEBUG_VERBOSE_ENABLED 1
#endif

/* Event bits — values from the reference subscriber_task.h. The carried-over
 * workflow files wait on the same bits, so the values must match. */
#ifndef MANIFEST_RECEIVED_BIT
#define MANIFEST_RECEIVED_BIT           (1 << 0)
#define FRAGMENT_RECEIVED_BIT           (1 << 1)
#define PROTECTED_UPDATE_COMPLETE_BIT   (1 << 2)
#endif

/* Defined in the reference subscriber task; carried here with the logic that
 * uses them. The workflow files reference them as externs. */
EventGroupHandle_t data_received_event_group = NULL;
volatile bool g_protected_update_active = false;
volatile bool g_protected_update_just_completed = false;

/*
 * How many times this firmware has finished handling something the platform
 * sent — a Protected Update bundle or a certificate — successfully or not.
 *
 * A waiter cannot ask a boolean "did MY request finish". The bool is global,
 * has several writers and several clearers, and it is equally true for a run
 * that finished a minute ago; every attempt to make it answer that question
 * produced a screen that reported another run's verdict as its own. A counter
 * does answer it: read it before publishing, and any change means something
 * completed after you asked. Nothing has to clear it, so nothing can race to.
 */
volatile uint32_t g_optiga_ingest_events = 0U;

/* Optional progress hook, so a UI can report what the chip did rather than only
 * what this firmware asked for. Weak: nothing has to provide it. */
void tesaiot_pu_progress(int event) __attribute__((weak));

/* Bundle payload staging — the optiga_trust_helpers globals the reference
 * subscriber feeds and protected_update() consumes. */
extern uint8_t *manifest_ecc_key;
extern size_t   manifest_ecc_key_length;
extern uint8_t *ecc_key_final_fragment_array;
extern size_t   ecc_key_final_fragment_array_length;
extern uint8_t *pubkey;
extern size_t   pubkey_length;
extern uint8_t *dev_cert_raw;
extern size_t   dev_cert_raw_len;
extern int optiga_verify_cert_key_pair(uint16_t cert_oid, uint16_t key_oid);

/* The enrolled pair, per this project's pre-provisioning plan: ECC Key 2 and
 * the certificate slot that belongs with it. DEVICE_CERTIFICATE_OID comes from
 * optiga_trust_helpers.h; the key has no constant of its own yet. */
#ifndef DEVICE_KEY_OID
#define DEVICE_KEY_OID (0xE0F1U)
#endif
extern uint8_t *external_trust_anchor;
extern size_t   external_trust_anchor_len;
extern volatile optiga_lib_status_t optiga_lib_status;

extern optiga_util_t *optiga_manager_acquire(void);
extern void optiga_manager_release(void);

/* Counted hold that keeps CM55 touch polling off SCB5 while the host talks to
 * the secure element — see tesaiot_optiga_manager.c. */
extern void optiga_manager_touch_hold(void);
extern void optiga_manager_touch_hold_reason(const char *reason);
extern void optiga_manager_touch_release(void);

//! [hsm_trustm_requested_oids_weak_fallback]
/* Which object this bundle is for — see the note on the definition. Weak so a
 * build without the MQTT request path still links. */
extern uint16_t trustm_requested_target_oid(void) __attribute__((weak));

extern uint16_t trustm_requested_anchor_oid(void) __attribute__((weak));

/* The anchor the last request named. Everything below used to bake 0xE0E8 in,
 * including the target's Change access condition — so asking for a different
 * anchor was accepted, stored, and then quietly ignored, and the chip would
 * verify the manifest against an object the platform had not signed for. That
 * is 0x800F with no diagnostic, the same failure the target OID caused before
 * it was made to follow the request. */
static uint16_t pu_anchor_oid(void)
{
    return (trustm_requested_anchor_oid != NULL) ? trustm_requested_anchor_oid()
         : 0xE0E8U;
}

static uint16_t pu_target_oid(void)
{
    return (trustm_requested_target_oid != NULL)
         ? trustm_requested_target_oid()
         : 0xE0E1U;   /* certificate slot — the platform's default target */
}
//! [hsm_trustm_requested_oids_weak_fallback]

/* The reference bakes the device UUID in at compile time as DEVICE_ID; here it
 * is runtime configuration, resolved through the same accessor the other
 * carried-over sources use (see the note in mqtt_client_config.h). */
#ifndef DEVICE_ID
#define DEVICE_ID tesaiot_mqtt_username()
#endif




/* Async-write completion machinery, carried with the callback that drives it
 * (reference lines 152-153). */
static volatile optiga_lib_status_t trust_m_async_status = OPTIGA_LIB_BUSY;
static SemaphoreHandle_t trust_m_write_semaphore = NULL;

/* ---- carried verbatim: async write callback (reference lines 193-203) ---- */
static void optiga_trust_m_callback(void *context, optiga_lib_status_t return_status)
{
 (void)context;
 trust_m_async_status = return_status;

 if (trust_m_write_semaphore != NULL) {
 BaseType_t xHigherPriorityTaskWoken = pdFALSE;
 xSemaphoreGiveFromISR(trust_m_write_semaphore, &xHigherPriorityTaskWoken);
 portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
 }
}

/* ---- carried verbatim: timestamp helper (reference lines 228-253) ---- */
static void get_iso8601_timestamp(char *buffer, size_t buffer_len)
{
	if (!buffer || buffer_len < 32) {
		return;
	}

	// Get current tick count for milliseconds
	TickType_t ticks = xTaskGetTickCount();
	uint32_t ms = (ticks % 1000);

	// Get current time from system
	// Note: This requires NTP to be synchronized
	time_t now = time(NULL);
	struct tm *tm_info = gmtime(&now);

	// Format: 2026-02-03T15:05:30.123Z
	snprintf(buffer, buffer_len,
	         "%04d-%02d-%02dT%02d:%02d:%02d.%03luZ",
	         tm_info->tm_year + 1900,
	         tm_info->tm_mon + 1,
	         tm_info->tm_mday,
	         tm_info->tm_hour,
	         tm_info->tm_min,
	         tm_info->tm_sec,
	         (unsigned long)ms);
}

/* ---- carried verbatim: certificate read + base64 encode (reference lines 264-340) ---- */
static cy_rslt_t read_and_encode_certificate(
	uint16_t oid,
	char *out_b64,
	size_t out_b64_len,
	size_t *out_der_len)
{
	// Validate inputs
	if (!out_b64 || out_b64_len < 2048 || !out_der_len) {
		printf("%s ERROR: Invalid certificate encode parameters\n", LABEL_SUBSCRIBER);
		return CY_RSLT_TYPE_ERROR;
	}

	// Allocate buffer for DER certificate (max 1800 bytes)
	uint8_t *cert_der = (uint8_t*)pvPortMalloc(2048);
	if (!cert_der) {
		printf("%s ERROR: Failed to allocate certificate buffer\n", LABEL_SUBSCRIBER);
		return CY_RSLT_TYPE_ERROR;
	}

	uint16_t cert_len = 2048;

	//! [hsm_optiga_manager_acquire_release]
	/* ...context: inside the certificate read helper ... */
	// Acquire OPTIGA instance (thread-safe)
	optiga_util_t *me_util = optiga_manager_acquire();
	if (!me_util) {
		printf("%s ERROR: OPTIGA instance not available\n", LABEL_SUBSCRIBER);
		vPortFree(cert_der);
		return CY_RSLT_TYPE_ERROR;
	}

	// Perform async read
	optiga_lib_status = OPTIGA_LIB_BUSY;
	optiga_lib_status_t status = optiga_util_read_data(me_util, oid, 0, cert_der, &cert_len);

	if (OPTIGA_LIB_SUCCESS != status) {
		printf("%s ERROR: OPTIGA read failed (0x%04X)\n", LABEL_SUBSCRIBER, status);
		optiga_manager_release();
		vPortFree(cert_der);
		return CY_RSLT_TYPE_ERROR;
	}

	// Wait for read completion (max 2 seconds)
	TickType_t start = xTaskGetTickCount();
	TickType_t timeout = pdMS_TO_TICKS(2000);
	while (optiga_lib_status == OPTIGA_LIB_BUSY && (xTaskGetTickCount() - start) < timeout) {
		vTaskDelay(pdMS_TO_TICKS(100));
	}

	if (optiga_lib_status != OPTIGA_LIB_SUCCESS) {
		printf("%s ERROR: OPTIGA read timeout/failed (0x%04X)\n", LABEL_SUBSCRIBER, optiga_lib_status);
		optiga_manager_release();
		vPortFree(cert_der);
		return CY_RSLT_TYPE_ERROR;
	}

	optiga_manager_release();
	//! [hsm_optiga_manager_acquire_release]
	*out_der_len = cert_len;

	// Base64 encode certificate
	size_t olen = 0;
	int ret = mbedtls_base64_encode(
		(unsigned char*)out_b64,
		out_b64_len,
		&olen,
		cert_der,
		cert_len
	);

	vPortFree(cert_der);

	if (ret != 0) {
		printf("%s ERROR: Base64 encode failed (%d)\n", LABEL_SUBSCRIBER, ret);
		return CY_RSLT_TYPE_ERROR;
	}

	out_b64[olen] = '\0';  // Null-terminate
	return CY_RSLT_SUCCESS;
}

/* ---- carried verbatim: certificate ACK (reference lines 355-501) ---- */
static cy_rslt_t send_certificate_ack(
	const char *correlation_id,
	const char *workflow,
	uint16_t target_oid,
	bool include_cert_data,
	uint32_t installation_duration_ms)
{
	cy_rslt_t result = CY_RSLT_SUCCESS;

	// Validate inputs
	if (!correlation_id || !workflow) {
		printf("%s ERROR: Invalid ACK parameters (corr_id=%p, workflow=%p)\n",
		       LABEL_SUBSCRIBER, (void*)correlation_id, (void*)workflow);
		return CY_RSLT_TYPE_ERROR;
	}

	// Get ISO8601 timestamp
	char timestamp[32];
	get_iso8601_timestamp(timestamp, sizeof(timestamp));

	// Allocate payload buffer dynamically based on phase
	// Phase 1: 512 bytes (minimal)
	// Phase 2: 4096 bytes (with certificate base64)
	size_t buffer_size = include_cert_data ? 4096 : 512;
	char *ack_payload = (char*)pvPortMalloc(buffer_size);
	if (!ack_payload) {
		printf("%s ERROR: Failed to allocate ACK payload buffer\n", LABEL_SUBSCRIBER);
		return CY_RSLT_TYPE_ERROR;
	}

	int payload_len = 0;

	if (!include_cert_data) {
		// Phase 1: Minimal ACK payload (~512 bytes)
		payload_len = snprintf(ack_payload, buffer_size,
		    "{"
		    "\"event\":\"certificate_installed\","
		    "\"timestamp\":\"%s\","
		    "\"oid\":\"0x%04X\","
		    "\"status\":\"success\","
		    "\"correlation_id\":\"%s\","
		    "\"workflow\":\"%s\","
		    "\"installation_duration_ms\":%lu"
		    "}",
		    timestamp,
		    target_oid,
		    correlation_id,
		    workflow,
		    (unsigned long)installation_duration_ms);
	} else {
		// Phase 2: Full data with certificate
		// Read and encode certificate
		char *cert_b64 = (char*)pvPortMalloc(3072);  // Base64 buffer (2048 DER → ~3KB base64)
		if (!cert_b64) {
			printf("%s ERROR: Failed to allocate certificate buffer\n", LABEL_SUBSCRIBER);
			vPortFree(ack_payload);
			return CY_RSLT_TYPE_ERROR;
		}

		size_t cert_der_len = 0;
		cy_rslt_t cert_result = read_and_encode_certificate(target_oid, cert_b64, 3072, &cert_der_len);

		if (cert_result == CY_RSLT_SUCCESS) {
			// Certificate read succeeded - include in payload
			payload_len = snprintf(ack_payload, buffer_size,
			    "{"
			    "\"event\":\"certificate_installed\","
			    "\"timestamp\":\"%s\","
			    "\"oid\":\"0x%04X\","
			    "\"status\":\"success\","
			    "\"correlation_id\":\"%s\","
			    "\"workflow\":\"%s\","
			    "\"certificate_der_b64\":\"%s\","
			    "\"optiga_verification\":\"passed\","
			    "\"installation_duration_ms\":%lu"
			    "}",
			    timestamp,
			    target_oid,
			    correlation_id,
			    workflow,
			    cert_b64,
			    (unsigned long)installation_duration_ms);
		} else {
			// Certificate read failed - send minimal payload
			printf("%s WARNING: Failed to read certificate, sending minimal ACK\n", LABEL_SUBSCRIBER);
			payload_len = snprintf(ack_payload, buffer_size,
			    "{"
			    "\"event\":\"certificate_installed\","
			    "\"timestamp\":\"%s\","
			    "\"oid\":\"0x%04X\","
			    "\"status\":\"success\","
			    "\"correlation_id\":\"%s\","
			    "\"workflow\":\"%s\","
			    "\"installation_duration_ms\":%lu"
			    "}",
			    timestamp,
			    target_oid,
			    correlation_id,
			    workflow,
			    (unsigned long)installation_duration_ms);
		}

		vPortFree(cert_b64);
	}

	if (payload_len < 0 || payload_len >= (int)buffer_size) {
		printf("%s ERROR: ACK payload buffer too small (needed=%d, have=%lu)\n",
		       LABEL_SUBSCRIBER, payload_len, (unsigned long)buffer_size);
		vPortFree(ack_payload);
		return CY_RSLT_TYPE_ERROR;
	}

	printf("%s [ACK] Sending certificate installation acknowledgment...\n", LABEL_SUBSCRIBER);
	if (include_cert_data) {
		printf("%s [ACK] Payload (%d bytes, with certificate)\n", LABEL_SUBSCRIBER, payload_len);
	} else {
		printf("%s [ACK] Payload (%d bytes): %s\n", LABEL_SUBSCRIBER, payload_len, ack_payload);
	}
	fflush(stdout);

	// Build MQTT publish info
	/* Identity seam: DEVICE_ID is runtime configuration here, so the topic
	 * cannot be a compile-time literal as it is in the reference. */
	char ack_topic[128];
	snprintf(ack_topic, sizeof(ack_topic), "device/%s/telemetry/system", DEVICE_ID);
	cy_mqtt_publish_info_t pub_info = {
		.qos = CY_MQTT_QOS0,
		.topic = ack_topic,
		.topic_len = strlen(ack_topic),
		.payload = ack_payload,
		.payload_len = (size_t)payload_len,
		.retain = false,
		.dup = false
	};

	// Publish ACK message
	result = cy_mqtt_publish(mqtt_connection, &pub_info);

	if (result == CY_RSLT_SUCCESS) {
		printf("%s [ACK] Certificate ACK published successfully\n", LABEL_SUBSCRIBER);
	} else {
		printf("%s [ACK] WARNING: Failed to publish certificate ACK: 0x%08lX\n",
		       LABEL_SUBSCRIBER, (unsigned long)result);
		printf("%s [ACK] Note: ACK failure is non-fatal - device continues normally\n",
		       LABEL_SUBSCRIBER);
	}

	fflush(stdout);
	vPortFree(ack_payload);
	return result;
}

/* ---- carried verbatim: certificate ACK (failure) (reference lines 512-580) ---- */
static cy_rslt_t send_certificate_ack_failed(
	const char *correlation_id,
	const char *workflow,
	const char *error_code,
	const char *error_message)
{
	cy_rslt_t result = CY_RSLT_SUCCESS;

	// Validate inputs
	if (!correlation_id || !workflow || !error_code || !error_message) {
		printf("%s ERROR: Invalid failed ACK parameters\n", LABEL_SUBSCRIBER);
		return CY_RSLT_TYPE_ERROR;
	}

	// Get ISO8601 timestamp
	char timestamp[32];
	get_iso8601_timestamp(timestamp, sizeof(timestamp));

	// Build failed ACK payload
	char ack_payload[512];
	int payload_len = snprintf(ack_payload, sizeof(ack_payload),
	    "{"
	    "\"event\":\"certificate_install_failed\","
	    "\"timestamp\":\"%s\","
	    "\"status\":\"failed\","
	    "\"correlation_id\":\"%s\","
	    "\"workflow\":\"%s\","
	    "\"error_code\":\"%s\","
	    "\"error_message\":\"%s\""
	    "}",
	    timestamp,
	    correlation_id,
	    workflow,
	    error_code,
	    error_message);

	if (payload_len < 0 || payload_len >= (int)sizeof(ack_payload)) {
		printf("%s ERROR: Failed ACK payload buffer too small\n", LABEL_SUBSCRIBER);
		return CY_RSLT_TYPE_ERROR;
	}

	printf("%s [ACK] Sending failed certificate installation acknowledgment...\n", LABEL_SUBSCRIBER);
	printf("%s [ACK] Error: %s (%s)\n", LABEL_SUBSCRIBER, error_message, error_code);
	fflush(stdout);

	// Build MQTT publish info
	/* Identity seam: DEVICE_ID is runtime configuration here, so the topic
	 * cannot be a compile-time literal as it is in the reference. */
	char ack_topic[128];
	snprintf(ack_topic, sizeof(ack_topic), "device/%s/telemetry/system", DEVICE_ID);
	cy_mqtt_publish_info_t pub_info = {
		.qos = CY_MQTT_QOS0,
		.topic = ack_topic,
		.topic_len = strlen(ack_topic),
		.payload = ack_payload,
		.payload_len = (size_t)payload_len,
		.retain = false,
		.dup = false
	};

	// Publish failed ACK message
	result = cy_mqtt_publish(mqtt_connection, &pub_info);

	if (result == CY_RSLT_SUCCESS) {
		printf("%s [ACK] Failed certificate ACK published successfully\n", LABEL_SUBSCRIBER);
	} else {
		printf("%s [ACK] WARNING: Failed to publish failed ACK: 0x%08lX\n",
		       LABEL_SUBSCRIBER, (unsigned long)result);
	}

	fflush(stdout);
	return result;
}

/* ---- carried verbatim: JSON context + callback (reference lines 669-726) ---- */
typedef struct {
	char *signing_cert_b64;      /* X.509 Certificate DER (base64 encoded) - ~580 bytes decoded */
	uint16_t signing_cert_len;   /* Length of base64 string (~773 chars) */
	char *manifest_b64;
	uint16_t manifest_len;
	char *fragment_0_b64;
	uint16_t fragment_0_len;
	char *fragment_1_b64;
	uint16_t fragment_1_len;
	char *fragment_2_b64;
	uint16_t fragment_2_len;
	char *correlation_id;        /* UUID for request-response matching */
	uint16_t correlation_id_len; /* Length of correlation_id string */
	uint8_t fragment_count;
} protected_update_ctx_t;

/* JSON callback function for Protected Update bundle parsing */
static cy_rslt_t protected_update_json_callback(cy_JSON_object_t* json_obj, void *arg)
{
	protected_update_ctx_t *ctx = (protected_update_ctx_t *)arg;
	char *key = json_obj->object_string;
	uint8_t key_len = json_obj->object_string_length;
	char *val = json_obj->value;
	uint16_t val_len = json_obj->value_length;

	// Parse string fields (signing_certificate, manifest, fragment_0, correlation_id)
	// Field names aligned with TESAIoT Platform Protected Update API v2.11
	if (json_obj->value_type == JSON_STRING_TYPE) {
		// signing_certificate: X.509 Certificate DER (~580 bytes, base64 encoded ~773 chars)
		if (key_len == 19 && strncmp(key, "signing_certificate", 19) == 0) {
			ctx->signing_cert_b64 = val;
			ctx->signing_cert_len = val_len;
		}
		// manifest: COSE_Sign1 protected update manifest (base64 encoded)
		else if (key_len == 8 && strncmp(key, "manifest", 8) == 0) {
			ctx->manifest_b64 = val;
			ctx->manifest_len = val_len;
		}
		// fragment_0: Encrypted certificate payload (base64 encoded)
		else if (key_len == 10 && strncmp(key, "fragment_0", 10) == 0) {
			ctx->fragment_0_b64 = val;
			ctx->fragment_0_len = val_len;
		}
		// correlation_id: UUID for request-response matching
		else if (key_len == 14 && strncmp(key, "correlation_id", 14) == 0) {
			ctx->correlation_id = val;
			ctx->correlation_id_len = val_len;
		}
	}
	// Parse number fields (fragment_count)
	else if (json_obj->value_type == JSON_NUMBER_TYPE) {
		if (key_len == 14 && strncmp(key, "fragment_count", 14) == 0) {
			ctx->fragment_count = (uint8_t)json_obj->intval;
		}
	}

	return CY_RSLT_SUCCESS;
}

/*******************************************************************************
* Entry point — called from this firmware's subscriber task when a message
* arrives on device/{device_id}/commands/protected_update.
*******************************************************************************/
/*
 * Create the event group the provisioning paths wait on, at subscriber start.
 *
 * It used to be created here only when a bundle arrived, on the reasoning that
 * nothing needs it before then. That is not true of the Enrol path: it refuses
 * to run while this handle is NULL - see ipc_hsm_handler.c - and a bundle can
 * only arrive after a CSR has been published, which is the thing Enrol was
 * about to do. On a board that has just been power cycled the two wait on each
 * other and the button never works, for the whole of that boot.
 *
 * Observed 2026-08-18: after a power cycle, Enrol reported "the connection came
 * up but nothing is listening for the platform's reply", the board published
 * nothing, and the platform's bridge confirmed no request ever reached it. One
 * tesaiot.protected_update(csr=True) from the REPL - a path that does not pass
 * that gate - created the handle as a side effect, and the button worked again
 * immediately. That is the whole of "it worked this morning": earlier console
 * runs had already created it.
 *
 * The comment on the gate called this a startup race worth waiting 10 s for. It
 * is not a race. Nothing was ever going to create the handle on its own.
 *
 * Creating it when the subscriber starts costs one event group per boot and
 * removes the dependency entirely. tesaiot_pu_ingest_bundle() still creates it
 * if it is somehow still NULL, so this is additive.
 */
void tesaiot_pu_ingest_init(void)
{
    if (data_received_event_group == NULL) {
        data_received_event_group = xEventGroupCreate();
    }
}

void tesaiot_pu_ingest_bundle(char *bundle_data, uint16_t bundle_size)
{
 /* Did this call actually handle a bundle meant for THIS request?
  *
  * pu_done is the target of nineteen failure gotos, and two of them are the
  * ordinary case rather than an error: a retained bundle that arrives on every
  * connect, and one whose correlation id belongs to somebody else. Announcing a
  * completion for those woke the waiting screen inside 100 ms, which then ran a
  * pair check against the certificate that was already there and reported
  * "the certificate does not belong to this chip's key" - a confident verdict
  * about a run in which nothing had been installed, while the real bundle was
  * still on its way. */
 bool pu_handled_for_us = false;
    /* Mirror of the reference queue element, so the carried body compiles
     * unchanged. need_free=false: the caller owns and frees the buffer. */
    struct {
        char *data;
        uint16_t data_size;
        bool need_free;
    } subscriber_q_data = { bundle_data, bundle_size, false };

    if (data_received_event_group == NULL) {
        data_received_event_group = xEventGroupCreate();
    }

     //! [hsm_optiga_touch_hold_whole_conversation]
     /* ...context: inside the Protected Update bundle ingest ... */
    /* Keep CM55 touch off the bus for the whole ingest.
     *
     * The secure element and the touch controller share SCB5 on this board.
     * The carried-over reference has no touchscreen and so no notion of this;
     * dropped in here unchanged, its longest writes lose the bus mid-transfer.
     * Observed 2026-08-06 on correlation bento-pu-1: the 8-byte trust anchor
     * metadata write succeeded, and the 580-byte certificate write that follows
     * failed with 0x0102 — OPTIGA_COMMS_ERROR, the transport layer, not the
     * chip refusing anything — after 57 seconds of comms retries.
     *
     * Held across the entire function rather than per operation: the ingest is
     * one long conversation with the chip, and releasing between steps would
     * reopen the same window. Counted, so the signature path inside
     * trustm_ecdsa_sign() nests without resuming polling early. Every exit runs
     * through pu_done, which releases it. */
    optiga_manager_touch_hold();
    //! [hsm_optiga_touch_hold_whole_conversation]
 {
#if TESAIOT_DEBUG_SUBSCRIBER_ENABLED
 	#if TESAIOT_DEBUG_VERBOSE_ENABLED
 	printf("%s Processing Protected Update bundle\n", LABEL_SUBSCRIBER);
 	#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
#endif

 	// CRITICAL: Set flag EARLY to block Publisher task creation during ENTIRE workflow
 	// This includes Trust Anchor write, READBACK verification, and Protected Update
 	// Without this, Publisher task may be created during READBACK delay causing interference
 	g_protected_update_active = true;
#if 0  /* TRACE messages disabled to reduce debug output during Protected Update */
 	printf("%s [TRACE-1] Flag set\n", LABEL_SUBSCRIBER);
 	fflush(stdout);
#endif

 	//! [hsm_trustm_update_state_progress]
 	/* ...context: inside the Protected Update bundle ingest ... */
 	// State update: Processing JSON bundle
 	trustm_update_state(TRUSTM_STATE_PROCESSING_JSON_BUNDLE, NULL, NULL);
#if 0  /* TRACE messages disabled */
 	printf("%s [TRACE-2] State updated\n", LABEL_SUBSCRIBER);
 	fflush(stdout);
#endif

 	if (!subscriber_q_data.data) {
 		#if TESAIOT_DEBUG_VERBOSE_ENABLED
 		printf("%s ERROR: NULL bundle data\n", LABEL_SUBSCRIBER);
 		#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 		g_protected_update_active = false;  // Reset flag on early exit
 		trustm_update_state(TRUSTM_STATE_PROTECTED_UPDATE_FAILED, NULL, "NULL bundle data");
 		goto pu_done;
 	}
 	//! [hsm_trustm_update_state_progress]

 	// Copy JSON buffer before parsing to prevent dangling pointers.
 	// Cypress JSON Parser callbacks store pointers into original buffer.
 	// MQTT library may reuse/overwrite buffer for next packet,
 	// so we copy to ensure data remains valid during entire processing.
 	char *json_copy = (char*) pvPortMalloc(subscriber_q_data.data_size + 1);
 	if (!json_copy) {
 		#if TESAIOT_DEBUG_VERBOSE_ENABLED
 		printf("%s ERROR: malloc JSON copy fail\n", LABEL_SUBSCRIBER);
 		#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 		g_protected_update_active = false;  // Reset flag on early exit
 		goto pu_done;
 	}
 	memcpy(json_copy, subscriber_q_data.data, subscriber_q_data.data_size);
 	json_copy[subscriber_q_data.data_size] = '\0'; // Null-terminate
 	__DSB(); __DMB(); __ISB(); // Memory barriers for cache coherency
 	// TRACE: JSON buffer copied successfully
 	printf("%s [TRACE-3] JSON buffer copied (%d bytes) at %p\n", LABEL_SUBSCRIBER, subscriber_q_data.data_size, (void*)json_copy);
 	fflush(stdout);

 	// Initialize context to collect parsed JSON fields
 	protected_update_ctx_t parse_ctx = {0};

 	// Register callback and parse JSON using Cypress JSON Parser
 	printf("%s [TRACE-4] About to parse JSON (size=%d)...\n", LABEL_SUBSCRIBER, subscriber_q_data.data_size);
 	fflush(stdout);

 	cy_JSON_parser_register_callback(protected_update_json_callback, &parse_ctx);

 	printf("%s [TRACE-5] Calling cy_JSON_parser()...\n", LABEL_SUBSCRIBER);
 	fflush(stdout);

 	cy_rslt_t result = cy_JSON_parser(json_copy, subscriber_q_data.data_size);

 	printf("%s [TRACE-6] cy_JSON_parser() returned: 0x%08lX\n", LABEL_SUBSCRIBER, (unsigned long)result);
 	fflush(stdout);

 	if (result != CY_RSLT_SUCCESS) {
 		printf("%s ERROR: JSON parse failed: 0x%08lX\n", LABEL_SUBSCRIBER, (unsigned long)result);
 		fflush(stdout);
 		g_protected_update_active = false;  // Reset flag on early exit
 		vPortFree(json_copy);
 		goto pu_done;
 	}

 	// DEBUG: JSON parse success
 	printf("%s [DEBUG] JSON parse OK (%d bytes)\n", LABEL_SUBSCRIBER, subscriber_q_data.data_size);
 	fflush(stdout);

	// Track start time for installation duration measurement (Phase 2)
	TickType_t pu_start_ticks = xTaskGetTickCount();

 	// Validate required fields
 	if (!parse_ctx.signing_cert_b64 || !parse_ctx.manifest_b64 || !parse_ctx.fragment_0_b64) {
 		printf("%s ERROR: Missing required fields (cert=%p manifest=%p frag0=%p)\n",
 		       LABEL_SUBSCRIBER,
 		       (void*)parse_ctx.signing_cert_b64,
 		       (void*)parse_ctx.manifest_b64,
 		       (void*)parse_ctx.fragment_0_b64);
 		fflush(stdout);
 		g_protected_update_active = false;  // Reset flag on early exit
 		vPortFree(json_copy);
 		goto pu_done;
 	}

 	// DEBUG: Required fields OK
 	printf("%s [DEBUG] Required fields OK\n", LABEL_SUBSCRIBER);
 	fflush(stdout);

 	// ========================================
 	// CORRELATION ID VALIDATION (Prevent Retained Message Race Condition)
 	// ========================================
 	// Problem: MQTT broker may retain old Protected Update responses.
 	// When device subscribes, it receives stale response with wrong correlation_id.
 	// Solution: Validate correlation_id matches current request before processing.
 	//! [hsm_trustm_correlation_replay_defence]
 	/* ...context: inside the Protected Update bundle ingest, after JSON parse ... */
 	const char *expected_corr_id = trustm_current_correlation_id();

 	/* No outstanding request means nothing on this device asked for this. The
 	 * platform retains the last bundle, so one is delivered on every connect —
 	 * and until now every connect applied it. That is a replay: it rewrites the
 	 * target, re-locks its access condition, and burns a step of the chip's
 	 * anti-rollback counter, all without anyone asking. Observed three times on
 	 * 2026-08-07, each one silently redoing the previous run. */
 	if (NULL == expected_corr_id) {
 		printf("%s Ignoring a Protected Update bundle nobody asked for. Retained "
 		       "bundles arrive on every connect; call tesaiot.protected_update() "
 		       "to arm one.\n", LABEL_SUBSCRIBER);
 		fflush(stdout);
 		g_protected_update_active = false;
 		vPortFree(json_copy);
 		goto pu_done;
 	}

 	if (expected_corr_id && parse_ctx.correlation_id) {
 		// Compare correlation_id (case-sensitive, exact match required)
 		bool match = (parse_ctx.correlation_id_len == strlen(expected_corr_id)) &&
 		             (strncmp(parse_ctx.correlation_id, expected_corr_id, parse_ctx.correlation_id_len) == 0);
 		             //! [hsm_trustm_correlation_replay_defence]

 		if (!match) {
 			printf("%s ====================================================================\n", LABEL_SUBSCRIBER);
 			printf("%s WARNING: Correlation ID mismatch - IGNORING STALE MESSAGE\n", LABEL_SUBSCRIBER);
 			printf("%s ====================================================================\n", LABEL_SUBSCRIBER);
 			printf("%s This is likely a retained message from a previous request.\n", LABEL_SUBSCRIBER);
 			printf("%s Expected: %s\n", LABEL_SUBSCRIBER, expected_corr_id);
 			printf("%s Received: %.*s\n", LABEL_SUBSCRIBER, parse_ctx.correlation_id_len, parse_ctx.correlation_id);
 			printf("%s Ignoring this message and waiting for correct response...\n", LABEL_SUBSCRIBER);
 			printf("%s ====================================================================\n", LABEL_SUBSCRIBER);
 			fflush(stdout);

 			g_protected_update_active = false;  // Reset flag - not processing this message
 			vPortFree(json_copy);
 			goto pu_done;  // Skip this message, wait for next one
 		}

 		// Correlation ID matches - safe to process
 		printf("%s Correlation ID matched: %.*s\n", LABEL_SUBSCRIBER,
 		       parse_ctx.correlation_id_len, parse_ctx.correlation_id);
 		fflush(stdout);
 		/* From here the platform has answered THIS request.
 		 *
 		 * Whether the chip then accepts the manifest is a different question, and
 		 * it is the waiter's own pair check that settles it. Setting this only on
 		 * full success meant a bundle the chip refused never woke the screen, which
 		 * sat out its whole sixty seconds and then blamed the platform for a
 		 * chip-side refusal. Set it once, here, where the identity is established. */
 		pu_handled_for_us = true;
 	} else if (expected_corr_id && !parse_ctx.correlation_id) {
 		// Expected correlation_id but didn't receive one - old server?
 		printf("%s WARNING: No correlation_id in response (old server protocol?)\n", LABEL_SUBSCRIBER);
 		/* This arm proceeds through the whole install. Without the flag a
 		 * Protected Update that fully succeeded left the screen waiting out its
 		 * sixty seconds and then blaming the platform for its own success. */
 		pu_handled_for_us = true;
 		printf("%s Proceeding anyway, but this may cause issues with retained messages.\n", LABEL_SUBSCRIBER);
 		fflush(stdout);
 	}

#if TESAIOT_DEBUG_SUBSCRIBER_ENABLED
 	#if TESAIOT_DEBUG_VERBOSE_ENABLED
 	printf("%s Fragment count: %u\n", LABEL_SUBSCRIBER, parse_ctx.fragment_count);
 	#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
#endif

 	// ========================================
 	// STEP 1: Process signing certificate (write to Trust M OID 0xE0E8)
 	// ========================================
 	printf("%s STEP 1: Processing signing certificate...\n", LABEL_SUBSCRIBER);
 	fflush(stdout);

#if 0  /* Signing certificate dump disabled to reduce debug output - causes UART buffer issues */
#if TESAIOT_DEBUG_SUBSCRIBER_ENABLED
 	#if TESAIOT_DEBUG_VERBOSE_ENABLED
 	printf("%s DEBUG: Starting signing certificate decode...\n", LABEL_SUBSCRIBER);
 	printf("%s DEBUG: signing_cert_len=%u\n", LABEL_SUBSCRIBER, parse_ctx.signing_cert_len);
 	#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */

 	// Print ENTIRE signing certificate string to find invalid character
 	#if TESAIOT_DEBUG_VERBOSE_ENABLED
 	printf("%s DEBUG: Full signing certificate string:\n\"", LABEL_SUBSCRIBER);
 	#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 	for (int i = 0; i < parse_ctx.signing_cert_len; i++) {
 		printf("%c", parse_ctx.signing_cert_b64[i]);
 	}
 	printf("\"\n");

 	// Also print as hex dump for precise diagnosis
 	#if TESAIOT_DEBUG_VERBOSE_ENABLED
 	printf("%s DEBUG: Hex dump:\n", LABEL_SUBSCRIBER);
 	#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 	for (int i = 0; i < parse_ctx.signing_cert_len; i++) {
 		printf("%02X ", (uint8_t)parse_ctx.signing_cert_b64[i]);
 		if ((i + 1) % 16 == 0) printf("\n");
 	}
 	printf("\n");
#endif
#endif  /* End signing certificate dump disabled */

 	size_t pubkey_decoded_len = 0;
 	uint8_t *pubkey_der = NULL;
 	int decode_ret = mbedtls_base64_decode(NULL, 0, &pubkey_decoded_len,
 	 (uint8_t*)parse_ctx.signing_cert_b64,
 	 parse_ctx.signing_cert_len);
#if TESAIOT_DEBUG_SUBSCRIBER_ENABLED
 	#if TESAIOT_DEBUG_VERBOSE_ENABLED
 	printf("%s DEBUG: Base64 decode size check: %d, len=%u\n", LABEL_SUBSCRIBER, decode_ret, (unsigned int)pubkey_decoded_len);
 	#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
#endif

 	if (decode_ret == MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
#if TESAIOT_DEBUG_SUBSCRIBER_ENABLED
 		#if TESAIOT_DEBUG_VERBOSE_ENABLED
 		printf("%s DEBUG: Allocating %u bytes for signing certificate...\n", LABEL_SUBSCRIBER, (unsigned int)pubkey_decoded_len);
 		#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
#endif
 		pubkey_der = (uint8_t*) pvPortMalloc(pubkey_decoded_len);
 		if (pubkey_der) {
#if TESAIOT_DEBUG_SUBSCRIBER_ENABLED
 			#if TESAIOT_DEBUG_VERBOSE_ENABLED
 			printf("%s DEBUG: pvPortMalloc OK, decoding...\n", LABEL_SUBSCRIBER);
 			#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
#endif
 			decode_ret = mbedtls_base64_decode(pubkey_der, pubkey_decoded_len,
 			 &pubkey_decoded_len,
 			 (uint8_t*)parse_ctx.signing_cert_b64,
 			 parse_ctx.signing_cert_len);
#if TESAIOT_DEBUG_SUBSCRIBER_ENABLED
 			#if TESAIOT_DEBUG_VERBOSE_ENABLED
 			printf("%s DEBUG: Decode complete: ret=%d, len=%u\n", LABEL_SUBSCRIBER, decode_ret, (unsigned int)pubkey_decoded_len);
 			#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
#endif
 		} else {
#if TESAIOT_DEBUG_SUBSCRIBER_ENABLED
 			#if TESAIOT_DEBUG_VERBOSE_ENABLED
 			printf("%s DEBUG: pvPortMalloc FAILED!\n", LABEL_SUBSCRIBER);
 			#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
#endif
 		}
 	}

 	if (decode_ret != 0 || !pubkey_der) {
 		#if TESAIOT_DEBUG_VERBOSE_ENABLED
 		printf("%s ERROR: signing certificate B64 decode fail\n", LABEL_SUBSCRIBER);
 		#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 		g_protected_update_active = false;  // Reset flag on early exit
 		if (pubkey_der) vPortFree(pubkey_der);
 		vPortFree(json_copy);
 		goto pu_done;
 	}

 	// Create semaphore for Trust M async operations (first time only)
#if TESAIOT_DEBUG_SUBSCRIBER_ENABLED
 	#if TESAIOT_DEBUG_VERBOSE_ENABLED
 	printf("%s DEBUG: Creating semaphore...\n", LABEL_SUBSCRIBER);
 	#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
#endif
 	if (trust_m_write_semaphore == NULL) {
 		trust_m_write_semaphore = xSemaphoreCreateBinary();
 		if (!trust_m_write_semaphore) {
 			#if TESAIOT_DEBUG_VERBOSE_ENABLED
 			printf("%s ERROR: semaphore fail\n", LABEL_SUBSCRIBER);
 			#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 			g_protected_update_active = false;  // Reset flag on early exit
 			vPortFree(pubkey_der);
 			vPortFree(json_copy);
 			goto pu_done;
 		}
#if TESAIOT_DEBUG_SUBSCRIBER_ENABLED
 		#if TESAIOT_DEBUG_VERBOSE_ENABLED
 		printf("%s DEBUG: Semaphore created\n", LABEL_SUBSCRIBER);
 		#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
#endif
 	} else {
#if TESAIOT_DEBUG_SUBSCRIBER_ENABLED
 		#if TESAIOT_DEBUG_VERBOSE_ENABLED
 		printf("%s DEBUG: Semaphore already exists\n", LABEL_SUBSCRIBER);
 		#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
#endif
 	}

 	// Write Trust Anchor to OID 0xE0E8
 	// Best Practice: Use persistent OPTIGA instance (no create/destroy)
 	printf("%s Acquiring OPTIGA instance...\n", LABEL_SUBSCRIBER);
 	fflush(stdout);
#if TESAIOT_DEBUG_SUBSCRIBER_ENABLED
 	#if TESAIOT_DEBUG_VERBOSE_ENABLED
 	printf("%s DEBUG: Acquiring persistent OPTIGA instance...\n", LABEL_SUBSCRIBER);
 	#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
#endif
 	trust_m_async_status = OPTIGA_LIB_BUSY;
 	optiga_util_t *me_util = optiga_manager_acquire();
 	printf("%s OPTIGA acquired: %s\n", LABEL_SUBSCRIBER, me_util ? "OK" : "FAILED");
 	fflush(stdout);
#if TESAIOT_DEBUG_SUBSCRIBER_ENABLED
 	#if TESAIOT_DEBUG_VERBOSE_ENABLED
 	printf("%s DEBUG: optiga_manager_acquire returned: %p\n", LABEL_SUBSCRIBER, me_util);
 	#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
#endif
 	if (!me_util) {
 		printf("%s ERROR: Failed to acquire OPTIGA instance\n", LABEL_SUBSCRIBER);
 		g_protected_update_active = false;  // Reset flag on early exit
 		vPortFree(pubkey_der);
 		vPortFree(json_copy);
 		goto pu_done;
 	}

 	// ========================================
 	// STEP 1: Write Trust Anchor metadata AND data
 	// ========================================
 	// NOTE:
 	//
 	// ERROR 0x8029 ROOT CAUSE: Trust M requires OID 0xE0E8 to have
 	// metadata type 0x11 (Trust Anchor) for manifest verification!
 	//
 	// Previous version SKIPPED metadata write (wrongly assumed already set)
 	// Result: Metadata read failed (0x0204) -> Trust M refuses OID as Trust Anchor!
 	//
 	// SOLUTION: Write metadata type 0x11 BEFORE writing data
 	// -> If metadata write fails (LcsO = 0x07), log warning but continue
 	// -> Hopefully metadata was already set correctly

 	// State update: Writing Trust Anchor
 	trustm_update_state(TRUSTM_STATE_WRITING_TRUST_ANCHOR, NULL, NULL);

 	// STEP 1.1: Write Trust Anchor metadata (type 0x11)
#if TESAIOT_DEBUG_SUBSCRIBER_ENABLED
 	#if TESAIOT_DEBUG_VERBOSE_ENABLED
 	printf("%s [1.1] Writing Trust Anchor metadata to OID 0xE0E8...\n", LABEL_SUBSCRIBER);
 	#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
#endif
 	uint8_t trust_anchor_metadata[] = {
 	 0x20, 0x06, // TLV: Length = 6 bytes
 	 0xD3, 0x01, 0x00, // Tag 0xD3 (Execute Access Condition), Length 1, Value 0x00 (Always)
 	 0xE8, 0x01, 0x11  // Tag 0xE8 (Data Object Type), Length 1, Value 0x11 (Trust Anchor)
 	};

 	optiga_lib_status = OPTIGA_LIB_BUSY;  // Set global status for polling
 	optiga_lib_status_t metadata_status = optiga_util_write_metadata(
 	 me_util, pu_anchor_oid(), trust_anchor_metadata, sizeof(trust_anchor_metadata)
 	);

 	if (OPTIGA_LIB_SUCCESS == metadata_status) {
 	 // Wait for metadata write completion by polling optiga_lib_status
 	 TickType_t start_ticks = xTaskGetTickCount();
 	 TickType_t timeout_ticks = pdMS_TO_TICKS(5000); // Increased to 5s to prevent MQTT disconnect

 	 while ((xTaskGetTickCount() - start_ticks) < timeout_ticks) {
 	 vTaskDelay(pdMS_TO_TICKS(100));
 	 if (optiga_lib_status != OPTIGA_LIB_BUSY) {
 	 break;
 	 }
 	 }
 	 trust_m_async_status = optiga_lib_status;  // Copy result for checking

 	 if (OPTIGA_LIB_SUCCESS == trust_m_async_status) {
#if TESAIOT_DEBUG_SUBSCRIBER_ENABLED
 	 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 	 printf("%s [1.1] Trust Anchor metadata write OK\n", LABEL_SUBSCRIBER);
 	 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
#endif

 	 /* Wait 500ms for Trust M to commit metadata to NVM */
#if TESAIOT_DEBUG_SUBSCRIBER_ENABLED
 	 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 	 printf("%s [1.1] Waiting 500ms for metadata NVM commit...\n", LABEL_SUBSCRIBER);
 	 fflush(stdout);
 	 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
#endif
 	 vTaskDelay(pdMS_TO_TICKS(500));

 	 /* Debug: Confirm task resumed after delay */
#if TESAIOT_DEBUG_SUBSCRIBER_ENABLED
 	 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 	 printf("%s [1.1] NVM commit delay complete, continuing to STEP 1.2...\n", LABEL_SUBSCRIBER);
 	 fflush(stdout);
 	 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
#endif
 	 } else {
 	 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 	 printf("%s [1.1] WARNING: Metadata write failed (0x%04X) - continuing with data write\n", LABEL_SUBSCRIBER,
 	 trust_m_async_status);
 	 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 	 }
 	} else {
 	 #if TESAIOT_DEBUG_VERBOSE_ENABLED
 	 printf("%s [1.1] WARNING: Metadata write request failed (0x%04X) - continuing with data write\n", LABEL_SUBSCRIBER,
 	 metadata_status);
 	 #endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 	}

 	// STEP 1.2: Write Trust Anchor data (public key DER)
#if TESAIOT_DEBUG_SUBSCRIBER_ENABLED
 	#if TESAIOT_DEBUG_VERBOSE_ENABLED
 	printf("%s [1.2] Writing Trust Anchor data (%u bytes) to OID 0xE0E8...\n", LABEL_SUBSCRIBER, (unsigned int)pubkey_decoded_len);
 	#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
#endif
 	optiga_lib_status = OPTIGA_LIB_BUSY;  // Set global status for polling
 	optiga_lib_status_t write_status = optiga_util_write_data(
 		me_util, pu_anchor_oid(), OPTIGA_UTIL_ERASE_AND_WRITE, 0, pubkey_der, pubkey_decoded_len
 	);

 	if (OPTIGA_LIB_SUCCESS == write_status) {
 		// Wait for Trust M operation by polling optiga_lib_status
 		TickType_t start_ticks = xTaskGetTickCount();
 		TickType_t timeout_ticks = pdMS_TO_TICKS(5000); // Increased to 5s to prevent MQTT disconnect
 		bool operation_complete = false;

 		while ((xTaskGetTickCount() - start_ticks) < timeout_ticks) {
 			vTaskDelay(pdMS_TO_TICKS(100));
 			if (optiga_lib_status != OPTIGA_LIB_BUSY) {
 				operation_complete = true;
 				break;
 			}
 		}
 		trust_m_async_status = optiga_lib_status;  // Copy result for checking

 		if (operation_complete && (OPTIGA_LIB_SUCCESS == trust_m_async_status)) {
#if TESAIOT_DEBUG_SUBSCRIBER_ENABLED
 			#if TESAIOT_DEBUG_VERBOSE_ENABLED
 			printf("%s [1.2] TrustAnchor data write OK\n", LABEL_SUBSCRIBER);
 			#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
#endif

				// Store Platform certificate for Protected Update
				// When provision() later calls write_trust_anchor(), it will use this
				// certificate instead of the hardcoded Infineon certificate
				extern uint8_t *external_trust_anchor;
				extern size_t external_trust_anchor_len;
				external_trust_anchor = pubkey_der;
				external_trust_anchor_len = pubkey_decoded_len;
				#if TESAIOT_DEBUG_SUBSCRIBER_ENABLED
				#if TESAIOT_DEBUG_VERBOSE_ENABLED
				printf("%s Stored Platform certificate for Protected Update (%u bytes)\n", LABEL_SUBSCRIBER, (unsigned int)pubkey_decoded_len);
				#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
				#endif

			// DEBUG: Public key hex dump (disabled to reduce serial load)
			#if 0  // DISABLED: HEX dump causes serial buffer overflow and task blocking
			printf("%s DEBUG: Public key hex (%u bytes)\n", LABEL_SUBSCRIBER, (unsigned int)pubkey_decoded_len);
			for (size_t i = 0; i < pubkey_decoded_len; i++) {
				printf("%02x", pubkey_der[i]);
			}
			printf("\n");
			#endif

			// ========================================
			// READBACK VERIFICATION: Verify OID 0xE0E8 data write was successful
			// ========================================
			/* Wait 500ms for Trust M to commit data to NVM before readback */
			#if TESAIOT_DEBUG_SUBSCRIBER_ENABLED
			#if TESAIOT_DEBUG_VERBOSE_ENABLED
			printf("%s READBACK: Waiting 500ms for data NVM commit...\n", LABEL_SUBSCRIBER);
			#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
			#endif
			vTaskDelay(pdMS_TO_TICKS(500));

			#if TESAIOT_DEBUG_SUBSCRIBER_ENABLED
			#if TESAIOT_DEBUG_VERBOSE_ENABLED
			printf("%s READBACK: Reading back OID 0xE0E8 to verify...\n", LABEL_SUBSCRIBER);
			#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
			#endif

			/* STEP 1: Read metadata to confirm type 0x11 (Trust Anchor) */
			uint8_t readback_metadata[64] = {0};
			uint16_t readback_metadata_len = sizeof(readback_metadata);

			optiga_lib_status = OPTIGA_LIB_BUSY;  // Set global status for polling
			optiga_lib_status_t read_metadata_status = optiga_util_read_metadata(
				me_util, pu_anchor_oid(), readback_metadata, &readback_metadata_len
			);

			if (OPTIGA_LIB_SUCCESS == read_metadata_status) {
				// Wait for metadata read completion by polling
				TickType_t start_ticks = xTaskGetTickCount();
				TickType_t timeout_ticks = pdMS_TO_TICKS(5000); // Increased to 5s to prevent MQTT disconnect
				bool operation_complete = false;

				while ((xTaskGetTickCount() - start_ticks) < timeout_ticks) {
					vTaskDelay(pdMS_TO_TICKS(100));
					if (optiga_lib_status != OPTIGA_LIB_BUSY) {
						operation_complete = true;
						break;
					}
				}
				trust_m_async_status = optiga_lib_status;  // Copy result

				if (operation_complete && (OPTIGA_LIB_SUCCESS == trust_m_async_status) && readback_metadata_len >= 2) {
					/* FULL Metadata TLV Parser for OID 0xE0E8 */
					printf("%s READBACK: OID 0xE0E8 Metadata (%u bytes):\n", LABEL_SUBSCRIBER, readback_metadata_len);

					/* Print full hex dump */
					printf("%s   HEX: ", LABEL_SUBSCRIBER);
					for (uint8_t i = 0; i < readback_metadata_len; i++) {
						printf("%02X ", readback_metadata[i]);
					}
					printf("\n");

					/* Parse TLV structure */
					if (readback_metadata[0] == 0x20) {
						uint8_t total_len = readback_metadata[1];
						printf("%s   Total TLV Length: %u bytes\n", LABEL_SUBSCRIBER, total_len);

						/* Parse individual tags */
						uint8_t idx = 2;
						bool found_type_tag = false;
						while (idx < readback_metadata_len && idx < (2 + total_len)) {
							uint8_t tag = readback_metadata[idx];
							uint8_t tag_len = (idx + 1 < readback_metadata_len) ? readback_metadata[idx + 1] : 0;

							printf("%s   Tag 0x%02X (len=%u): ", LABEL_SUBSCRIBER, tag, tag_len);

							switch (tag) {
								case 0xC0: /* LcsO - Lifecycle State */
									if (tag_len >= 1 && idx + 2 < readback_metadata_len) {
										uint8_t lcso = readback_metadata[idx + 2];
										printf("LcsO = 0x%02X ", lcso);
										if (lcso == 0x01) printf("(Creation)");
										else if (lcso == 0x03) printf("(Initialization)");
										else if (lcso == 0x07) printf("(Operational)");
										else if (lcso == 0x0F) printf("(Terminated)");
									}
									break;
								case 0xC4: /* Max Size */
									if (tag_len >= 2 && idx + 3 < readback_metadata_len) {
										uint16_t max_size = (readback_metadata[idx + 2] << 8) | readback_metadata[idx + 3];
										printf("MaxSize = %u bytes", max_size);
									}
									break;
								case 0xC5: /* Used Size */
									if (tag_len >= 2 && idx + 3 < readback_metadata_len) {
										uint16_t used_size = (readback_metadata[idx + 2] << 8) | readback_metadata[idx + 3];
										printf("UsedSize = %u bytes", used_size);
									}
									break;
								case 0xD0: /* Change Access Condition */
									printf("ChangeAccess = ");
									for (uint8_t j = 0; j < tag_len && idx + 2 + j < readback_metadata_len; j++) {
										printf("%02X ", readback_metadata[idx + 2 + j]);
									}
									break;
								case 0xD1: /* Read Access Condition */
									printf("ReadAccess = ");
									for (uint8_t j = 0; j < tag_len && idx + 2 + j < readback_metadata_len; j++) {
										printf("%02X ", readback_metadata[idx + 2 + j]);
									}
									break;
								case 0xD3: /* Execute Access Condition */
									printf("ExecuteAccess = ");
									for (uint8_t j = 0; j < tag_len && idx + 2 + j < readback_metadata_len; j++) {
										printf("%02X ", readback_metadata[idx + 2 + j]);
									}
									break;
								case 0xE8: /* Data Object Type */
									found_type_tag = true;
									if (tag_len >= 1 && idx + 2 < readback_metadata_len) {
										uint8_t obj_type = readback_metadata[idx + 2];
										printf("Type = 0x%02X ", obj_type);
										if (obj_type == 0x00) printf("(BSTR)");
										else if (obj_type == 0x01) printf("(UPCTR)");
										else if (obj_type == 0x11) printf("(TRUST_ANCHOR) *** REQUIRED ***");
										else if (obj_type == 0x12) printf("(DEVCERT)");
										else if (obj_type == 0x21) printf("(PRESSEC)");
										else if (obj_type == 0x22) printf("(PTFBIND)");
										else if (obj_type == 0x23) printf("(UPDATSEC)");
 										else if (obj_type == 0x31) printf("(AUTOREF)");
									}
									break;
								default:
									printf("Unknown tag, data: ");
									for (uint8_t j = 0; j < tag_len && idx + 2 + j < readback_metadata_len; j++) {
										printf("%02X ", readback_metadata[idx + 2 + j]);
									}
									break;
							}
							printf("\n");
							idx += 2 + tag_len;
						}

						/* Summary */
						if (!found_type_tag) {
							printf("%s   *** CRITICAL: No Type tag (0xE8) found! ***\n", LABEL_SUBSCRIBER);
							printf("%s   *** OID 0xE0E8 is NOT configured as Trust Anchor (type 0x11) ***\n", LABEL_SUBSCRIBER);
							printf("%s   *** Protected Update WILL FAIL with Error 0x8029 ***\n", LABEL_SUBSCRIBER);
						}
					}
				} else {
					#if TESAIOT_DEBUG_VERBOSE_ENABLED
					printf("%s WARNING: Metadata read failed (status=0x%04X, len=%u)\n", LABEL_SUBSCRIBER, trust_m_async_status, readback_metadata_len);
					#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
				}
			} else {
				#if TESAIOT_DEBUG_VERBOSE_ENABLED
				printf("%s ERROR: Metadata read request failed (0x%04X)\n", LABEL_SUBSCRIBER, read_metadata_status);
				#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
			}

			// STEP 2: Read data back from OID 0xE0E8
			uint8_t readback_cert[800] = {0}; // X.509 Certificate DER = ~580 bytes
			uint16_t readback_cert_len = sizeof(readback_cert);

			optiga_lib_status = OPTIGA_LIB_BUSY;  // Set global status for polling
			optiga_lib_status_t read_data_status = optiga_util_read_data(
				me_util, pu_anchor_oid(), 0, readback_cert, &readback_cert_len
			);

			if (OPTIGA_LIB_SUCCESS == read_data_status) {
				// Wait for data read completion by polling
				TickType_t start_ticks = xTaskGetTickCount();
				TickType_t timeout_ticks = pdMS_TO_TICKS(5000); // Increased to 5s to prevent MQTT disconnect
				bool operation_complete = false;

				while ((xTaskGetTickCount() - start_ticks) < timeout_ticks) {
					vTaskDelay(pdMS_TO_TICKS(100));
					if (optiga_lib_status != OPTIGA_LIB_BUSY) {
						operation_complete = true;
						break;
					}
				}
				trust_m_async_status = optiga_lib_status;  // Copy result

				if (operation_complete && (OPTIGA_LIB_SUCCESS == trust_m_async_status)) {
					#if TESAIOT_DEBUG_SUBSCRIBER_ENABLED
					#if TESAIOT_DEBUG_VERBOSE_ENABLED
					printf("%s READBACK: Successfully read %u bytes from OID 0xE0E8\n", LABEL_SUBSCRIBER, readback_cert_len);
					#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
					#endif

					// STEP 3: Compare with original public key
					if (readback_cert_len == pubkey_decoded_len) {
						bool match = true;
						for (size_t i = 0; i < pubkey_decoded_len; i++) {
							if (readback_cert[i] != pubkey_der[i]) {
								match = false;
								#if TESAIOT_DEBUG_VERBOSE_ENABLED
								printf("%s MISMATCH at byte %zu: wrote 0x%02X, read 0x%02X\n", LABEL_SUBSCRIBER,
									i, pubkey_der[i], readback_cert[i]);
								#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
								break;
							}
						}

						if (match) {
							#if TESAIOT_DEBUG_SUBSCRIBER_ENABLED
							#if TESAIOT_DEBUG_VERBOSE_ENABLED
							printf("%s READBACK VERIFIED: Public key matches 100%%!\n", LABEL_SUBSCRIBER);
							#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
							#endif
						} else {
							#if TESAIOT_DEBUG_VERBOSE_ENABLED
							printf("%s READBACK FAILED: Public key corrupted in Trust M!\n", LABEL_SUBSCRIBER);
							#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
						}
					} else {
						#if TESAIOT_DEBUG_VERBOSE_ENABLED
						printf("%s LENGTH MISMATCH: Wrote %zu bytes, read %u bytes\n", LABEL_SUBSCRIBER, pubkey_decoded_len, readback_cert_len);
						#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
					}

					// READBACK hex dump (disabled to reduce serial load)
					#if 0  // DISABLED: HEX dump causes serial buffer overflow
					printf("%s READBACK: Public key hex (%u bytes)\n", LABEL_SUBSCRIBER, readback_cert_len);
					for (size_t i = 0; i < readback_cert_len; i++) {
						printf("%02x", readback_cert[i]);
					}
					printf("\n");
					#endif
				} else {
					#if TESAIOT_DEBUG_VERBOSE_ENABLED
					printf("%s ERROR: Data read failed (0x%04X)\n", LABEL_SUBSCRIBER, trust_m_async_status);
					#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
				}
			} else {
				#if TESAIOT_DEBUG_VERBOSE_ENABLED
				printf("%s ERROR: Data read request failed (0x%04X)\n", LABEL_SUBSCRIBER, read_data_status);
				#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
			}

			// ========================================
			// NOTE: Force Trust M NVM commit before manifest verification
			// ========================================
			// PROBLEM: Error 0x8029 occurs because Trust M hasn't committed Trust Anchor
			// to NVM before manifest verification reads it.
			//
			// Timeline:
			// 1. optiga_util_write_data(0xE0E8) -> writes to cache -> returns success
			// 2. Immediate readback -> reads from cache -> matches 100%
			// 3. optiga_util_protected_update_start() -> reads OID 0xE0E8 from NVM
			// -> But NVM not updated yet -> sees old data (zeros)
			// -> Signature verification fails -> Error 0x8029!
			//
			// SOLUTION:
			// 1. Add 500ms delay to allow background NVM commit
			// 2. Re-read OID 0xE0E8 to force synchronization point
			// 3. This ensures Trust M has committed before manifest verification
			//
			// NVM COMMIT WAIT - Release mutex first to avoid deadlock
			optiga_manager_release();
			vTaskDelay(pdMS_TO_TICKS(500));  // 500ms is enough for NVM commit

 		} else {
 			#if TESAIOT_DEBUG_VERBOSE_ENABLED
 			printf("%s TrustAnchor-FAIL:0x%04X\n", LABEL_SUBSCRIBER, trust_m_async_status);
 			#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 			g_protected_update_active = false;  // Reset flag on early exit
 			optiga_manager_release(); // Release (not destroy) persistent instance
 			vPortFree(pubkey_der);
 			vPortFree(json_copy);
 			goto pu_done;
 		}
 	} else {
 		#if TESAIOT_DEBUG_VERBOSE_ENABLED
 		printf("%s TrustAnchor-write-fail:0x%04X\n", LABEL_SUBSCRIBER, write_status);
 		#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 		g_protected_update_active = false;  // Reset flag on early exit
 		optiga_manager_release(); // Release (not destroy) persistent instance
 		vPortFree(pubkey_der);
 		vPortFree(json_copy);
 		goto pu_done;
 	}

 	// NOTE: optiga_manager_release() already called before NVM wait
 	pubkey = pubkey_der;
 	pubkey_length = pubkey_decoded_len;

 	// ========================================
 	// STEP 2: Process manifest (decode and store)
 	// ========================================
 	printf("%s STEP 2: Processing manifest...\n", LABEL_SUBSCRIBER);
 	fflush(stdout);
 	size_t manifest_decoded_len = 0;
 	uint8_t *manifest_buf = NULL;
 	decode_ret = mbedtls_base64_decode(NULL, 0, &manifest_decoded_len,
 	 (uint8_t*)parse_ctx.manifest_b64,
 	 parse_ctx.manifest_len);

 	if (decode_ret == MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
 		manifest_buf = (uint8_t*) pvPortMalloc(manifest_decoded_len);
 		if (manifest_buf) {
 			decode_ret = mbedtls_base64_decode(manifest_buf, manifest_decoded_len,
 			 &manifest_decoded_len,
 			 (uint8_t*)parse_ctx.manifest_b64,
 			 parse_ctx.manifest_len);
 		}
 	}

 	if (decode_ret != 0 || !manifest_buf) {
 		#if TESAIOT_DEBUG_VERBOSE_ENABLED
 		printf("%s ERROR: manifest B64 decode fail\n", LABEL_SUBSCRIBER);
 		#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 		g_protected_update_active = false;  // Reset flag on early exit
 		if (manifest_buf) vPortFree(manifest_buf);
 		vPortFree(json_copy);
 		goto pu_done;
 	}

 	__DSB(); __DMB(); __ISB();
 	manifest_ecc_key = manifest_buf;
 	manifest_ecc_key_length = manifest_decoded_len;
 	printf("%s Manifest OK (%lu bytes)\n", LABEL_SUBSCRIBER, (unsigned long)manifest_decoded_len);

 	/* DEBUG: Parse manifest CBOR structure to verify Trust Anchor OID */
 	printf("%s Manifest Analysis:\n", LABEL_SUBSCRIBER);
 	printf("%s   First 16 bytes: ", LABEL_SUBSCRIBER);
 	for (size_t i = 0; i < 16 && i < manifest_decoded_len; i++) {
 		printf("%02X ", manifest_buf[i]);
 	}
 	printf("\n");

 	/* CBOR COSE_Sign1 structure: bytes[8:9] contain Trust Anchor OID */
 	if (manifest_decoded_len >= 10) {
 		uint16_t manifest_ta_oid = (manifest_buf[8] << 8) | manifest_buf[9];
 		printf("%s   Trust Anchor OID in manifest: 0x%04X ", LABEL_SUBSCRIBER, manifest_ta_oid);
 		if (manifest_ta_oid == pu_anchor_oid()) {
 			printf("(MATCHES device config!)\n");
 		} else {
 			printf("*** MISMATCH! Device uses 0xE0E8 ***\n");
 		}
 	}

 	/* Show signature algorithm (byte 4) */
 	if (manifest_decoded_len >= 5) {
 		uint8_t sign_algo = manifest_buf[4];
 		printf("%s   Signature Algorithm: 0x%02X ", LABEL_SUBSCRIBER, sign_algo);
 		if (sign_algo == 0x26) printf("(ES256 - ECDSA P-256)\n");
 		else if (sign_algo == 0x27) printf("(ES384)\n");
 		else printf("(Unknown)\n");
 	}

#if 0  /* Manifest hex dump disabled to reduce debug output - causes UART buffer issues */
 	/* DEBUG: Full manifest hex dump for Server comparison */
 	printf("%s ========== MANIFEST HEX DUMP (%lu bytes) ==========\n", LABEL_SUBSCRIBER, (unsigned long)manifest_decoded_len);
 	for (size_t i = 0; i < manifest_decoded_len; i++) {
 		printf("%02X", manifest_buf[i]);
 		if ((i + 1) % 32 == 0) printf("\n");
 		else if ((i + 1) % 8 == 0) printf(" ");
 	}
 	if (manifest_decoded_len % 32 != 0) printf("\n");
 	printf("%s ========== END MANIFEST HEX DUMP ==========\n", LABEL_SUBSCRIBER);
 	fflush(stdout);
#endif

 	xEventGroupSetBits(data_received_event_group, MANIFEST_RECEIVED_BIT);

 	// ========================================
 	// STEP 3: Process fragments (decode and concatenate)
 	// ========================================
 	printf("%s STEP 3: Processing fragments...\n", LABEL_SUBSCRIBER);
 	fflush(stdout);
 	size_t fragment_0_decoded_len = 0;
 	size_t fragment_1_decoded_len = 0;
 	size_t fragment_2_decoded_len = 0;
 	uint8_t *fragment_0_buf = NULL;
 	uint8_t *fragment_1_buf = NULL;
 	uint8_t *fragment_2_buf = NULL;

 	// Decode fragment_0 (always present)
 	decode_ret = mbedtls_base64_decode(NULL, 0, &fragment_0_decoded_len,
 	 (uint8_t*)parse_ctx.fragment_0_b64,
 	 parse_ctx.fragment_0_len);
 	if (decode_ret == MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
 		fragment_0_buf = (uint8_t*) pvPortMalloc(fragment_0_decoded_len);
 		if (fragment_0_buf) {
 			decode_ret = mbedtls_base64_decode(fragment_0_buf, fragment_0_decoded_len,
 			 &fragment_0_decoded_len,
 			 (uint8_t*)parse_ctx.fragment_0_b64,
 			 parse_ctx.fragment_0_len);
 		}
 	}

 	if (decode_ret != 0 || !fragment_0_buf) {
 		#if TESAIOT_DEBUG_VERBOSE_ENABLED
 		printf("%s ERROR: fragment_0 B64 decode fail\n", LABEL_SUBSCRIBER);
 		#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 		g_protected_update_active = false;  // Reset flag on early exit
 		if (fragment_0_buf) vPortFree(fragment_0_buf);
 		vPortFree(json_copy);
 		goto pu_done;
 	}

 	// Decode fragment_1 if exists
 	if (parse_ctx.fragment_1_b64 && parse_ctx.fragment_1_len > 0) {
 		decode_ret = mbedtls_base64_decode(NULL, 0, &fragment_1_decoded_len,
 		 (uint8_t*)parse_ctx.fragment_1_b64,
 		 parse_ctx.fragment_1_len);
 		if (decode_ret == MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
 			fragment_1_buf = (uint8_t*) pvPortMalloc(fragment_1_decoded_len);
 			if (fragment_1_buf) {
 				decode_ret = mbedtls_base64_decode(fragment_1_buf, fragment_1_decoded_len,
 				 &fragment_1_decoded_len,
 				 (uint8_t*)parse_ctx.fragment_1_b64,
 				 parse_ctx.fragment_1_len);
 			}
 		}

 		if (decode_ret != 0 || !fragment_1_buf) {
 			#if TESAIOT_DEBUG_VERBOSE_ENABLED
 			printf("%s ERROR: fragment_1 B64 decode fail\n", LABEL_SUBSCRIBER);
 			#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 			g_protected_update_active = false;  // Reset flag on early exit
 			vPortFree(fragment_0_buf);
 			if (fragment_1_buf) vPortFree(fragment_1_buf);
 			vPortFree(json_copy);
 			goto pu_done;
 		}
 	}

 	// Decode fragment_2 if exists (rare)
 	if (parse_ctx.fragment_2_b64 && parse_ctx.fragment_2_len > 0) {
 		decode_ret = mbedtls_base64_decode(NULL, 0, &fragment_2_decoded_len,
 		 (uint8_t*)parse_ctx.fragment_2_b64,
 		 parse_ctx.fragment_2_len);
 		if (decode_ret == MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
 			fragment_2_buf = (uint8_t*) pvPortMalloc(fragment_2_decoded_len);
 			if (fragment_2_buf) {
 				decode_ret = mbedtls_base64_decode(fragment_2_buf, fragment_2_decoded_len,
 				 &fragment_2_decoded_len,
 				 (uint8_t*)parse_ctx.fragment_2_b64,
 				 parse_ctx.fragment_2_len);
 			}
 		}

 		if (decode_ret != 0 || !fragment_2_buf) {
 			#if TESAIOT_DEBUG_VERBOSE_ENABLED
 			printf("%s ERROR: fragment_2 B64 decode fail\n", LABEL_SUBSCRIBER);
 			#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 			g_protected_update_active = false;  // Reset flag on early exit
 			vPortFree(fragment_0_buf);
 			if (fragment_1_buf) vPortFree(fragment_1_buf);
 			if (fragment_2_buf) vPortFree(fragment_2_buf);
 			vPortFree(json_copy);
 			goto pu_done;
 		}
 	}

 	// Concatenate fragments into single buffer
 	size_t total_fragment_len = fragment_0_decoded_len + fragment_1_decoded_len + fragment_2_decoded_len;
 	uint8_t *fragments_combined = (uint8_t*) pvPortMalloc(total_fragment_len);
 	if (!fragments_combined) {
 		#if TESAIOT_DEBUG_VERBOSE_ENABLED
 		printf("%s ERROR: malloc combined fragments fail\n", LABEL_SUBSCRIBER);
 		#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 		g_protected_update_active = false;  // Reset flag on early exit
 		vPortFree(fragment_0_buf);
 		if (fragment_1_buf) vPortFree(fragment_1_buf);
 		if (fragment_2_buf) vPortFree(fragment_2_buf);
 		vPortFree(json_copy);
 		goto pu_done;
 	}

 	// Copy fragment_0 (always present)
 	memcpy(fragments_combined, fragment_0_buf, fragment_0_decoded_len);

 	// Copy fragment_1 if exists
 	if (fragment_1_buf) {
 		memcpy(fragments_combined + fragment_0_decoded_len, fragment_1_buf, fragment_1_decoded_len);
 	}

 	// Copy fragment_2 if exists
 	if (fragment_2_buf) {
 		memcpy(fragments_combined + fragment_0_decoded_len + fragment_1_decoded_len,
 		 fragment_2_buf, fragment_2_decoded_len);
 	}

 	__DSB(); __DMB(); __ISB();
 	ecc_key_final_fragment_array = fragments_combined;
 	ecc_key_final_fragment_array_length = total_fragment_len;

 	vPortFree(fragment_0_buf);
 	if (fragment_1_buf) vPortFree(fragment_1_buf);
 	if (fragment_2_buf) vPortFree(fragment_2_buf);

 	#if TESAIOT_DEBUG_SUBSCRIBER_ENABLED
 	#if TESAIOT_DEBUG_VERBOSE_ENABLED
 	printf("%s Fragments OK (%lu bytes total)\n", LABEL_SUBSCRIBER, (unsigned long)total_fragment_len);
 	#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 	#endif

		// DEBUG: Fragment hex dump (disabled to reduce serial load)
		#if 0  // DISABLED: HEX dump causes serial buffer overflow
		printf("%s DEBUG: Fragment hex (%lu bytes)\n", LABEL_SUBSCRIBER, (unsigned long)total_fragment_len);
		for (size_t i = 0; i < total_fragment_len; i++) {
			printf("%02x", fragments_combined[i]);
		}
		printf("\n");
		#endif

 	xEventGroupSetBits(data_received_event_group, FRAGMENT_RECEIVED_BIT);

 	// NOTE: PROTECTED_UPDATE_COMPLETE_BIT is set AFTER STEP 4.2 success (see below)
 	#if TESAIOT_DEBUG_SUBSCRIBER_ENABLED
 	#if TESAIOT_DEBUG_VERBOSE_ENABLED
 	printf("%s Protected Update bundle complete\n", LABEL_SUBSCRIBER);
 	#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 	#endif

 	// ========================================
 	// STEP 3.5: Configure Target OID metadata with MUD field
 	// ========================================
 	// CRITICAL: Target OID 0xE0E1 MUST have MUD field pointing to Trust Anchor 0xE0E8
 	// Without MUD, OPTIGA Trust M cannot verify manifest signature → Error 0x8007
 	// Reference: tesaiot_protected_update_isolated.c Step 3
 	{
 		/* Declared outside the diagnostics guard: the MUD write below needs it,
 		 * so a build with TESAIOT_DEBUG_VERBOSE_ENABLED off did not compile at
 		 * all. That went unnoticed because the only project compiling these
 		 * sources also had the diagnostics on. */
 		const uint16_t mud_target = pu_target_oid();
 		#if TESAIOT_DEBUG_VERBOSE_ENABLED
		printf("%s [3.5] Configuring Target OID 0x%04X metadata with MUD → 0xE0E8...\n",
		       LABEL_SUBSCRIBER, mud_target);
 		fflush(stdout);
 		#endif

 		// Acquire OPTIGA instance for metadata write
 		optiga_util_t *me_mud = optiga_manager_acquire();
 		if (!me_mud) {
 			printf("%s ERROR: Failed to acquire OPTIGA for MUD config\n", LABEL_SUBSCRIBER);
 			g_protected_update_active = false;
 			vPortFree(json_copy);
 			if (subscriber_q_data.need_free && subscriber_q_data.data) {
 				vPortFree(subscriber_q_data.data);
 			}
 			goto pu_done;
 		}

 		/* Change access = Int(anchor): only a manifest signed by the certificate
 		 * in that object may write this one.
 		 *
 		 * Tag C1 is deliberately absent. It used to carry 0x0000, resetting the
 		 * chip's anti-rollback counter immediately before every update so that
 		 * a manifest carrying version 1 would be accepted. That defeats the
 		 * mechanism outright: with the counter forced to zero each time, an old
 		 * manifest captured off the wire replays cleanly forever, which is the
 		 * one thing the counter exists to prevent. The platform computes
 		 * max(chip, database, requested) + 1 from the current_version this
 		 * firmware now reads off tag C1 and sends with the request, so nothing
 		 * needs the counter held down. */
 		const uint16_t mud_anchor = pu_anchor_oid();
 		uint8_t target_mud_metadata[] = {
 			0x20, 0x05,
 			0xD0, 0x03, 0x21,
 			(uint8_t)(mud_anchor >> 8), (uint8_t)(mud_anchor & 0xFFU)
 		};
 		optiga_lib_status = OPTIGA_LIB_BUSY;
 		optiga_lib_status_t mud_status = optiga_util_write_metadata(
 			me_mud,
 			mud_target,  // the object the manifest was built around
 			target_mud_metadata,
 			sizeof(target_mud_metadata)
 		);

 		if (mud_status != OPTIGA_LIB_SUCCESS) {
 			printf("%s ERROR: Target OID MUD write failed: 0x%04X\n", LABEL_SUBSCRIBER, mud_status);
 			optiga_manager_release();
 			g_protected_update_active = false;
 			vPortFree(json_copy);
 			if (subscriber_q_data.need_free && subscriber_q_data.data) {
 				vPortFree(subscriber_q_data.data);
 			}
 			goto pu_done;
 		}

 		// Wait for async operation
 		uint32_t mud_timeout_ms = 5000, mud_elapsed_ms = 0;
 		while (optiga_lib_status == OPTIGA_LIB_BUSY && mud_elapsed_ms < mud_timeout_ms) {
 			vTaskDelay(pdMS_TO_TICKS(100));
 			mud_elapsed_ms += 10;
 		}

 		if (optiga_lib_status != OPTIGA_LIB_SUCCESS) {
 			printf("%s ERROR: Target OID MUD write callback failed: 0x%04X\n", LABEL_SUBSCRIBER, optiga_lib_status);
 			optiga_manager_release();
 			g_protected_update_active = false;
 			vPortFree(json_copy);
 			if (subscriber_q_data.need_free && subscriber_q_data.data) {
 				vPortFree(subscriber_q_data.data);
 			}
 			goto pu_done;
 		}

 		optiga_manager_release();
 		#if TESAIOT_DEBUG_VERBOSE_ENABLED
 		printf("%s [3.5] Target OID 0x%04X MUD configured (→ 0xE0E8)\n", LABEL_SUBSCRIBER, mud_target);
 		fflush(stdout);
 		#endif

 		// Wait for NVM commit
 		vTaskDelay(pdMS_TO_TICKS(200));
 	}

 	// ========================================
 	// STEP 4: Execute OPTIGA Protected Update
 	// ========================================
 	// NOTE: g_protected_update_active was set to true at the START of this case
 	// to prevent Publisher task creation during Trust Anchor write and READBACK
 	printf("%s ====================================================================\n", LABEL_SUBSCRIBER);
 	printf("%s STEP 4: Executing OPTIGA Trust M Protected Update\n", LABEL_SUBSCRIBER);
 	printf("%s ====================================================================\n", LABEL_SUBSCRIBER);
 	fflush(stdout);

 	// Acquire OPTIGA instance
 	optiga_util_t *me_protupd = optiga_manager_acquire();
 	if (!me_protupd) {
 		#if TESAIOT_DEBUG_VERBOSE_ENABLED
 		printf("%s ERROR: Failed to acquire OPTIGA instance\n", LABEL_SUBSCRIBER);
 		#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 		g_protected_update_active = false;  // Reset flag before exit
 		vPortFree(json_copy);
 		if (subscriber_q_data.need_free && subscriber_q_data.data) {
 			vPortFree(subscriber_q_data.data);
 		}
 		goto pu_done;
 	}

 	// Step 4.1: Start Protected Update with manifest
	// CRITICAL: Use single printf with fflush to avoid interleaved output
	printf("%s [4.1] Manifest(%u) -> calling protected_update_start...\n", LABEL_SUBSCRIBER, (unsigned int)manifest_ecc_key_length);
	fflush(stdout);

	// State update: Verifying manifest signature
	trustm_update_state(TRUSTM_STATE_VERIFYING_MANIFEST, NULL, NULL);

 	optiga_lib_status = OPTIGA_LIB_BUSY;  // Set global status for polling

 	optiga_lib_status_t protupd_status = optiga_util_protected_update_start(
 		me_protupd,
 		1, // manifest_version = 1 (Infineon SDK standard - all v5.3.0/v5.4.0 examples use version 1)
 		manifest_ecc_key,
 		manifest_ecc_key_length
 	);

	// DEBUG: If we reach here, function returned
	printf("%s [4.1] optiga_util_protected_update_start() returned: 0x%04X\n", LABEL_SUBSCRIBER, protupd_status);
	fflush(stdout);

 	if (OPTIGA_LIB_SUCCESS != protupd_status) {
 		#if TESAIOT_DEBUG_VERBOSE_ENABLED
 		printf("%s ERROR: optiga_util_protected_update_start() failed (0x%04X)\n", LABEL_SUBSCRIBER, protupd_status);
 		#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 		g_protected_update_active = false;  // Reset flag before exit
 		optiga_manager_release();
 		vPortFree(json_copy);
 		if (subscriber_q_data.need_free && subscriber_q_data.data) {
 			vPortFree(subscriber_q_data.data);
 		}
 		goto pu_done;
 	}

 	// Wait for manifest verification by polling (5 second timeout)
	// NOTE: Using 100ms delay (same as isolated test) to ensure proper async handling
 	TickType_t start_ticks = xTaskGetTickCount();
 	TickType_t timeout_ticks = pdMS_TO_TICKS(5000);
 	bool manifest_verified = false;

	printf("%s [4.1] Waiting for callback (optiga_lib_status=0x%04X)...\n", LABEL_SUBSCRIBER, optiga_lib_status);
	fflush(stdout);

 	while ((xTaskGetTickCount() - start_ticks) < timeout_ticks) {
 		vTaskDelay(pdMS_TO_TICKS(100));  // 100ms delay like isolated test
 		if (optiga_lib_status != OPTIGA_LIB_BUSY) {
 			manifest_verified = true;
 			break;
 		}
 	}
 	trust_m_async_status = optiga_lib_status;  // Copy result

 	if (manifest_verified) {
 		if (OPTIGA_LIB_SUCCESS != trust_m_async_status) {
 			#if TESAIOT_DEBUG_VERBOSE_ENABLED
 			printf("%s ERROR: Manifest verification FAILED (0x%04X)\n", LABEL_SUBSCRIBER, trust_m_async_status);
 			printf("%s This means Trust Anchor signature verification failed.\n", LABEL_SUBSCRIBER);
 			printf("%s Check: 1) Trust Anchor in OID 0xE0E8 matches Platform signing key\n", LABEL_SUBSCRIBER);
 			printf("%s 2) Manifest signature is valid\n", LABEL_SUBSCRIBER);
 			#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 			g_protected_update_active = false;  // Reset flag before exit
 			optiga_manager_release();
 			vPortFree(json_copy);
 			if (subscriber_q_data.need_free && subscriber_q_data.data) {
 				vPortFree(subscriber_q_data.data);
 			}
 			goto pu_done;
 		}
 		#if TESAIOT_DEBUG_SUBSCRIBER_ENABLED
 		#if TESAIOT_DEBUG_VERBOSE_ENABLED
 		printf("%s [4.1] Manifest verification OK (Trust Anchor signature valid)\n", LABEL_SUBSCRIBER);
 		#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 		#endif
 		/* Tell whoever is watching. This is the only event in the whole flow that
 		 * a compromised host cannot manufacture: the chip checked the platform's
 		 * signature against the trust anchor in its own storage, and refused to
 		 * write until it matched. Everything else the screen can show is metadata
 		 * this firmware wrote and read back itself.
 		 *
 		 * Deliberately outside the diagnostics guards. It sat inside them for one
 		 * flash, which compiled the event out of the shipping build and left the
 		 * screen ticking that line green off step arithmetic - true, because the
 		 * failure path never reaches step 6, but inferred by the firmware rather
 		 * than reported by the chip, which is the opposite of what the line
 		 * claims. A security claim may not depend on a debug switch. */
 		if (NULL != tesaiot_pu_progress) {
 			tesaiot_pu_progress(TESAIOT_PU_CHIP_VERIFIED_MANIFEST);
 		}
 	} else {
 		#if TESAIOT_DEBUG_VERBOSE_ENABLED
 		printf("%s ERROR: Manifest verification timeout (5s)\n", LABEL_SUBSCRIBER);
 		#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 		g_protected_update_active = false;  // Reset flag before exit
 		optiga_manager_release();
 		vPortFree(json_copy);
 		if (subscriber_q_data.need_free && subscriber_q_data.data) {
 			vPortFree(subscriber_q_data.data);
 		}
 		goto pu_done;
 	}

 	// Step 4.2: Send fragments to write certificate
 	#if TESAIOT_DEBUG_SUBSCRIBER_ENABLED
 	#if TESAIOT_DEBUG_VERBOSE_ENABLED
 	printf("%s [4.2] Sending fragments (%u bytes) to Trust M for OID 0xE0E1 write...\n", LABEL_SUBSCRIBER, (unsigned int)ecc_key_final_fragment_array_length);
 	#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 	#endif
 	optiga_lib_status = OPTIGA_LIB_BUSY;  // Set global status for polling
 	protupd_status = optiga_util_protected_update_final(
 		me_protupd,
 		ecc_key_final_fragment_array,
 		ecc_key_final_fragment_array_length
 	);

 	if (OPTIGA_LIB_SUCCESS != protupd_status) {
 		#if TESAIOT_DEBUG_VERBOSE_ENABLED
 		printf("%s ERROR: optiga_util_protected_update_final() failed (0x%04X)\n", LABEL_SUBSCRIBER, protupd_status);
 		#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 		g_protected_update_active = false;  // Reset flag before exit
 		optiga_manager_release();
 		vPortFree(json_copy);
 		if (subscriber_q_data.need_free && subscriber_q_data.data) {
 			vPortFree(subscriber_q_data.data);
 		}
 		goto pu_done;
 	}

 	// Wait for fragment write by polling (5 second timeout)
	// NOTE: Using 100ms delay (same as isolated test)
 	start_ticks = xTaskGetTickCount();
 	timeout_ticks = pdMS_TO_TICKS(5000);
 	bool fragment_written = false;

 	while ((xTaskGetTickCount() - start_ticks) < timeout_ticks) {
 		vTaskDelay(pdMS_TO_TICKS(100));  // 100ms delay like isolated test
 		if (optiga_lib_status != OPTIGA_LIB_BUSY) {
 			fragment_written = true;
 			break;
 		}
 	}
 	trust_m_async_status = optiga_lib_status;  // Copy result

 	if (fragment_written) {
 		if (OPTIGA_LIB_SUCCESS != trust_m_async_status) {
 			#if TESAIOT_DEBUG_VERBOSE_ENABLED
 			printf("%s ERROR: Fragment write FAILED (0x%04X)\n", LABEL_SUBSCRIBER, trust_m_async_status);
 			printf("%s Certificate payload could not be written to OID 0xE0E1\n", LABEL_SUBSCRIBER);
 			#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 			g_protected_update_active = false;  // Reset flag before exit
 			optiga_manager_release();
 			vPortFree(json_copy);
 			if (subscriber_q_data.need_free && subscriber_q_data.data) {
 				vPortFree(subscriber_q_data.data);
 			}
 			goto pu_done;
 		}
 		#if TESAIOT_DEBUG_SUBSCRIBER_ENABLED
 		#if TESAIOT_DEBUG_VERBOSE_ENABLED
 		printf("%s [4.2] Fragment write OK\n", LABEL_SUBSCRIBER);
 		#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 		#endif
 	} else {
 		#if TESAIOT_DEBUG_VERBOSE_ENABLED
 		printf("%s ERROR: Fragment write timeout (5s)\n", LABEL_SUBSCRIBER);
 		#endif /* TESAIOT_DEBUG_VERBOSE_ENABLED */
 		g_protected_update_active = false;  // Reset flag before exit
 		optiga_manager_release();
 		vPortFree(json_copy);
 		if (subscriber_q_data.need_free && subscriber_q_data.data) {
 			vPortFree(subscriber_q_data.data);
 		}
 		goto pu_done;
 	}

 	// Protected Update completed successfully
 	// Set "just completed" flag FIRST - this prevents:
 	// 1. MQTT-Task from creating Publisher task
 	// 2. Subscriber from processing stray cmd=2 from Server
 	g_protected_update_just_completed = true;
 	g_protected_update_active = false;  // Reset flag - but Publisher task creation blocked by just_completed

 	// Signal workflow completion via event group - workflow waits on this bit
 	xEventGroupSetBits(data_received_event_group, PROTECTED_UPDATE_COMPLETE_BIT);
 	trustm_update_state(TRUSTM_STATE_PROTECTED_UPDATE_SUCCESS, NULL, NULL);

 	// Always print success message (unconditional)
 	printf("%s ====================================================================\n", LABEL_SUBSCRIBER);
 	printf("%s PROTECTED UPDATE COMPLETED SUCCESSFULLY!\n", LABEL_SUBSCRIBER);
 	printf("%s Certificate written to OID 0xE0E1 via Protected Update mechanism\n", LABEL_SUBSCRIBER);
 	printf("%s ====================================================================\n", LABEL_SUBSCRIBER);
 	fflush(stdout);

	// ========================================
	// Send Certificate Installation ACK to TESAIoT Platform (Phase 1: Core ACK)
	// ========================================
	// Copy correlation_id to null-terminated string for safe passing
	// parse_ctx.correlation_id points into json_copy buffer (not null-terminated)
	if (parse_ctx.correlation_id && parse_ctx.correlation_id_len > 0 && parse_ctx.correlation_id_len < 37) {
		char correlation_id_str[37]; // UUID v4 max length (36 + null terminator)
		memcpy(correlation_id_str, parse_ctx.correlation_id, parse_ctx.correlation_id_len);
		correlation_id_str[parse_ctx.correlation_id_len] = '\0';


		// Calculate installation duration
		TickType_t pu_end_ticks = xTaskGetTickCount();
		uint32_t duration_ms = (uint32_t)((pu_end_ticks - pu_start_ticks) * portTICK_PERIOD_MS);

		cy_rslt_t ack_result = send_certificate_ack(
			correlation_id_str,
			"protected_update",
			0xE0E1,  // Target OID where certificate was written
			false,   // Phase 1: minimal payload (~512 bytes, no certificate data)
			duration_ms  // Installation duration in milliseconds
		);

		if (ack_result == CY_RSLT_SUCCESS) {
			printf("%s Certificate installation ACK sent successfully (duration: %lu ms)\n",
			       LABEL_SUBSCRIBER, (unsigned long)duration_ms);
		} else {
			printf("%s WARNING: Failed to send certificate ACK (0x%08lX)\n",
			       LABEL_SUBSCRIBER, (unsigned long)ack_result);
			printf("%s Note: ACK failure is non-fatal - device operation continues\n",
			       LABEL_SUBSCRIBER);
		}
		fflush(stdout);
	} else {
		printf("%s WARNING: No correlation_id available - skipping ACK\n", LABEL_SUBSCRIBER);
		fflush(stdout);
	}

 	// ========================================
 	// CRITICAL: Force certificate cache refresh
 	// ========================================
 	// PROBLEM: PSA crypto and mbedTLS cache certificates in RAM.
 	// After Protected Update writes new certificate to OID 0xE0E1,
 	// the cache still contains the old certificate.
 	// This causes Menu 5 (diagnostics) to show old certificate until board reset.
 	//
 	// SOLUTION: Force re-read OID 0xE0E1 to invalidate/refresh cache
 	// ========================================
 	printf("%s [CACHE] Force re-read OID 0xE0E1 to refresh certificate cache...\n", LABEL_SUBSCRIBER);
 	fflush(stdout);

 	uint8_t cert_readback[600];
 	uint16_t cert_readback_len = sizeof(cert_readback);

 	optiga_lib_status = OPTIGA_LIB_BUSY;
 	optiga_lib_status_t read_status = optiga_util_read_data(me_util, 0xE0E1, 0, cert_readback, &cert_readback_len);

 	if (OPTIGA_LIB_SUCCESS == read_status) {
 		// Wait for read completion
 		TickType_t start_ticks = xTaskGetTickCount();
 		TickType_t timeout_ticks = pdMS_TO_TICKS(5000); // Increased to 5s to prevent MQTT disconnect
 		while (optiga_lib_status == OPTIGA_LIB_BUSY && (xTaskGetTickCount() - start_ticks) < timeout_ticks) {
 			vTaskDelay(pdMS_TO_TICKS(100));
 		}

 		if (OPTIGA_LIB_SUCCESS == optiga_lib_status) {
 			printf("%s [CACHE] Certificate cache refreshed - new cert ready (%u bytes)\n", LABEL_SUBSCRIBER, cert_readback_len);
 		} else {
 			printf("%s [CACHE] WARNING: Certificate re-read failed (0x%04X)\n", LABEL_SUBSCRIBER, optiga_lib_status);
 		}
 	} else {
 		printf("%s [CACHE] WARNING: Read request failed (0x%04X)\n", LABEL_SUBSCRIBER, read_status);
 	}
 	fflush(stdout);

 	optiga_manager_release();

 	// Cleanup JSON copy buffer
 	vPortFree(json_copy);

 	// Free original MQTT data buffer if allocated in callback
 	if (subscriber_q_data.need_free && subscriber_q_data.data) {
 		vPortFree(subscriber_q_data.data);
 	}
 	goto pu_done;
 }
//! [hsm_trustm_reset_state_end_of_run]
/* ...context: the single pu_done exit of the bundle ingest ... */
pu_done:
    optiga_manager_touch_release();

 /* Announce completion here, not at the end of the write.
  *
  * The increment used to sit beside the success flag, roughly ten seconds
  * before this function releases the chip: the acknowledgement publish and the
  * 0xE0E1 read-back are both still ahead of it. The waiting provisioning task
  * woke on that increment and went straight into optiga_verify_cert_key_pair(),
  * which then queued behind this task's own chip gate - and if that wait
  * exceeded its ten second ceiling the pair check ran anyway, alongside this
  * task, which is the 0x0102 collision the gate exists to prevent.
  *
  * After the release, and after the touch hold, is the only point at which this
  * task is genuinely finished with the chip. It matches what the certificate
  * path already does. */
 if (pu_handled_for_us)
 {
 /* Disarm before announcing. The request has been answered, so nothing is
  * outstanding, and trustm_current_correlation_id() must stop matching.
  *
  * The platform RETAINS the last bundle, so one is redelivered on every
  * subsequent connect. While the id stayed armed for the rest of the boot
  * each of those redeliveries matched and was executed: a Protected Update
  * nobody asked for, rewriting the target, re-locking its access condition
  * on top of an unlock the operator had just performed, and consuming
  * enough of the C heap that the MQTT publisher task could not be created -
  * which then surfaced as an unrelated "could not publish" three layers
  * away. Measured 2026-08-08.
  *
  * Disarming here rather than in each caller is deliberate. The callers -
  * the HSM Security screens, tesaiot.protected_update() from MicroPython,
  * and the reference menu loop - each have to remember otherwise, and two
  * of the three did not. This is the one point every one of them passes
  * through, and it is the point at which the statement "a request is
  * outstanding" stops being true. */
 trustm_reset_state();
 g_optiga_ingest_events++;
 }
 //! [hsm_trustm_reset_state_end_of_run]
    return;
}


/*******************************************************************************
* Direct certificate install — device/{device_id}/commands/certificate
*
* The CSR workflow does not always answer with a Protected Update bundle. When
* the platform signs a CSR it publishes the certificate straight to
* commands/certificate ("Publishing certificate DIRECTLY for CSR workflow",
* event csr_publish_success) and never touches commands/manifest,
* commands/fragment or commands/protected_update. Observed end to end on
* 2026-08-05 for correlation id bento-enrol-1: signed in 4.7 s, published, and
* dropped on the floor here because nothing was listening for that topic.
*
* Carried from the reference subscriber task's UPDATE_DEVICE_CERTIFICATES case.
* write_device_certificate_and_verify() reads the staging globals, converts PEM
* to DER and writes DEVICE_CERTIFICATE_OID, then reads it back to verify.
*******************************************************************************/
/*
 * Hold touch across the whole install.
 *
 * This runs on the subscriber task, arriving whenever the platform answers, and
 * every chip call it makes ran with CM55 free to poll the FT5406 on the same
 * SCB: optiga_slot_manifest_anchor() -> tesaiot_read_metadata() takes the
 * manager mutex but no hold, and write_device_certificate_and_verify() takes
 * neither. The bundle path beside it has held touch since the 0x0102 that cost
 * 57 seconds of retries on correlation bento-pu-1; this path never did, and the
 * HSM page made it far more likely by putting a screen in front of it.
 *
 * The hold is counted, so the pair check's own hold nests correctly. One exit,
 * so no early return can leave the panel dead.
 */
/* Two different facts, deliberately not one.
 *
 * `answered` means the platform replied to us, so a waiting screen should stop
 * waiting. `installed` means the certificate is actually in the chip.
 *
 * They were folded together, and the fold set g_protected_update_just_completed
 * - the permanent "this object takes signed manifests only, ordinary writes are
 * gone" latch - because a write FAILED. One 0x0102 during the certificate
 * write, which is the exact failure this branch exists to survive, then made
 * every later certificate in that boot be dropped as "already completed". A
 * latch about the chip's lifecycle must never be set by a transport error. */
static bool pu_ingest_certificate_held(char *cert_payload, uint16_t cert_size,
                                       bool *installed);

//! [hsm_pu_ingest_certificate_completion]
void tesaiot_pu_ingest_certificate(char *cert_payload, uint16_t cert_size)
{
    bool installed = false;
    optiga_manager_touch_hold_reason("Installing the device certificate");
    bool answered = pu_ingest_certificate_held(cert_payload, cert_size, &installed);
    optiga_manager_touch_release();

    /* Announce completion here, once, for every path on which the platform
     * actually answered — including the ones that failed to install.
     *
     * Setting it only on the success path left a waiter to burn its full 60
     * seconds and then report "the platform did not deliver a bundle", which
     * blames the wrong subsystem for a write the chip refused. Setting it
     * inside, before the pair check, put two tasks into that check at once.
     * After the hold is released and after every chip transaction this function
     * makes is the only place that is both complete and safe. */
    if (installed) {
        g_protected_update_just_completed = true;
    }
    if (answered) {
        /* Same disarm as the bundle path, for the same reason: the platform
         * retains this topic too, and an armed id turns every later connect
         * into a certificate install nobody asked for. */
        trustm_reset_state();
        g_optiga_ingest_events++;   /* stop the waiter either way */
    }
}
//! [hsm_pu_ingest_certificate_completion]

static bool pu_ingest_certificate_held(char *cert_payload, uint16_t cert_size,
                                       bool *installed)
{
    if (g_protected_update_just_completed) {
        printf("%s Ignoring certificate after Protected Update completed\n",
               LABEL_SUBSCRIBER);
        return false;
    }
    if (cert_size == 0 || cert_payload == NULL) {
        printf("%s Ignoring empty certificate (sz=%u)\n",
               LABEL_SUBSCRIBER, (unsigned)cert_size);
        return false;
    }

    /* Enrolment installs a certificate with an ordinary write. Once Protected
     * Update has run against this slot the chip will not accept one: the Change
     * access condition becomes Int(anchor), and every plain write is refused
     * from then on. That lock is the point of Protected Update and it cannot be
     * undone from this side.
     *
     * Without this check the write fails inside the vendor library and the log
     * shows a bare status code, which reads like a platform or transport fault.
     * Say what actually happened, and name the call that still works. */
    {
        uint16_t anchor = optiga_slot_manifest_anchor(DEVICE_CERTIFICATE_OID);
        if (0U != anchor) {
            printf("%s REFUSED: 0x%04X only accepts a signed manifest now "
                   "(Change = Int(0x%04X)), so plain certificate enrolment "
                   "cannot write it. Use tesaiot.protected_update(\"%04X\", "
                   "\"%04X\") instead — it delivers a certificate through the "
                   "same slot by the path the chip still permits.\n",
                   LABEL_SUBSCRIBER, DEVICE_CERTIFICATE_OID, anchor,
                   DEVICE_CERTIFICATE_OID, anchor);
            fflush(stdout);
            return true;
        }
    }

    /* Copied because write_device_certificate_and_verify() reads the globals
     * while the caller still owns the queue buffer. */
    char *cert_data = (char *)pvPortMalloc(cert_size + 1);
    if (!cert_data) {
        printf("%s ERROR: malloc cert fail\n", LABEL_SUBSCRIBER);
        return true;
    }
    memcpy(cert_data, cert_payload, cert_size);
    cert_data[cert_size] = '\0';
    __DSB(); __DMB(); __ISB();

    dev_cert_raw = (uint8_t *)cert_data;
    dev_cert_raw_len = cert_size;

    optiga_lib_status_t status = write_device_certificate_and_verify();

    vPortFree(cert_data);
    dev_cert_raw = NULL;
    dev_cert_raw_len = 0;

    if (OPTIGA_LIB_SUCCESS != status) {
        printf("%s Certificate write FAILED: 0x%04X\n",
               LABEL_SUBSCRIBER, (unsigned)status);
        fflush(stdout);
        return true;
    }

    /* Prove the certificate belongs to the key before calling this a success.
     *
     * write_device_certificate_and_verify() logs "Skipping certificate
     * verification (CSR workflow)" and leaves the check to the next TLS
     * handshake — which means a certificate written to the wrong slot, or
     * signed from a stale CSR, is only discovered on the next connection, as a
     * CertificateVerify failure that reads like a server rejection.
     *
     * This is the same question optiga.verify_pair() answers, and it costs one
     * signature: sign a challenge with the private half in DEVICE_KEY_OID and
     * check it against the public key in the certificate just written. On
     * 2026-08-05 that check passed for correlation bento-enrol-2 — but only
     * because it was typed at the REPL. Enrolment should not need an operator
     * watching to know whether it worked. */
    int pair = optiga_verify_cert_key_pair(DEVICE_CERTIFICATE_OID, DEVICE_KEY_OID);
    if (pair == 1) {
        printf("%s Certificate installed in 0x%04X and verified against key 0x%04X\n",
               LABEL_SUBSCRIBER, DEVICE_CERTIFICATE_OID, DEVICE_KEY_OID);
    } else if (pair == 0) {
        printf("%s INSTALLED BUT MISMATCHED: certificate in 0x%04X does not belong "
               "to key 0x%04X — enrolment did not take\n",
               LABEL_SUBSCRIBER, DEVICE_CERTIFICATE_OID, DEVICE_KEY_OID);
    } else {
        printf("%s Certificate installed in 0x%04X; pair check could not run (%d)\n",
               LABEL_SUBSCRIBER, DEVICE_CERTIFICATE_OID, pair);
    }
    fflush(stdout);

    if (installed) { *installed = true; }
    return true;   /* the platform answered and the certificate is in the chip */
}
