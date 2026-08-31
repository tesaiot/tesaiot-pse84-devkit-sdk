# BENTO Secure Library — SDK reference

Generated from the shipped headers and cross-checked against the shipped
binary: a function appears here only if `nm` found it in the archive.
Regenerate with `./bento-release.sh docs`.

| Header | Archive API | Open-source fns | What it is |
|---|---|---|---|
| [bento_link.h](bento_link.md) | 3 | 0 |  |
| [claw_safety.h](claw_safety.md) | 13 | 0 | BentoClaw safety guardrails — rate limiter, circuit breaker, transport trust levels. OWASP Agent... |
| [claw_session.h](claw_session.md) | 9 | 0 | BentoClaw session memory — JSONL ring buffer on LittleFS. Stores last N conversation messages fo... |
| [claw_transport.h](claw_transport.md) | 6 | 0 | BentoClaw transport abstraction — HTTPS POST to TESAIoT APISIX gateway. Uses cy_http_client over... |
| [lfs_wifi_creds.h](lfs_wifi_creds.md) | 6 | 0 | LittleFS-based persistent storage for WiFi credentials. Primary credential store — survives firm... |
| [tacp.h](tacp.md) | 6 | 0 | TESAIoT Control Protocol (TACP) — header file. Defines magic bytes, command IDs, ring buffer, an... |
| [wifi_creds_types.h](wifi_creds_types.md) | 0 | 0 | WiFi credential entry type and constants shared between: - lfs_wifi_creds.c (LittleFS-based, pri... |

**43 archive API functions across 7 headers**, plus 0 functions declared here whose implementation ships as open source in this package (yours to read and change). "Archive API" means `nm` found the symbol exported by the shipped binary; `api.txt` lists 48 exported symbols in total — the difference is data symbols and functions whose only declaration is in `bento_secure_undeclared.h`.
