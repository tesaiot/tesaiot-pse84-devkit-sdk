/******************************************************************************
* File Name:   psa_its_ram_stubs.c
*
* Description: Minimal RAM-backed PSA Internal Trusted Storage (ITS) stubs.
*              Required by MBEDTLS_PSA_CRYPTO_SE_C for SE key slot persistence.
*
*              This implementation stores key metadata in RAM only (volatile).
*              Keys are lost on reboot — acceptable since we use
*              PSA_KEY_PERSISTENCE_VOLATILE for OPTIGA mTLS keys.
*
*              NO flash operations. NO filesystem dependency.
******************************************************************************/

#include "psa/internal_trusted_storage.h"
#include <string.h>
#include <stdlib.h>

/* Simple fixed-size slot table for ITS entries.
 * PSA SE framework stores ~1 entry per active key slot.
 * 8 slots is plenty for mTLS (typically 1-2 active keys). */
#define ITS_MAX_SLOTS  8
#define ITS_MAX_DATA   256

typedef struct {
    psa_storage_uid_t uid;
    uint8_t           data[ITS_MAX_DATA];
    size_t            len;
    bool              used;
} its_slot_t;

static its_slot_t s_its_slots[ITS_MAX_SLOTS];

static its_slot_t *its_find(psa_storage_uid_t uid)
{
    for (int i = 0; i < ITS_MAX_SLOTS; i++) {
        if (s_its_slots[i].used && s_its_slots[i].uid == uid) {
            return &s_its_slots[i];
        }
    }
    return NULL;
}

static its_slot_t *its_alloc(void)
{
    for (int i = 0; i < ITS_MAX_SLOTS; i++) {
        if (!s_its_slots[i].used) {
            return &s_its_slots[i];
        }
    }
    return NULL;
}

psa_status_t psa_its_set(psa_storage_uid_t uid,
                          size_t data_length,
                          const void *p_data,
                          psa_storage_create_flags_t create_flags)
{
    (void)create_flags;

    if (data_length > ITS_MAX_DATA) {
        return PSA_ERROR_INSUFFICIENT_STORAGE;
    }

    its_slot_t *slot = its_find(uid);
    if (!slot) {
        slot = its_alloc();
        if (!slot) {
            return PSA_ERROR_INSUFFICIENT_STORAGE;
        }
    }

    slot->uid  = uid;
    slot->len  = data_length;
    slot->used = true;
    if (data_length > 0 && p_data) {
        memcpy(slot->data, p_data, data_length);
    }

    return PSA_SUCCESS;
}

psa_status_t psa_its_get(psa_storage_uid_t uid,
                          size_t data_offset,
                          size_t data_size,
                          void *p_data,
                          size_t *p_data_length)
{
    its_slot_t *slot = its_find(uid);
    if (!slot) {
        return PSA_ERROR_DOES_NOT_EXIST;
    }

    if (data_offset > slot->len) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }

    size_t avail = slot->len - data_offset;
    size_t copy  = (data_size < avail) ? data_size : avail;

    if (copy > 0 && p_data) {
        memcpy(p_data, slot->data + data_offset, copy);
    }
    if (p_data_length) {
        *p_data_length = copy;
    }

    return PSA_SUCCESS;
}

psa_status_t psa_its_get_info(psa_storage_uid_t uid,
                               struct psa_storage_info_t *p_info)
{
    its_slot_t *slot = its_find(uid);
    if (!slot) {
        return PSA_ERROR_DOES_NOT_EXIST;
    }

    if (p_info) {
        p_info->size  = (uint32_t)slot->len;
        p_info->flags = 0;
    }

    return PSA_SUCCESS;
}

psa_status_t psa_its_remove(psa_storage_uid_t uid)
{
    its_slot_t *slot = its_find(uid);
    if (!slot) {
        return PSA_ERROR_DOES_NOT_EXIST;
    }

    memset(slot, 0, sizeof(*slot));
    return PSA_SUCCESS;
}
