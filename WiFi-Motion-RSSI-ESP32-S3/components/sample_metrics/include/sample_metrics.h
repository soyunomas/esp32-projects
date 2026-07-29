#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t queries;
    uint64_t successful_samples;
    uint64_t read_errors;
    uint64_t schedule_misses;
    uint64_t repeated_samples;
    uint64_t rssi_changes;
    bool has_previous_query;
    bool has_previous_rssi;
    int8_t previous_rssi_dbm;
    int64_t previous_query_ms;
    int64_t last_rssi_change_ms;
} sample_metrics_t;

typedef struct {
    bool sample_ok;
    bool rssi_changed;
    int64_t query_interval_ms;
    int64_t rssi_change_interval_ms;
    int64_t rssi_unchanged_ms;
} sample_observation_t;

void sample_metrics_init(sample_metrics_t *metrics);
sample_observation_t sample_metrics_record_success(sample_metrics_t *metrics,
                                                   int64_t timestamp_ms,
                                                   int8_t rssi_dbm,
                                                   uint32_t expected_interval_ms);
sample_observation_t sample_metrics_record_error(sample_metrics_t *metrics,
                                                 int64_t timestamp_ms,
                                                 uint32_t expected_interval_ms);

#ifdef __cplusplus
}
#endif
