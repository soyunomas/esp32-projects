#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CSI_FEATURE_MAX_SUBCARRIERS 192U

typedef struct {
    float previous_amplitudes[CSI_FEATURE_MAX_SUBCARRIERS];
    int8_t previous_first[CSI_FEATURE_MAX_SUBCARRIERS];
    int8_t previous_second[CSI_FEATURE_MAX_SUBCARRIERS];
    float previous_mean_amplitude;
    size_t previous_subcarrier_count;
    uint64_t frames_seen;
    bool has_previous_frame;
} csi_feature_extractor_t;

typedef struct {
    bool valid;
    bool temporal_delta_valid;
    size_t subcarrier_count;
    uint64_t frame_index;
    float mean_amplitude;
    float amplitude_variance;
    float amplitude_range;
    float mean_energy;
    float temporal_mean_absolute_delta;
    float temporal_normalized_mean_absolute_delta;
    float temporal_complex_correlation_distance;
} csi_frame_features_t;

typedef struct {
    float temporal_delta_sum;
    float temporal_delta_peak;
    uint32_t sample_count;
} csi_temporal_aggregator_t;

typedef struct {
    float mean;
    float peak;
    uint32_t sample_count;
} csi_temporal_aggregate_t;

void csi_feature_extractor_init(csi_feature_extractor_t *extractor);
csi_frame_features_t csi_feature_extract(csi_feature_extractor_t *extractor,
                                         const int8_t *csi,
                                         size_t length,
                                         bool first_word_invalid);
void csi_temporal_aggregator_init(csi_temporal_aggregator_t *aggregator);
void csi_temporal_aggregator_push(csi_temporal_aggregator_t *aggregator,
                                  float temporal_delta);
bool csi_temporal_aggregator_take(csi_temporal_aggregator_t *aggregator,
                                  csi_temporal_aggregate_t *aggregate);

#ifdef __cplusplus
}
#endif
