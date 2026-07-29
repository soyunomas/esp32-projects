#include "motion_detector.h"

#include <math.h>
#include <string.h>

#define ROBUST_MAD_SCALE 1.4826f

static float max_float(float a, float b)
{
    return a > b ? a : b;
}

static void sort_float(float *values, size_t count)
{
    for (size_t i = 1U; i < count; ++i) {
        const float value = values[i];
        size_t position = i;
        while (position > 0U && values[position - 1U] > value) {
            values[position] = values[position - 1U];
            position--;
        }
        values[position] = value;
    }
}

static float median(float *values, size_t count)
{
    sort_float(values, count);
    const size_t middle = count / 2U;
    if ((count % 2U) != 0U) {
        return values[middle];
    }
    return (values[middle - 1U] + values[middle]) * 0.5f;
}

static void mean_and_stddev(const float *values,
                            size_t count,
                            float *center,
                            float *spread)
{
    double mean = 0.0;
    double m2 = 0.0;
    for (size_t i = 0U; i < count; ++i) {
        const double delta = (double)values[i] - mean;
        mean += delta / (double)(i + 1U);
        const double delta2 = (double)values[i] - mean;
        m2 += delta * delta2;
    }
    *center = (float)mean;
    *spread = count > 1U ? (float)sqrt(m2 / (double)(count - 1U)) : 0.0f;
}

static void median_and_mad(const float *values,
                           size_t count,
                           float *center,
                           float *spread,
                           float *working)
{
    memcpy(working, values, count * sizeof(working[0]));
    *center = median(working, count);
    for (size_t i = 0U; i < count; ++i) {
        working[i] = fabsf(values[i] - *center);
    }
    *spread = median(working, count) * ROBUST_MAD_SCALE;
}

static void baseline_statistics(motion_baseline_mode_t mode,
                                const float *values,
                                size_t count,
                                float *center,
                                float *spread,
                                float *working)
{
    if (mode == MOTION_BASELINE_MEDIAN_MAD) {
        median_and_mad(values, count, center, spread, working);
    } else {
        mean_and_stddev(values, count, center, spread);
    }
}

static void update_thresholds(motion_detector_t *detector)
{
    detector->threshold = max_float(
        detector->config.minimum_threshold,
        detector->baseline_center +
            detector->config.sigma_multiplier * detector->baseline_spread);
    detector->release_threshold =
        detector->threshold * detector->config.release_threshold_ratio;
}

bool motion_detector_config_valid(const motion_detector_config_t *config)
{
    return config != NULL &&
           config->window_size >= 4U &&
           config->window_size <= MOTION_DETECTOR_MAX_WINDOW &&
           config->calibration_samples >= 2U &&
           config->calibration_samples <= MOTION_DETECTOR_MAX_CALIBRATION &&
           config->algorithm >= MOTION_FEATURE_MEAN_ABSOLUTE_DIFFERENCE &&
           config->algorithm < MOTION_FEATURE_COUNT &&
           config->baseline_mode >= MOTION_BASELINE_MEAN_STDDEV &&
           config->baseline_mode < MOTION_BASELINE_COUNT &&
           isfinite(config->sigma_multiplier) &&
           config->sigma_multiplier > 0.0f &&
           isfinite(config->minimum_threshold) &&
           config->minimum_threshold > 0.0f &&
           isfinite(config->release_threshold_ratio) &&
           config->release_threshold_ratio > 0.0f &&
           config->release_threshold_ratio < 1.0f &&
           isfinite(config->baseline_update_alpha) &&
           config->baseline_update_alpha > 0.0f &&
           config->baseline_update_alpha <= 1.0f &&
           config->trigger_consecutive >= 1U &&
           config->clear_consecutive >= 1U;
}

bool motion_detector_config_for_profile(motion_detector_config_t *config,
                                        motion_sensitivity_profile_t profile,
                                        motion_feature_algorithm_t algorithm,
                                        motion_baseline_mode_t baseline_mode)
{
    if (config == NULL || profile < MOTION_PROFILE_LOW ||
        profile >= MOTION_PROFILE_COUNT) {
        return false;
    }

    *config = (motion_detector_config_t){
        .window_size = 20U,
        .calibration_samples = 120U,
        .algorithm = algorithm,
        .baseline_mode = baseline_mode,
        .minimum_threshold = 0.30f,
        .release_threshold_ratio = 0.75f,
        .baseline_update_alpha = 0.01f,
    };

    switch (profile) {
    case MOTION_PROFILE_LOW:
        config->sigma_multiplier = 8.0f;
        config->trigger_consecutive = 4U;
        config->clear_consecutive = 10U;
        break;
    case MOTION_PROFILE_BALANCED:
        config->sigma_multiplier = 6.0f;
        config->trigger_consecutive = 3U;
        config->clear_consecutive = 8U;
        break;
    case MOTION_PROFILE_HIGH:
        config->sigma_multiplier = 4.0f;
        config->release_threshold_ratio = 0.70f;
        config->baseline_update_alpha = 0.02f;
        config->trigger_consecutive = 2U;
        config->clear_consecutive = 5U;
        break;
    default:
        return false;
    }
    return motion_detector_config_valid(config);
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
    detector->release_threshold =
        config->minimum_threshold * config->release_threshold_ratio;
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
    float values[MOTION_DETECTOR_MAX_WINDOW] = {0};
    const size_t count = detector->config.window_size;
    for (size_t i = 0U; i < count; ++i) {
        values[i] = (float)detector->samples[(detector->write_index + i) % count];
    }

    double mean = 0.0;
    for (size_t i = 0U; i < count; ++i) {
        mean += values[i];
    }
    mean /= (double)count;

    switch (detector->config.algorithm) {
    case MOTION_FEATURE_MEAN_ABSOLUTE_DIFFERENCE: {
        double total = 0.0;
        for (size_t i = 1U; i < count; ++i) {
            total += fabs((double)values[i] - (double)values[i - 1U]);
        }
        return (float)(total / (double)(count - 1U));
    }
    case MOTION_FEATURE_STANDARD_DEVIATION:
    case MOTION_FEATURE_SAMPLE_VARIANCE: {
        double sum = 0.0;
        for (size_t i = 0U; i < count; ++i) {
            const double delta = (double)values[i] - mean;
            sum += delta * delta;
        }
        const double variance = sum / (double)(count - 1U);
        return detector->config.algorithm == MOTION_FEATURE_STANDARD_DEVIATION ?
                   (float)sqrt(variance) : (float)variance;
    }
    case MOTION_FEATURE_RANGE: {
        float minimum = values[0];
        float maximum = values[0];
        for (size_t i = 1U; i < count; ++i) {
            minimum = values[i] < minimum ? values[i] : minimum;
            maximum = values[i] > maximum ? values[i] : maximum;
        }
        return maximum - minimum;
    }
    case MOTION_FEATURE_MEDIAN_ABSOLUTE_DEVIATION: {
        const float center = median(values, count);
        for (size_t i = 0U; i < count; ++i) {
            values[i] = fabsf(values[i] - center);
        }
        return median(values, count);
    }
    default:
        return 0.0f;
    }
}

static motion_detector_result_t make_result(const motion_detector_t *detector,
                                            bool score_ready,
                                            bool changed,
                                            float score)
{
    return (motion_detector_result_t){
        .state = detector->state,
        .score_ready = score_ready,
        .calibrated = detector->state != MOTION_DETECTOR_CALIBRATING,
        .state_changed = changed,
        .score = score,
        .threshold = detector->threshold,
        .release_threshold = detector->release_threshold,
        .baseline_mean = detector->baseline_center,
        .baseline_stddev = detector->baseline_spread,
    };
}

static void initialize_baseline(motion_detector_t *detector)
{
    baseline_statistics(detector->config.baseline_mode,
                        detector->calibration_scores,
                        detector->calibration_count,
                        &detector->baseline_center,
                        &detector->baseline_spread,
                        detector->statistics_working);
    const size_t first = detector->calibration_count > MOTION_DETECTOR_BASELINE_WINDOW ?
                             detector->calibration_count - MOTION_DETECTOR_BASELINE_WINDOW : 0U;
    detector->baseline_score_count = detector->calibration_count - first;
    for (size_t i = 0U; i < detector->baseline_score_count; ++i) {
        detector->baseline_scores[i] = detector->calibration_scores[first + i];
    }
    detector->baseline_write_index =
        detector->baseline_score_count % MOTION_DETECTOR_BASELINE_WINDOW;
    update_thresholds(detector);
}

static void update_idle_baseline(motion_detector_t *detector, float score)
{
    detector->baseline_scores[detector->baseline_write_index] = score;
    detector->baseline_write_index =
        (detector->baseline_write_index + 1U) % MOTION_DETECTOR_BASELINE_WINDOW;
    if (detector->baseline_score_count < MOTION_DETECTOR_BASELINE_WINDOW) {
        detector->baseline_score_count++;
    }

    float candidate_center;
    float candidate_spread;
    baseline_statistics(detector->config.baseline_mode,
                        detector->baseline_scores,
                        detector->baseline_score_count,
                        &candidate_center,
                        &candidate_spread,
                        detector->statistics_working);
    const float alpha = detector->config.baseline_update_alpha;
    detector->baseline_center += alpha * (candidate_center - detector->baseline_center);
    detector->baseline_spread += alpha * (candidate_spread - detector->baseline_spread);
    update_thresholds(detector);
}

motion_detector_result_t motion_detector_push(motion_detector_t *detector,
                                               float sample)
{
    if (detector == NULL || !isfinite(sample)) {
        const motion_detector_result_t invalid = {0};
        return invalid;
    }

    const size_t window = detector->config.window_size;
    detector->samples[detector->write_index] = sample;
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
        detector->calibration_scores[detector->calibration_count++] = score;
        if (detector->calibration_count >= detector->config.calibration_samples) {
            initialize_baseline(detector);
            detector->state = MOTION_DETECTOR_IDLE;
            changed = true;
        }
        return make_result(detector, true, changed, score);
    }

    if (detector->state == MOTION_DETECTOR_IDLE) {
        if (score > detector->threshold) {
            detector->above_count++;
            if (detector->above_count >= detector->config.trigger_consecutive) {
                detector->state = MOTION_DETECTOR_ACTIVE;
                detector->above_count = 0U;
                detector->below_count = 0U;
                changed = true;
            }
        } else {
            detector->above_count = 0U;
            if (score <= detector->release_threshold) {
                update_idle_baseline(detector, score);
            }
        }
    } else if (score <= detector->release_threshold) {
        detector->below_count++;
        if (detector->below_count >= detector->config.clear_consecutive) {
            detector->state = MOTION_DETECTOR_IDLE;
            detector->below_count = 0U;
            changed = true;
            update_idle_baseline(detector, score);
        }
    } else {
        detector->below_count = 0U;
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

const char *motion_feature_algorithm_name(motion_feature_algorithm_t algorithm)
{
    static const char *const names[MOTION_FEATURE_COUNT] = {
        "mean_absolute_difference", "standard_deviation", "sample_variance",
        "range", "median_absolute_deviation",
    };
    return algorithm >= 0 && algorithm < MOTION_FEATURE_COUNT ?
               names[algorithm] : "unknown";
}

const char *motion_baseline_mode_name(motion_baseline_mode_t mode)
{
    static const char *const names[MOTION_BASELINE_COUNT] = {
        "mean_stddev", "median_mad",
    };
    return mode >= 0 && mode < MOTION_BASELINE_COUNT ? names[mode] : "unknown";
}

const char *motion_sensitivity_profile_name(motion_sensitivity_profile_t profile)
{
    static const char *const names[MOTION_PROFILE_COUNT] = {
        "low", "balanced", "high",
    };
    return profile >= 0 && profile < MOTION_PROFILE_COUNT ?
               names[profile] : "unknown";
}
