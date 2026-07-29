#include "reference_selector.h"

#include <assert.h>
#include <string.h>

static reference_ssid_t ssid(const char *text)
{
    reference_ssid_t result = {0};
    result.length = (uint8_t)strlen(text);
    memcpy(result.bytes, text, result.length);
    return result;
}

static reference_candidate_t candidate(uint8_t bssid_last,
                                       const char *ssid_text,
                                       uint16_t samples,
                                       uint16_t total_scans,
                                       int16_t median_rssi_x10,
                                       uint16_t mad_x10)
{
    reference_candidate_t result = {
        .bssid = {0U, 0U, 0U, 0U, 0U, bssid_last},
        .ssid = ssid(ssid_text),
        .channel = 1U,
        .samples = samples,
        .observed_scans = samples,
        .total_scans = total_scans,
        .median_rssi_x10 = median_rssi_x10,
        .mad_x10 = mad_x10,
        .ssid_stable = true,
        .channel_stable = true,
    };
    return result;
}

int main(void)
{
    const reference_selector_policy_t policy = {
        .minimum_samples = 5U,
        .minimum_presence_permille = 800U,
        .minimum_rssi_x10 = -900,
        .maximum_mad_x10 = 50U,
        .maximum_references = 2U,
    };
    reference_candidate_t candidates[] = {
        candidate(3U, "other", 10U, 10U, -400, 10U),
        candidate(2U, "mesh", 10U, 10U, -500, 20U),
        candidate(1U, "mesh", 10U, 10U, -500, 20U),
        candidate(4U, "weak", 10U, 10U, -950, 10U),
    };
    reference_decision_t decisions[4];
    size_t selected_count = 0U;

    assert(reference_selector_select(candidates,
                                     4U,
                                     NULL,
                                     0U,
                                     &policy,
                                     decisions,
                                     4U,
                                     &selected_count) ==
           REFERENCE_SELECTOR_OK);
    assert(selected_count == 2U);
    assert(decisions[0].selected);
    assert(decisions[0].rank == 1U);
    assert(decisions[2].selected);
    assert(decisions[2].rank == 2U);
    assert(!decisions[1].selected);
    assert(decisions[1].rank == 3U);
    assert(decisions[1].rejection_flags &
           REFERENCE_REJECT_LIMIT_REACHED);
    assert(decisions[3].rejection_flags & REFERENCE_REJECT_WEAK_RSSI);

    const reference_ssid_t manual[] = {ssid("mesh")};
    assert(reference_selector_select(candidates,
                                     4U,
                                     manual,
                                     1U,
                                     &policy,
                                     decisions,
                                     4U,
                                     &selected_count) ==
           REFERENCE_SELECTOR_OK);
    assert(selected_count == 2U);
    assert(decisions[1].selected);
    assert(decisions[2].selected);
    assert(decisions[0].rejection_flags &
           REFERENCE_REJECT_SSID_NOT_SELECTED);

    candidates[3] = candidates[0];
    assert(reference_selector_select(candidates,
                                     4U,
                                     NULL,
                                     0U,
                                     &policy,
                                     decisions,
                                     4U,
                                     &selected_count) ==
           REFERENCE_SELECTOR_DUPLICATE_BSSID);
    return 0;
}
