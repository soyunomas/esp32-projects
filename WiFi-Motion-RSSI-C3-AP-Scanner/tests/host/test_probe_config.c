#include "probe_config.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "reference_store.h"

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t mode;
    uint8_t ssid_count;
    reference_ssid_t ssids[PROBE_CONFIG_MAX_SSIDS];
    char admin_username[PROBE_CONFIG_ADMIN_USER_MAX_LENGTH + 1U];
    char admin_password[PROBE_CONFIG_ADMIN_PASSWORD_MAX_LENGTH + 1U];
    uint32_t crc32;
} probe_config_v2_blob_t;

static probe_config_blob_t automatic(void)
{
    probe_config_blob_t config;
    probe_config_set_defaults(&config);
    return config;
}

static void make_manual(probe_config_blob_t *config)
{
    config->mode = PROBE_CONFIG_MODE_MANUAL;
    config->ssid_count = 1U;
    config->ssids[0].bytes[0] = 'A';
    config->ssids[0].length = 1U;
    memcpy(config->admin_username, "owner", 6U);
    memcpy(config->admin_password, "secret", 7U);
    probe_config_finalize(config);
}

int main(void)
{
    probe_config_blob_t config = automatic();
    assert(probe_config_validate(&config));
    assert(config.trigger_score_x100 == 250U);
    assert(config.trigger_consecutive == 1U);
    assert(config.inter_scan_delay_ms == 500U);
    assert(config.motion_duration_seconds == 4U);
    assert(config.calibration_scans == 25U);

    for (uint8_t confirmation = 1U; confirmation <= 3U; ++confirmation) {
        config = automatic();
        config.trigger_consecutive = confirmation;
        probe_config_finalize(&config);
        assert(probe_config_validate(&config));
    }
    const uint16_t calibration_options[] = {15U, 25U, 40U};
    for (size_t index = 0U;
         index < sizeof(calibration_options) /
                     sizeof(calibration_options[0]);
         ++index) {
        config = automatic();
        config.calibration_scans = calibration_options[index];
        probe_config_finalize(&config);
        assert(probe_config_validate(&config));
    }

    config = automatic();
    make_manual(&config);
    assert(probe_config_validate(&config));
    config.ssids[1] = config.ssids[0];
    config.ssid_count = 2U;
    probe_config_finalize(&config);
    assert(!probe_config_validate(&config));

    config = automatic();
    config.trigger_consecutive = 9U;
    config.calibration_scans = 99U;
    config.inter_scan_delay_ms = 333U;
    config.motion_duration_seconds = 7U;
    config.trigger_score_x100 = 999U;
    probe_config_finalize(&config);
    bool changed = false;
    probe_config_blob_t repaired;
    assert(probe_config_decode(
        &config, sizeof(config), &repaired, &changed));
    assert(changed);
    assert(repaired.trigger_score_x100 == 250U);
    assert(repaired.trigger_consecutive == 1U);
    assert(repaired.inter_scan_delay_ms == 500U);
    assert(repaired.motion_duration_seconds == 4U);
    assert(repaired.calibration_scans == 25U);

    probe_config_v2_blob_t old = {0};
    old.magic = PROBE_CONFIG_MAGIC;
    old.version = PROBE_CONFIG_PREVIOUS_VERSION;
    old.mode = PROBE_CONFIG_MODE_MANUAL;
    old.ssid_count = 1U;
    old.ssids[0].bytes[0] = 'Z';
    old.ssids[0].length = 1U;
    memcpy(old.admin_username, "legacy", 7U);
    memcpy(old.admin_password, "legacy-pass", 12U);
    old.crc32 = reference_store_crc32(
        &old, offsetof(probe_config_v2_blob_t, crc32));
    assert(probe_config_decode(&old, sizeof(old), &config, &changed));
    assert(changed);
    assert(config.version == PROBE_CONFIG_VERSION);
    assert(config.mode == PROBE_CONFIG_MODE_MANUAL);
    assert(config.ssid_count == 1U);
    assert(config.ssids[0].bytes[0] == 'Z');
    assert(strcmp(config.admin_username, "legacy") == 0);
    assert(strcmp(config.admin_password, "legacy-pass") == 0);
    assert(config.trigger_consecutive == 1U);
    assert(config.calibration_scans == 25U);

    assert(probe_config_release_score_x100(250U) == 125U);
    assert(probe_config_release_score_x100(1U) == 0U);
    assert(probe_config_release_score_x100(350U) < 350U);
    assert(probe_config_clear_consecutive(2U, 500U) == 4U);
    assert(probe_config_clear_consecutive(4U, 740U) == 6U);
    assert(probe_config_clear_consecutive(8U, 1240U) == 7U);
    assert(probe_config_clear_consecutive(4U, 0U) == 1U);

    config = automatic();
    make_manual(&config);
    probe_config_blob_t defaults = automatic();
    config.trigger_score_x100 = defaults.trigger_score_x100;
    config.trigger_consecutive = defaults.trigger_consecutive;
    config.inter_scan_delay_ms = defaults.inter_scan_delay_ms;
    config.motion_duration_seconds = defaults.motion_duration_seconds;
    config.calibration_scans = defaults.calibration_scans;
    probe_config_finalize(&config);
    assert(config.mode == PROBE_CONFIG_MODE_MANUAL);
    assert(config.ssid_count == 1U);
    assert(strcmp(config.admin_username, "owner") == 0);
    assert(strcmp(config.admin_password, "secret") == 0);
    return 0;
}
