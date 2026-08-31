/******************************************************************************
* File Name:   tesaiot_root_ca.h
*
* Description: TLS trust anchors for the TESAIoT MQTT brokers. Used by
*              mqtt_client_config.c to verify the server certificate.
*
*              TWO anchors are pinned, because the two platforms do not share
*              one. Until 2026-08-17 this file said they did, and for the CA
*              shipped on 2026-08-04 that was true. It is not true of the CA
*              mqtt.tesaiot.com presents now — measured, not assumed:
*
*                mqtt.tesaiot.dev  CN=TESAIoT Intermediate CA
*                                  SHA-256(DER) e3ff5011...4e0d7e98
*                                  valid 2025-09-06 .. 2030-09-05
*                mqtt.tesaiot.com  CN=TESA IoT Platform Intermediate CA
*                                  SHA-256(DER) ec17a278...c1377188
*                                  valid 2026-02-05 .. 2031-02-04
*
*              Different CN, different key, different issue date. A board
*              pinned to only one of them fails the other's handshake while
*              verifying the server certificate — before MQTT CONNECT is ever
*              sent, so the failure does not look like a credentials problem.
*
*              Both are concatenated below. mbedTLS parses a PEM bundle into a
*              trust list and verifies against whichever anchor matches the
*              issuer the server presents, so switching broker is a config
*              change (tesaiot.config_set('broker', ...)) with no rebuild.
*
*              Cost of the second anchor: 1,176 B flash for the literal and
*              ~1.2 KB of the 90 KB FreeRTOS heap while a TLS session is up.
*              Verification cost is unchanged — one anchor is checked per
*              handshake either way, and both are RSA-2048.
*
*              We pin to the Intermediate CA rather than the Root CA (RSA-4096)
*              because RSA-4096 signature verification needs more heap than is
*              available during the handshake. Each intermediate directly signs
*              its broker, so pinning to it is valid and is tighter trust than
*              pinning the root.
*
*              PKI chain: Root CA (RSA-4096) -> Intermediate CA (RSA-2048) -> server
*              Server presents [server cert, Intermediate CA] in the handshake.
*
* Usage:
*   security_info.root_ca = TESAIOT_ROOT_CA_CERTIFICATE;
*   security_info.root_ca_size = sizeof(TESAIOT_ROOT_CA_CERTIFICATE);
******************************************************************************/

#ifndef TESAIOT_ROOT_CA_H
#define TESAIOT_ROOT_CA_H

/* Anchor 1 — serves mqtt.tesaiot.dev
 * Subject: CN=TESAIoT Intermediate CA
 * Issuer:  C=TH, ST=Bangkok, L=Bangkok, O=TESA IoT Platform,
 *          OU=Certificate Authority, CN=TESAIoT Root CA
 * Key:     RSA-2048     Valid: 2025-09-06 .. 2030-09-05
 */
#define TESAIOT_CA_TESAIOT_INTERMEDIATE \
"-----BEGIN CERTIFICATE-----\n" \
"MIIFGTCCAwGgAwIBAgIUQyPdoywMvhynALmoOkaVRjRxdgMwDQYJKoZIhvcNAQEL\n" \
"BQAwgYcxCzAJBgNVBAYTAlRIMRAwDgYDVQQIEwdCYW5na29rMRAwDgYDVQQHEwdC\n" \
"YW5na29rMRowGAYDVQQKExFURVNBIElvVCBQbGF0Zm9ybTEeMBwGA1UECxMVQ2Vy\n" \
"dGlmaWNhdGUgQXV0aG9yaXR5MRgwFgYDVQQDEw9URVNBSW9UIFJvb3QgQ0EwHhcN\n" \
"MjUwOTA2MTE1NDUyWhcNMzAwOTA1MTE1NTIyWjAiMSAwHgYDVQQDExdURVNBSW9U\n" \
"IEludGVybWVkaWF0ZSBDQTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEB\n" \
"AMkIdY3CwBaqRtQkugeRMgXOT8maywvc56zHil/nBsbrfD1c4WdF1sUKnALiKlOz\n" \
"6PER5lQRKOloVxRsXfA3/5JnRXipoPpjh0wSxzdec8rmOAB2zOjPx9ZC4OFS1Oc5\n" \
"3nfE015dCM3qmt5YU+0fMrOkdHg84cG3mqwlEPCiyR2q6Q90eEe6oDAbdZf/VP/8\n" \
"m6Nto+h5q9HskeLb+Q597I/1mGxVfuQ44dLEhjIi4xXsVoYEP/huPd6QKNMfiYyd\n" \
"UggGzIOxOWXvpbAaERzMV4ORCBQLP0GT+ErzthjOoDdsYCn524vPVHUnDaKaNr2e\n" \
"DdQYL+9FQJuJKILJx/yDx30CAwEAAaOB4DCB3TAOBgNVHQ8BAf8EBAMCAQYwDwYD\n" \
"VR0TAQH/BAUwAwEB/zAdBgNVHQ4EFgQUYOv9VE15yidpsZ6fT0RhYwXVuJgwHwYD\n" \
"VR0jBBgwFoAUzeeXC4tRrrEdw4RCROaPjfMtVhkwQQYIKwYBBQUHAQEENTAzMDEG\n" \
"CCsGAQUFBzAChiVodHRwOi8vdGVzYS12YXVsdDo4MjAwL3YxL3BraS1yb290L2Nh\n" \
"MDcGA1UdHwQwMC4wLKAqoCiGJmh0dHA6Ly90ZXNhLXZhdWx0OjgyMDAvdjEvcGtp\n" \
"LXJvb3QvY3JsMA0GCSqGSIb3DQEBCwUAA4ICAQBlpS+p+VHNBoNZoyx+tYwPn/QK\n" \
"WYsmd6m4RL7Tdb+MQ1w3STDrcQBXZvpgn1V00cKoEnOrw2obtW/zIdAjXm3XGh9g\n" \
"ODna7olj/lRZa5OxyN4Yy2HdWLI6vomMF24GJz0Ve+aeNbLAU7TET2wzUiOpi15E\n" \
"wsByAXpsud7bYPnyuQ+QF2Aiau0ZVB5sMmylvHVRewXeGSb6mnQHsadtMEDJHkMr\n" \
"uhY/1WZmafjETo7uOQi2BNv2X7taOam9NCIluXNLQxrQyljyrF7tkeeJQfRp0vJc\n" \
"cP1AKmbu5wQJyDtLfJD6IbyyOkth5yyVnWHLZkOXjJ3sgXN6/mtMEv7y0wItOSVd\n" \
"e7N/O5CK+BOYYrqQXdHF5Vd0fuCaUrVVC2nXWZPXTtet4ShN6uHQKJY+Bx1IU2/U\n" \
"yBVj6vk1fQM5P1ixL44xNd3tanNX3n9Z/rVToo8iuHSVQkjuFqbc3puFpoWjMZr6\n" \
"uOEUkamOryx4wC/dC0Y5IO8M/MqGZWocwf4YuAJF5ApkyNbXTscnJYZmi3F31+6p\n" \
"rKzG2CUpNm2b5FxU9zefBe8ASoMF/h6+yqrnohHTtAWmvhh1IcJDpDiYOWu2E21c\n" \
"KRVacmR6J4vQOSL9sj1HVw+zntFPNBQo7pu3ISQgBMBmknR9vWXCy4/tXo+2pgDI\n" \
"xhdvQh0uPlE5Luai0A==\n" \
"-----END CERTIFICATE-----\n"

/* Anchor 2 — serves mqtt.tesaiot.com
 * Subject: CN=TESA IoT Platform Intermediate CA
 * Issuer:  CN=TESAIoT Root CA
 * Key:     RSA-2048     Valid: 2026-02-05 .. 2031-02-04
 * Source:  starter bundle f8916b4d-6b42-4bc1-9333-2741b92e46d3, 2026-08-17
 */
#define TESAIOT_CA_TESA_PLATFORM_INTERMEDIATE \
"-----BEGIN CERTIFICATE-----\n" \
"MIIDNzCCAh+gAwIBAgIUdfmuZDUFN1R5vOe43qWG8uqobcUwDQYJKoZIhvcNAQEL\n" \
"BQAwGjEYMBYGA1UEAxMPVEVTQUlvVCBSb290IENBMB4XDTI2MDIwNTA0NDQyNloX\n" \
"DTMxMDIwNDA0NDQ1NlowLDEqMCgGA1UEAxMhVEVTQSBJb1QgUGxhdGZvcm0gSW50\n" \
"ZXJtZWRpYXRlIENBMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA0FvV\n" \
"xs/HmFwsX7qHDKYTjJF2h/hGxlfoee4T0t0thyVr5kVPxrfZKclyju2kaalIo7Bf\n" \
"jd6AiAyspTW+iHXqEq6s7Zyl/njU4RmLJAzyzBnkZf/XSQ21BvtZEkMyjQCeg52E\n" \
"f1x5vMfBoYfyUKLUNNNAMK8pFEKcmUKrnIApxWdeskvTd61nFk289CCJFdWgtFHh\n" \
"+o+6aUUwnjoKtBNyJhGNiqqWgFSJQfgpeSX0z+VGgeN8iRXHKj5xzYYxQUkXGi1A\n" \
"5KspNDjXsUqCPMae2wpkfdzxklz0QN6FdXtw7CpdeIEVhjLMnRqs2SPlQHJfcrWs\n" \
"PCI4m3zLM369006jCwIDAQABo2MwYTAOBgNVHQ8BAf8EBAMCAQYwDwYDVR0TAQH/\n" \
"BAUwAwEB/zAdBgNVHQ4EFgQU3kb7ZkJs+4kZ8sx0NNfaiwXyyNYwHwYDVR0jBBgw\n" \
"FoAUk8x3giVhFkGrhHxia3T85lqvQFcwDQYJKoZIhvcNAQELBQADggEBAEZEkf6l\n" \
"FQo6FHYfCPyp4KHWiixvPLuBZmbayyJ6qLLvyMPZ+bodbVRgsALlBexy9AakCR/2\n" \
"nCYUEMztehVlgOK9fkM6/M1Bru7/h+iXJPMXLYCAMtJhiDpb1VgsAwVwUtfewZom\n" \
"Zn5jN2VWzHlNAe96ViVIQRc8gJiyHWyLzt06De+e+acSU3tbviIwUPQ2JX0eL3Cf\n" \
"gQXA25czGNlhID1SPtd09W9ciDAqxj1lKNcTF0ullH3sw0nRI/8N/BaqjzAbA5/A\n" \
"d+Y91vbipPxQ4Qn/r4WvOB564dSMeT5gDOub1fuFEJuTnkzANqNxE59vQTeNc1kg\n" \
"J7iuQw1S3TVHDso=\n" \
"-----END CERTIFICATE-----\n"

/* Both anchors, in one PEM bundle. Order does not matter — mbedTLS matches on
 * the issuer name, not on position. */
#define TESAIOT_ROOT_CA_CERTIFICATE \
    TESAIOT_CA_TESAIOT_INTERMEDIATE \
    TESAIOT_CA_TESA_PLATFORM_INTERMEDIATE

#endif /* TESAIOT_ROOT_CA_H */
