# 04 -- Connectivity API

BENTO PSoC Edge E84 Firmware SDK -- WiFi, MQTT, and Credential Storage

---

## Architecture Overview

The connectivity stack runs on **CM33_NS** (non-secure core) because the CYW55513 WiFi chip connects via SDHC0, which is PPC-protected for CM33_NS exclusively. CM55 cannot access SDHC0 directly -- any attempt causes a BusFault.

```
CM55 (UI/Sensors)                    CM33_NS (WiFi/MicroPython)
  |                                    |
  |-- IPC_CMD_WIFI_STATE_PUSH -------->|  (status notifications)
  |<-- IPC_CMD_WIFI_SCAN --------------|  (scan request)
  |<-- IPC_CMD_WIFI_CONNECT ----------|  (connect request)
  |                                    |-- cy_wcm_* (Cypress WCM)
  |                                    |-- cy_mqtt_* (Cypress MQTT)
  |                                    |-- SDHC0 --> CYW55513
```

All MicroPython WiFi and MQTT functions execute directly on CM33_NS via Cypress WCM/MQTT libraries. No IPC proxy is involved for MicroPython calls. IPC commands exist only for CM55 LVGL UI pages that need WiFi status.

---

## 1. WiFi Manager (C API)

**Header:** `common/modules/wifi_manager/wifi_manager.h`

The WiFi Manager provides both blocking and non-blocking APIs. Non-blocking variants are designed for use from LVGL callbacks on CM55 where blocking would stall the display.

### Types

```c
#define WIFI_MGR_SCAN_MAX_ENTRIES  (6)

typedef struct {
    char    ssid[33];       /* SSID, null-terminated, max 32 chars */
    int8_t  rssi;           /* Signal strength in dBm */
    uint8_t security;       /* 0=open, 1=WEP, 2=WPA, 3=WPA2, 4=WPA3 */
    uint8_t channel;        /* WiFi channel number */
} wifi_mgr_scan_entry_t;

typedef enum {
    WIFI_MGR_MODE_NONE = 0,
    WIFI_MGR_MODE_SOFTAP,
    WIFI_MGR_MODE_STA,
} wifi_mgr_mode_t;

typedef struct {
    wifi_mgr_mode_t mode;
    bool            connected;
    char            ip_addr[16];
    char            ssid[33];
    int8_t          rssi;
    uint8_t         mac_addr[6];
} wifi_mgr_status_t;
```

### Blocking API

| Function | Signature | Description |
|----------|-----------|-------------|
| `wifi_manager_init` | `bool wifi_manager_init(void)` | Initialize WiFi manager. Creates the WiFi FreeRTOS task. |
| `wifi_manager_start_softap` | `bool wifi_manager_start_softap(void)` | Start SoftAP with SSID/password from `wifi_config.h`. |
| `wifi_manager_connect` | `bool wifi_manager_connect(const char *ssid, const char *password)` | Connect to a WiFi network in STA mode. |
| `wifi_manager_scan` | `int wifi_manager_scan(wifi_mgr_scan_entry_t *out, size_t max_entries)` | Blocking scan. Returns number of entries found, or -1 on error. |
| `wifi_manager_disconnect` | `void wifi_manager_disconnect(void)` | Disconnect from current WiFi network. |
| `wifi_manager_get_status` | `void wifi_manager_get_status(wifi_mgr_status_t *status)` | Fill status struct with current WiFi state. |
| `wifi_manager_is_connected` | `bool wifi_manager_is_connected(void)` | Check if WiFi is connected (SoftAP or STA). |
| `wifi_manager_get_ip` | `const char *wifi_manager_get_ip(void)` | Get IP address string. Returns `"0.0.0.0"` if not connected. |
| `wifi_manager_last_error` | `cy_rslt_t wifi_manager_last_error(void)` | Get last `cy_rslt_t` error code. |

### Non-Blocking API (for LVGL callbacks)

These follow a start/ready/result pattern to avoid blocking the GFX task:

```c
/* Scan */
bool wifi_manager_scan_start(void);                          /* Send IPC, return immediately */
bool wifi_manager_scan_ready(void);                          /* Poll (100ms timer) */
int  wifi_manager_scan_result(wifi_mgr_scan_entry_t *out,
                               size_t max_entries);          /* Get results after ready==true */

/* Status */
bool wifi_manager_status_start(void);                        /* Send IPC, return immediately */
bool wifi_manager_status_ready(void);                        /* Poll (100ms timer) */
bool wifi_manager_status_result(wifi_mgr_status_t *status);  /* Get results after ready==true */
```

**Usage pattern (LVGL timer callback):**
```c
static void wifi_timer_cb(lv_timer_t *timer) {
    if (!scan_in_progress) {
        wifi_manager_scan_start();
        scan_in_progress = true;
    } else if (wifi_manager_scan_ready()) {
        int n = wifi_manager_scan_result(entries, 6);
        update_ui_list(entries, n);
        scan_in_progress = false;
    }
}
```

---

## 2. WiFi Credential Storage

### Type Definition

**Header:** `common/mpy/wifi_creds_types.h`

```c
#define QSPI_WIFI_CREDS_MAGIC   0x57494649U  /* "WIFI" */
#define QSPI_WIFI_CREDS_VERSION 1
#define QSPI_WIFI_CREDS_MAX     6            /* Max saved networks */

typedef struct __attribute__((packed)) {
    char    ssid[33];       /* 32 chars + null terminator */
    char    password[65];   /* 64 chars + null (WPA2 max=63) */
    uint8_t security;       /* 0=open, 2=WPA, 6=WPA2_AES_PSK */
    uint8_t flags;          /* bit0=auto_connect, bit1-7=reserved */
} qspi_wifi_entry_t;        /* sizeof = 100 bytes */
```

### LittleFS Credential Store (Primary)

**Header:** `common/mpy/lfs_wifi_creds.h`

Stores credentials in `/.wifi_creds` on the MicroPython LittleFS2 filesystem (55MB QSPI partition at 0x900000). Survives firmware reflash.

| Function | Signature | Description |
|----------|-----------|-------------|
| `lfs_wifi_creds_init` | `void lfs_wifi_creds_init(void)` | Initialize store. **Must** be called from MicroPython task after VFS mount. |
| `lfs_wifi_creds_deinit` | `void lfs_wifi_creds_deinit(void)` | Invalidate handle before MicroPython soft reset. |
| `lfs_wifi_creds_ready` | `bool lfs_wifi_creds_ready(void)` | Check if store is ready for use. |
| `lfs_wifi_creds_read` | `int lfs_wifi_creds_read(qspi_wifi_entry_t *entries, int max_entries)` | Read all saved credentials. Validates magic, version, checksum. Returns entry count. |
| `lfs_wifi_creds_write` | `bool lfs_wifi_creds_write(const qspi_wifi_entry_t *entries, int count)` | Write credentials with magic + version + entries + CRC32 checksum. |
| `lfs_wifi_creds_needs_resave` | `bool lfs_wifi_creds_needs_resave(void)` | Check if XOR-32 to CRC32 migration is needed. |

**File format (binary):**
```
[4B magic: 0x57494649] [1B version: 1] [N * 100B entries] [4B CRC32]
```

### QSPI Credential Store (Deprecated)

**Header:** `common/mpy/qspi_wifi_creds.h`

This header now redirects to `wifi_creds_types.h`. The raw QSPI sector storage has been replaced by the LittleFS-based store.

### Boot Auto-Connect Flow

1. `mpy_task_entry()` initializes MicroPython and mounts VFS
2. `lfs_wifi_creds_init()` opens credential store
3. `lfs_wifi_creds_read()` loads saved entries into `g_boot_wifi_creds[]`
4. WiFi IPC worker attempts auto-connect with saved credentials
5. On new successful `wifi.connect()`, credentials are saved automatically
6. Deferred flush: `wifi_creds_flush_if_dirty()` runs between REPL iterations

---

## 3. MicroPython `wifi` Module

**Source:** `common/mpy/modwifi.c`

All functions use lazy initialization -- the WiFi stack (SDIO + WCM) starts on first use.

| Function | Signature | Returns | Description |
|----------|-----------|---------|-------------|
| `wifi.scan()` | `scan()` | `[(ssid, rssi, security, channel), ...]` | Scan for WiFi networks. Timeout: 10s. Max 20 results. Empty SSIDs filtered out. |
| `wifi.connect(ssid, password)` | `connect(ssid, password)` | `True/False` | Connect to WPA2 network. Retries 3 times with 2s delay. Auto-saves credentials on success. |
| `wifi.disconnect()` | `disconnect()` | `None` | Disconnect from current network. Pushes disconnected state to CM55. |
| `wifi.status()` | `status()` | `dict` | Returns `{'mode': str, 'connected': bool, 'ip': str, 'ssid': str, 'rssi': int}`. Mode is `"off"`, `"idle"`, or `"sta"`. |
| `wifi.is_connected()` | `is_connected()` | `bool` | Check if connected to an AP. |
| `wifi.ip()` | `ip()` | `str` | Get current IP address. Tries STA first, then AP. Returns `"0.0.0.0"` if not connected. |
| `wifi.ping(host, timeout=5000)` | `ping(host, timeout=5000)` | `int` | Ping an IP address. Returns round-trip time in ms, or -1 on failure. Only dotted-decimal IPs supported (no hostname resolution). |
| `wifi.softap(ssid, password)` | `softap(ssid="PSoC-Edge-MPY", password="micropython")` | `True/False` | Start SoftAP. Default IP: 192.168.4.1, channel 1, WPA2-AES-PSK. |

### Side Effects on Connect/Disconnect

When `wifi.connect()` succeeds:
1. Pushes WiFi connected state to CM55 topbar via `sensor_auto_push_wifi_state(true)`
2. Triggers NTP time sync and pushes time to CM55 via `sensor_auto_ntp_and_push_time()`
3. Saves credentials to QSPI LittleFS (if not already saved, max 6 entries)

When `wifi.disconnect()` is called:
1. Pushes WiFi disconnected state to CM55 topbar

### Example

```python
import wifi

# Scan for networks
networks = wifi.scan()
for ssid, rssi, sec, ch in networks:
    print(f"{ssid}: {rssi}dBm ch{ch}")

# Connect to a network
if wifi.connect("MyNetwork", "MyPassword"):
    print("Connected! IP:", wifi.ip())
    print("Ping 8.8.8.8:", wifi.ping("8.8.8.8"), "ms")
else:
    print("Connection failed")

# Start as access point
wifi.softap("BENTO-AP", "password123")
```

---

## 4. MicroPython `mqtt` Module

**Source:** `common/mpy/modmqtt.c`

Direct `cy_mqtt` calls on CM33_NS. Non-TLS only (current version). Single connection supported at a time.

### Configuration Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `MQTT_NETWORK_BUFFER_SIZE` | 2048 | Internal network buffer |
| `MQTT_MSG_TOPIC_MAX` | 128 | Max topic length for received messages |
| `MQTT_MSG_PAYLOAD_MAX` | 256 | Max payload length for received messages |
| `MQTT_BROKER_MAX` | 64 | Max broker hostname length |
| `MQTT_CLIENT_ID_MAX` | 32 | Max client ID length |
| `MQTT_DEFAULT_KEEPALIVE_SEC` | 60 | Default keep-alive interval |

### API

| Function | Signature | Returns | Description |
|----------|-----------|---------|-------------|
| `mqtt.connect(broker, ...)` | `connect(broker, port=1883, client_id="psoc-edge", username=None, password=None, keepalive=60)` | `True/False` | Connect to MQTT broker. Cleans up any previous connection first. |
| `mqtt.disconnect()` | `disconnect()` | `None` | Disconnect and release resources. |
| `mqtt.publish(topic, payload, qos=0)` | `publish(topic, payload, qos=0)` | `True/False` | Publish message. Payload accepts `str` or `bytes`. QoS 0-2 supported. |
| `mqtt.subscribe(topic, qos=0)` | `subscribe(topic, qos=0)` | `True/False` | Subscribe to topic. Returns `False` if broker rejects. |
| `mqtt.is_connected()` | `is_connected()` | `bool` | Check if MQTT session is active. |
| `mqtt.get_message()` | `get_message()` | `(topic, payload)` or `None` | Poll for received message. Returns tuple `(str, bytes)` or `None`. Single-message buffer -- only the latest message is retained. |

### Message Reception Model

The MQTT module uses a single-message buffer for subscribed messages. When a message arrives via the event callback, it overwrites the previous message. Call `get_message()` frequently to avoid missing messages.

### Example

```python
import wifi, mqtt, time

wifi.connect("MyNetwork", "MyPassword")

mqtt.connect("broker.hivemq.com", port=1883, client_id="psoc-edge-01")

mqtt.subscribe("bento/sensor/temperature")

# Publish sensor data
mqtt.publish("bento/data", '{"temp": 25.3}')

# Poll for messages
while True:
    msg = mqtt.get_message()
    if msg:
        topic, payload = msg
        print(f"Received: {topic} -> {payload}")
    time.sleep_ms(100)

mqtt.disconnect()
```

---

## 5. WiFi IPC Commands

**Header:** `common/shared/include/ipc_communication.h`

These IPC commands are used by CM55 LVGL UI pages to query WiFi state from CM33_NS. MicroPython code does NOT use these -- it calls WCM directly.

| Command | Code | Direction | Description |
|---------|------|-----------|-------------|
| `IPC_CMD_WIFI_SCAN` | `0xD0` | CM55 --> CM33 | Request WiFi scan |
| `IPC_CMD_WIFI_CONNECT` | `0xD1` | CM55 --> CM33 | Request WiFi connect |
| `IPC_CMD_WIFI_DISCONNECT` | `0xD2` | CM55 --> CM33 | Request WiFi disconnect |
| `IPC_CMD_WIFI_STATUS` | `0xD3` | CM55 --> CM33 | Query WiFi status |
| `IPC_CMD_WIFI_IP` | `0xD4` | CM55 --> CM33 | Query IP address |
| `IPC_CMD_WIFI_SOFTAP` | `0xD5` | CM55 --> CM33 | Start SoftAP |
| `IPC_CMD_WIFI_STATE_PUSH` | `0xD8` | CM33 --> CM55 | Push connected/disconnected state. `data[0]`: 0=disconnected, 1=connected |
| `IPC_CMD_TIME_PUSH` | `0xD9` | CM33 --> CM55 | Push formatted time string after NTP sync |

### MQTT IPC Commands

| Command | Code | Direction | Description |
|---------|------|-----------|-------------|
| `IPC_CMD_MQTT_CONNECT` | `0xF0` | CM55 --> CM33 | Request MQTT connect |
| `IPC_CMD_MQTT_DISCONNECT` | `0xF1` | CM55 --> CM33 | Request MQTT disconnect |
| `IPC_CMD_MQTT_PUBLISH` | `0xF2` | CM55 --> CM33 | Request MQTT publish |
| `IPC_CMD_MQTT_SUBSCRIBE` | `0xF3` | CM55 --> CM33 | Request MQTT subscribe |
| `IPC_CMD_MQTT_POLL` | `0xF6` | CM55 --> CM33 | Poll for received message |

---

## 6. Board Support

| Feature | AI Kit | Eva Kit | Game Console |
|---------|--------|---------|--------------|
| WiFi (CYW55513) | Yes | Yes | Yes |
| WiFi STA mode | Yes | Yes | Yes |
| WiFi SoftAP mode | Yes | Yes | Yes |
| MQTT client | Yes | Yes | Yes |
| Credential storage (QSPI) | Yes | Yes | Yes |
| Auto-connect on boot | Yes | Yes | Yes |

All three boards share the same CYW55513 WiFi chip connected via SDHC0.

---

## 7. Known Issues

| Issue | Description | Workaround |
|-------|-------------|------------|
| SDPCM TX credit starvation | DNS flood from DHCP Option 6 consumed all TX credits | DHCP patched to remove DNS option |
| `cy_socket_recv` latency | Data sits in buffer until FIN or timeout (5s) | Use 100ms recv timeout (polling) |
| Hostname ping not supported | `wifi.ping()` only accepts dotted-decimal IP addresses | Resolve hostname manually or use IP |
| Single MQTT message buffer | `get_message()` only returns the latest message | Poll frequently to avoid missing messages |
| IPC pipe deadlock | `SendMessage()` inside IPC handler causes permanent freeze | Use deferred flag pattern |

---

## Source Files

| File | Path |
|------|------|
| WiFi Manager header | `common/modules/wifi_manager/wifi_manager.h` |
| MicroPython wifi module | `common/mpy/modwifi.c` |
| MicroPython mqtt module | `common/mpy/modmqtt.c` |
| Credential types | `common/mpy/wifi_creds_types.h` |
| LittleFS credential store | `common/mpy/lfs_wifi_creds.h` |
| IPC command definitions | `common/shared/include/ipc_communication.h` |
