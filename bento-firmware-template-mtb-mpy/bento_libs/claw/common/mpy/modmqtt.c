/*******************************************************************************
 * File Name: modmqtt.c
 *
 * Description: MicroPython 'mqtt' module for CM33_NS.
 *              Direct cy_mqtt calls on CM33_NS (WiFi + lwIP + MQTT same core).
 *              Replaces the old IPC proxy to CM55 which is broken since
 *              WiFi moved to CM33_NS (SDHC0 PPC-protected).
 *
 * v2.0: Phase 16 — Direct cy_mqtt (no IPC)
 *
 *******************************************************************************/

#include "py/runtime.h"
#include "py/obj.h"
#include "py/objstr.h"

#include "cy_mqtt_api.h"
#include "wifi_init.h"

#include <string.h>
#include <stdio.h>

/*******************************************************************************
 * Configuration
 *******************************************************************************/
#define MQTT_NETWORK_BUFFER_SIZE   (2048U)
#define MQTT_DESCRIPTOR            "mpy-mqtt"
#define MQTT_DEFAULT_KEEPALIVE_SEC (60U)

/* Static buffers for received messages (subscribe callback) */
#define MQTT_MSG_TOPIC_MAX         (128U)
#define MQTT_MSG_PAYLOAD_MAX       (256U)

/* Static buffers for broker/client strings (must persist until cy_mqtt_delete) */
#define MQTT_BROKER_MAX            (64U)
#define MQTT_CLIENT_ID_MAX         (32U)
#define MQTT_USERNAME_MAX          (32U)
#define MQTT_PASSWORD_MAX          (32U)

/*******************************************************************************
 * Static State
 *******************************************************************************/

/* Network buffer for cy_mqtt (must persist until delete) */
static uint8_t mqtt_network_buffer[MQTT_NETWORK_BUFFER_SIZE];

/* MQTT handle */
static cy_mqtt_t mqtt_handle = NULL;
static bool mqtt_lib_initialized = false;
static volatile bool mqtt_connected = false;

/* Persistent string copies for cy_mqtt (pointers must remain valid) */
static char mqtt_broker_str[MQTT_BROKER_MAX];
static char mqtt_client_id_str[MQTT_CLIENT_ID_MAX];
static char mqtt_username_str[MQTT_USERNAME_MAX];
static char mqtt_password_str[MQTT_PASSWORD_MAX];

/* Last received message buffer */
static char mqtt_msg_topic[MQTT_MSG_TOPIC_MAX];
static char mqtt_msg_payload[MQTT_MSG_PAYLOAD_MAX];
static uint16_t mqtt_msg_topic_len = 0;
static size_t mqtt_msg_payload_len = 0;
static volatile bool mqtt_msg_available = false;

/*******************************************************************************
 * Event Callback (called from cy_mqtt internal event thread)
 *******************************************************************************/
static void mqtt_event_callback(cy_mqtt_t handle, cy_mqtt_event_t event,
                                 void *user_data)
{
    (void)handle;
    (void)user_data;

    switch (event.type) {
    case CY_MQTT_EVENT_TYPE_DISCONNECT:
        mqtt_connected = false;
        printf("[MQTT] Disconnected (reason=%d)\r\n",
               (int)event.data.reason);
        break;

    case CY_MQTT_EVENT_TYPE_SUBSCRIPTION_MESSAGE_RECEIVE: {
        cy_mqtt_publish_info_t *msg = &event.data.pub_msg.received_message;

        /* Copy topic */
        uint16_t tlen = msg->topic_len;
        if (tlen >= MQTT_MSG_TOPIC_MAX) tlen = MQTT_MSG_TOPIC_MAX - 1;
        memcpy(mqtt_msg_topic, msg->topic, tlen);
        mqtt_msg_topic[tlen] = '\0';
        mqtt_msg_topic_len = tlen;

        /* Copy payload */
        size_t plen = msg->payload_len;
        if (plen >= MQTT_MSG_PAYLOAD_MAX) plen = MQTT_MSG_PAYLOAD_MAX - 1;
        memcpy(mqtt_msg_payload, msg->payload, plen);
        mqtt_msg_payload[plen] = '\0';
        mqtt_msg_payload_len = plen;

        mqtt_msg_available = true;
        break;
    }

    default:
        break;
    }
}

/*******************************************************************************
 * Helper: ensure MQTT library initialized
 *******************************************************************************/
static cy_rslt_t ensure_mqtt_init(void)
{
    if (mqtt_lib_initialized) return CY_RSLT_SUCCESS;

    /* WiFi must be ready first */
    if (!app_wifi_is_ready()) {
        cy_rslt_t result = app_wifi_init();
        if (result != CY_RSLT_SUCCESS) {
            mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("WiFi init failed"));
        }
    }

    cy_rslt_t result = cy_mqtt_init();
    if (result != CY_RSLT_SUCCESS) {
        printf("[MQTT] cy_mqtt_init failed: 0x%08lX\r\n", (unsigned long)result);
        return result;
    }

    mqtt_lib_initialized = true;
    printf("[MQTT] Library initialized\r\n");
    return CY_RSLT_SUCCESS;
}

/*******************************************************************************
 * Helper: clean up MQTT resources (for disconnect / soft reboot)
 *******************************************************************************/
static void mqtt_cleanup(void)
{
    if (mqtt_handle != NULL) {
        if (mqtt_connected) {
            cy_mqtt_disconnect(mqtt_handle);
            mqtt_connected = false;
        }
        cy_mqtt_delete(mqtt_handle);
        mqtt_handle = NULL;
    }
    mqtt_msg_available = false;
}

/*******************************************************************************
 * mqtt.connect(broker, port=1883, client_id="psoc-edge",
 *              username=None, password=None, keepalive=60) -> bool
 *******************************************************************************/
static mp_obj_t mqtt_connect(size_t n_args, const mp_obj_t *pos_args,
                              mp_map_t *kw_args)
{
    enum { ARG_broker, ARG_port, ARG_client_id,
           ARG_username, ARG_password, ARG_keepalive };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_broker,    MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_port,      MP_ARG_INT,  {.u_int = 1883} },
        { MP_QSTR_client_id, MP_ARG_OBJ,  {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_username,  MP_ARG_OBJ,  {.u_obj = mp_const_none} },
        { MP_QSTR_password,  MP_ARG_OBJ,  {.u_obj = mp_const_none} },
        { MP_QSTR_keepalive, MP_ARG_INT,  {.u_int = MQTT_DEFAULT_KEEPALIVE_SEC} },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args,
                     MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    /* Ensure MQTT library is initialized */
    cy_rslt_t result = ensure_mqtt_init();
    if (result != CY_RSLT_SUCCESS) {
        return mp_const_false;
    }

    /* Clean up previous connection if any */
    mqtt_cleanup();

    /* Copy broker string (must persist until cy_mqtt_delete) */
    size_t broker_len;
    const char *broker = mp_obj_str_get_data(args[ARG_broker].u_obj, &broker_len);
    if (broker_len >= MQTT_BROKER_MAX) broker_len = MQTT_BROKER_MAX - 1;
    memcpy(mqtt_broker_str, broker, broker_len);
    mqtt_broker_str[broker_len] = '\0';

    uint16_t port = (uint16_t)args[ARG_port].u_int;

    /* Copy client_id (must persist) */
    const char *client_id = "psoc-edge";
    size_t cid_len = 9;
    if (args[ARG_client_id].u_obj != MP_OBJ_NULL &&
        args[ARG_client_id].u_obj != mp_const_none) {
        client_id = mp_obj_str_get_data(args[ARG_client_id].u_obj, &cid_len);
    }
    if (cid_len >= MQTT_CLIENT_ID_MAX) cid_len = MQTT_CLIENT_ID_MAX - 1;
    memcpy(mqtt_client_id_str, client_id, cid_len);
    mqtt_client_id_str[cid_len] = '\0';

    /* Username/password (optional) */
    bool has_username = (args[ARG_username].u_obj != mp_const_none);
    bool has_password = (args[ARG_password].u_obj != mp_const_none);
    size_t uname_len = 0, pass_len = 0;

    if (has_username) {
        const char *uname = mp_obj_str_get_data(args[ARG_username].u_obj, &uname_len);
        if (uname_len >= MQTT_USERNAME_MAX) uname_len = MQTT_USERNAME_MAX - 1;
        memcpy(mqtt_username_str, uname, uname_len);
        mqtt_username_str[uname_len] = '\0';
    }
    if (has_password) {
        const char *pass = mp_obj_str_get_data(args[ARG_password].u_obj, &pass_len);
        if (pass_len >= MQTT_PASSWORD_MAX) pass_len = MQTT_PASSWORD_MAX - 1;
        memcpy(mqtt_password_str, pass, pass_len);
        mqtt_password_str[pass_len] = '\0';
    }

    /* Broker info */
    cy_mqtt_broker_info_t broker_info = {
        .hostname     = mqtt_broker_str,
        .hostname_len = (uint16_t)broker_len,
        .port         = port,
    };

    printf("[MQTT] Connecting to %s:%u ...\r\n", mqtt_broker_str, port);

    /* Create MQTT instance (no TLS for now) */
    result = cy_mqtt_create(mqtt_network_buffer, MQTT_NETWORK_BUFFER_SIZE,
                            NULL,  /* No TLS credentials */
                            &broker_info,
                            MQTT_DESCRIPTOR,
                            &mqtt_handle);
    if (result != CY_RSLT_SUCCESS) {
        printf("[MQTT] cy_mqtt_create failed: 0x%08lX\r\n", (unsigned long)result);
        mqtt_handle = NULL;
        return mp_const_false;
    }

    /* Register event callback */
    result = cy_mqtt_register_event_callback(mqtt_handle, mqtt_event_callback, NULL);
    if (result != CY_RSLT_SUCCESS) {
        printf("[MQTT] register callback failed: 0x%08lX\r\n", (unsigned long)result);
        cy_mqtt_delete(mqtt_handle);
        mqtt_handle = NULL;
        return mp_const_false;
    }

    /* Connect info */
    cy_mqtt_connect_info_t connect_info = {
        .client_id     = mqtt_client_id_str,
        .client_id_len = (uint16_t)cid_len,
        .username      = has_username ? mqtt_username_str : NULL,
        .username_len  = has_username ? (uint16_t)uname_len : 0,
        .password      = has_password ? mqtt_password_str : NULL,
        .password_len  = has_password ? (uint16_t)pass_len : 0,
        .clean_session = true,
        .keep_alive_sec = (uint16_t)args[ARG_keepalive].u_int,
        .will_info     = NULL,
    };

    result = cy_mqtt_connect(mqtt_handle, &connect_info);
    if (result != CY_RSLT_SUCCESS) {
        printf("[MQTT] cy_mqtt_connect failed: 0x%08lX\r\n", (unsigned long)result);
        cy_mqtt_delete(mqtt_handle);
        mqtt_handle = NULL;
        return mp_const_false;
    }

    mqtt_connected = true;
    printf("[MQTT] Connected to %s:%u as '%s'\r\n",
           mqtt_broker_str, port, mqtt_client_id_str);
    return mp_const_true;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(mqtt_connect_obj, 1, mqtt_connect);

/*******************************************************************************
 * mqtt.disconnect() -> None
 *******************************************************************************/
static mp_obj_t mqtt_disconnect(void)
{
    mqtt_cleanup();
    printf("[MQTT] Disconnected\r\n");
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mqtt_disconnect_obj, mqtt_disconnect);

/*******************************************************************************
 * mqtt.publish(topic, payload, qos=0, retain=False) -> bool
 *******************************************************************************/
static mp_obj_t mqtt_publish(size_t n_args, const mp_obj_t *args)
{
    if (!mqtt_connected || mqtt_handle == NULL) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("MQTT not connected"));
    }

    size_t topic_len;
    const char *topic = mp_obj_str_get_data(args[0], &topic_len);

    /* Accept str or bytes for payload */
    const char *payload;
    size_t payload_len;
    if (mp_obj_is_str(args[1])) {
        payload = mp_obj_str_get_data(args[1], &payload_len);
    } else {
        mp_buffer_info_t bufinfo;
        mp_get_buffer_raise(args[1], &bufinfo, MP_BUFFER_READ);
        payload = bufinfo.buf;
        payload_len = bufinfo.len;
    }

    uint8_t qos = 0;
    if (n_args >= 3) qos = (uint8_t)mp_obj_get_int(args[2]);

    cy_mqtt_publish_info_t pub_info = {
        .qos         = (cy_mqtt_qos_t)qos,
        .retain      = false,
        .dup         = false,
        .topic       = topic,
        .topic_len   = (uint16_t)topic_len,
        .payload     = payload,
        .payload_len = payload_len,
    };

    cy_rslt_t result = cy_mqtt_publish(mqtt_handle, &pub_info);
    if (result != CY_RSLT_SUCCESS) {
        printf("[MQTT] publish failed: 0x%08lX\r\n", (unsigned long)result);
        return mp_const_false;
    }

    return mp_const_true;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mqtt_publish_obj, 2, 3, mqtt_publish);

/*******************************************************************************
 * mqtt.subscribe(topic, qos=0) -> bool
 *******************************************************************************/
static mp_obj_t mqtt_subscribe(size_t n_args, const mp_obj_t *args)
{
    if (!mqtt_connected || mqtt_handle == NULL) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("MQTT not connected"));
    }

    size_t topic_len;
    const char *topic = mp_obj_str_get_data(args[0], &topic_len);

    uint8_t qos = 0;
    if (n_args >= 2) qos = (uint8_t)mp_obj_get_int(args[1]);

    cy_mqtt_subscribe_info_t sub_info = {
        .qos           = (cy_mqtt_qos_t)qos,
        .topic         = topic,
        .topic_len     = (uint16_t)topic_len,
        .allocated_qos = CY_MQTT_QOS_INVALID,
    };

    cy_rslt_t result = cy_mqtt_subscribe(mqtt_handle, &sub_info, 1);
    if (result != CY_RSLT_SUCCESS) {
        printf("[MQTT] subscribe failed: 0x%08lX\r\n", (unsigned long)result);
        return mp_const_false;
    }

    if (sub_info.allocated_qos == CY_MQTT_QOS_INVALID) {
        printf("[MQTT] subscribe rejected by broker\r\n");
        return mp_const_false;
    }

    printf("[MQTT] Subscribed to '%.*s' (QoS=%d)\r\n",
           (int)topic_len, topic, (int)sub_info.allocated_qos);
    return mp_const_true;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mqtt_subscribe_obj, 1, 2, mqtt_subscribe);

/*******************************************************************************
 * mqtt.is_connected() -> bool
 *******************************************************************************/
static mp_obj_t mqtt_is_connected(void)
{
    return mp_obj_new_bool(mqtt_connected && mqtt_handle != NULL);
}
static MP_DEFINE_CONST_FUN_OBJ_0(mqtt_is_connected_obj, mqtt_is_connected);

/*******************************************************************************
 * mqtt.get_message() -> (topic, payload) or None
 *******************************************************************************/
static mp_obj_t mqtt_get_message(void)
{
    if (!mqtt_msg_available) {
        return mp_const_none;
    }

    /* Consume the message */
    mqtt_msg_available = false;

    mp_obj_t tuple[2] = {
        mp_obj_new_str(mqtt_msg_topic, mqtt_msg_topic_len),
        mp_obj_new_bytes((const byte *)mqtt_msg_payload, mqtt_msg_payload_len),
    };
    return mp_obj_new_tuple(2, tuple);
}
static MP_DEFINE_CONST_FUN_OBJ_0(mqtt_get_message_obj, mqtt_get_message);

/*******************************************************************************
 * Module Definition
 *******************************************************************************/
static const mp_rom_map_elem_t mqtt_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),      MP_ROM_QSTR(MP_QSTR_mqtt) },
    { MP_ROM_QSTR(MP_QSTR_connect),        MP_ROM_PTR(&mqtt_connect_obj) },
    { MP_ROM_QSTR(MP_QSTR_disconnect),     MP_ROM_PTR(&mqtt_disconnect_obj) },
    { MP_ROM_QSTR(MP_QSTR_publish),        MP_ROM_PTR(&mqtt_publish_obj) },
    { MP_ROM_QSTR(MP_QSTR_subscribe),      MP_ROM_PTR(&mqtt_subscribe_obj) },
    { MP_ROM_QSTR(MP_QSTR_is_connected),   MP_ROM_PTR(&mqtt_is_connected_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_message),    MP_ROM_PTR(&mqtt_get_message_obj) },
};
static MP_DEFINE_CONST_DICT(mqtt_module_globals, mqtt_module_globals_table);

const mp_obj_module_t mp_module_mqtt = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mqtt_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_mqtt, mp_module_mqtt);
