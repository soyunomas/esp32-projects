#include "reference_store.h"

#include <stddef.h>

uint32_t reference_store_crc32(const void *data, size_t size)
{
    const uint8_t *bytes = data;
    uint32_t crc = UINT32_MAX;
    for (size_t index = 0U; index < size; ++index) {
        crc ^= bytes[index];
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

void reference_store_finalize(reference_store_blob_t *blob)
{
    if (blob == NULL) {
        return;
    }
    blob->magic = REFERENCE_STORE_MAGIC;
    blob->version = REFERENCE_STORE_VERSION;
    blob->crc32 = reference_store_crc32(
        blob, offsetof(reference_store_blob_t, crc32));
}

bool reference_store_validate(const reference_store_blob_t *blob)
{
    if (blob == NULL || blob->magic != REFERENCE_STORE_MAGIC ||
        blob->version != REFERENCE_STORE_VERSION || blob->count == 0U ||
        blob->count > REFERENCE_STORE_MAX_REFERENCES ||
        blob->manual_selection > 1U) {
        return false;
    }
    for (uint8_t index = 0U; index < blob->count; ++index) {
        const reference_store_entry_t *entry = &blob->entries[index];
        if (entry->channel == 0U || entry->channel > 14U ||
            entry->ssid.length > REFERENCE_SELECTOR_SSID_MAX_LENGTH) {
            return false;
        }
    }
    return blob->crc32 ==
           reference_store_crc32(
               blob, offsetof(reference_store_blob_t, crc32));
}
