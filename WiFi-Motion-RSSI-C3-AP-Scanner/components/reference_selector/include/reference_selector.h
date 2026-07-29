#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define REFERENCE_SELECTOR_BSSID_LENGTH 6U
#define REFERENCE_SELECTOR_SSID_MAX_LENGTH 32U
#define REFERENCE_SELECTOR_MAX_CANDIDATES 32U

typedef struct {
    uint8_t bytes[REFERENCE_SELECTOR_SSID_MAX_LENGTH];
    uint8_t length;
} reference_ssid_t;

typedef struct {
    uint8_t bssid[REFERENCE_SELECTOR_BSSID_LENGTH];
    reference_ssid_t ssid;
    uint8_t channel;
    uint16_t samples;
    uint16_t observed_scans;
    uint16_t total_scans;
    int16_t median_rssi_x10;
    uint16_t mad_x10;
    bool ssid_stable;
    bool channel_stable;
} reference_candidate_t;

typedef struct {
    uint16_t minimum_samples;
    uint16_t minimum_presence_permille;
    int16_t minimum_rssi_x10;
    uint16_t maximum_mad_x10;
    uint8_t maximum_references;
} reference_selector_policy_t;

typedef enum {
    REFERENCE_REJECT_NONE = 0,
    REFERENCE_REJECT_SSID_NOT_SELECTED = 1U << 0,
    REFERENCE_REJECT_INSUFFICIENT_SAMPLES = 1U << 1,
    REFERENCE_REJECT_LOW_PRESENCE = 1U << 2,
    REFERENCE_REJECT_WEAK_RSSI = 1U << 3,
    REFERENCE_REJECT_HIGH_MAD = 1U << 4,
    REFERENCE_REJECT_UNSTABLE_CHANNEL = 1U << 5,
    REFERENCE_REJECT_LIMIT_REACHED = 1U << 6,
    REFERENCE_REJECT_UNSTABLE_SSID = 1U << 7,
} reference_rejection_t;

typedef struct {
    size_t candidate_index;
    uint32_t rejection_flags;
    uint8_t rank;
    bool selected;
} reference_decision_t;

typedef enum {
    REFERENCE_SELECTOR_OK = 0,
    REFERENCE_SELECTOR_INVALID_ARGUMENT,
    REFERENCE_SELECTOR_TOO_MANY_CANDIDATES,
    REFERENCE_SELECTOR_DUPLICATE_BSSID,
} reference_selector_status_t;

reference_selector_status_t reference_selector_select(
    const reference_candidate_t *candidates,
    size_t candidate_count,
    const reference_ssid_t *manual_ssids,
    size_t manual_ssid_count,
    const reference_selector_policy_t *policy,
    reference_decision_t *decisions,
    size_t decision_capacity,
    size_t *selected_count);

#ifdef __cplusplus
}
#endif
