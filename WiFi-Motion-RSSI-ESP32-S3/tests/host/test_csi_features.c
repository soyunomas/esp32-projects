#include "csi_features.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static bool close_to(float actual, float expected)
{
    return fabsf(actual - expected) < 0.001f;
}

int main(void)
{
    csi_feature_extractor_t extractor;
    csi_feature_extractor_init(&extractor);

    const int8_t first[] = {3, 4, 0, 0, 6, 8};
    csi_frame_features_t result =
        csi_feature_extract(&extractor, first, sizeof(first), false);
    assert(result.valid);
    assert(!result.temporal_delta_valid);
    assert(result.subcarrier_count == 3U);
    assert(result.frame_index == 1U);
    assert(close_to(result.mean_amplitude, 5.0f));
    assert(close_to(result.amplitude_variance, 16.666666f));
    assert(close_to(result.amplitude_range, 10.0f));
    assert(close_to(result.mean_energy, 41.666666f));

    const int8_t second[] = {0, 0, 3, 4, 6, 8};
    result = csi_feature_extract(&extractor, second, sizeof(second), false);
    assert(result.valid);
    assert(result.temporal_delta_valid);
    assert(result.frame_index == 2U);
    assert(close_to(result.temporal_mean_absolute_delta, 3.333333f));
    assert(close_to(result.temporal_normalized_mean_absolute_delta,
                    0.666666f));
    assert(close_to(result.temporal_complex_correlation_distance, 0.2f));

    const int8_t invalid_prefix[] = {99, 99, 99, 99, 3, 4, 6, 8};
    result = csi_feature_extract(&extractor,
                                 invalid_prefix,
                                 sizeof(invalid_prefix),
                                 true);
    assert(result.valid);
    assert(result.subcarrier_count == 2U);
    assert(close_to(result.mean_amplitude, 7.5f));
    assert(!result.temporal_delta_valid);

    assert(!csi_feature_extract(&extractor, second, 5U, false).valid);
    assert(!csi_feature_extract(NULL, second, sizeof(second), false).valid);
    assert(!csi_feature_extract(&extractor, NULL, sizeof(second), false).valid);

    csi_temporal_aggregator_t aggregator;
    csi_temporal_aggregator_init(&aggregator);
    csi_temporal_aggregate_t aggregate = {0};
    assert(!csi_temporal_aggregator_take(&aggregator, &aggregate));
    csi_temporal_aggregator_push(&aggregator, 0.5f);
    csi_temporal_aggregator_push(&aggregator, 1.5f);
    csi_temporal_aggregator_push(&aggregator, 1.0f);
    csi_temporal_aggregator_push(&aggregator, NAN);
    csi_temporal_aggregator_push(&aggregator, -1.0f);
    assert(csi_temporal_aggregator_take(&aggregator, &aggregate));
    assert(aggregate.sample_count == 3U);
    assert(close_to(aggregate.mean, 1.0f));
    assert(close_to(aggregate.peak, 1.5f));
    assert(!csi_temporal_aggregator_take(&aggregator, &aggregate));

    puts("csi_features tests passed");
    return 0;
}
