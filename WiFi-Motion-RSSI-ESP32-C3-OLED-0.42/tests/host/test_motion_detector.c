#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "motion_detector.h"

#define ASSERT_TRUE(condition)                                                   \
    do {                                                                         \
        if (!(condition)) {                                                      \
            fprintf(stderr, "Assertion failed at %s:%d: %s\n",                 \
                    __FILE__, __LINE__, #condition);                             \
            exit(EXIT_FAILURE);                                                  \
        }                                                                        \
    } while (0)

#define ASSERT_NEAR(actual, expected, tolerance)                                 \
    ASSERT_TRUE(fabs((double)(actual) - (double)(expected)) <= (tolerance))

static motion_detector_config_t test_config(void)
{
    return (motion_detector_config_t){
        .window_size = 6U,
        .calibration_samples = 8U,
        .algorithm = MOTION_FEATURE_MEAN_ABSOLUTE_DIFFERENCE,
        .baseline_mode = MOTION_BASELINE_MEAN_STDDEV,
        .sigma_multiplier = 4.0f,
        .minimum_threshold = 0.30f,
        .release_threshold_ratio = 0.75f,
        .baseline_update_alpha = 0.01f,
        .trigger_consecutive = 2U,
        .clear_consecutive = 3U,
    };
}

static motion_detector_result_t push_many(motion_detector_t *detector,
                                          const int8_t *values,
                                          size_t count)
{
    motion_detector_result_t result = {0};
    for (size_t i = 0; i < count; ++i) {
        result = motion_detector_push(detector, values[i]);
    }
    return result;
}

static void test_config_validation(void)
{
    motion_detector_config_t config = test_config();
    ASSERT_TRUE(motion_detector_config_valid(&config));
    config.window_size = 3U;
    ASSERT_TRUE(!motion_detector_config_valid(&config));
    config = test_config();
    config.minimum_threshold = 0.0f;
    ASSERT_TRUE(!motion_detector_config_valid(&config));
    config = test_config();
    config.release_threshold_ratio = 1.0f;
    ASSERT_TRUE(!motion_detector_config_valid(&config));
    config = test_config();
    config.calibration_samples = MOTION_DETECTOR_MAX_CALIBRATION + 1U;
    ASSERT_TRUE(!motion_detector_config_valid(&config));
}

static void test_stable_signal_calibrates_without_triggering(void)
{
    motion_detector_t detector;
    const motion_detector_config_t config = test_config();
    ASSERT_TRUE(motion_detector_init(&detector, &config));
    const int8_t stable[] = {
        -60, -60, -59, -60, -60, -59, -60, -60,
        -59, -60, -60, -59, -60, -60, -59, -60,
        -60, -59, -60, -60, -59, -60, -60, -59,
    };
    const motion_detector_result_t result =
        push_many(&detector, stable, sizeof(stable) / sizeof(stable[0]));
    ASSERT_TRUE(result.calibrated);
    ASSERT_TRUE(result.state == MOTION_DETECTOR_IDLE);
    ASSERT_TRUE(result.threshold >= config.minimum_threshold);
}

static void calibrate(motion_detector_t *detector)
{
    const int8_t stable[] = {
        -60, -60, -60, -60, -60, -60, -60,
        -60, -60, -60, -60, -60, -60, -60,
    };
    const motion_detector_result_t result =
        push_many(detector, stable, sizeof(stable) / sizeof(stable[0]));
    ASSERT_TRUE(result.calibrated);
    ASSERT_TRUE(result.state == MOTION_DETECTOR_IDLE);
}

static void test_motion_sequence_triggers(void)
{
    motion_detector_t detector;
    const motion_detector_config_t config = test_config();
    ASSERT_TRUE(motion_detector_init(&detector, &config));
    calibrate(&detector);
    const int8_t movement[] = {-52, -69, -50, -71, -49, -70, -51, -68};
    const motion_detector_result_t result =
        push_many(&detector, movement, sizeof(movement) / sizeof(movement[0]));
    ASSERT_TRUE(result.state == MOTION_DETECTOR_ACTIVE);
    ASSERT_TRUE(result.score > result.threshold);
}

static void test_hysteresis_clears_after_stability(void)
{
    motion_detector_t detector;
    const motion_detector_config_t config = test_config();
    ASSERT_TRUE(motion_detector_init(&detector, &config));
    calibrate(&detector);
    const int8_t movement[] = {-52, -69, -50, -71, -49, -70, -51, -68};
    motion_detector_result_t result =
        push_many(&detector, movement, sizeof(movement) / sizeof(movement[0]));
    ASSERT_TRUE(result.state == MOTION_DETECTOR_ACTIVE);
    const int8_t stable[] = {
        -60, -60, -60, -60, -60, -60, -60, -60,
        -60, -60, -60, -60, -60, -60, -60, -60,
    };
    result = push_many(&detector, stable, sizeof(stable) / sizeof(stable[0]));
    ASSERT_TRUE(result.state == MOTION_DETECTOR_IDLE);
}

static void test_minimum_threshold_is_respected(void)
{
    motion_detector_t detector;
    motion_detector_config_t config = test_config();
    config.minimum_threshold = 1.25f;
    ASSERT_TRUE(motion_detector_init(&detector, &config));
    calibrate(&detector);
    ASSERT_NEAR(detector.threshold, 1.25f, 0.0001);
}

static void test_fractional_signal_is_preserved(void)
{
    motion_detector_config_t config = test_config();
    config.window_size = 4U;
    config.calibration_samples = 4U;
    config.sigma_multiplier = 3.0f;
    config.minimum_threshold = 0.05f;
    motion_detector_t detector;
    ASSERT_TRUE(motion_detector_init(&detector, &config));

    const float quiet[] = {
        0.50f, 0.51f, 0.49f, 0.50f, 0.51f, 0.49f, 0.50f,
    };
    motion_detector_result_t result = {0};
    for (size_t i = 0U; i < sizeof(quiet) / sizeof(quiet[0]); ++i) {
        result = motion_detector_push(&detector, quiet[i]);
    }
    ASSERT_TRUE(result.calibrated);
    ASSERT_TRUE(result.state == MOTION_DETECTOR_IDLE);
    ASSERT_TRUE(result.score > 0.0f);

    const float activity[] = {0.0f, 2.0f, 0.0f, 2.0f, 0.0f, 2.0f};
    for (size_t i = 0U; i < sizeof(activity) / sizeof(activity[0]); ++i) {
        result = motion_detector_push(&detector, activity[i]);
    }
    ASSERT_TRUE(result.state == MOTION_DETECTOR_ACTIVE);
    ASSERT_TRUE(result.score > result.threshold);
}

static uint32_t pseudo_random_state = 0x12345678U;

static int8_t stable_noise_sample(void)
{
    pseudo_random_state = pseudo_random_state * 1664525U + 1013904223U;
    const int noise = (int)((pseudo_random_state >> 24U) % 3U) - 1;
    return (int8_t)(-60 + noise);
}

static void test_long_stable_run_and_detection_latency(void)
{
    motion_detector_t detector;
    const motion_detector_config_t config = {
        .window_size = 20U,
        .calibration_samples = 120U,
        .algorithm = MOTION_FEATURE_MEAN_ABSOLUTE_DIFFERENCE,
        .baseline_mode = MOTION_BASELINE_MEAN_STDDEV,
        .sigma_multiplier = 6.0f,
        .minimum_threshold = 0.30f,
        .release_threshold_ratio = 0.75f,
        .baseline_update_alpha = 0.01f,
        .trigger_consecutive = 3U,
        .clear_consecutive = 8U,
    };
    ASSERT_TRUE(motion_detector_init(&detector, &config));
    motion_detector_result_t result = {0};
    for (size_t i = 0; i < 250U; ++i) {
        result = motion_detector_push(&detector, stable_noise_sample());
    }
    ASSERT_TRUE(result.calibrated);
    ASSERT_TRUE(result.state == MOTION_DETECTOR_IDLE);
    for (size_t i = 0; i < 1000U; ++i) {
        result = motion_detector_push(&detector, stable_noise_sample());
        ASSERT_TRUE(result.state != MOTION_DETECTOR_ACTIVE);
    }
    size_t samples_to_trigger = 0U;
    for (; samples_to_trigger < 12U; ++samples_to_trigger) {
        const int8_t rssi = (samples_to_trigger % 2U == 0U) ? -48 : -73;
        result = motion_detector_push(&detector, rssi);
        if (result.state == MOTION_DETECTOR_ACTIVE) {
            break;
        }
    }
    ASSERT_TRUE(result.state == MOTION_DETECTOR_ACTIVE);
    ASSERT_TRUE(samples_to_trigger < 12U);
}

static void test_profiles_are_valid_and_ordered(void)
{
    motion_detector_config_t low;
    motion_detector_config_t balanced;
    motion_detector_config_t high;
    ASSERT_TRUE(motion_detector_config_for_profile(
        &low, MOTION_PROFILE_LOW, MOTION_FEATURE_RANGE,
        MOTION_BASELINE_MEDIAN_MAD));
    ASSERT_TRUE(motion_detector_config_for_profile(
        &balanced, MOTION_PROFILE_BALANCED, MOTION_FEATURE_RANGE,
        MOTION_BASELINE_MEDIAN_MAD));
    ASSERT_TRUE(motion_detector_config_for_profile(
        &high, MOTION_PROFILE_HIGH, MOTION_FEATURE_RANGE,
        MOTION_BASELINE_MEDIAN_MAD));
    ASSERT_TRUE(low.sigma_multiplier > balanced.sigma_multiplier);
    ASSERT_TRUE(balanced.sigma_multiplier > high.sigma_multiplier);
    ASSERT_TRUE(low.trigger_consecutive > high.trigger_consecutive);
}

static void test_every_algorithm_uses_the_same_synthetic_sequence(void)
{
    const int8_t stable[] = {
        -60, -60, -60, -60, -60, -60, -60,
        -60, -60, -60, -60, -60, -60, -60,
    };
    const int8_t movement[] = {
        -48, -74, -47, -75, -49, -73, -46, -76,
        -48, -74, -47, -75,
    };

    for (int baseline = MOTION_BASELINE_MEAN_STDDEV;
         baseline < MOTION_BASELINE_COUNT; ++baseline) {
        for (int algorithm = MOTION_FEATURE_MEAN_ABSOLUTE_DIFFERENCE;
             algorithm < MOTION_FEATURE_COUNT; ++algorithm) {
            motion_detector_config_t config = test_config();
            config.algorithm = (motion_feature_algorithm_t)algorithm;
            config.baseline_mode = (motion_baseline_mode_t)baseline;
            motion_detector_t detector;
            ASSERT_TRUE(motion_detector_init(&detector, &config));
            motion_detector_result_t result =
                push_many(&detector, stable, sizeof(stable) / sizeof(stable[0]));
            ASSERT_TRUE(result.state == MOTION_DETECTOR_IDLE);
            result = push_many(&detector, movement,
                               sizeof(movement) / sizeof(movement[0]));
            ASSERT_TRUE(result.score_ready);
            ASSERT_TRUE(result.score > result.threshold);
            ASSERT_TRUE(result.state == MOTION_DETECTOR_ACTIVE);
            ASSERT_TRUE(result.release_threshold < result.threshold);
        }
    }
}

static void test_baseline_updates_only_after_returning_to_idle(void)
{
    motion_detector_config_t config = test_config();
    motion_detector_t detector;
    ASSERT_TRUE(motion_detector_init(&detector, &config));
    const int8_t calibration[] = {
        -60, -59, -60, -59, -60, -59, -60,
        -59, -60, -59, -60, -59, -60, -59,
    };
    motion_detector_result_t result = push_many(
        &detector, calibration, sizeof(calibration) / sizeof(calibration[0]));
    ASSERT_TRUE(result.state == MOTION_DETECTOR_IDLE);
    const int8_t movement[] = {-48, -74, -47, -75, -49, -73, -46, -76};
    result = push_many(&detector, movement,
                       sizeof(movement) / sizeof(movement[0]));
    ASSERT_TRUE(result.state == MOTION_DETECTOR_ACTIVE);
    const float active_baseline = result.baseline_mean;
    result = push_many(&detector, movement,
                       sizeof(movement) / sizeof(movement[0]));
    ASSERT_NEAR(result.baseline_mean, active_baseline, 0.000001);

    const int8_t quiet[] = {
        -58, -58, -58, -58, -58, -58, -58, -58,
        -58, -58, -58, -58, -58, -58, -58, -58,
        -58, -58, -58, -58,
    };
    result = push_many(&detector, quiet, sizeof(quiet) / sizeof(quiet[0]));
    ASSERT_TRUE(result.state == MOTION_DETECTOR_IDLE);
    ASSERT_TRUE(result.baseline_mean < active_baseline);
}

int main(void)
{
    test_config_validation();
    test_stable_signal_calibrates_without_triggering();
    test_motion_sequence_triggers();
    test_hysteresis_clears_after_stability();
    test_minimum_threshold_is_respected();
    test_fractional_signal_is_preserved();
    test_long_stable_run_and_detection_latency();
    test_profiles_are_valid_and_ordered();
    test_every_algorithm_uses_the_same_synthetic_sequence();
    test_baseline_updates_only_after_returning_to_idle();
    puts("All motion detector tests passed.");
    return EXIT_SUCCESS;
}
