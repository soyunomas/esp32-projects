#include "multiref_detector.h"

#include <assert.h>
#include <string.h>

static const uint8_t BSSID_A[6] = {0U, 1U, 2U, 3U, 4U, 5U};
static const uint8_t BSSID_B[6] = {6U, 7U, 8U, 9U, 10U, 11U};

static multiref_config_t config(void)
{
    return (multiref_config_t){
        .minimum_coverage_permille = 600U,
        .noise_floor_x10 = 20U,
        .trigger_score_x100 = 200U,
        .release_score_x100 = 100U,
        .adaptive_sigma_x100 = 600U,
        .baseline_alpha_permille = 10U,
        .adaptive_window = 8U,
        .warmup_scans = 3U,
        .trigger_consecutive = 2U,
        .clear_consecutive = 3U,
        .unhealthy_consecutive = 2U,
        .recovery_consecutive = 2U,
        .stale_after_scans = 2U,
    };
}

static multiref_detector_t detector(void)
{
    const multiref_reference_t references[] = {
        {
            .bssid = {0U, 1U, 2U, 3U, 4U, 5U},
            .baseline_rssi_x10 = -500,
            .baseline_mad_x10 = 10U,
        },
        {
            .bssid = {6U, 7U, 8U, 9U, 10U, 11U},
            .baseline_rssi_x10 = -800,
            .baseline_mad_x10 = 20U,
        },
    };
    multiref_detector_t result;
    const multiref_config_t detector_config = config();
    assert(multiref_detector_init(&result,
                                  &detector_config,
                                  references,
                                  2U) == MULTIREF_OK);
    return result;
}

static multiref_result_t scan(multiref_detector_t *instance,
                              uint32_t scan_id,
                              bool see_a,
                              int8_t rssi_a,
                              bool see_b,
                              int8_t rssi_b)
{
    assert(multiref_detector_begin_scan(instance, scan_id) ==
           MULTIREF_OK);
    if (see_a) {
        assert(multiref_detector_observe(
                   instance, BSSID_A, rssi_a) == MULTIREF_OK);
    }
    if (see_b) {
        assert(multiref_detector_observe(
                   instance, BSSID_B, rssi_b) == MULTIREF_OK);
    }
    multiref_result_t result;
    assert(multiref_detector_end_scan(instance, &result) ==
           MULTIREF_OK);
    return result;
}

int main(void)
{
    multiref_config_t invalid = config();
    invalid.release_score_x100 = invalid.trigger_score_x100;
    assert(!multiref_config_valid(&invalid));

    multiref_detector_t instance = detector();
    multiref_result_t result = scan(&instance, 1U, true, -50, true, -80);
    assert(result.state == MULTIREF_STATE_WARMUP);
    result = scan(&instance, 2U, true, -50, true, -80);
    result = scan(&instance, 3U, true, -50, true, -80);
    assert(result.state == MULTIREF_STATE_IDLE);
    assert(result.state_changed);
    assert(result.previous_state == MULTIREF_STATE_WARMUP);
    assert(result.score_ready && result.score_x100 == 0U);

    result = scan(&instance, 4U, true, -45, true, -75);
    assert(result.state == MULTIREF_STATE_IDLE);
    assert(result.score_x100 == 250U);
    assert(result.trigger_count == 1U);
    assert(result.trigger_required == 2U);
    const int16_t frozen_a = instance.references[0].baseline_rssi_x10;
    result = scan(&instance, 5U, true, -45, true, -75);
    assert(result.state == MULTIREF_STATE_MOTION);
    assert(result.previous_state == MULTIREF_STATE_IDLE);
    assert(instance.references[0].baseline_rssi_x10 == frozen_a);

    result = scan(&instance, 6U, true, -50, true, -80);
    result = scan(&instance, 7U, true, -50, true, -80);
    result = scan(&instance, 8U, true, -50, true, -80);
    assert(result.state == MULTIREF_STATE_IDLE);
    assert(result.previous_state == MULTIREF_STATE_MOTION);

    result = scan(&instance, 9U, false, 0, false, 0);
    assert(result.state == MULTIREF_STATE_IDLE);
    assert(!result.score_ready);
    result = scan(&instance, 10U, false, 0, false, 0);
    assert(result.state == MULTIREF_STATE_NO_DATA);
    result = scan(&instance, 11U, true, -40, false, 0);
    assert(result.state == MULTIREF_STATE_NO_DATA);
    assert(!result.score_ready);

    result = scan(&instance, 12U, true, -50, true, -80);
    result = scan(&instance, 13U, true, -50, true, -80);
    assert(result.state == MULTIREF_STATE_WARMUP);
    result = scan(&instance, 14U, true, -50, true, -80);
    result = scan(&instance, 15U, true, -50, true, -80);
    result = scan(&instance, 16U, true, -50, true, -80);
    assert(result.state == MULTIREF_STATE_IDLE);

    multiref_detector_t degraded = detector();
    (void)scan(&degraded, 1U, true, -50, true, -80);
    (void)scan(&degraded, 2U, true, -50, true, -80);
    (void)scan(&degraded, 3U, true, -50, true, -80);
    result = scan(&degraded, 4U, true, -50, false, 0);
    assert(result.state == MULTIREF_STATE_IDLE);
    result = scan(&degraded, 5U, true, -50, false, 0);
    assert(result.state == MULTIREF_STATE_DEGRADED);
    result = scan(&degraded, 6U, true, -50, false, 0);
    assert(result.stale_references == 1U);
    assert(result.oldest_reference_age_scans == 3U);

    assert(multiref_detector_begin_scan(&instance, 17U) == MULTIREF_OK);
    assert(multiref_detector_observe(&instance, BSSID_A, -50) ==
           MULTIREF_OK);
    assert(multiref_detector_observe(&instance, BSSID_A, -49) ==
           MULTIREF_DUPLICATE_OBSERVATION);
    assert(multiref_detector_end_scan(&instance, &result) == MULTIREF_OK);
    assert(multiref_detector_begin_scan(&instance, 17U) ==
           MULTIREF_SCAN_ID_NOT_INCREASING);

    multiref_reference_t duplicates[2] = {
        {.bssid = {1U}},
        {.bssid = {1U}},
    };
    const multiref_config_t valid = config();
    assert(multiref_detector_init(
               &instance, &valid, duplicates, 2U) ==
           MULTIREF_INVALID_ARGUMENT);
    assert(strcmp(multiref_state_name(MULTIREF_STATE_DEGRADED),
                  "DEGRADED") == 0);

    multiref_detector_t adaptive = detector();
    (void)scan(&adaptive, 1U, true, -50, true, -80);
    (void)scan(&adaptive, 2U, true, -49, true, -79);
    result = scan(&adaptive, 3U, true, -48, true, -78);
    assert(result.state == MULTIREF_STATE_IDLE);
    assert(result.trigger_score_x100 >
           adaptive.config.trigger_score_x100);
    assert(result.release_score_x100 >
           adaptive.config.release_score_x100);
    assert(result.baseline_score_spread_x100 > 0U);
    return 0;
}
