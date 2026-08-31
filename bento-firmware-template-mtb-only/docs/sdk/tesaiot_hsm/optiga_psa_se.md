# optiga_psa_se.h

Header file for OPTIGA PSA Secure Element driver Related Document: See README.md

> This header declares **5 functions implemented in open source that ships in this package** — not in the closed archive. Read the .c next to it; you own that code.

## Constants

| Name | Value |
|---|---|
| `OPTIGA_PSA_SE_H` | `#include` |

## Functions (implemented in open source in this package)

### `optiga_psa_clear_all_slots`

```c
void optiga_psa_clear_all_slots(void);
```

@brief Clear all PSA key slots (reset internal map) Call this after MQTT disconnect to ensure fresh state for next connection. Required when switching between MQTT test and CSR workflow to avoid TLS errors.

### `optiga_psa_forget_all_slots`

```c
void optiga_psa_forget_all_slots(void);
```

Forgets every slot including slot 0. Only after PSA has wiped its own key store — see the note on the definition.

### `optiga_psa_get_signing_key_oid`

```c
optiga_key_id_t optiga_psa_get_signing_key_oid(void);
```

@brief Get the currently configured OPTIGA signing key OID @return Current key OID

### `optiga_psa_register`

```c
psa_status_t optiga_psa_register(void);
```

File Name:   optiga_psa_se.h Description: Header file for OPTIGA PSA Secure Element driver Related Document: See README.md Copyright 2024-2025, Cypress Semiconductor Corporation (an Infineon company) or an affiliate of Cypress Semiconductor Corporation.  All rights reserved. This software, including source code, documentation and related materials ("Software") is owned by Cypress Semiconductor Corporation or one of its affiliates ("Cypress") and is protected by and subject to worldwide patent protection (United States and foreign), United States copyright laws and international treaty provisions. Therefore, you may use this Software only as provided in the license agreement accompanying the software package from which you obtained this Software ("EULA"). If no EULA applies, Cypress hereby grants you a personal, non-exclusive, non-transferable license to copy, modify, and compile the Software source code solely for use in connection with Cypress's integrated circuit products.  Any reproduction, modification, translation, compilation, or representation of this Software except as specified above is prohibited without the express written permission of Cypress. Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress reserves the right to make changes to the Software without notice. Cypress does not assume any liability arising out of the application or use of the Software or any product or circuit described in the Software. Cypress does not authorize its products for use in any products where a malfunction or failure of the Cypress product may reasonably be expected to result in significant property damage, injury or death ("High Risk Product"). By including Cypress's product in a High Risk Product, the manufacturer of such system or application assumes all risk of such use and in doing so agrees to indemnify Cypress against all liability. / #ifndef OPTIGA_PSA_SE_H #define OPTIGA_PSA_SE_H #include "psa/crypto.h" #include "include/common/optiga_lib_common.h" #ifdef __cplusplus extern "C" { #endif /** @brief Register OPTIGA as a PSA Secure Element driver @return psa_status_t PSA_SUCCESS on success, error code otherwise

### `optiga_psa_set_signing_key_oid`

```c
void optiga_psa_set_signing_key_oid(optiga_key_id_t oid);
```

@brief Set the OPTIGA key OID to use for TLS signing @param oid Key OID (0xE0F0 for factory cert, 0xE0F1 for TESAIoT cert) Call this BEFORE psa_generate_key() to select which key to use. Must match the certificate being used: - Certificate 0xE0E0 (factory) -> Key 0xE0F0 - Certificate 0xE0E1 (TESAIoT) -> Key 0xE0F1
