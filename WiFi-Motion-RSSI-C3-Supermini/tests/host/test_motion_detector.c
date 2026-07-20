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
        .sigma_multiplier = 4.0f,
        .minimum_threshold = 0.30f,
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
        .sigma_multiplier = 6.0f,
        .minimum_threshold = 0.30f,
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

int main(void)
{
    test_config_validation();
    test_stable_signal_calibrates_without_triggering();
    test_motion_sequence_triggers();
    test_hysteresis_clears_after_stability();
    test_minimum_threshold_is_respected();
    test_long_stable_run_and_detection_latency();
    puts("All motion detector tests passed.");
    return EXIT_SUCCESS;
}
