#include "motion_detector.h"

#include <math.h>
#include <string.h>

static float max_float(float a, float b)
{
    return a > b ? a : b;
}

bool motion_detector_config_valid(const motion_detector_config_t *config)
{
    return config != NULL &&
           config->window_size >= 4U &&
           config->window_size <= MOTION_DETECTOR_MAX_WINDOW &&
           config->calibration_samples >= 2U &&
           isfinite(config->sigma_multiplier) &&
           config->sigma_multiplier > 0.0f &&
           isfinite(config->minimum_threshold) &&
           config->minimum_threshold > 0.0f &&
           config->trigger_consecutive >= 1U &&
           config->clear_consecutive >= 1U;
}

bool motion_detector_init(motion_detector_t *detector,
                          const motion_detector_config_t *config)
{
    if (detector == NULL || !motion_detector_config_valid(config)) {
        return false;
    }

    memset(detector, 0, sizeof(*detector));
    detector->config = *config;
    detector->state = MOTION_DETECTOR_CALIBRATING;
    detector->threshold = config->minimum_threshold;
    return true;
}

void motion_detector_reset(motion_detector_t *detector)
{
    if (detector == NULL) {
        return;
    }

    const motion_detector_config_t config = detector->config;
    (void)motion_detector_init(detector, &config);
}

static float calculate_score(const motion_detector_t *detector)
{
    double total = 0.0;
    const size_t window = detector->config.window_size;

    for (size_t i = 1U; i < window; ++i) {
        const size_t previous_index = (detector->write_index + i - 1U) % window;
        const size_t current_index = (detector->write_index + i) % window;
        total += fabs((double)detector->samples[current_index] -
                      (double)detector->samples[previous_index]);
    }

    return (float)(total / (double)(window - 1U));
}

static float calibration_stddev(const motion_detector_t *detector)
{
    if (detector->calibration_count < 2U) {
        return 0.0f;
    }

    return (float)sqrt(detector->calibration_m2 /
                       (double)(detector->calibration_count - 1U));
}

static motion_detector_result_t make_result(const motion_detector_t *detector,
                                            bool score_ready,
                                            bool changed,
                                            float score)
{
    motion_detector_result_t result = {
        .state = detector->state,
        .score_ready = score_ready,
        .calibrated = detector->state != MOTION_DETECTOR_CALIBRATING,
        .state_changed = changed,
        .score = score,
        .threshold = detector->threshold,
        .baseline_mean = (float)detector->calibration_mean,
        .baseline_stddev = calibration_stddev(detector),
    };
    return result;
}

motion_detector_result_t motion_detector_push(motion_detector_t *detector,
                                               int8_t rssi_dbm)
{
    if (detector == NULL) {
        const motion_detector_result_t invalid = {0};
        return invalid;
    }

    const size_t window = detector->config.window_size;
    detector->samples[detector->write_index] = rssi_dbm;
    detector->write_index = (detector->write_index + 1U) % window;
    if (detector->sample_count < window) {
        detector->sample_count++;
    }

    if (detector->sample_count < window) {
        return make_result(detector, false, false, 0.0f);
    }

    const float score = calculate_score(detector);
    bool changed = false;

    if (detector->state == MOTION_DETECTOR_CALIBRATING) {
        detector->calibration_count++;
        const double delta = (double)score - detector->calibration_mean;
        detector->calibration_mean += delta / (double)detector->calibration_count;
        const double delta2 = (double)score - detector->calibration_mean;
        detector->calibration_m2 += delta * delta2;

        if (detector->calibration_count >= detector->config.calibration_samples) {
            const float stddev = calibration_stddev(detector);
            detector->threshold = max_float(
                detector->config.minimum_threshold,
                (float)detector->calibration_mean +
                    detector->config.sigma_multiplier * stddev);
            detector->state = MOTION_DETECTOR_IDLE;
            changed = true;
        }

        return make_result(detector, true, changed, score);
    }

    if (score > detector->threshold) {
        detector->above_count++;
        detector->below_count = 0U;
    } else {
        detector->below_count++;
        detector->above_count = 0U;
    }

    if (detector->state == MOTION_DETECTOR_IDLE &&
        detector->above_count >= detector->config.trigger_consecutive) {
        detector->state = MOTION_DETECTOR_ACTIVE;
        detector->above_count = 0U;
        changed = true;
    } else if (detector->state == MOTION_DETECTOR_ACTIVE &&
               detector->below_count >= detector->config.clear_consecutive) {
        detector->state = MOTION_DETECTOR_IDLE;
        detector->below_count = 0U;
        changed = true;
    }

    return make_result(detector, true, changed, score);
}

const char *motion_detector_state_name(motion_detector_state_t state)
{
    switch (state) {
    case MOTION_DETECTOR_CALIBRATING:
        return "calibrating";
    case MOTION_DETECTOR_IDLE:
        return "idle";
    case MOTION_DETECTOR_ACTIVE:
        return "motion";
    default:
        return "unknown";
    }
}
