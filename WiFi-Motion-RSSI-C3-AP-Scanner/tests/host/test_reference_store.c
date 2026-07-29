#include "reference_store.h"

#include <assert.h>
#include <string.h>

static reference_store_blob_t valid_blob(void)
{
    reference_store_blob_t blob = {
        .count = 2U,
        .manual_selection = 0U,
        .entries = {
            {
                .bssid = {0U, 1U, 2U, 3U, 4U, 5U},
                .ssid = {.bytes = {'A'}, .length = 1U},
                .channel = 1U,
                .median_rssi_x10 = -400,
                .mad_x10 = 10U,
            },
            {
                .bssid = {6U, 7U, 8U, 9U, 10U, 11U},
                .ssid = {.bytes = {'B'}, .length = 1U},
                .channel = 6U,
                .median_rssi_x10 = -800,
                .mad_x10 = 20U,
            },
        },
    };
    reference_store_finalize(&blob);
    return blob;
}

int main(void)
{
    reference_store_blob_t blob = valid_blob();
    assert(reference_store_validate(&blob));
    assert(blob.magic == REFERENCE_STORE_MAGIC);
    assert(blob.version == REFERENCE_STORE_VERSION);

    reference_store_blob_t corrupt = blob;
    corrupt.entries[0].channel = 11U;
    assert(!reference_store_validate(&corrupt));
    reference_store_finalize(&corrupt);
    assert(reference_store_validate(&corrupt));

    corrupt = blob;
    corrupt.count = REFERENCE_STORE_MAX_REFERENCES + 1U;
    reference_store_finalize(&corrupt);
    assert(!reference_store_validate(&corrupt));

    corrupt = blob;
    corrupt.entries[0].channel = 0U;
    reference_store_finalize(&corrupt);
    assert(!reference_store_validate(&corrupt));

    assert(reference_store_crc32("123456789", 9U) == 0xCBF43926U);
    assert(!reference_store_validate(NULL));
    return 0;
}
