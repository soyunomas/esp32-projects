#ifndef MULTIREF_DETECTOR_H
#define MULTIREF_DETECTOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MULTIREF_DETECTOR_BSSID_LENGTH 6U
#define MULTIREF_DETECTOR_MAX_REFERENCES 8U
#define MULTIREF_DETECTOR_MAX_SCORE_WINDOW 32U

typedef enum {
    MULTIREF_STATE_CALIBRATING = 0,
    MULTIREF_STATE_WARMUP,
    MULTIREF_STATE_IDLE,
    MULTIREF_STATE_MOTION,
    MULTIREF_STATE_DEGRADED,
    MULTIREF_STATE_NO_DATA,
} multiref_state_t;

typedef enum {
    MULTIREF_OK = 0,
    MULTIREF_INVALID_ARGUMENT,
    MULTIREF_NO_OPEN_SCAN,
    MULTIREF_SCAN_ALREADY_OPEN,
    MULTIREF_SCAN_ID_NOT_INCREASING,
    MULTIREF_DUPLICATE_OBSERVATION,
} multiref_status_t;

typedef struct {
    uint8_t bssid[MULTIREF_DETECTOR_BSSID_LENGTH];
    int16_t baseline_rssi_x10;
    uint16_t baseline_mad_x10;
} multiref_reference_t;

typedef struct {
    uint16_t minimum_coverage_permille;
    uint16_t noise_floor_x10;
    uint16_t trigger_score_x100;
    uint16_t release_score_x100;
    uint16_t adaptive_sigma_x100;
    uint16_t baseline_alpha_permille;
    uint8_t adaptive_window;
    uint8_t warmup_scans;
    uint8_t trigger_consecutive;
    uint8_t clear_consecutive;
    uint8_t unhealthy_consecutive;
    uint8_t recovery_consecutive;
    uint8_t stale_after_scans;
} multiref_config_t;

typedef struct {
    multiref_state_t state;
    multiref_state_t previous_state;
    bool state_changed;
    bool score_ready;
    uint16_t score_x100;
    uint16_t trigger_score_x100;
    uint16_t release_score_x100;
    uint16_t baseline_score_center_x100;
    uint16_t baseline_score_spread_x100;
    uint16_t coverage_permille;
    uint8_t observed_references;
    uint8_t reference_count;
    uint8_t trigger_count;
    uint8_t trigger_required;
    uint8_t stale_references;
    uint16_t oldest_reference_age_scans;
} multiref_result_t;

typedef struct {
    multiref_config_t config;
    multiref_reference_t references[MULTIREF_DETECTOR_MAX_REFERENCES];
    int8_t current_rssi[MULTIREF_DETECTOR_MAX_REFERENCES];
    bool seen[MULTIREF_DETECTOR_MAX_REFERENCES];
    uint16_t age_scans[MULTIREF_DETECTOR_MAX_REFERENCES];
    uint16_t baseline_scores[MULTIREF_DETECTOR_MAX_SCORE_WINDOW];
    uint16_t adaptive_trigger_score_x100;
    uint16_t adaptive_release_score_x100;
    uint16_t baseline_score_center_x100;
    uint16_t baseline_score_spread_x100;
    uint8_t reference_count;
    uint8_t baseline_score_count;
    uint8_t baseline_score_write_index;
    uint8_t warmup_count;
    uint8_t trigger_count;
    uint8_t clear_count;
    uint8_t unhealthy_count;
    uint8_t recovery_count;
    uint32_t last_scan_id;
    bool scan_open;
    multiref_state_t state;
} multiref_detector_t;

bool multiref_config_valid(const multiref_config_t *config);
multiref_status_t multiref_detector_init(
    multiref_detector_t *detector,
    const multiref_config_t *config,
    const multiref_reference_t *references,
    size_t reference_count);
multiref_status_t multiref_detector_begin_scan(
    multiref_detector_t *detector, uint32_t scan_id);
multiref_status_t multiref_detector_observe(
    multiref_detector_t *detector,
    const uint8_t bssid[MULTIREF_DETECTOR_BSSID_LENGTH],
    int8_t rssi);
multiref_status_t multiref_detector_end_scan(
    multiref_detector_t *detector, multiref_result_t *result);
const char *multiref_state_name(multiref_state_t state);

#endif
