#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "reference_selector.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OBSERVATION_STORE_MAX_ENTRIES 32U
#define OBSERVATION_STORE_MAX_SAMPLES_PER_ENTRY 128U

typedef struct {
    bool occupied;
    uint8_t bssid[REFERENCE_SELECTOR_BSSID_LENGTH];
    reference_ssid_t ssid;
    uint8_t channel;
    bool ssid_stable;
    bool channel_stable;
    int8_t rssi[OBSERVATION_STORE_MAX_SAMPLES_PER_ENTRY];
    uint16_t sample_count;
    uint16_t observed_scans;
    uint32_t last_observed_scan_id;
} observation_store_entry_t;

typedef struct {
    observation_store_entry_t entries[OBSERVATION_STORE_MAX_ENTRIES];
    size_t entry_count;
    size_t entry_capacity;
    size_t samples_per_entry;
    uint16_t completed_scans;
    uint32_t current_scan_id;
    uint32_t last_completed_scan_id;
    bool scan_open;
} observation_store_t;

typedef enum {
    OBSERVATION_STORE_OK = 0,
    OBSERVATION_STORE_INVALID_ARGUMENT,
    OBSERVATION_STORE_SCAN_ALREADY_OPEN,
    OBSERVATION_STORE_NO_OPEN_SCAN,
    OBSERVATION_STORE_SCAN_ID_NOT_INCREASING,
    OBSERVATION_STORE_DUPLICATE_OBSERVATION,
    OBSERVATION_STORE_TABLE_FULL,
    OBSERVATION_STORE_SAMPLE_CAPACITY_REACHED,
    OBSERVATION_STORE_OUTPUT_TOO_SMALL,
} observation_store_status_t;

observation_store_status_t observation_store_init(
    observation_store_t *store,
    size_t entry_capacity,
    size_t samples_per_entry);
observation_store_status_t observation_store_begin_scan(
    observation_store_t *store,
    uint32_t scan_id);
observation_store_status_t observation_store_observe(
    observation_store_t *store,
    const uint8_t bssid[REFERENCE_SELECTOR_BSSID_LENGTH],
    const uint8_t *ssid,
    size_t ssid_length,
    uint8_t channel,
    int8_t rssi);
observation_store_status_t observation_store_end_scan(
    observation_store_t *store);
observation_store_status_t observation_store_export_candidates(
    const observation_store_t *store,
    reference_candidate_t *candidates,
    size_t candidate_capacity,
    size_t *candidate_count);

#ifdef __cplusplus
}
#endif
