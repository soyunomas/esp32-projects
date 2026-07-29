#include "reference_selector.h"

#include <string.h>

static uint16_t presence_permille(const reference_candidate_t *candidate)
{
    if (candidate->total_scans == 0U) {
        return 0U;
    }
    const uint32_t scaled =
        (uint32_t)candidate->observed_scans * 1000U /
        candidate->total_scans;
    return scaled > 1000U ? 1000U : (uint16_t)scaled;
}

static bool ssid_equal(const reference_ssid_t *left,
                       const reference_ssid_t *right)
{
    return left->length == right->length &&
           left->length <= REFERENCE_SELECTOR_SSID_MAX_LENGTH &&
           memcmp(left->bytes, right->bytes, left->length) == 0;
}

static bool ssid_selected(const reference_ssid_t *ssid,
                          const reference_ssid_t *manual_ssids,
                          size_t manual_ssid_count)
{
    for (size_t index = 0U; index < manual_ssid_count; ++index) {
        if (ssid_equal(ssid, &manual_ssids[index])) {
            return true;
        }
    }
    return false;
}

static bool candidate_precedes(const reference_candidate_t *left,
                               const reference_candidate_t *right)
{
    const uint16_t left_presence = presence_permille(left);
    const uint16_t right_presence = presence_permille(right);
    if (left_presence != right_presence) {
        return left_presence > right_presence;
    }
    if (left->median_rssi_x10 != right->median_rssi_x10) {
        return left->median_rssi_x10 > right->median_rssi_x10;
    }
    if (left->mad_x10 != right->mad_x10) {
        return left->mad_x10 < right->mad_x10;
    }
    return memcmp(left->bssid,
                  right->bssid,
                  REFERENCE_SELECTOR_BSSID_LENGTH) < 0;
}

static bool valid_arguments(const reference_candidate_t *candidates,
                            size_t candidate_count,
                            const reference_ssid_t *manual_ssids,
                            size_t manual_ssid_count,
                            const reference_selector_policy_t *policy,
                            reference_decision_t *decisions,
                            size_t decision_capacity,
                            size_t *selected_count)
{
    if (policy == NULL || selected_count == NULL ||
        candidate_count > decision_capacity ||
        policy->maximum_references == 0U ||
        policy->minimum_presence_permille > 1000U) {
        return false;
    }
    if (candidate_count > 0U &&
        (candidates == NULL || decisions == NULL)) {
        return false;
    }
    if (manual_ssid_count > 0U && manual_ssids == NULL) {
        return false;
    }
    for (size_t index = 0U; index < manual_ssid_count; ++index) {
        if (manual_ssids[index].length == 0U ||
            manual_ssids[index].length >
                REFERENCE_SELECTOR_SSID_MAX_LENGTH) {
            return false;
        }
    }
    return true;
}

reference_selector_status_t reference_selector_select(
    const reference_candidate_t *candidates,
    size_t candidate_count,
    const reference_ssid_t *manual_ssids,
    size_t manual_ssid_count,
    const reference_selector_policy_t *policy,
    reference_decision_t *decisions,
    size_t decision_capacity,
    size_t *selected_count)
{
    if (!valid_arguments(candidates,
                         candidate_count,
                         manual_ssids,
                         manual_ssid_count,
                         policy,
                         decisions,
                         decision_capacity,
                         selected_count)) {
        return REFERENCE_SELECTOR_INVALID_ARGUMENT;
    }
    if (candidate_count > REFERENCE_SELECTOR_MAX_CANDIDATES) {
        return REFERENCE_SELECTOR_TOO_MANY_CANDIDATES;
    }
    for (size_t index = 0U; index < candidate_count; ++index) {
        if (candidates[index].ssid.length >
                REFERENCE_SELECTOR_SSID_MAX_LENGTH ||
            candidates[index].samples >
                candidates[index].observed_scans ||
            candidates[index].observed_scans >
                candidates[index].total_scans) {
            return REFERENCE_SELECTOR_INVALID_ARGUMENT;
        }
        for (size_t previous = 0U; previous < index; ++previous) {
            if (memcmp(candidates[index].bssid,
                       candidates[previous].bssid,
                       REFERENCE_SELECTOR_BSSID_LENGTH) == 0) {
                return REFERENCE_SELECTOR_DUPLICATE_BSSID;
            }
        }
    }

    size_t eligible[REFERENCE_SELECTOR_MAX_CANDIDATES];
    size_t eligible_count = 0U;
    for (size_t index = 0U; index < candidate_count; ++index) {
        const reference_candidate_t *candidate = &candidates[index];
        uint32_t flags = REFERENCE_REJECT_NONE;
        if (manual_ssid_count > 0U &&
            !ssid_selected(&candidate->ssid,
                           manual_ssids,
                           manual_ssid_count)) {
            flags |= REFERENCE_REJECT_SSID_NOT_SELECTED;
        }
        if (candidate->samples < policy->minimum_samples) {
            flags |= REFERENCE_REJECT_INSUFFICIENT_SAMPLES;
        }
        if (presence_permille(candidate) <
            policy->minimum_presence_permille) {
            flags |= REFERENCE_REJECT_LOW_PRESENCE;
        }
        if (candidate->median_rssi_x10 < policy->minimum_rssi_x10) {
            flags |= REFERENCE_REJECT_WEAK_RSSI;
        }
        if (candidate->mad_x10 > policy->maximum_mad_x10) {
            flags |= REFERENCE_REJECT_HIGH_MAD;
        }
        if (!candidate->channel_stable || candidate->channel == 0U) {
            flags |= REFERENCE_REJECT_UNSTABLE_CHANNEL;
        }
        if (!candidate->ssid_stable) {
            flags |= REFERENCE_REJECT_UNSTABLE_SSID;
        }
        decisions[index] = (reference_decision_t){
            .candidate_index = index,
            .rejection_flags = flags,
            .rank = 0U,
            .selected = false,
        };
        if (flags == REFERENCE_REJECT_NONE) {
            eligible[eligible_count++] = index;
        }
    }

    for (size_t index = 1U; index < eligible_count; ++index) {
        const size_t value = eligible[index];
        size_t position = index;
        while (position > 0U &&
               candidate_precedes(&candidates[value],
                                  &candidates[eligible[position - 1U]])) {
            eligible[position] = eligible[position - 1U];
            position--;
        }
        eligible[position] = value;
    }

    const size_t limit =
        eligible_count < policy->maximum_references
            ? eligible_count
            : policy->maximum_references;
    for (size_t rank = 0U; rank < eligible_count; ++rank) {
        reference_decision_t *decision = &decisions[eligible[rank]];
        decision->rank = (uint8_t)(rank + 1U);
        if (rank < limit) {
            decision->selected = true;
        } else {
            decision->rejection_flags |=
                REFERENCE_REJECT_LIMIT_REACHED;
        }
    }
    *selected_count = limit;
    return REFERENCE_SELECTOR_OK;
}
