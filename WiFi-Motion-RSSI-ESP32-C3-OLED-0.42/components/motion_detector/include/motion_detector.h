#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOTION_DETECTOR_MAX_WINDOW 128U
#define MOTION_DETECTOR_MAX_CALIBRATION 512U
#define MOTION_DETECTOR_BASELINE_WINDOW 64U

typedef enum {
    MOTION_FEATURE_MEAN_ABSOLUTE_DIFFERENCE = 0,
    MOTION_FEATURE_STANDARD_DEVIATION,
    MOTION_FEATURE_SAMPLE_VARIANCE,
    MOTION_FEATURE_RANGE,
    MOTION_FEATURE_MEDIAN_ABSOLUTE_DEVIATION,
    MOTION_FEATURE_COUNT,
} motion_feature_algorithm_t;

typedef enum {
    MOTION_BASELINE_MEAN_STDDEV = 0,
    MOTION_BASELINE_MEDIAN_MAD,
    MOTION_BASELINE_COUNT,
} motion_baseline_mode_t;

typedef enum {
    MOTION_PROFILE_LOW = 0,
    MOTION_PROFILE_BALANCED,
    MOTION_PROFILE_HIGH,
    MOTION_PROFILE_COUNT,
} motion_sensitivity_profile_t;

typedef struct {
    size_t window_size;
    size_t calibration_samples;
    motion_feature_algorithm_t algorithm;
    motion_baseline_mode_t baseline_mode;
    float sigma_multiplier;
    float minimum_threshold;
    float release_threshold_ratio;
    float baseline_update_alpha;
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
    float release_threshold;
    float baseline_mean;
    float baseline_stddev;
} motion_detector_result_t;

typedef struct {
    motion_detector_config_t config;
    float samples[MOTION_DETECTOR_MAX_WINDOW];
    size_t sample_count;
    size_t write_index;

    float calibration_scores[MOTION_DETECTOR_MAX_CALIBRATION];
    float statistics_working[MOTION_DETECTOR_MAX_CALIBRATION];
    size_t calibration_count;
    float baseline_scores[MOTION_DETECTOR_BASELINE_WINDOW];
    size_t baseline_score_count;
    size_t baseline_write_index;
    float baseline_center;
    float baseline_spread;
    float threshold;
    float release_threshold;

    size_t above_count;
    size_t below_count;
    motion_detector_state_t state;
} motion_detector_t;

bool motion_detector_config_valid(const motion_detector_config_t *config);
bool motion_detector_config_for_profile(motion_detector_config_t *config,
                                        motion_sensitivity_profile_t profile,
                                        motion_feature_algorithm_t algorithm,
                                        motion_baseline_mode_t baseline_mode);
bool motion_detector_init(motion_detector_t *detector,
                          const motion_detector_config_t *config);
void motion_detector_reset(motion_detector_t *detector);
motion_detector_result_t motion_detector_push(motion_detector_t *detector,
                                               float sample);
const char *motion_detector_state_name(motion_detector_state_t state);
const char *motion_feature_algorithm_name(motion_feature_algorithm_t algorithm);
const char *motion_baseline_mode_name(motion_baseline_mode_t mode);
const char *motion_sensitivity_profile_name(motion_sensitivity_profile_t profile);

#ifdef __cplusplus
}
#endif
