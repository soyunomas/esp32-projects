#ifndef REFERENCE_STORE_H
#define REFERENCE_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "reference_selector.h"

#define REFERENCE_STORE_MAGIC 0x52454653U
#define REFERENCE_STORE_VERSION 1U
#define REFERENCE_STORE_MAX_REFERENCES 8U

typedef enum {
    REFERENCE_STORE_OK = 0,
    REFERENCE_STORE_NOT_FOUND,
    REFERENCE_STORE_INVALID,
    REFERENCE_STORE_IO_ERROR,
} reference_store_status_t;

typedef struct {
    uint8_t bssid[REFERENCE_SELECTOR_BSSID_LENGTH];
    reference_ssid_t ssid;
    uint8_t channel;
    int16_t median_rssi_x10;
    uint16_t mad_x10;
} reference_store_entry_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t count;
    uint8_t manual_selection;
    reference_store_entry_t entries[REFERENCE_STORE_MAX_REFERENCES];
    uint32_t crc32;
} reference_store_blob_t;

uint32_t reference_store_crc32(const void *data, size_t size);
void reference_store_finalize(reference_store_blob_t *blob);
bool reference_store_validate(const reference_store_blob_t *blob);
reference_store_status_t reference_store_load(reference_store_blob_t *blob);
reference_store_status_t reference_store_save(reference_store_blob_t *blob);
reference_store_status_t reference_store_erase(void);

#endif
