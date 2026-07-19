#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOTION_DETECTOR_MAX_WINDOW 128U

typedef struct {
    size_t window_size;
    size_t calibration_samples;
    float sigma_multiplier;
    float minimum_threshold;
    size_t trigger_consecutive;
    size_t clear_consecutive;
} motion_detector_config_t;

typedef enum {
    MOTION_DETECTOR_CALIBRATING = 0,
    MOTION_DETECTOR_IDLE,
    MOTION_DETECTOR_ACTIVE,
} motion_detector_state_t;

typedef struct {
    motion_detector_state_t state;
    bool score_ready;
    bool calibrated;
    bool state_changed;
    float score;
    float threshold;
    float baseline_mean;
    float baseline_stddev;
} motion_detector_result_t;

typedef struct {
    motion_detector_config_t config;
    int8_t samples[MOTION_DETECTOR_MAX_WINDOW];
    size_t sample_count;
    size_t write_index;

    size_t calibration_count;
    double calibration_mean;
    double calibration_m2;
    float threshold;

    size_t above_count;
    size_t below_count;
    motion_detector_state_t state;
} motion_detector_t;

bool motion_detector_config_valid(const motion_detector_config_t *config);
bool motion_detector_init(motion_detector_t *detector,
                          const motion_detector_config_t *config);
void motion_detector_reset(motion_detector_t *detector);
motion_detector_result_t motion_detector_push(motion_detector_t *detector,
                                               int8_t rssi_dbm);
const char *motion_detector_state_name(motion_detector_state_t state);

#ifdef __cplusplus
}
#endif
