#include "probe_config.h"

#include <stddef.h>
#include <string.h>

#include "reference_store.h"

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#define DEFAULT_TRIGGER_SCORE_X100 \
    CONFIG_PROBE_DETECTOR_TRIGGER_SCORE_X100
#define DEFAULT_TRIGGER_CONSECUTIVE \
    CONFIG_PROBE_DETECTOR_TRIGGER_CONSECUTIVE
#define DEFAULT_INTER_SCAN_DELAY_MS CONFIG_PROBE_INTER_SCAN_DELAY_MS
#define DEFAULT_MOTION_DURATION_SECONDS \
    CONFIG_PROBE_MOTION_DURATION_SECONDS
#define DEFAULT_CALIBRATION_SCANS CONFIG_PROBE_CALIBRATION_SCANS
#else
#define DEFAULT_TRIGGER_SCORE_X100 \
    PROBE_CONFIG_TRIGGER_SCORE_BALANCED_X100
#define DEFAULT_TRIGGER_CONSECUTIVE 1U
#define DEFAULT_INTER_SCAN_DELAY_MS PROBE_CONFIG_SCAN_DELAY_NORMAL_MS
#define DEFAULT_MOTION_DURATION_SECONDS \
    PROBE_CONFIG_MOTION_DURATION_NORMAL_S
#define DEFAULT_CALIBRATION_SCANS \
    PROBE_CONFIG_CALIBRATION_NORMAL_SCANS
#endif

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

static bool valid_trigger_score(uint16_t value)
{
    return value == PROBE_CONFIG_TRIGGER_SCORE_SENSITIVE_X100 ||
           value == PROBE_CONFIG_TRIGGER_SCORE_BALANCED_X100 ||
           value == PROBE_CONFIG_TRIGGER_SCORE_CONSERVATIVE_X100;
}

static bool valid_scan_delay(uint16_t value)
{
    return value == PROBE_CONFIG_SCAN_DELAY_FAST_MS ||
           value == PROBE_CONFIG_SCAN_DELAY_NORMAL_MS ||
           value == PROBE_CONFIG_SCAN_DELAY_SLOW_MS;
}

static bool valid_motion_duration(uint8_t value)
{
    return value == PROBE_CONFIG_MOTION_DURATION_SHORT_S ||
           value == PROBE_CONFIG_MOTION_DURATION_NORMAL_S ||
           value == PROBE_CONFIG_MOTION_DURATION_LONG_S;
}

static bool valid_calibration_scans(uint16_t value)
{
    return value == PROBE_CONFIG_CALIBRATION_FAST_SCANS ||
           value == PROBE_CONFIG_CALIBRATION_NORMAL_SCANS ||
           value == PROBE_CONFIG_CALIBRATION_PRECISE_SCANS;
}

static bool validate_common(uint8_t mode,
                            uint8_t ssid_count,
                            const reference_ssid_t *ssids,
                            const char *username,
                            const char *password)
{
    if (mode > PROBE_CONFIG_MODE_MANUAL ||
        ssid_count > PROBE_CONFIG_MAX_SSIDS) {
        return false;
    }
    const size_t username_length =
        strnlen(username, PROBE_CONFIG_ADMIN_USER_MAX_LENGTH + 1U);
    const size_t password_length =
        strnlen(password, PROBE_CONFIG_ADMIN_PASSWORD_MAX_LENGTH + 1U);
    if (username_length == 0U ||
        username_length > PROBE_CONFIG_ADMIN_USER_MAX_LENGTH ||
        password_length < 4U ||
        password_length > PROBE_CONFIG_ADMIN_PASSWORD_MAX_LENGTH ||
        (mode == PROBE_CONFIG_MODE_AUTOMATIC && ssid_count != 0U) ||
        (mode == PROBE_CONFIG_MODE_MANUAL && ssid_count == 0U)) {
        return false;
    }
    for (uint8_t index = 0U; index < ssid_count; ++index) {
        if (ssids[index].length == 0U ||
            ssids[index].length > REFERENCE_SELECTOR_SSID_MAX_LENGTH) {
            return false;
        }
        for (uint8_t other = (uint8_t)(index + 1U);
             other < ssid_count;
             ++other) {
            if (ssids[index].length == ssids[other].length &&
                memcmp(ssids[index].bytes,
                       ssids[other].bytes,
                       ssids[index].length) == 0) {
                return false;
            }
        }
    }
    return true;
}

void probe_config_set_defaults(probe_config_blob_t *config)
{
    if (config == NULL) {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->mode = PROBE_CONFIG_MODE_AUTOMATIC;
    memcpy(config->admin_username,
           PROBE_CONFIG_DEFAULT_ADMIN_USER,
           sizeof(PROBE_CONFIG_DEFAULT_ADMIN_USER));
    memcpy(config->admin_password,
           PROBE_CONFIG_DEFAULT_ADMIN_PASSWORD,
           sizeof(PROBE_CONFIG_DEFAULT_ADMIN_PASSWORD));
    config->trigger_score_x100 = DEFAULT_TRIGGER_SCORE_X100;
    config->trigger_consecutive = DEFAULT_TRIGGER_CONSECUTIVE;
    config->inter_scan_delay_ms = DEFAULT_INTER_SCAN_DELAY_MS;
    config->motion_duration_seconds = DEFAULT_MOTION_DURATION_SECONDS;
    config->calibration_scans = DEFAULT_CALIBRATION_SCANS;
    probe_config_finalize(config);
}

void probe_config_finalize(probe_config_blob_t *config)
{
    if (config == NULL) {
        return;
    }
    config->magic = PROBE_CONFIG_MAGIC;
    config->version = PROBE_CONFIG_VERSION;
    config->crc32 = reference_store_crc32(
        config, offsetof(probe_config_blob_t, crc32));
}

bool probe_config_validate(const probe_config_blob_t *config)
{
    if (config == NULL || config->magic != PROBE_CONFIG_MAGIC ||
        config->version != PROBE_CONFIG_VERSION) {
        return false;
    }
    if (!validate_common(config->mode,
                         config->ssid_count,
                         config->ssids,
                         config->admin_username,
                         config->admin_password) ||
        !valid_trigger_score(config->trigger_score_x100) ||
        config->trigger_consecutive <
            PROBE_CONFIG_TRIGGER_CONSECUTIVE_MIN ||
        config->trigger_consecutive >
            PROBE_CONFIG_TRIGGER_CONSECUTIVE_MAX ||
        !valid_scan_delay(config->inter_scan_delay_ms) ||
        !valid_motion_duration(config->motion_duration_seconds) ||
        !valid_calibration_scans(config->calibration_scans)) {
        return false;
    }
    return config->crc32 ==
           reference_store_crc32(
               config, offsetof(probe_config_blob_t, crc32));
}

bool probe_config_repair(probe_config_blob_t *config)
{
    if (config == NULL ||
        !validate_common(config->mode,
                         config->ssid_count,
                         config->ssids,
                         config->admin_username,
                         config->admin_password)) {
        return false;
    }
    if (!valid_trigger_score(config->trigger_score_x100)) {
        config->trigger_score_x100 = DEFAULT_TRIGGER_SCORE_X100;
    }
    if (config->trigger_consecutive <
            PROBE_CONFIG_TRIGGER_CONSECUTIVE_MIN ||
        config->trigger_consecutive >
            PROBE_CONFIG_TRIGGER_CONSECUTIVE_MAX) {
        config->trigger_consecutive = DEFAULT_TRIGGER_CONSECUTIVE;
    }
    if (!valid_scan_delay(config->inter_scan_delay_ms)) {
        config->inter_scan_delay_ms = DEFAULT_INTER_SCAN_DELAY_MS;
    }
    if (!valid_motion_duration(config->motion_duration_seconds)) {
        config->motion_duration_seconds =
            DEFAULT_MOTION_DURATION_SECONDS;
    }
    if (!valid_calibration_scans(config->calibration_scans)) {
        config->calibration_scans = DEFAULT_CALIBRATION_SCANS;
    }
    probe_config_finalize(config);
    return true;
}

bool probe_config_decode(const void *data,
                         size_t size,
                         probe_config_blob_t *config,
                         bool *changed)
{
    if (data == NULL || config == NULL) {
        return false;
    }
    if (size == sizeof(*config)) {
        memcpy(config, data, sizeof(*config));
        const uint32_t expected_crc = reference_store_crc32(
            config, offsetof(probe_config_blob_t, crc32));
        if (config->magic != PROBE_CONFIG_MAGIC ||
            config->version != PROBE_CONFIG_VERSION ||
            config->crc32 != expected_crc ||
            !validate_common(config->mode,
                             config->ssid_count,
                             config->ssids,
                             config->admin_username,
                             config->admin_password)) {
            return false;
        }
        const probe_config_blob_t original = *config;
        if (!probe_config_repair(config)) {
            return false;
        }
        if (changed != NULL) {
            *changed = memcmp(&original, config, sizeof(original)) != 0;
        }
        return true;
    }
    if (size != sizeof(probe_config_v2_blob_t)) {
        return false;
    }
    probe_config_v2_blob_t previous;
    memcpy(&previous, data, sizeof(previous));
    if (previous.magic != PROBE_CONFIG_MAGIC ||
        previous.version != PROBE_CONFIG_PREVIOUS_VERSION ||
        previous.crc32 != reference_store_crc32(
                              &previous,
                              offsetof(probe_config_v2_blob_t, crc32)) ||
        !validate_common(previous.mode,
                         previous.ssid_count,
                         previous.ssids,
                         previous.admin_username,
                         previous.admin_password)) {
        return false;
    }
    probe_config_set_defaults(config);
    config->mode = previous.mode;
    config->ssid_count = previous.ssid_count;
    memcpy(config->ssids, previous.ssids, sizeof(previous.ssids));
    memcpy(config->admin_username,
           previous.admin_username,
           sizeof(previous.admin_username));
    memcpy(config->admin_password,
           previous.admin_password,
           sizeof(previous.admin_password));
    probe_config_finalize(config);
    if (changed != NULL) {
        *changed = true;
    }
    return true;
}

uint16_t probe_config_release_score_x100(uint16_t trigger_score_x100)
{
    return trigger_score_x100 > 1U
               ? (uint16_t)(trigger_score_x100 / 2U)
               : 0U;
}

uint8_t probe_config_clear_consecutive(uint8_t duration_seconds,
                                       uint32_t scan_cycle_ms)
{
    if (duration_seconds == 0U || scan_cycle_ms == 0U) {
        return 1U;
    }
    uint32_t scans =
        ((uint32_t)duration_seconds * 1000U + scan_cycle_ms - 1U) /
        scan_cycle_ms;
    if (scans == 0U) {
        scans = 1U;
    } else if (scans > UINT8_MAX) {
        scans = UINT8_MAX;
    }
    return (uint8_t)scans;
}
