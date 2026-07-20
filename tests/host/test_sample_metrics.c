#include <stdio.h>
#include <stdlib.h>

#include "sample_metrics.h"

#define ASSERT_TRUE(condition)                                                   \
    do {                                                                         \
        if (!(condition)) {                                                       \
            fprintf(stderr, "Assertion failed at %s:%d: %s\n",                  \
                    __FILE__, __LINE__, #condition);                              \
            exit(EXIT_FAILURE);                                                   \
        }                                                                         \
    } while (0)

static void test_success_and_change_cadence(void)
{
    sample_metrics_t metrics;
    sample_metrics_init(&metrics);

    sample_observation_t observation =
        sample_metrics_record_success(&metrics, 1000, -60, 100);
    ASSERT_TRUE(observation.sample_ok);
    ASSERT_TRUE(!observation.rssi_changed);
    ASSERT_TRUE(observation.query_interval_ms == -1);
    ASSERT_TRUE(observation.rssi_change_interval_ms == -1);
    ASSERT_TRUE(observation.rssi_unchanged_ms == 0);

    observation = sample_metrics_record_success(&metrics, 1100, -60, 100);
    ASSERT_TRUE(!observation.rssi_changed);
    ASSERT_TRUE(observation.query_interval_ms == 100);
    ASSERT_TRUE(observation.rssi_unchanged_ms == 100);

    observation = sample_metrics_record_success(&metrics, 1200, -58, 100);
    ASSERT_TRUE(observation.rssi_changed);
    ASSERT_TRUE(observation.rssi_change_interval_ms == 200);
    ASSERT_TRUE(observation.rssi_unchanged_ms == 0);
    ASSERT_TRUE(metrics.queries == 3U);
    ASSERT_TRUE(metrics.successful_samples == 3U);
    ASSERT_TRUE(metrics.repeated_samples == 1U);
    ASSERT_TRUE(metrics.rssi_changes == 1U);
}

static void test_errors_and_schedule_misses(void)
{
    sample_metrics_t metrics;
    sample_metrics_init(&metrics);

    (void)sample_metrics_record_success(&metrics, 0, -65, 100);
    sample_observation_t observation =
        sample_metrics_record_error(&metrics, 350, 100);
    ASSERT_TRUE(!observation.sample_ok);
    ASSERT_TRUE(observation.query_interval_ms == 350);
    ASSERT_TRUE(metrics.read_errors == 1U);
    ASSERT_TRUE(metrics.schedule_misses == 2U);

    observation = sample_metrics_record_success(&metrics, 450, -65, 100);
    ASSERT_TRUE(observation.rssi_unchanged_ms == 450);
    ASSERT_TRUE(metrics.repeated_samples == 1U);
}

int main(void)
{
    test_success_and_change_cadence();
    test_errors_and_schedule_misses();
    puts("All sample metrics tests passed.");
    return EXIT_SUCCESS;
}
