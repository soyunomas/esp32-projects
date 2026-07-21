#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PRESENCE_MAX_TRACKED_APS 6
#define PRESENCE_MAX_CANDIDATES 24
#define PRESENCE_MAX_WINDOW 32

typedef enum {
    PRESENCE_STATE_DISCOVERY = 0,
    PRESENCE_STATE_CALIBRATING,
    PRESENCE_STATE_IDLE,
    PRESENCE_STATE_MOTION,
    PRESENCE_STATE_ERROR,
} presence_state_t;

typedef struct {
    uint8_t bssid[6];
    int8_t rssi;
    uint8_t channel;
} presence_observation_t;

typedef struct {
    uint8_t max_tracked_aps;
    uint8_t discovery_scans;
    uint8_t minimum_candidate_hits;
    uint8_t window_size;
    uint16_t calibration_samples;
    float sigma_multiplier;
    float minimum_threshold;
    float release_ratio;
    float baseline_alpha;
    float missing_ap_penalty;
    uint8_t trigger_consecutive;
    uint8_t clear_consecutive;
    int8_t minimum_rssi;
} presence_detector_config_t;

typedef struct {
    uint8_t bssid[6];
    uint8_t channel;
    int8_t current_rssi;
    bool seen_in_last_scan;
    uint32_t total_seen;
    float variation;
} presence_track_status_t;

typedef struct {
    presence_state_t state;
    float score;
    float baseline_center;
    float baseline_spread;
    float trigger_threshold;
    float release_threshold;
    uint16_t calibration_collected;
    uint8_t tracked_count;
    uint8_t visible_tracked_count;
    uint32_t scans_processed;
    uint32_t motion_events;
    presence_track_status_t tracks[PRESENCE_MAX_TRACKED_APS];
} presence_detector_status_t;

typedef struct {
    uint8_t bssid[6];
    uint8_t channel;
    uint16_t hits;
    int32_t rssi_sum;
} presence_candidate_internal_t;

typedef struct {
    uint8_t bssid[6];
    uint8_t channel;
    int8_t current_rssi;
    bool seen_in_last_scan;
    uint32_t total_seen;
    int8_t history[PRESENCE_MAX_WINDOW];
    uint8_t history_count;
    uint8_t history_head;
    float variation;
} presence_track_internal_t;

typedef struct {
    presence_detector_config_t config;
    presence_state_t state;
    presence_candidate_internal_t candidates[PRESENCE_MAX_CANDIDATES];
    uint8_t candidate_count;
    presence_track_internal_t tracks[PRESENCE_MAX_TRACKED_APS];
    uint8_t tracked_count;
    uint8_t discovery_scans_done;
    uint16_t calibration_collected;
    double calibration_mean;
    double calibration_m2;
    float baseline_center;
    float baseline_spread;
    float score;
    float trigger_threshold;
    float release_threshold;
    uint8_t trigger_streak;
    uint8_t clear_streak;
    uint32_t scans_processed;
    uint32_t motion_events;
} presence_detector_t;

presence_detector_config_t presence_detector_default_config(void);
bool presence_detector_validate_config(presence_detector_config_t *config);
void presence_detector_init(presence_detector_t *detector,
                            const presence_detector_config_t *config);
void presence_detector_set_config(presence_detector_t *detector,
                                  const presence_detector_config_t *config,
                                  bool rediscover);
void presence_detector_recalibrate(presence_detector_t *detector,
                                   bool rediscover);
void presence_detector_process_scan(presence_detector_t *detector,
                                    const presence_observation_t *observations,
                                    size_t observation_count);
void presence_detector_get_status(const presence_detector_t *detector,
                                  presence_detector_status_t *status);
const char *presence_detector_state_name(presence_state_t state);

#ifdef __cplusplus
}
#endif
