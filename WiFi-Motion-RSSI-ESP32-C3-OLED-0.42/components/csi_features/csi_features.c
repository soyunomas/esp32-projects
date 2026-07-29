#include "csi_features.h"

#include <math.h>
#include <string.h>

void csi_feature_extractor_init(csi_feature_extractor_t *extractor)
{
    if (extractor != NULL) {
        memset(extractor, 0, sizeof(*extractor));
    }
}

csi_frame_features_t csi_feature_extract(csi_feature_extractor_t *extractor,
                                         const int8_t *csi,
                                         size_t length,
                                         bool first_word_invalid)
{
    csi_frame_features_t result = {0};
    if (extractor == NULL || csi == NULL) {
        return result;
    }

    const size_t offset = first_word_invalid ? 4U : 0U;
    if (length <= offset || ((length - offset) % 2U) != 0U) {
        return result;
    }
    const size_t subcarriers = (length - offset) / 2U;
    if (subcarriers == 0U || subcarriers > CSI_FEATURE_MAX_SUBCARRIERS) {
        return result;
    }

    float amplitudes[CSI_FEATURE_MAX_SUBCARRIERS];
    float amplitude_sum = 0.0f;
    float energy_sum = 0.0f;
    float minimum = 0.0f;
    float maximum = 0.0f;
    float temporal_delta_sum = 0.0f;
    float correlation_real = 0.0f;
    float correlation_imaginary = 0.0f;
    float previous_energy_sum = 0.0f;
    const bool comparable = extractor->has_previous_frame &&
                            extractor->previous_subcarrier_count == subcarriers;

    for (size_t index = 0; index < subcarriers; index++) {
        const size_t byte_index = offset + index * 2U;
        const float first = (float)csi[byte_index];
        const float second = (float)csi[byte_index + 1U];
        const float energy = first * first + second * second;
        const float amplitude = sqrtf(energy);
        amplitudes[index] = amplitude;
        amplitude_sum += amplitude;
        energy_sum += energy;
        if (index == 0U || amplitude < minimum) {
            minimum = amplitude;
        }
        if (index == 0U || amplitude > maximum) {
            maximum = amplitude;
        }
        if (comparable) {
            temporal_delta_sum +=
                fabsf(amplitude - extractor->previous_amplitudes[index]);
            const float previous_first =
                (float)extractor->previous_first[index];
            const float previous_second =
                (float)extractor->previous_second[index];
            correlation_real += first * previous_first +
                                second * previous_second;
            correlation_imaginary += second * previous_first -
                                     first * previous_second;
            previous_energy_sum +=
                previous_first * previous_first +
                previous_second * previous_second;
        }
    }

    const float count = (float)subcarriers;
    const float mean = amplitude_sum / count;
    float squared_difference_sum = 0.0f;
    float normalized_temporal_delta_sum = 0.0f;
    for (size_t index = 0; index < subcarriers; index++) {
        const float difference = amplitudes[index] - mean;
        squared_difference_sum += difference * difference;
        if (comparable && mean > 0.0f &&
            extractor->previous_mean_amplitude > 0.0f) {
            normalized_temporal_delta_sum += fabsf(
                amplitudes[index] / mean -
                extractor->previous_amplitudes[index] /
                    extractor->previous_mean_amplitude);
        }
        extractor->previous_amplitudes[index] = amplitudes[index];
        const size_t byte_index = offset + index * 2U;
        extractor->previous_first[index] = csi[byte_index];
        extractor->previous_second[index] = csi[byte_index + 1U];
    }

    extractor->previous_mean_amplitude = mean;
    extractor->previous_subcarrier_count = subcarriers;
    extractor->has_previous_frame = true;
    extractor->frames_seen++;

    result.valid = true;
    result.temporal_delta_valid = comparable;
    result.subcarrier_count = subcarriers;
    result.frame_index = extractor->frames_seen;
    result.mean_amplitude = mean;
    result.amplitude_variance = squared_difference_sum / count;
    result.amplitude_range = maximum - minimum;
    result.mean_energy = energy_sum / count;
    result.temporal_mean_absolute_delta =
        comparable ? temporal_delta_sum / count : 0.0f;
    result.temporal_normalized_mean_absolute_delta =
        comparable && mean > 0.0f ? normalized_temporal_delta_sum / count
                                  : 0.0f;
    const float correlation_denominator =
        sqrtf(energy_sum * previous_energy_sum);
    if (comparable && correlation_denominator > 0.0f) {
        const float correlation_magnitude =
            sqrtf(correlation_real * correlation_real +
                  correlation_imaginary * correlation_imaginary);
        const float similarity =
            fminf(1.0f, correlation_magnitude / correlation_denominator);
        result.temporal_complex_correlation_distance =
            fmaxf(0.0f, 1.0f - similarity);
    }
    return result;
}

void csi_temporal_aggregator_init(csi_temporal_aggregator_t *aggregator)
{
    if (aggregator != NULL) {
        memset(aggregator, 0, sizeof(*aggregator));
    }
}

void csi_temporal_aggregator_push(csi_temporal_aggregator_t *aggregator,
                                  float temporal_delta)
{
    if (aggregator == NULL || !isfinite(temporal_delta) ||
        temporal_delta < 0.0f) {
        return;
    }
    if (aggregator->sample_count == 0U ||
        temporal_delta > aggregator->temporal_delta_peak) {
        aggregator->temporal_delta_peak = temporal_delta;
    }
    aggregator->temporal_delta_sum += temporal_delta;
    aggregator->sample_count++;
}

bool csi_temporal_aggregator_take(csi_temporal_aggregator_t *aggregator,
                                  csi_temporal_aggregate_t *aggregate)
{
    if (aggregator == NULL || aggregate == NULL ||
        aggregator->sample_count == 0U) {
        return false;
    }
    *aggregate = (csi_temporal_aggregate_t) {
        .mean = aggregator->temporal_delta_sum /
                (float)aggregator->sample_count,
        .peak = aggregator->temporal_delta_peak,
        .sample_count = aggregator->sample_count,
    };
    csi_temporal_aggregator_init(aggregator);
    return true;
}
