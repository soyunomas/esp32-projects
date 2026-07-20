#include "sample_metrics.h"

#include <string.h>

static sample_observation_t record_query(sample_metrics_t *metrics,
                                         int64_t timestamp_ms,
                                         uint32_t expected_interval_ms)
{
    sample_observation_t observation = {
        .query_interval_ms = -1,
        .rssi_change_interval_ms = -1,
        .rssi_unchanged_ms = -1,
    };

    metrics->queries++;
    if (metrics->has_previous_query) {
        const int64_t elapsed_ms = timestamp_ms - metrics->previous_query_ms;
        observation.query_interval_ms = elapsed_ms;
        if (elapsed_ms > 0 && expected_interval_ms > 0U) {
            const uint64_t elapsed_intervals =
                (uint64_t)elapsed_ms / (uint64_t)expected_interval_ms;
            if (elapsed_intervals > 1U) {
                metrics->schedule_misses += elapsed_intervals - 1U;
            }
        }
    }

    metrics->previous_query_ms = timestamp_ms;
    metrics->has_previous_query = true;
    return observation;
}

void sample_metrics_init(sample_metrics_t *metrics)
{
    if (metrics != NULL) {
        memset(metrics, 0, sizeof(*metrics));
    }
}

sample_observation_t sample_metrics_record_success(sample_metrics_t *metrics,
                                                   int64_t timestamp_ms,
                                                   int8_t rssi_dbm,
                                                   uint32_t expected_interval_ms)
{
    if (metrics == NULL) {
        const sample_observation_t invalid = {0};
        return invalid;
    }

    sample_observation_t observation =
        record_query(metrics, timestamp_ms, expected_interval_ms);
    observation.sample_ok = true;
    metrics->successful_samples++;

    if (!metrics->has_previous_rssi) {
        metrics->has_previous_rssi = true;
        metrics->previous_rssi_dbm = rssi_dbm;
        metrics->last_rssi_change_ms = timestamp_ms;
        observation.rssi_unchanged_ms = 0;
        return observation;
    }

    if (rssi_dbm != metrics->previous_rssi_dbm) {
        observation.rssi_changed = true;
        observation.rssi_change_interval_ms =
            timestamp_ms - metrics->last_rssi_change_ms;
        observation.rssi_unchanged_ms = 0;
        metrics->rssi_changes++;
        metrics->previous_rssi_dbm = rssi_dbm;
        metrics->last_rssi_change_ms = timestamp_ms;
    } else {
        metrics->repeated_samples++;
        observation.rssi_unchanged_ms =
            timestamp_ms - metrics->last_rssi_change_ms;
    }

    return observation;
}

sample_observation_t sample_metrics_record_error(sample_metrics_t *metrics,
                                                 int64_t timestamp_ms,
                                                 uint32_t expected_interval_ms)
{
    if (metrics == NULL) {
        const sample_observation_t invalid = {0};
        return invalid;
    }

    sample_observation_t observation =
        record_query(metrics, timestamp_ms, expected_interval_ms);
    metrics->read_errors++;
    return observation;
}
