/******************************************************************************
* File Name:   tesaiot_https_root_ca.h
*
* Description: TLS trust anchor for HTTPS endpoints (api.tesaiot.com).
*              Uses Let's Encrypt E7 intermediate CA (ECDSA P-384) rather than
*              ISRG Root X1 (RSA-4096) because RSA-4096 verification causes
*              heap allocation failure (MPI_ALLOC_FAILED) on PSoC Edge.
*
*              Chain: ISRG Root X1 (RSA-4096) -> E7 (ECDSA P-384) -> tesaiot.com
*              Server presents [leaf, E7] in TLS handshake.
*              We verify: leaf <- E7 (our trust anchor). No RSA-4096 needed.
*
*              NOTE: E7 expires 2027-03-12. Must update when Let's Encrypt
*              rotates intermediates.
*
* Usage:
*   cy_socket_setsockopt(sock, CY_SOCKET_SOL_TLS,
*       CY_SOCKET_SO_TRUSTED_ROOTCA_CERTIFICATE,
*       TESAIOT_HTTPS_ROOT_CA, sizeof(TESAIOT_HTTPS_ROOT_CA));
******************************************************************************/

#ifndef TESAIOT_HTTPS_ROOT_CA_H
#define TESAIOT_HTTPS_ROOT_CA_H

/* Let's Encrypt E7 Intermediate CA
 * Subject: C=US, O=Let's Encrypt, CN=E7
 * Issuer: C=US, O=Internet Security Research Group, CN=ISRG Root X1
 * Key: ECDSA P-384
 * Valid: 2024-03-13 to 2027-03-12
 */
#define TESAIOT_HTTPS_ROOT_CA \
"-----BEGIN CERTIFICATE-----\n" \
"MIIEVzCCAj+gAwIBAgIRAKp18eYrjwoiCWbTi7/UuqEwDQYJKoZIhvcNAQELBQAw\n" \
"TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh\n" \
"cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMjQwMzEzMDAwMDAw\n" \
"WhcNMjcwMzEyMjM1OTU5WjAyMQswCQYDVQQGEwJVUzEWMBQGA1UEChMNTGV0J3Mg\n" \
"RW5jcnlwdDELMAkGA1UEAxMCRTcwdjAQBgcqhkjOPQIBBgUrgQQAIgNiAARB6AST\n" \
"CFh/vjcwDMCgQer+VtqEkz7JANurZxLP+U9TCeioL6sp5Z8VRvRbYk4P1INBmbef\n" \
"QHJFHCxcSjKmwtvGBWpl/9ra8HW0QDsUaJW2qOJqceJ0ZVFT3hbUHifBM/2jgfgw\n" \
"gfUwDgYDVR0PAQH/BAQDAgGGMB0GA1UdJQQWMBQGCCsGAQUFBwMCBggrBgEFBQcD\n" \
"ATASBgNVHRMBAf8ECDAGAQH/AgEAMB0GA1UdDgQWBBSuSJ7chx1EoG/aouVgdAR4\n" \
"wpwAgDAfBgNVHSMEGDAWgBR5tFnme7bl5AFzgAiIyBpY9umbbjAyBggrBgEFBQcB\n" \
"AQQmMCQwIgYIKwYBBQUHMAKGFmh0dHA6Ly94MS5pLmxlbmNyLm9yZy8wEwYDVR0g\n" \
"BAwwCjAIBgZngQwBAgEwJwYDVR0fBCAwHjAcoBqgGIYWaHR0cDovL3gxLmMubGVu\n" \
"Y3Iub3JnLzANBgkqhkiG9w0BAQsFAAOCAgEAjx66fDdLk5ywFn3CzA1w1qfylHUD\n" \
"aEf0QZpXcJseddJGSfbUUOvbNR9N/QQ16K1lXl4VFyhmGXDT5Kdfcr0RvIIVrNxF\n" \
"h4lqHtRRCP6RBRstqbZ2zURgqakn/Xip0iaQL0IdfHBZr396FgknniRYFckKORPG\n" \
"yM3QKnd66gtMst8I5nkRQlAg/Jb+Gc3egIvuGKWboE1G89NTsN9LTDD3PLj0dUMr\n" \
"OIuqVjLB8pEC6yk9enrlrqjXQgkLEYhXzq7dLafv5Vkig6Gl0nuuqjqfp0Q1bi1o\n" \
"yVNAlXe6aUXw92CcghC9bNsKEO1+M52YY5+ofIXlS/SEQbvVYYBLZ5yeiglV6t3S\n" \
"M6H+vTG0aP9YHzLn/KVOHzGQfXDP7qM5tkf+7diZe7o2fw6O7IvN6fsQXEQQj8TJ\n" \
"UXJxv2/uJhcuy/tSDgXwHM8Uk34WNbRT7zGTGkQRX0gsbjAea/jYAoWv0ZvQRwpq\n" \
"Pe79D/i7Cep8qWnA+7AE/3B3S/3dEEYmc0lpe1366A/6GEgk3ktr9PEoQrLChs6I\n" \
"tu3wnNLB2euC8IKGLQFpGtOO/2/hiAKjyajaBP25w1jF0Wl8Bbqne3uZ2q1GyPFJ\n" \
"YRmT7/OXpmOH/FVLtwS+8ng1cAmpCujPwteJZNcDG0sF2n/sc0+SQf49fdyUK0ty\n" \
"+VUwFj9tmWxyR/M=\n" \
"-----END CERTIFICATE-----\n"

#endif /* TESAIOT_HTTPS_ROOT_CA_H */
