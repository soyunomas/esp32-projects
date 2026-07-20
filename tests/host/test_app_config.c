#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "nvs.h"
#include "nvs_flash.h"

#define MOCK_BLOB_MAX 512U

typedef struct {
    bool present;
    char key[16];
    uint8_t data[MOCK_BLOB_MAX];
    size_t size;
} mock_blob_t;

static mock_blob_t blobs[3];

size_t host_strlcpy(char *destination, const char *source, size_t size)
{
    const size_t length = strlen(source);
    if (size > 0U) {
        const size_t copied = length < size - 1U ? length : size - 1U;
        memcpy(destination, source, copied);
        destination[copied] = '\0';
    }
    return length;
}

static mock_blob_t *find_blob(const char *key)
{
    for (size_t index = 0U; index < 3U; ++index) {
        if (blobs[index].present && strcmp(blobs[index].key, key) == 0) {
            return &blobs[index];
        }
    }
    return NULL;
}

static mock_blob_t *allocate_blob(const char *key)
{
    mock_blob_t *blob = find_blob(key);
    if (blob != NULL) {
        return blob;
    }
    for (size_t index = 0U; index < 3U; ++index) {
        if (!blobs[index].present) {
            blobs[index].present = true;
            host_strlcpy(blobs[index].key, key, sizeof(blobs[index].key));
            return &blobs[index];
        }
    }
    return NULL;
}

esp_err_t nvs_flash_init(void)
{
    return ESP_OK;
}

esp_err_t nvs_flash_erase(void)
{
    memset(blobs, 0, sizeof(blobs));
    return ESP_OK;
}

esp_err_t nvs_open(const char *namespace_name,
                   int open_mode,
                   nvs_handle_t *handle)
{
    assert(strcmp(namespace_name, "motion") == 0);
    if (open_mode == NVS_READONLY && !blobs[0].present && !blobs[1].present) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    *handle = 1;
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle)
{
    assert(handle == 1);
}

esp_err_t nvs_get_blob(nvs_handle_t handle,
                       const char *key,
                       void *value,
                       size_t *length)
{
    assert(handle == 1);
    mock_blob_t *blob = find_blob(key);
    if (blob == NULL) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (*length < blob->size) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(value, blob->data, blob->size);
    *length = blob->size;
    return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t handle,
                       const char *key,
                       const void *value,
                       size_t length)
{
    assert(handle == 1);
    mock_blob_t *blob = allocate_blob(key);
    if (blob == NULL || length > sizeof(blob->data)) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(blob->data, value, length);
    blob->size = length;
    return ESP_OK;
}

esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key)
{
    assert(handle == 1);
    mock_blob_t *blob = find_blob(key);
    if (blob == NULL) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    memset(blob, 0, sizeof(*blob));
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    assert(handle == 1);
    return ESP_OK;
}

static void assert_config_equal(const app_runtime_config_t *left,
                                const app_runtime_config_t *right)
{
    assert(strcmp(left->wifi_ssid, right->wifi_ssid) == 0);
    assert(strcmp(left->wifi_password, right->wifi_password) == 0);
    assert(left->sample_interval_ms == right->sample_interval_ms);
    assert(left->detection_source == right->detection_source);
    assert(left->sensitivity_profile == right->sensitivity_profile);
    assert(left->detector.window_size == right->detector.window_size);
    assert(left->detector.calibration_samples ==
           right->detector.calibration_samples);
    assert(left->detector.algorithm == right->detector.algorithm);
    assert(left->detector.baseline_mode == right->detector.baseline_mode);
    assert(left->detector.sigma_multiplier == right->detector.sigma_multiplier);
    assert(left->detector.minimum_threshold == right->detector.minimum_threshold);
    assert(left->detector.release_threshold_ratio ==
           right->detector.release_threshold_ratio);
    assert(left->detector.baseline_update_alpha ==
           right->detector.baseline_update_alpha);
    assert(left->detector.trigger_consecutive ==
           right->detector.trigger_consecutive);
    assert(left->detector.clear_consecutive ==
           right->detector.clear_consecutive);
}

static void test_validation_and_round_trip(void)
{
    app_detection_source_t parsed_source = APP_DETECTION_SOURCE_COUNT;
    assert(app_detection_source_parse("rssi", &parsed_source));
    assert(parsed_source == APP_DETECTION_SOURCE_RSSI);
    assert(app_detection_source_parse("csi", &parsed_source));
    assert(parsed_source == APP_DETECTION_SOURCE_CSI);
    assert(app_detection_source_parse("both", &parsed_source));
    assert(parsed_source == APP_DETECTION_SOURCE_BOTH);
    assert(!app_detection_source_parse("invalid", &parsed_source));
    assert(!app_detection_source_active(APP_DETECTION_SOURCE_RSSI,
                                        false, true));
    assert(app_detection_source_active(APP_DETECTION_SOURCE_RSSI,
                                       true, false));
    assert(app_detection_source_active(APP_DETECTION_SOURCE_CSI,
                                       false, true));
    assert(!app_detection_source_active(APP_DETECTION_SOURCE_CSI,
                                        true, false));
    assert(app_detection_source_active(APP_DETECTION_SOURCE_BOTH,
                                       true, false));
    assert(app_detection_source_active(APP_DETECTION_SOURCE_BOTH,
                                       false, true));
    assert(!app_detection_source_active(APP_DETECTION_SOURCE_BOTH,
                                        false, false));

    app_runtime_config_t expected;
    app_config_defaults(&expected);
    host_strlcpy(expected.wifi_ssid, "test-network", sizeof(expected.wifi_ssid));
    host_strlcpy(expected.wifi_password,
                 "test-password",
                 sizeof(expected.wifi_password));
    expected.sample_interval_ms = 250U;
    expected.detection_source = APP_DETECTION_SOURCE_BOTH;
    expected.detector.algorithm = MOTION_FEATURE_RANGE;
    assert(app_config_valid(&expected));
    assert(app_config_save(&expected) == ESP_OK);

    app_runtime_config_t actual;
    assert(app_config_load(&actual) == ESP_OK);
    assert_config_equal(&expected, &actual);

    expected.wifi_password[0] = 'x';
    expected.wifi_password[1] = '\0';
    assert(!app_config_valid(&expected));
    assert(app_config_save(&expected) == ESP_ERR_INVALID_ARG);

    memset(expected.wifi_password, 'z', 64U);
    expected.wifi_password[64] = '\0';
    assert(!app_config_valid(&expected));
    memset(expected.wifi_password, 'a', 64U);
    assert(app_config_valid(&expected));
    expected.detection_source = APP_DETECTION_SOURCE_COUNT;
    assert(!app_config_valid(&expected));
}

static void test_corruption_and_erase(void)
{
    mock_blob_t *blob = find_blob("runtime_v4");
    assert(blob != NULL);
    blob->data[0] ^= 0xffU;
    app_runtime_config_t config;
    assert(app_config_load(&config) == ESP_ERR_INVALID_STATE);

    assert(app_config_erase() == ESP_OK);
    assert(app_config_load(&config) == ESP_OK);
    app_runtime_config_t defaults;
    app_config_defaults(&defaults);
    assert_config_equal(&defaults, &config);
}

typedef struct {
    uint32_t sample_interval_ms;
    motion_sensitivity_profile_t sensitivity_profile;
    motion_detector_config_t detector;
} runtime_v2_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    runtime_v2_t config;
} stored_v2_t;

static void test_v2_migration(void)
{
    app_runtime_config_t defaults;
    app_config_defaults(&defaults);
    const stored_v2_t legacy = {
        .magic = 0x4D4F5431UL,
        .version = 2U,
        .size = sizeof(runtime_v2_t),
        .config = {
            .sample_interval_ms = 333U,
            .sensitivity_profile = MOTION_PROFILE_HIGH,
            .detector = defaults.detector,
        },
    };
    assert(nvs_set_blob(1, "runtime_v2", &legacy, sizeof(legacy)) == ESP_OK);

    app_runtime_config_t migrated;
    assert(app_config_load(&migrated) == ESP_OK);
    assert(migrated.sample_interval_ms == 333U);
    assert(migrated.sensitivity_profile == MOTION_PROFILE_HIGH);
    assert(find_blob("runtime_v2") == NULL);
    assert(find_blob("runtime_v4") != NULL);
    assert(migrated.detection_source == APP_DETECTION_SOURCE_RSSI);
}

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    char wifi_ssid[APP_CONFIG_WIFI_SSID_MAX_LENGTH + 1U];
    char wifi_password[APP_CONFIG_WIFI_PASSWORD_MAX_LENGTH + 1U];
    uint32_t sample_interval_ms;
    uint32_t sensitivity_profile;
    uint32_t window_size;
    uint32_t calibration_samples;
    uint32_t algorithm;
    uint32_t baseline_mode;
    float sigma_multiplier;
    float minimum_threshold;
    float release_threshold_ratio;
    float baseline_update_alpha;
    uint32_t trigger_consecutive;
    uint32_t clear_consecutive;
} stored_v3_t;

static void test_v3_migration(void)
{
    assert(app_config_erase() == ESP_OK);
    app_runtime_config_t defaults;
    app_config_defaults(&defaults);
    stored_v3_t legacy = {
        .magic = 0x4D4F5431UL,
        .version = 3U,
        .size = sizeof(stored_v3_t),
        .sample_interval_ms = 444U,
        .sensitivity_profile = MOTION_PROFILE_HIGH,
        .window_size = defaults.detector.window_size,
        .calibration_samples = defaults.detector.calibration_samples,
        .algorithm = defaults.detector.algorithm,
        .baseline_mode = defaults.detector.baseline_mode,
        .sigma_multiplier = defaults.detector.sigma_multiplier,
        .minimum_threshold = defaults.detector.minimum_threshold,
        .release_threshold_ratio =
            defaults.detector.release_threshold_ratio,
        .baseline_update_alpha = defaults.detector.baseline_update_alpha,
        .trigger_consecutive = defaults.detector.trigger_consecutive,
        .clear_consecutive = defaults.detector.clear_consecutive,
    };
    host_strlcpy(legacy.wifi_ssid, "legacy-network",
                 sizeof(legacy.wifi_ssid));
    host_strlcpy(legacy.wifi_password, "legacy-password",
                 sizeof(legacy.wifi_password));
    assert(nvs_set_blob(1, "runtime_v3", &legacy, sizeof(legacy)) == ESP_OK);

    app_runtime_config_t migrated;
    assert(app_config_load(&migrated) == ESP_OK);
    assert(migrated.sample_interval_ms == 444U);
    assert(migrated.detection_source == APP_DETECTION_SOURCE_RSSI);
    assert(strcmp(migrated.wifi_ssid, "legacy-network") == 0);
    assert(find_blob("runtime_v3") == NULL);
    assert(find_blob("runtime_v4") != NULL);
}

int main(void)
{
    assert(app_config_init() == ESP_OK);
    test_validation_and_round_trip();
    test_corruption_and_erase();
    test_v2_migration();
    test_v3_migration();
    puts("app_config_tests: OK");
    return 0;
}
