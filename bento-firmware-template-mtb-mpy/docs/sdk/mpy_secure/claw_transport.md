# claw_transport.h

BentoClaw transport abstraction — HTTPS POST to TESAIoT APISIX gateway. Uses cy_http_client over TLS. API: claw_https_connect(host, port, api_key) -> bool claw_https_post(path, json, json_len, resp, resp_max) -> int claw_https_disconnect() -> void claw_https_connected() -> bool

## Functions (exported by the archive)

### `claw_http_connect_insecure`

```c
bool claw_http_connect_insecure(const char *host, uint16_t port);
```

Plaintext HTTP, for keyless public endpoints that carry nothing worth hiding (the weather services). Never send an API key over this. Separate from claw_https_connect() on purpose: the port number there is attacker- reachable, so it must not be able to select plaintext.

### `claw_https_connect`

```c
bool claw_https_connect(const char *host, uint16_t port, const char *api_key);
```

Connect to TESAIoT APISIX gateway. host: APISIX hostname (e.g., "api.tesaiot.com") port: HTTPS port (typically 443) api_key: device API key for key-auth plugin Returns true on success.

### `claw_https_connected`

```c
bool claw_https_connected(void);
```

File Name: claw_transport.h Description: BentoClaw transport abstraction — HTTPS POST to TESAIoT APISIX gateway. Uses cy_http_client over TLS. API: claw_https_connect(host, port, api_key) -> bool claw_https_post(path, json, json_len, resp, resp_max) -> int claw_https_disconnect() -> void claw_https_connected() -> bool / #ifndef CLAW_TRANSPORT_H #define CLAW_TRANSPORT_H #include <stdint.h> #include <stddef.h> #include <stdbool.h> /* Connection state

### `claw_https_disconnect`

```c
void claw_https_disconnect(void);
```

Disconnect from server.

### `claw_https_get`

```c
int claw_https_get(const char *path, char *resp_buf, size_t resp_max);
```

GET on the open connection. Returns body length, or -1. Used for the keyless services the watch face reads (weather, IP geolocation).

### `claw_https_post`

```c
int claw_https_post(const char *path, const char *json, size_t json_len, char *resp_buf, size_t resp_max);
```

POST JSON to endpoint. path: API path (e.g., "/ai/v1/chat") json: request body (JSON string) json_len: length of json resp_buf: output buffer for response body resp_max: max bytes to write to resp_buf Returns response body length, or -1 on error.

## Constants

| Name | Value |
|---|---|
| `CLAW_TRANSPORT_H` | `#include` |
