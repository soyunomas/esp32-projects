#include "multiref_detector.h"

#include <limits.h>
#include <string.h>

static uint8_t increment_saturated(uint8_t value)
{
    return value == UINT8_MAX ? value : (uint8_t)(value + 1U);
}

static uint16_t median_u16(uint16_t *values, size_t count)
{
    for (size_t index = 1U; index < count; ++index) {
        const uint16_t value = values[index];
        size_t position = index;
        while (position > 0U && values[position - 1U] > value) {
            values[position] = values[position - 1U];
            position--;
        }
        values[position] = value;
    }
    const size_t middle = count / 2U;
    if ((count & 1U) != 0U) {
        return values[middle];
    }
    return (uint16_t)(((uint32_t)values[middle - 1U] +
                       values[middle]) /
                      2U);
}

static uint16_t saturate_u16(uint32_t value)
{
    return value > UINT16_MAX ? UINT16_MAX : (uint16_t)value;
}

static void update_score_thresholds(multiref_detector_t *detector)
{
    uint16_t working[MULTIREF_DETECTOR_MAX_SCORE_WINDOW];
    const size_t count = detector->baseline_score_count;
    if (count == 0U) {
        detector->adaptive_trigger_score_x100 =
            detector->config.trigger_score_x100;
        detector->adaptive_release_score_x100 =
            detector->config.release_score_x100;
        detector->baseline_score_center_x100 = 0U;
        detector->baseline_score_spread_x100 = 0U;
        return;
    }
    memcpy(working,
           detector->baseline_scores,
           count * sizeof(working[0]));
    const uint16_t center = median_u16(working, count);
    for (size_t index = 0U; index < count; ++index) {
        const uint16_t value = detector->baseline_scores[index];
        working[index] =
            value >= center ? value - center : center - value;
    }
    const uint16_t mad = median_u16(working, count);
    const uint16_t spread =
        saturate_u16(((uint32_t)mad * 1483U + 500U) / 1000U);
    const uint32_t adaptive =
        (uint32_t)center +
        ((uint32_t)spread * detector->config.adaptive_sigma_x100 +
         50U) /
            100U;
    uint16_t trigger = saturate_u16(adaptive);
    if (trigger < detector->config.trigger_score_x100) {
        trigger = detector->config.trigger_score_x100;
    }
    uint16_t release = saturate_u16(
        ((uint32_t)trigger * detector->config.release_score_x100) /
        detector->config.trigger_score_x100);
    if (release < detector->config.release_score_x100) {
        release = detector->config.release_score_x100;
    }
    if (release >= trigger) {
        release = (uint16_t)(trigger - 1U);
    }
    detector->baseline_score_center_x100 = center;
    detector->baseline_score_spread_x100 = spread;
    detector->adaptive_trigger_score_x100 = trigger;
    detector->adaptive_release_score_x100 = release;
}

static void reset_score_baseline(multiref_detector_t *detector)
{
    detector->baseline_score_count = 0U;
    detector->baseline_score_write_index = 0U;
    memset(detector->baseline_scores, 0, sizeof(detector->baseline_scores));
    update_score_thresholds(detector);
}

static void add_baseline_score(multiref_detector_t *detector,
                               uint16_t score)
{
    detector->baseline_scores[detector->baseline_score_write_index] =
        score;
    detector->baseline_score_write_index =
        (uint8_t)((detector->baseline_score_write_index + 1U) %
                  detector->config.adaptive_window);
    if (detector->baseline_score_count <
        detector->config.adaptive_window) {
        detector->baseline_score_count++;
    }
    update_score_thresholds(detector);
}

bool multiref_config_valid(const multiref_config_t *config)
{
    return config != NULL &&
           config->minimum_coverage_permille >= 1U &&
           config->minimum_coverage_permille <= 1000U &&
           config->noise_floor_x10 >= 1U &&
           config->trigger_score_x100 >= 1U &&
           config->release_score_x100 <
               config->trigger_score_x100 &&
           config->adaptive_sigma_x100 >= 1U &&
           config->baseline_alpha_permille <= 1000U &&
           config->adaptive_window >= 3U &&
           config->adaptive_window <=
               MULTIREF_DETECTOR_MAX_SCORE_WINDOW &&
           config->warmup_scans >= 1U &&
           config->trigger_consecutive >= 1U &&
           config->clear_consecutive >= 1U &&
           config->unhealthy_consecutive >= 1U &&
           config->recovery_consecutive >= 1U &&
           config->stale_after_scans >= 1U;
}

multiref_status_t multiref_detector_init(
    multiref_detector_t *detector,
    const multiref_config_t *config,
    const multiref_reference_t *references,
    size_t reference_count)
{
    if (detector == NULL || !multiref_config_valid(config) ||
        references == NULL || reference_count == 0U ||
        reference_count > MULTIREF_DETECTOR_MAX_REFERENCES) {
        return MULTIREF_INVALID_ARGUMENT;
    }
    for (size_t left = 0U; left < reference_count; ++left) {
        for (size_t right = left + 1U;
             right < reference_count;
             ++right) {
            if (memcmp(references[left].bssid,
                       references[right].bssid,
                       MULTIREF_DETECTOR_BSSID_LENGTH) == 0) {
                return MULTIREF_INVALID_ARGUMENT;
            }
        }
    }
    memset(detector, 0, sizeof(*detector));
    detector->config = *config;
    memcpy(detector->references,
           references,
           reference_count * sizeof(references[0]));
    detector->reference_count = (uint8_t)reference_count;
    for (size_t index = 0U; index < reference_count; ++index) {
        detector->age_scans[index] = UINT16_MAX;
    }
    detector->state = MULTIREF_STATE_WARMUP;
    reset_score_baseline(detector);
    return MULTIREF_OK;
}

multiref_status_t multiref_detector_begin_scan(
    multiref_detector_t *detector, uint32_t scan_id)
{
    if (detector == NULL || scan_id == 0U) {
        return MULTIREF_INVALID_ARGUMENT;
    }
    if (detector->scan_open) {
        return MULTIREF_SCAN_ALREADY_OPEN;
    }
    if (scan_id <= detector->last_scan_id) {
        return MULTIREF_SCAN_ID_NOT_INCREASING;
    }
    memset(detector->seen, 0, sizeof(detector->seen));
    detector->last_scan_id = scan_id;
    detector->scan_open = true;
    return MULTIREF_OK;
}

multiref_status_t multiref_detector_observe(
    multiref_detector_t *detector,
    const uint8_t bssid[MULTIREF_DETECTOR_BSSID_LENGTH],
    int8_t rssi)
{
    if (detector == NULL || bssid == NULL) {
        return MULTIREF_INVALID_ARGUMENT;
    }
    if (!detector->scan_open) {
        return MULTIREF_NO_OPEN_SCAN;
    }
    for (uint8_t index = 0U;
         index < detector->reference_count;
         ++index) {
        if (memcmp(detector->references[index].bssid,
                   bssid,
                   MULTIREF_DETECTOR_BSSID_LENGTH) != 0) {
            continue;
        }
        if (detector->seen[index]) {
            return MULTIREF_DUPLICATE_OBSERVATION;
        }
        detector->seen[index] = true;
        detector->current_rssi[index] = rssi;
        break;
    }
    return MULTIREF_OK;
}

static void reset_motion_counters(multiref_detector_t *detector)
{
    detector->trigger_count = 0U;
    detector->clear_count = 0U;
}

static void set_state(multiref_detector_t *detector,
                      multiref_state_t state,
                      bool *changed)
{
    if (detector->state != state) {
        detector->state = state;
        *changed = true;
    }
}

static void adapt_baselines(multiref_detector_t *detector)
{
    const uint16_t alpha = detector->config.baseline_alpha_permille;
    if (alpha == 0U) {
        return;
    }
    for (uint8_t index = 0U;
         index < detector->reference_count;
         ++index) {
        if (!detector->seen[index]) {
            continue;
        }
        multiref_reference_t *reference =
            &detector->references[index];
        const int32_t sample_x10 =
            (int32_t)detector->current_rssi[index] * 10;
        const int32_t delta =
            sample_x10 - reference->baseline_rssi_x10;
        reference->baseline_rssi_x10 +=
            (int16_t)(delta * alpha / 1000);
        const uint32_t absolute_delta =
            delta < 0 ? (uint32_t)-delta : (uint32_t)delta;
        const int32_t mad_delta =
            (int32_t)absolute_delta - reference->baseline_mad_x10;
        int32_t updated_mad =
            (int32_t)reference->baseline_mad_x10 +
            mad_delta * alpha / 1000;
        if (updated_mad < 0) {
            updated_mad = 0;
        } else if (updated_mad > UINT16_MAX) {
            updated_mad = UINT16_MAX;
        }
        reference->baseline_mad_x10 = (uint16_t)updated_mad;
    }
}

multiref_status_t multiref_detector_end_scan(
    multiref_detector_t *detector, multiref_result_t *result)
{
    if (detector == NULL || result == NULL) {
        return MULTIREF_INVALID_ARGUMENT;
    }
    if (!detector->scan_open) {
        return MULTIREF_NO_OPEN_SCAN;
    }
    detector->scan_open = false;
    bool changed = false;
    const multiref_state_t previous_state = detector->state;
    uint8_t observed = 0U;
    uint8_t stale = 0U;
    uint16_t oldest_age = 0U;
    uint16_t deviations[MULTIREF_DETECTOR_MAX_REFERENCES] = {0};
    for (uint8_t index = 0U;
         index < detector->reference_count;
         ++index) {
        if (detector->seen[index]) {
            detector->age_scans[index] = 0U;
        } else if (detector->age_scans[index] < UINT16_MAX) {
            detector->age_scans[index]++;
        }
        if (detector->age_scans[index] > oldest_age) {
            oldest_age = detector->age_scans[index];
        }
        if (detector->age_scans[index] >
            detector->config.stale_after_scans) {
            stale++;
        }
        if (!detector->seen[index]) {
            continue;
        }
        const multiref_reference_t *reference =
            &detector->references[index];
        const int32_t delta =
            (int32_t)detector->current_rssi[index] * 10 -
            reference->baseline_rssi_x10;
        const uint32_t absolute_delta =
            delta < 0 ? (uint32_t)-delta : (uint32_t)delta;
        const uint16_t scale =
            reference->baseline_mad_x10 >
                    detector->config.noise_floor_x10
                ? reference->baseline_mad_x10
                : detector->config.noise_floor_x10;
        uint32_t normalized = absolute_delta * 100U / scale;
        if (normalized > UINT16_MAX) {
            normalized = UINT16_MAX;
        }
        deviations[observed++] = (uint16_t)normalized;
    }
    const uint16_t coverage =
        (uint16_t)((uint32_t)observed * 1000U /
                   detector->reference_count);
    const bool no_data = observed == 0U;
    const bool unhealthy =
        no_data ||
        coverage < detector->config.minimum_coverage_permille;
    if (unhealthy) {
        detector->unhealthy_count =
            increment_saturated(detector->unhealthy_count);
        detector->recovery_count = 0U;
        reset_motion_counters(detector);
        if (detector->unhealthy_count >=
            detector->config.unhealthy_consecutive) {
            const multiref_state_t unhealthy_state =
                no_data ||
                        detector->state == MULTIREF_STATE_NO_DATA
                    ? MULTIREF_STATE_NO_DATA
                    : MULTIREF_STATE_DEGRADED;
            set_state(detector, unhealthy_state, &changed);
        }
        *result = (multiref_result_t){
            .state = detector->state,
            .previous_state = previous_state,
            .state_changed = changed,
            .score_ready = false,
            .trigger_score_x100 =
                detector->adaptive_trigger_score_x100,
            .release_score_x100 =
                detector->adaptive_release_score_x100,
            .baseline_score_center_x100 =
                detector->baseline_score_center_x100,
            .baseline_score_spread_x100 =
                detector->baseline_score_spread_x100,
            .coverage_permille = coverage,
            .observed_references = observed,
            .reference_count = detector->reference_count,
            .trigger_count = detector->trigger_count,
            .trigger_required =
                detector->config.trigger_consecutive,
            .stale_references = stale,
            .oldest_reference_age_scans = oldest_age,
        };
        return MULTIREF_OK;
    }

    detector->unhealthy_count = 0U;
    const uint16_t score = median_u16(deviations, observed);
    if (detector->state == MULTIREF_STATE_DEGRADED ||
        detector->state == MULTIREF_STATE_NO_DATA) {
        detector->recovery_count =
            increment_saturated(detector->recovery_count);
        if (detector->recovery_count >=
            detector->config.recovery_consecutive) {
            detector->recovery_count = 0U;
            detector->warmup_count = 0U;
            reset_score_baseline(detector);
            set_state(detector, MULTIREF_STATE_WARMUP, &changed);
        }
    } else if (detector->state == MULTIREF_STATE_WARMUP) {
        add_baseline_score(detector, score);
        detector->warmup_count =
            increment_saturated(detector->warmup_count);
        if (detector->warmup_count >=
            detector->config.warmup_scans) {
            set_state(detector, MULTIREF_STATE_IDLE, &changed);
        }
    } else if (detector->state == MULTIREF_STATE_IDLE) {
        if (score >= detector->adaptive_trigger_score_x100) {
            detector->trigger_count =
                increment_saturated(detector->trigger_count);
            if (detector->trigger_count >=
                detector->config.trigger_consecutive) {
                reset_motion_counters(detector);
                set_state(detector, MULTIREF_STATE_MOTION, &changed);
            }
        } else {
            detector->trigger_count = 0U;
            if (score <= detector->adaptive_release_score_x100) {
                adapt_baselines(detector);
                add_baseline_score(detector, score);
            }
        }
    } else if (detector->state == MULTIREF_STATE_MOTION) {
        if (score <= detector->adaptive_release_score_x100) {
            detector->clear_count =
                increment_saturated(detector->clear_count);
            if (detector->clear_count >=
                detector->config.clear_consecutive) {
                reset_motion_counters(detector);
                set_state(detector, MULTIREF_STATE_IDLE, &changed);
                adapt_baselines(detector);
                add_baseline_score(detector, score);
            }
        } else {
            detector->clear_count = 0U;
        }
    }
    *result = (multiref_result_t){
        .state = detector->state,
        .previous_state = previous_state,
        .state_changed = changed,
        .score_ready = true,
        .score_x100 = score,
        .trigger_score_x100 =
            detector->adaptive_trigger_score_x100,
        .release_score_x100 =
            detector->adaptive_release_score_x100,
        .baseline_score_center_x100 =
            detector->baseline_score_center_x100,
        .baseline_score_spread_x100 =
            detector->baseline_score_spread_x100,
        .coverage_permille = coverage,
        .observed_references = observed,
        .reference_count = detector->reference_count,
        .trigger_count = detector->trigger_count,
        .trigger_required = detector->config.trigger_consecutive,
        .stale_references = stale,
        .oldest_reference_age_scans = oldest_age,
    };
    return MULTIREF_OK;
}

const char *multiref_state_name(multiref_state_t state)
{
    switch (state) {
    case MULTIREF_STATE_CALIBRATING:
        return "CALIBRATING";
    case MULTIREF_STATE_WARMUP:
        return "WARMUP";
    case MULTIREF_STATE_IDLE:
        return "IDLE";
    case MULTIREF_STATE_MOTION:
        return "MOTION";
    case MULTIREF_STATE_DEGRADED:
        return "DEGRADED";
    case MULTIREF_STATE_NO_DATA:
        return "NO_DATA";
    default:
        return "UNKNOWN";
    }
}
