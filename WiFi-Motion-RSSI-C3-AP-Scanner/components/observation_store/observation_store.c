#include "observation_store.h"

#include <string.h>

static void sort_i8(int8_t *values, size_t count)
{
    for (size_t index = 1U; index < count; ++index) {
        const int8_t value = values[index];
        size_t position = index;
        while (position > 0U && value < values[position - 1U]) {
            values[position] = values[position - 1U];
            position--;
        }
        values[position] = value;
    }
}

static void sort_u16(uint16_t *values, size_t count)
{
    for (size_t index = 1U; index < count; ++index) {
        const uint16_t value = values[index];
        size_t position = index;
        while (position > 0U && value < values[position - 1U]) {
            values[position] = values[position - 1U];
            position--;
        }
        values[position] = value;
    }
}

static int16_t median_rssi_x10(const int8_t *samples, size_t count)
{
    int8_t working[OBSERVATION_STORE_MAX_SAMPLES_PER_ENTRY];
    memcpy(working, samples, count * sizeof(working[0]));
    sort_i8(working, count);
    if ((count & 1U) != 0U) {
        return (int16_t)working[count / 2U] * 10;
    }
    return (int16_t)(working[count / 2U - 1U] +
                     working[count / 2U]) *
           5;
}

static uint16_t median_u16(uint16_t *values, size_t count)
{
    sort_u16(values, count);
    if ((count & 1U) != 0U) {
        return values[count / 2U];
    }
    return (uint16_t)(((uint32_t)values[count / 2U - 1U] +
                       values[count / 2U] + 1U) /
                      2U);
}

static uint16_t mad_x10(const int8_t *samples,
                        size_t count,
                        int16_t center_x10)
{
    uint16_t deviations[OBSERVATION_STORE_MAX_SAMPLES_PER_ENTRY];
    for (size_t index = 0U; index < count; ++index) {
        const int16_t delta =
            (int16_t)samples[index] * 10 - center_x10;
        deviations[index] =
            (uint16_t)(delta < 0 ? -delta : delta);
    }
    return median_u16(deviations, count);
}

static observation_store_entry_t *find_entry(
    observation_store_t *store,
    const uint8_t bssid[REFERENCE_SELECTOR_BSSID_LENGTH])
{
    for (size_t index = 0U; index < store->entry_count; ++index) {
        if (memcmp(store->entries[index].bssid,
                   bssid,
                   REFERENCE_SELECTOR_BSSID_LENGTH) == 0) {
            return &store->entries[index];
        }
    }
    return NULL;
}

observation_store_status_t observation_store_init(
    observation_store_t *store,
    size_t entry_capacity,
    size_t samples_per_entry)
{
    if (store == NULL || entry_capacity == 0U ||
        entry_capacity > OBSERVATION_STORE_MAX_ENTRIES ||
        samples_per_entry == 0U ||
        samples_per_entry >
            OBSERVATION_STORE_MAX_SAMPLES_PER_ENTRY) {
        return OBSERVATION_STORE_INVALID_ARGUMENT;
    }
    memset(store, 0, sizeof(*store));
    store->entry_capacity = entry_capacity;
    store->samples_per_entry = samples_per_entry;
    return OBSERVATION_STORE_OK;
}

observation_store_status_t observation_store_begin_scan(
    observation_store_t *store,
    uint32_t scan_id)
{
    if (store == NULL || scan_id == 0U) {
        return OBSERVATION_STORE_INVALID_ARGUMENT;
    }
    if (store->scan_open) {
        return OBSERVATION_STORE_SCAN_ALREADY_OPEN;
    }
    if (scan_id <= store->last_completed_scan_id) {
        return OBSERVATION_STORE_SCAN_ID_NOT_INCREASING;
    }
    store->current_scan_id = scan_id;
    store->scan_open = true;
    return OBSERVATION_STORE_OK;
}

observation_store_status_t observation_store_observe(
    observation_store_t *store,
    const uint8_t bssid[REFERENCE_SELECTOR_BSSID_LENGTH],
    const uint8_t *ssid,
    size_t ssid_length,
    uint8_t channel,
    int8_t rssi)
{
    if (store == NULL || bssid == NULL || channel == 0U ||
        channel > 14U ||
        ssid_length > REFERENCE_SELECTOR_SSID_MAX_LENGTH ||
        (ssid == NULL && ssid_length > 0U)) {
        return OBSERVATION_STORE_INVALID_ARGUMENT;
    }
    if (!store->scan_open) {
        return OBSERVATION_STORE_NO_OPEN_SCAN;
    }

    observation_store_entry_t *entry = find_entry(store, bssid);
    if (entry == NULL) {
        if (store->entry_count >= store->entry_capacity) {
            return OBSERVATION_STORE_TABLE_FULL;
        }
        entry = &store->entries[store->entry_count++];
        memset(entry, 0, sizeof(*entry));
        entry->occupied = true;
        memcpy(entry->bssid,
               bssid,
               REFERENCE_SELECTOR_BSSID_LENGTH);
        if (ssid_length > 0U) {
            memcpy(entry->ssid.bytes, ssid, ssid_length);
        }
        entry->ssid.length = (uint8_t)ssid_length;
        entry->channel = channel;
        entry->ssid_stable = true;
        entry->channel_stable = true;
    }
    if (entry->last_observed_scan_id == store->current_scan_id) {
        return OBSERVATION_STORE_DUPLICATE_OBSERVATION;
    }
    if (entry->ssid.length != ssid_length ||
        (ssid_length > 0U &&
         memcmp(entry->ssid.bytes, ssid, ssid_length) != 0)) {
        entry->ssid_stable = false;
    }
    if (entry->channel != channel) {
        entry->channel_stable = false;
    }

    entry->last_observed_scan_id = store->current_scan_id;
    if (entry->observed_scans < UINT16_MAX) {
        entry->observed_scans++;
    }
    if (entry->sample_count >= store->samples_per_entry) {
        return OBSERVATION_STORE_SAMPLE_CAPACITY_REACHED;
    }
    entry->rssi[entry->sample_count++] = rssi;
    return OBSERVATION_STORE_OK;
}

observation_store_status_t observation_store_end_scan(
    observation_store_t *store)
{
    if (store == NULL) {
        return OBSERVATION_STORE_INVALID_ARGUMENT;
    }
    if (!store->scan_open) {
        return OBSERVATION_STORE_NO_OPEN_SCAN;
    }
    if (store->completed_scans < UINT16_MAX) {
        store->completed_scans++;
    }
    store->last_completed_scan_id = store->current_scan_id;
    store->scan_open = false;
    return OBSERVATION_STORE_OK;
}

observation_store_status_t observation_store_export_candidates(
    const observation_store_t *store,
    reference_candidate_t *candidates,
    size_t candidate_capacity,
    size_t *candidate_count)
{
    if (store == NULL || candidate_count == NULL ||
        (store->entry_count > 0U && candidates == NULL)) {
        return OBSERVATION_STORE_INVALID_ARGUMENT;
    }
    if (store->scan_open) {
        return OBSERVATION_STORE_SCAN_ALREADY_OPEN;
    }
    if (candidate_capacity < store->entry_count) {
        return OBSERVATION_STORE_OUTPUT_TOO_SMALL;
    }

    for (size_t index = 0U; index < store->entry_count; ++index) {
        const observation_store_entry_t *entry = &store->entries[index];
        const int16_t center =
            median_rssi_x10(entry->rssi, entry->sample_count);
        candidates[index] = (reference_candidate_t){
            .ssid = entry->ssid,
            .channel = entry->channel,
            .samples = entry->sample_count,
            .observed_scans = entry->observed_scans,
            .total_scans = store->completed_scans,
            .median_rssi_x10 = center,
            .mad_x10 = mad_x10(entry->rssi,
                               entry->sample_count,
                               center),
            .ssid_stable = entry->ssid_stable,
            .channel_stable = entry->channel_stable,
        };
        memcpy(candidates[index].bssid,
               entry->bssid,
               REFERENCE_SELECTOR_BSSID_LENGTH);
    }
    *candidate_count = store->entry_count;
    return OBSERVATION_STORE_OK;
}
