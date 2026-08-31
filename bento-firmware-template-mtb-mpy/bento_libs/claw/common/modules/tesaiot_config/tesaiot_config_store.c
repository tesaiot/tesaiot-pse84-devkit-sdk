/*******************************************************************************
 * File Name: tesaiot_config_store.c
 *
 * Description: TESAIoT connectivity config store — parser, loader, saver.
 *              Uses MicroPython exec_python_str() for LittleFS I/O.
 *              Thread-safe via FreeRTOS mutex + copy-on-read.
 *              Atomic save: .tmp + os.rename() for power-loss safety.
 *
 *              Config file format: key=value text, one per line.
 *              Comments (#) and empty lines are skipped.
 *              Unknown keys are silently ignored (forward compatible).
 *
 * Author:      Wiroon Sriborrirux (BDH)
 *
 ******************************************************************************/

#include "tesaiot_config_store.h"
#include "tesaiot_config_defaults.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define printf(...) ((void)0)  /* UART contention fix — see mqtt_task.c */

/* Which storage backend does the file I/O.
 *
 * mtb-mpy (default): the two I/O blocks below run Python against the VM's
 * VFS, exactly as they always have. Defaulting to 1 keeps every existing
 * compile — including the Makefile.micropython one, which passes no flag —
 * byte-for-byte on the path that is proven on hardware.
 *
 * mtb-only (BENTO_HAS_MPY=0 from variants/mtb-only.mk): the same two blocks
 * use bento_storage, the C lfs2 mount over the same flash window. Only the
 * I/O moves; the parser, the serialiser and the public API are shared. */
#ifndef BENTO_HAS_MPY
#define BENTO_HAS_MPY 1
#endif

#if BENTO_HAS_MPY
/* MicroPython includes for exec_python_str bridge */
#include "py/obj.h"
#include "py/runtime.h"
#include "py/builtin.h"
#include "py/objstr.h"
#include "py/nlr.h"
#include "py/lexer.h"
#include "py/parse.h"
#include "py/compile.h"
#else
#include "bento_storage.h"
#endif

/*******************************************************************************
 * Static State
 ******************************************************************************/
static tesaiot_config_t     s_config;
static SemaphoreHandle_t    s_mutex;
static StaticSemaphore_t    s_mutex_buf;
static volatile bool        s_initialized = false;

/*******************************************************************************
 * exec_python_str — bridge to MicroPython VFS
 * Same pattern as lfs_wifi_creds.c / claw_session.c
 ******************************************************************************/
#if BENTO_HAS_MPY
static void exec_python_str(const char *src)
{
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_str_len(
            MP_QSTR__lt_stdin_gt_, src, strlen(src), 0);
        mp_parse_tree_t pt = mp_parse(lex, MP_PARSE_FILE_INPUT);
        mp_obj_t module_fun = mp_compile(&pt, MP_QSTR__lt_stdin_gt_, false);
        mp_call_function_0(module_fun);
        nlr_pop();
    } else {
        mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);
    }
}
#endif /* BENTO_HAS_MPY */

/*******************************************************************************
 * Mode Name Strings
 ******************************************************************************/
static const char *s_mode_names[] = {
    "mTLS + OPTIGA",
    "mTLS Software",
    "serverTLS",
    "HTTPS API",
};

static const char *s_mode_config_values[] = {
    "mtls",
    "mtls_sw",
    "server_tls",
    "https_api",
};

const char *tesaiot_mode_name(tesaiot_mode_t mode)
{
    if (mode < TESAIOT_MODE_COUNT) return s_mode_names[mode];
    return "Unknown";
}

/*******************************************************************************
 * Set Defaults
 ******************************************************************************/
static void config_set_defaults(tesaiot_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));

    cfg->tls_mode = TESAIOT_DEFAULT_TLS_MODE;
    strncpy(cfg->device_id,        TESAIOT_DEFAULT_DEVICE_ID,        sizeof(cfg->device_id) - 1);
    strncpy(cfg->mtls_device_id,   TESAIOT_DEFAULT_MTLS_DEVICE_ID,   sizeof(cfg->mtls_device_id) - 1);
    strncpy(cfg->factory_uid,      TESAIOT_DEFAULT_FACTORY_UID,      sizeof(cfg->factory_uid) - 1);
    strncpy(cfg->api_key,          TESAIOT_DEFAULT_API_KEY,          sizeof(cfg->api_key) - 1);
    strncpy(cfg->apisix_api_key,   TESAIOT_DEFAULT_APISIX_API_KEY,   sizeof(cfg->apisix_api_key) - 1);
    strncpy(cfg->mqtt_pass,        TESAIOT_DEFAULT_MQTT_PASS,        sizeof(cfg->mqtt_pass) - 1);

    strncpy(cfg->broker,       TESAIOT_DEFAULT_BROKER,       sizeof(cfg->broker) - 1);
    cfg->port               = TESAIOT_DEFAULT_PORT;
    strncpy(cfg->sni_hostname, TESAIOT_DEFAULT_SNI_HOSTNAME, sizeof(cfg->sni_hostname) - 1);
    cfg->qos                = TESAIOT_DEFAULT_QOS;
    cfg->keepalive          = TESAIOT_DEFAULT_KEEPALIVE;
    cfg->timeout_ms         = TESAIOT_DEFAULT_TIMEOUT_MS;
    cfg->max_retries        = TESAIOT_DEFAULT_MAX_RETRIES;
    cfg->retry_interval_ms  = TESAIOT_DEFAULT_RETRY_INTERVAL;
    cfg->network_buffer     = TESAIOT_DEFAULT_NETWORK_BUFFER;

    strncpy(cfg->api_host,       TESAIOT_DEFAULT_API_HOST,       sizeof(cfg->api_host) - 1);
    cfg->api_port             = TESAIOT_DEFAULT_API_PORT;
    strncpy(cfg->api_endpoint,   TESAIOT_DEFAULT_API_ENDPOINT,   sizeof(cfg->api_endpoint) - 1);
    strncpy(cfg->api_key_header, TESAIOT_DEFAULT_API_KEY_HEADER, sizeof(cfg->api_key_header) - 1);
    cfg->http_buf_size        = TESAIOT_DEFAULT_HTTP_BUF_SIZE;
    cfg->http_send_timeout_ms = TESAIOT_DEFAULT_HTTP_SEND_TIMEOUT;
    cfg->http_recv_timeout_ms = TESAIOT_DEFAULT_HTTP_RECV_TIMEOUT;

    strncpy(cfg->wifi_ssid,     TESAIOT_DEFAULT_WIFI_SSID,     sizeof(cfg->wifi_ssid) - 1);
    strncpy(cfg->wifi_pass,     TESAIOT_DEFAULT_WIFI_PASS,     sizeof(cfg->wifi_pass) - 1);
    strncpy(cfg->wifi_security, TESAIOT_DEFAULT_WIFI_SECURITY, sizeof(cfg->wifi_security) - 1);

    strncpy(cfg->sntp_server,  TESAIOT_DEFAULT_SNTP_SERVER, sizeof(cfg->sntp_server) - 1);
    cfg->sntp_timezone      = TESAIOT_DEFAULT_SNTP_TIMEZONE;

    cfg->debug_level        = TESAIOT_DEFAULT_DEBUG_LEVEL;
    cfg->_version           = TESAIOT_CONFIG_VERSION;
    cfg->_checksum          = 0;
}

/*******************************************************************************
 * Parse a single key=value line into config struct
 ******************************************************************************/
static bool config_parse_field(tesaiot_config_t *cfg, const char *key, const char *val)
{
    /* Identity */
    if (strcmp(key, "tls_mode") == 0) {
        for (int i = 0; i < (int)TESAIOT_MODE_COUNT; i++) {
            if (strcmp(val, s_mode_config_values[i]) == 0) {
                cfg->tls_mode = (tesaiot_mode_t)i;
                return true;
            }
        }
        /* Numeric fallback */
        int v = atoi(val);
        if (v >= 0 && v < (int)TESAIOT_MODE_COUNT) {
            cfg->tls_mode = (tesaiot_mode_t)v;
            return true;
        }
        return false;
    }
    if (strcmp(key, "device_id") == 0)      { strncpy(cfg->device_id,      val, sizeof(cfg->device_id) - 1);      return true; }
    if (strcmp(key, "mtls_device_id") == 0) { strncpy(cfg->mtls_device_id, val, sizeof(cfg->mtls_device_id) - 1); return true; }
    if (strcmp(key, "factory_uid") == 0)  { strncpy(cfg->factory_uid, val, sizeof(cfg->factory_uid) - 1); return true; }
    if (strcmp(key, "api_key") == 0)      { strncpy(cfg->api_key,     val, sizeof(cfg->api_key) - 1);     return true; }
    if (strcmp(key, "apisix_api_key") == 0) { strncpy(cfg->apisix_api_key, val, sizeof(cfg->apisix_api_key) - 1); return true; }
    if (strcmp(key, "mqtt_pass") == 0)     { strncpy(cfg->mqtt_pass, val, sizeof(cfg->mqtt_pass) - 1); return true; }

    /* MQTT */
    if (strcmp(key, "broker") == 0)          { strncpy(cfg->broker,       val, sizeof(cfg->broker) - 1);       return true; }
    if (strcmp(key, "port") == 0)            { cfg->port = (uint16_t)atoi(val);            return true; }
    if (strcmp(key, "sni_hostname") == 0)    { strncpy(cfg->sni_hostname, val, sizeof(cfg->sni_hostname) - 1); return true; }
    if (strcmp(key, "qos") == 0)             { cfg->qos = (uint8_t)atoi(val);              return true; }
    if (strcmp(key, "keepalive") == 0)       { cfg->keepalive = (uint16_t)atoi(val);       return true; }
    if (strcmp(key, "timeout_ms") == 0)      { cfg->timeout_ms = (uint32_t)atoi(val);      return true; }
    if (strcmp(key, "max_retries") == 0)     { cfg->max_retries = (uint8_t)atoi(val);      return true; }
    if (strcmp(key, "retry_interval_ms") == 0) { cfg->retry_interval_ms = (uint32_t)atoi(val); return true; }
    if (strcmp(key, "network_buffer") == 0)  { cfg->network_buffer = (uint16_t)atoi(val);  return true; }

    /* HTTPS */
    if (strcmp(key, "api_host") == 0)           { strncpy(cfg->api_host,       val, sizeof(cfg->api_host) - 1);       return true; }
    if (strcmp(key, "api_port") == 0)           { cfg->api_port = (uint16_t)atoi(val);                                return true; }
    if (strcmp(key, "api_endpoint") == 0)       { strncpy(cfg->api_endpoint,   val, sizeof(cfg->api_endpoint) - 1);   return true; }
    if (strcmp(key, "api_key_header") == 0)     { strncpy(cfg->api_key_header, val, sizeof(cfg->api_key_header) - 1); return true; }
    if (strcmp(key, "http_buf_size") == 0)      { cfg->http_buf_size = (uint16_t)atoi(val);                           return true; }
    if (strcmp(key, "http_send_timeout_ms") == 0) { cfg->http_send_timeout_ms = (uint32_t)atoi(val);                  return true; }
    if (strcmp(key, "http_recv_timeout_ms") == 0) { cfg->http_recv_timeout_ms = (uint32_t)atoi(val);                  return true; }

    /* WiFi */
    if (strcmp(key, "wifi_ssid") == 0)     { strncpy(cfg->wifi_ssid,     val, sizeof(cfg->wifi_ssid) - 1);     return true; }
    if (strcmp(key, "wifi_pass") == 0)     { strncpy(cfg->wifi_pass,     val, sizeof(cfg->wifi_pass) - 1);     return true; }
    if (strcmp(key, "wifi_security") == 0) { strncpy(cfg->wifi_security, val, sizeof(cfg->wifi_security) - 1); return true; }

    /* SNTP */
    if (strcmp(key, "sntp_server") == 0)   { strncpy(cfg->sntp_server, val, sizeof(cfg->sntp_server) - 1); return true; }
    if (strcmp(key, "sntp_timezone") == 0) { cfg->sntp_timezone = (int8_t)atoi(val); return true; }

    /* Debug */
    if (strcmp(key, "debug_level") == 0) { cfg->debug_level = (uint8_t)atoi(val); return true; }

    /* Unknown key — silently skip (forward compatible) */
    return false;
}

/*******************************************************************************
 * Parse raw file content into config struct
 ******************************************************************************/
static void config_parse_content(tesaiot_config_t *cfg, const char *content, size_t len)
{
    const char *p = content;
    const char *end = content + len;
    char line_buf[128];

    while (p < end) {
        /* Extract one line */
        const char *eol = p;
        while (eol < end && *eol != '\n') eol++;

        size_t line_len = (size_t)(eol - p);
        if (line_len >= sizeof(line_buf)) line_len = sizeof(line_buf) - 1;
        memcpy(line_buf, p, line_len);
        line_buf[line_len] = '\0';

        /* Strip trailing \r */
        if (line_len > 0 && line_buf[line_len - 1] == '\r')
            line_buf[--line_len] = '\0';

        p = (eol < end) ? eol + 1 : end;

        /* Skip empty lines and comments */
        if (line_len == 0 || line_buf[0] == '#') continue;

        /* Split key=value */
        char *eq = strchr(line_buf, '=');
        if (!eq) continue;

        *eq = '\0';
        const char *key = line_buf;
        const char *val = eq + 1;

        config_parse_field(cfg, key, val);
    }
}

/*******************************************************************************
 * Load config from LittleFS via Python I/O
 ******************************************************************************/
bool tesaiot_config_load(void)
{
    if (!s_initialized) return false;

//! [j8_config_load_c_path]
/* ...context: inside tesaiot_config_load() ... */
#if !BENTO_HAS_MPY
    /* Read the file through the C lfs2 mount. Same file, same parse. */
    static char read_buf[2048];
    int n = bento_storage_read_file(TESAIOT_CONFIG_FILE_PATH,
                                    read_buf, sizeof(read_buf));
    bool loaded = false;
    if (n > 0) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        config_set_defaults(&s_config);
        config_parse_content(&s_config, read_buf, (size_t)n);
        s_config._version = TESAIOT_CONFIG_VERSION;
        xSemaphoreGive(s_mutex);
        loaded = true;
    }
    //! [j8_config_load_c_path]
#else
    /* Read file content via Python bridge */
    exec_python_str(
        "try:\n"
        "    _tcf = open('" TESAIOT_CONFIG_FILE_PATH "', 'r')\n"
        "    _tcd = _tcf.read()\n"
        "    _tcf.close()\n"
        "except:\n"
        "    _tcd = ''\n"
    );

    bool loaded = false;
    nlr_buf_t nlr;

    if (nlr_push(&nlr) == 0) {
        mp_obj_t raw = mp_load_global(MP_QSTR__tcd);
        mp_buffer_info_t bufinfo;
        mp_get_buffer_raise(raw, &bufinfo, MP_BUFFER_READ);

        if (bufinfo.len > 0) {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            config_set_defaults(&s_config);
            config_parse_content(&s_config, bufinfo.buf, bufinfo.len);
            s_config._version = TESAIOT_CONFIG_VERSION;
            xSemaphoreGive(s_mutex);
            loaded = true;
        }

        /* Clean up Python globals */
        mp_store_global(MP_QSTR__tcd, mp_const_none);
        mp_store_global(MP_QSTR__tcf, mp_const_none);
        nlr_pop();
    } else {
        printf("[TESAIOT_CFG] load: Python exception\r\n");
        mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);
    }
#endif /* BENTO_HAS_MPY */

    if (loaded) {
        printf("[TESAIOT_CFG] loaded: mode=%s broker=%s port=%u\r\n",
               tesaiot_mode_name(s_config.tls_mode),
               s_config.broker, s_config.port);
    }

    return loaded;
}

/*******************************************************************************
 * Save config to LittleFS via Python I/O (atomic: .tmp + rename)
 ******************************************************************************/
bool tesaiot_config_save(void)
{
    if (!s_initialized) return false;

    /* Build config content as key=value lines.
     * Use a static buffer to avoid heap allocation. */
    static char save_buf[2048];
    int pos = 0;
    tesaiot_config_t cfg;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    memcpy(&cfg, &s_config, sizeof(cfg));
    xSemaphoreGive(s_mutex);

    pos += snprintf(save_buf + pos, sizeof(save_buf) - pos,
        "# TESAIoT Config v%lu\n", (unsigned long)cfg._version);
    pos += snprintf(save_buf + pos, sizeof(save_buf) - pos,
        "tls_mode=%s\n", s_mode_config_values[cfg.tls_mode]);
    pos += snprintf(save_buf + pos, sizeof(save_buf) - pos,
        "device_id=%s\n", cfg.device_id);
    pos += snprintf(save_buf + pos, sizeof(save_buf) - pos,
        "mtls_device_id=%s\n", cfg.mtls_device_id);
    pos += snprintf(save_buf + pos, sizeof(save_buf) - pos,
        "factory_uid=%s\n", cfg.factory_uid);
    pos += snprintf(save_buf + pos, sizeof(save_buf) - pos,
        "api_key=%s\n", cfg.api_key);
    pos += snprintf(save_buf + pos, sizeof(save_buf) - pos,
        "apisix_api_key=%s\n", cfg.apisix_api_key);
    pos += snprintf(save_buf + pos, sizeof(save_buf) - pos,
        "mqtt_pass=%s\n", cfg.mqtt_pass);

    /* MQTT */
    pos += snprintf(save_buf + pos, sizeof(save_buf) - pos,
        "broker=%s\n", cfg.broker);
    pos += snprintf(save_buf + pos, sizeof(save_buf) - pos,
        "port=%u\n", cfg.port);
    pos += snprintf(save_buf + pos, sizeof(save_buf) - pos,
        "sni_hostname=%s\n", cfg.sni_hostname);
    pos += snprintf(save_buf + pos, sizeof(save_buf) - pos,
        "qos=%u\n", cfg.qos);
    pos += snprintf(save_buf + pos, sizeof(save_buf) - pos,
        "keepalive=%u\n", cfg.keepalive);
    pos += snprintf(save_buf + pos, sizeof(save_buf) - pos,
        "timeout_ms=%lu\n", (unsigned long)cfg.timeout_ms);
    pos += snprintf(save_buf + pos, sizeof(save_buf) - pos,
        "max_retries=%u\n", cfg.max_retries);
    pos += snprintf(save_buf + pos, sizeof(save_buf) - pos,
        "retry_interval_ms=%lu\n", (unsigned long)cfg.retry_interval_ms);
    pos += snprintf(save_buf + pos, sizeof(save_buf) - pos,
        "network_buffer=%u\n", cfg.network_buffer);

    /* HTTPS */
    pos += snprintf(save_buf + pos, sizeof(save_buf) - pos,
        "api_host=%s\n", cfg.api_host);
    pos += snprintf(save_buf + pos, sizeof(save_buf) - pos,
        "api_port=%u\n", cfg.api_port);
    pos += snprintf(save_buf + pos, sizeof(save_buf) - pos,
        "api_endpoint=%s\n", cfg.api_endpoint);
    pos += snprintf(save_buf + pos, sizeof(save_buf) - pos,
        "api_key_header=%s\n", cfg.api_key_header);
    pos += snprintf(save_buf + pos, sizeof(save_buf) - pos,
        "http_buf_size=%u\n", cfg.http_buf_size);
    pos += snprintf(save_buf + pos, sizeof(save_buf) - pos,
        "http_send_timeout_ms=%lu\n", (unsigned long)cfg.http_send_timeout_ms);
    pos += snprintf(save_buf + pos, sizeof(save_buf) - pos,
        "http_recv_timeout_ms=%lu\n", (unsigned long)cfg.http_recv_timeout_ms);

    /* WiFi */
    pos += snprintf(save_buf + pos, sizeof(save_buf) - pos,
        "wifi_ssid=%s\n", cfg.wifi_ssid);
    pos += snprintf(save_buf + pos, sizeof(save_buf) - pos,
        "wifi_pass=%s\n", cfg.wifi_pass);
    pos += snprintf(save_buf + pos, sizeof(save_buf) - pos,
        "wifi_security=%s\n", cfg.wifi_security);

    /* SNTP */
    pos += snprintf(save_buf + pos, sizeof(save_buf) - pos,
        "sntp_server=%s\n", cfg.sntp_server);
    pos += snprintf(save_buf + pos, sizeof(save_buf) - pos,
        "sntp_timezone=%d\n", cfg.sntp_timezone);

    /* Debug */
    pos += snprintf(save_buf + pos, sizeof(save_buf) - pos,
        "debug_level=%u\n", cfg.debug_level);

//! [j8_config_save_c_path]
/* ...context: inside tesaiot_config_save() ... */
#if !BENTO_HAS_MPY
    /* Write through the C lfs2 mount. bento_storage_write_file() is the same
     * .tmp + remove + rename sequence the Python block performs. */
    bool ok = bento_storage_write_file(TESAIOT_CONFIG_FILE_PATH,
                                       save_buf, (size_t)pos);
                                       //! [j8_config_save_c_path]
#else
    /* Write via Python: .tmp + rename for atomic save */
    bool ok = false;
    nlr_buf_t nlr;

    if (nlr_push(&nlr) == 0) {
        mp_obj_t data = mp_obj_new_str(save_buf, (size_t)pos);
        mp_store_global(MP_QSTR__tcs, data);

        exec_python_str(
            "try:\n"
            "    _tcf = open('" TESAIOT_CONFIG_FILE_PATH ".tmp', 'w')\n"
            "    _tcf.write(_tcs)\n"
            "    _tcf.close()\n"
            "    import os\n"
            "    try:\n"
            "        os.remove('" TESAIOT_CONFIG_FILE_PATH "')\n"
            "    except:\n"
            "        pass\n"
            "    os.rename('" TESAIOT_CONFIG_FILE_PATH ".tmp', '" TESAIOT_CONFIG_FILE_PATH "')\n"
            "    _tcok = True\n"
            "except:\n"
            "    _tcok = False\n"
        );

        mp_obj_t ok_obj = mp_load_global(MP_QSTR__tcok);
        ok = mp_obj_is_true(ok_obj);

        /* Clean up Python globals */
        mp_store_global(MP_QSTR__tcs, mp_const_none);
        mp_store_global(MP_QSTR__tcf, mp_const_none);
        mp_store_global(MP_QSTR__tcok, mp_const_none);
        nlr_pop();
    } else {
        printf("[TESAIOT_CFG] save: Python exception\r\n");
        mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);
    }
#endif /* BENTO_HAS_MPY */

    if (ok) {
        printf("[TESAIOT_CFG] saved (%d bytes)\r\n", pos);
    } else {
        printf("[TESAIOT_CFG] save FAILED\r\n");
    }

    return ok;
}

/*******************************************************************************
 * Public API
 ******************************************************************************/

//! [j4_config_init_defaults]
bool tesaiot_config_init(void)
{
    if (s_initialized) return true;

    s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_buf);
    config_set_defaults(&s_config);

    s_initialized = true;

    /* Try to load from LittleFS; if missing, create default file */
    if (!tesaiot_config_load()) {
        printf("[TESAIOT_CFG] no config file, creating defaults\r\n");
        tesaiot_config_save();
    }

    return true;
}
//! [j4_config_init_defaults]

void tesaiot_config_reset(void)
{
    if (!s_initialized) return;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    config_set_defaults(&s_config);
    xSemaphoreGive(s_mutex);

    tesaiot_config_save();
    printf("[TESAIOT_CFG] reset to defaults\r\n");
}

void tesaiot_config_get(tesaiot_config_t *out)
{
    if (!out || !s_initialized) return;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    memcpy(out, &s_config, sizeof(*out));
    xSemaphoreGive(s_mutex);
}

const tesaiot_config_t *tesaiot_config_get_ptr(void)
{
    return &s_config;
}

bool tesaiot_config_set_field(const char *key, const char *value)
{
    if (!key || !value || !s_initialized) return false;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool parsed = config_parse_field(&s_config, key, value);
    xSemaphoreGive(s_mutex);

    if (!parsed) {
        printf("[TESAIOT_CFG] unknown key: %s\r\n", key);
        return false;
    }

    return tesaiot_config_save();
}

bool tesaiot_config_set_field_nosave(const char *key, const char *value)
{
    if (!key || !value || !s_initialized) return false;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool parsed = config_parse_field(&s_config, key, value);
    xSemaphoreGive(s_mutex);

    if (!parsed) {
        printf("[TESAIOT_CFG] unknown key (nosave): %s\r\n", key);
        return false;
    }

    return true;
}
