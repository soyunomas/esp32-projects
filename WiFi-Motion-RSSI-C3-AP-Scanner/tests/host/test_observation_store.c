#include "observation_store.h"

#include <assert.h>
#include <string.h>

static const uint8_t BSSID_A[REFERENCE_SELECTOR_BSSID_LENGTH] =
    {0U, 0U, 0U, 0U, 0U, 1U};
static const uint8_t BSSID_B[REFERENCE_SELECTOR_BSSID_LENGTH] =
    {0U, 0U, 0U, 0U, 0U, 2U};
static const uint8_t BSSID_C[REFERENCE_SELECTOR_BSSID_LENGTH] =
    {0U, 0U, 0U, 0U, 0U, 3U};

int main(void)
{
    observation_store_t store;
    assert(observation_store_init(&store, 2U, 2U) ==
           OBSERVATION_STORE_OK);
    assert(observation_store_begin_scan(&store, 1U) ==
           OBSERVATION_STORE_OK);
    assert(observation_store_observe(
               &store, BSSID_A, (const uint8_t *)"mesh", 4U, 1U, -50) ==
           OBSERVATION_STORE_OK);
    assert(observation_store_observe(
               &store, BSSID_B, (const uint8_t *)"mesh", 4U, 6U, -80) ==
           OBSERVATION_STORE_OK);
    assert(observation_store_observe(
               &store, BSSID_A, (const uint8_t *)"mesh", 4U, 1U, -49) ==
           OBSERVATION_STORE_DUPLICATE_OBSERVATION);
    assert(observation_store_observe(
               &store, BSSID_C, (const uint8_t *)"other", 5U, 11U, -60) ==
           OBSERVATION_STORE_TABLE_FULL);
    assert(observation_store_end_scan(&store) == OBSERVATION_STORE_OK);

    assert(observation_store_begin_scan(&store, 2U) ==
           OBSERVATION_STORE_OK);
    assert(observation_store_observe(
               &store, BSSID_A, (const uint8_t *)"mesh", 4U, 1U, -52) ==
           OBSERVATION_STORE_OK);
    assert(observation_store_observe(
               &store, BSSID_B, (const uint8_t *)"mesh2", 5U, 11U, -81) ==
           OBSERVATION_STORE_OK);
    assert(observation_store_end_scan(&store) == OBSERVATION_STORE_OK);

    assert(observation_store_begin_scan(&store, 3U) ==
           OBSERVATION_STORE_OK);
    assert(observation_store_observe(
               &store, BSSID_A, (const uint8_t *)"mesh", 4U, 1U, -54) ==
           OBSERVATION_STORE_SAMPLE_CAPACITY_REACHED);
    assert(observation_store_end_scan(&store) == OBSERVATION_STORE_OK);

    reference_candidate_t candidates[2];
    size_t candidate_count = 0U;
    assert(observation_store_export_candidates(
               &store, candidates, 1U, &candidate_count) ==
           OBSERVATION_STORE_OUTPUT_TOO_SMALL);
    assert(observation_store_export_candidates(
               &store, candidates, 2U, &candidate_count) ==
           OBSERVATION_STORE_OK);
    assert(candidate_count == 2U);

    assert(memcmp(candidates[0].bssid,
                  BSSID_A,
                  REFERENCE_SELECTOR_BSSID_LENGTH) == 0);
    assert(candidates[0].samples == 2U);
    assert(candidates[0].observed_scans == 3U);
    assert(candidates[0].total_scans == 3U);
    assert(candidates[0].median_rssi_x10 == -510);
    assert(candidates[0].mad_x10 == 10U);
    assert(candidates[0].ssid_stable);
    assert(candidates[0].channel_stable);

    assert(candidates[1].samples == 2U);
    assert(candidates[1].observed_scans == 2U);
    assert(!candidates[1].ssid_stable);
    assert(!candidates[1].channel_stable);

    const reference_selector_policy_t policy = {
        .minimum_samples = 2U,
        .minimum_presence_permille = 500U,
        .minimum_rssi_x10 = -900,
        .maximum_mad_x10 = 50U,
        .maximum_references = 2U,
    };
    reference_decision_t decisions[2];
    size_t selected_count = 0U;
    assert(reference_selector_select(candidates,
                                     candidate_count,
                                     NULL,
                                     0U,
                                     &policy,
                                     decisions,
                                     2U,
                                     &selected_count) ==
           REFERENCE_SELECTOR_OK);
    assert(selected_count == 1U);
    assert(decisions[0].selected);
    assert(decisions[1].rejection_flags &
           REFERENCE_REJECT_UNSTABLE_SSID);
    assert(decisions[1].rejection_flags &
           REFERENCE_REJECT_UNSTABLE_CHANNEL);

    assert(observation_store_begin_scan(&store, 3U) ==
           OBSERVATION_STORE_SCAN_ID_NOT_INCREASING);
    assert(observation_store_end_scan(&store) ==
           OBSERVATION_STORE_NO_OPEN_SCAN);
    return 0;
}
