#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "csi_features.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool valid;
    int8_t rssi_dbm;
    uint16_t csi_length;
    uint32_t wifi_timestamp_us;
    int64_t received_at_us;
    uint64_t frames_received;
    uint64_t frames_processed;
    uint64_t frames_dropped;
    csi_temporal_aggregate_t temporal_aggregate;
    csi_temporal_aggregate_t normalized_temporal_aggregate;
    csi_temporal_aggregate_t complex_correlation_aggregate;
    csi_frame_features_t features;
} csi_capture_snapshot_t;

esp_err_t csi_capture_start(const uint8_t source_mac[6]);
bool csi_capture_get_snapshot(csi_capture_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif
