/******************************************************************************
* File Name:   tesaiot_https_root_ca.h
*
* Description: TLS trust anchor for HTTPS endpoints (api.tesaiot.dev).
*
*              Pins ISRG Root YE (ECDSA P-384), the issuer of Let's Encrypt
*              YE2, which signs the certificate api.tesaiot.dev presents.
*
*              Chain the server sends:
*                tesaiot.dev  <- YE2  <- ISRG Root YE  <- ISRG Root X2  <- X1
*              We anchor at Root YE, so YE2 arrives in the handshake and the
*              path is one hop. Verified with:
*                openssl verify -partial_chain -CAfile rootYE.pem \
*                               -untrusted YE2.pem leaf.pem   -> OK
*
*              WHY NOT THE INTERMEDIATE. The previous anchor was Let's Encrypt
*              E7, and it stopped verifying because the endpoint moved to the
*              YE2 intermediate. Pinning an intermediate guarantees a repeat:
*              they rotate. Root YE runs to 2032-09-02.
*
*              WHY NOT ISRG Root X1. It is RSA-4096, and verifying it blows the
*              heap on PSoC Edge (MPI_ALLOC_FAILED). Every certificate in this
*              chain is ECDSA P-384, so that path is never taken.
*
*              WHY NOT api.tesaiot.com. That host is fronted by Amazon RSA 2048
*              M01, a different CA entirely. The board's broker is
*              mqtt.tesaiot.dev, and the API belongs with it.
*
*              Measured 2026-08-31 against the live endpoint.
*
*******************************************************************************
* (c) 2026, TESAIoT. All rights reserved.
*******************************************************************************/

#ifndef TESAIOT_HTTPS_ROOT_CA_H
#define TESAIOT_HTTPS_ROOT_CA_H

/* ISRG Root YE — ECDSA P-384 — notAfter 2032-09-02 */
#define TESAIOT_HTTPS_ROOT_CA \
    "-----BEGIN CERTIFICATE-----\n" \
    "MIICpjCCAiugAwIBAgIRAIchZfw0tuX7qK3Vs3BftTowCgYIKoZIzj0EAwMwTzEL\n" \
    "MAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2VhcmNo\n" \
    "IEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDIwHhcNMjYwNTEzMDAwMDAwWhcN\n" \
    "MzIwOTAyMjM1OTU5WjAuMQswCQYDVQQGEwJVUzENMAsGA1UEChMESVNSRzEQMA4G\n" \
    "A1UEAxMHUm9vdCBZRTB2MBAGByqGSM49AgEGBSuBBAAiA2IABDwS/6vhrcVqcbBo\n" \
    "+wgdI3fwn9x7DNJJOY/lTOti0vkwuRN87RhEhTH17E7XyFjWsPYhIPt/wzOqxTd2\n" \
    "b+4ZJNy9ID04YywF9U5zasDVyGSNErVNtz8uSGh5izW87j77GaOB6zCB6DAOBgNV\n" \
    "HQ8BAf8EBAMCAQYwEwYDVR0lBAwwCgYIKwYBBQUHAwEwDwYDVR0TAQH/BAUwAwEB\n" \
    "/zAdBgNVHQ4EFgQUo8gmWo6hTNA1Y/ybI8g6rlbzT1YwHwYDVR0jBBgwFoAUfEKW\n" \
    "rt5LSDv6kviejM9ti6lyN5UwMgYIKwYBBQUHAQEEJjAkMCIGCCsGAQUFBzAChhZo\n" \
    "dHRwOi8veDIuaS5sZW5jci5vcmcvMBMGA1UdIAQMMAowCAYGZ4EMAQIBMCcGA1Ud\n" \
    "HwQgMB4wHKAaoBiGFmh0dHA6Ly94Mi5jLmxlbmNyLm9yZy8wCgYIKoZIzj0EAwMD\n" \
    "aQAwZgIxAMU19WCtmxVND8UHBZRoma49Z7jPs64Dma0eTu1OChVbB/2J7GV3nvYK\n" \
    "Ax54uk1G9QIxAO0miLVJu8PLNiXXXkiE/gsK3CTRTF/aeo4bMX42Zw40csRU6AC2\n" \
    "6hSW1/IWaas6dg==\n" \
    "-----END CERTIFICATE-----\n"

#endif /* TESAIOT_HTTPS_ROOT_CA_H */
