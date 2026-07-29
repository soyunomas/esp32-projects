#include "app_config.h"

#include <string.h>

#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#define CONFIG_NAMESPACE "motion"
#define CONFIG_BLOB_KEY "runtime_v4"
#define CONFIG_V3_BLOB_KEY "runtime_v3"
#define CONFIG_V2_BLOB_KEY "runtime_v2"
#define CONFIG_MAGIC 0x4D4F5431UL

static motion_sensitivity_profile_t configured_profile(void)
{
#if CONFIG_MOTION_PROFILE_LOW
    return MOTION_PROFILE_LOW;
#elif CONFIG_MOTION_PROFILE_HIGH
    return MOTION_PROFILE_HIGH;
#else
    return MOTION_PROFILE_BALANCED;
#endif
}

static motion_feature_algorithm_t configured_algorithm(void)
{
#if CONFIG_MOTION_FEATURE_STANDARD_DEVIATION
    return MOTION_FEATURE_STANDARD_DEVIATION;
#elif CONFIG_MOTION_FEATURE_SAMPLE_VARIANCE
    return MOTION_FEATURE_SAMPLE_VARIANCE;
#elif CONFIG_MOTION_FEATURE_RANGE
    return MOTION_FEATURE_RANGE;
#elif CONFIG_MOTION_FEATURE_MEDIAN_ABSOLUTE_DEVIATION
    return MOTION_FEATURE_MEDIAN_ABSOLUTE_DEVIATION;
#else
    return MOTION_FEATURE_MEAN_ABSOLUTE_DIFFERENCE;
#endif
}

static motion_baseline_mode_t configured_baseline_mode(void)
{
#if CONFIG_MOTION_BASELINE_MEDIAN_MAD
    return MOTION_BASELINE_MEDIAN_MAD;
#else
    return MOTION_BASELINE_MEAN_STDDEV;
#endif
}

typedef struct {
    uint32_t sample_interval_ms;
    motion_sensitivity_profile_t sensitivity_profile;
    motion_detector_config_t detector;
} app_runtime_config_v2_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    app_runtime_config_v2_t config;
} stored_config_v2_t;

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
} stored_config_v3_t;

typedef struct {
    stored_config_v3_t v3;
    uint32_t detection_source;
} stored_config_v4_t;

const char *app_detection_source_name(app_detection_source_t source)
{
    switch (source) {
    case APP_DETECTION_SOURCE_RSSI:
        return "rssi";
    case APP_DETECTION_SOURCE_CSI:
        return "csi";
    case APP_DETECTION_SOURCE_BOTH:
        return "both";
    default:
        return "unknown";
    }
}

bool app_detection_source_parse(const char *name,
                                app_detection_source_t *source)
{
    if (name == NULL || source == NULL) {
        return false;
    }
    for (int value = APP_DETECTION_SOURCE_RSSI;
         value < APP_DETECTION_SOURCE_COUNT; ++value) {
        if (strcmp(name,
                   app_detection_source_name(
                       (app_detection_source_t)value)) == 0) {
            *source = (app_detection_source_t)value;
            return true;
        }
    }
    return false;
}

bool app_detection_source_active(app_detection_source_t source,
                                 bool rssi_active,
                                 bool csi_active)
{
    if (source == APP_DETECTION_SOURCE_CSI) {
        return csi_active;
    }
    if (source == APP_DETECTION_SOURCE_BOTH) {
        return rssi_active || csi_active;
    }
    return source == APP_DETECTION_SOURCE_RSSI && rssi_active;
}

esp_err_t app_config_init(void)
{
    esp_err_t error = nvs_flash_init();
    if (error == ESP_ERR_NVS_NO_FREE_PAGES ||
        error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        error = nvs_flash_init();
    }
    return error;
}

void app_config_defaults(app_runtime_config_t *config)
{
    if (config == NULL) {
        return;
    }

    *config = (app_runtime_config_t){
        .wifi_ssid = CONFIG_MOTION_WIFI_SSID,
        .wifi_password = CONFIG_MOTION_WIFI_PASSWORD,
        .sample_interval_ms = CONFIG_MOTION_SAMPLE_INTERVAL_MS,
        .detection_source = APP_DETECTION_SOURCE_RSSI,
        .sensitivity_profile = configured_profile(),
        .detector = {
            .window_size = CONFIG_MOTION_WINDOW_SIZE,
            .calibration_samples = CONFIG_MOTION_CALIBRATION_SAMPLES,
            .algorithm = configured_algorithm(),
            .baseline_mode = configured_baseline_mode(),
            .sigma_multiplier =
                (float)CONFIG_MOTION_SIGMA_MULTIPLIER_X100 / 100.0f,
            .minimum_threshold =
                (float)CONFIG_MOTION_MIN_THRESHOLD_X100 / 100.0f,
            .release_threshold_ratio =
                (float)CONFIG_MOTION_RELEASE_THRESHOLD_RATIO_X100 / 100.0f,
            .baseline_update_alpha =
                (float)CONFIG_MOTION_BASELINE_UPDATE_ALPHA_X10000 / 10000.0f,
            .trigger_consecutive = CONFIG_MOTION_TRIGGER_CONSECUTIVE,
            .clear_consecutive = CONFIG_MOTION_CLEAR_CONSECUTIVE,
        },
    };
}

bool app_config_valid(const app_runtime_config_t *config)
{
    if (config == NULL ||
        strnlen(config->wifi_ssid, sizeof(config->wifi_ssid)) >=
            sizeof(config->wifi_ssid) ||
        strnlen(config->wifi_password, sizeof(config->wifi_password)) >=
            sizeof(config->wifi_password)) {
        return false;
    }

    const size_t password_length = strlen(config->wifi_password);
    bool password_valid = password_length == 0U ||
                          (password_length >= 8U && password_length <= 63U);
    if (password_length == 64U) {
        password_valid = true;
        for (size_t index = 0U; index < password_length; ++index) {
            const char character = config->wifi_password[index];
            if (!((character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f') ||
                  (character >= 'A' && character <= 'F'))) {
                password_valid = false;
                break;
            }
        }
    }
    return password_valid &&
           (config->wifi_ssid[0] != '\0' || password_length == 0U) &&
           config->sample_interval_ms >= 50U &&
           config->sample_interval_ms <= 5000U &&
           config->detection_source >= APP_DETECTION_SOURCE_RSSI &&
           config->detection_source < APP_DETECTION_SOURCE_COUNT &&
           config->sensitivity_profile >= MOTION_PROFILE_LOW &&
           config->sensitivity_profile < MOTION_PROFILE_COUNT &&
           motion_detector_config_valid(&config->detector);
}

static void stored_v4_from_runtime(stored_config_v4_t *stored,
                                   const app_runtime_config_t *config)
{
    *stored = (stored_config_v4_t){
        .v3 = {
            .magic = CONFIG_MAGIC,
            .version = APP_CONFIG_SCHEMA_VERSION,
            .size = sizeof(*stored),
            .sample_interval_ms = config->sample_interval_ms,
            .sensitivity_profile = (uint32_t)config->sensitivity_profile,
            .window_size = (uint32_t)config->detector.window_size,
            .calibration_samples =
                (uint32_t)config->detector.calibration_samples,
            .algorithm = (uint32_t)config->detector.algorithm,
            .baseline_mode = (uint32_t)config->detector.baseline_mode,
            .sigma_multiplier = config->detector.sigma_multiplier,
            .minimum_threshold = config->detector.minimum_threshold,
            .release_threshold_ratio =
                config->detector.release_threshold_ratio,
            .baseline_update_alpha = config->detector.baseline_update_alpha,
            .trigger_consecutive =
                (uint32_t)config->detector.trigger_consecutive,
            .clear_consecutive =
                (uint32_t)config->detector.clear_consecutive,
        },
        .detection_source = (uint32_t)config->detection_source,
    };
    strlcpy(stored->v3.wifi_ssid,
            config->wifi_ssid,
            sizeof(stored->v3.wifi_ssid));
    strlcpy(stored->v3.wifi_password,
            config->wifi_password,
            sizeof(stored->v3.wifi_password));
}

static bool runtime_from_stored_v4(app_runtime_config_t *config,
                                   const stored_config_v4_t *stored)
{
    const stored_config_v3_t *v3 = &stored->v3;
    if (v3->magic != CONFIG_MAGIC ||
        v3->version != APP_CONFIG_SCHEMA_VERSION ||
        v3->size != sizeof(*stored)) {
        return false;
    }

    app_runtime_config_t candidate = {
        .sample_interval_ms = v3->sample_interval_ms,
        .detection_source =
            (app_detection_source_t)stored->detection_source,
        .sensitivity_profile =
            (motion_sensitivity_profile_t)v3->sensitivity_profile,
        .detector = {
            .window_size = v3->window_size,
            .calibration_samples = v3->calibration_samples,
            .algorithm = (motion_feature_algorithm_t)v3->algorithm,
            .baseline_mode = (motion_baseline_mode_t)v3->baseline_mode,
            .sigma_multiplier = v3->sigma_multiplier,
            .minimum_threshold = v3->minimum_threshold,
            .release_threshold_ratio = v3->release_threshold_ratio,
            .baseline_update_alpha = v3->baseline_update_alpha,
            .trigger_consecutive = v3->trigger_consecutive,
            .clear_consecutive = v3->clear_consecutive,
        },
    };
    memcpy(candidate.wifi_ssid, v3->wifi_ssid, sizeof(candidate.wifi_ssid));
    memcpy(candidate.wifi_password,
           v3->wifi_password,
           sizeof(candidate.wifi_password));
    if (!app_config_valid(&candidate)) {
        return false;
    }

    *config = candidate;
    return true;
}

static esp_err_t migrate_v3(app_runtime_config_t *config)
{
    nvs_handle_t handle;
    esp_err_t error = nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (error != ESP_OK) {
        return error;
    }
    stored_config_v3_t stored = {0};
    size_t size = sizeof(stored);
    error = nvs_get_blob(handle, CONFIG_V3_BLOB_KEY, &stored, &size);
    nvs_close(handle);
    if (error != ESP_OK) {
        return error;
    }
    if (size != sizeof(stored) || stored.magic != CONFIG_MAGIC ||
        stored.version != 3U || stored.size != sizeof(stored)) {
        return ESP_ERR_INVALID_STATE;
    }

    app_runtime_config_t migrated = {
        .sample_interval_ms = stored.sample_interval_ms,
        .detection_source = APP_DETECTION_SOURCE_RSSI,
        .sensitivity_profile =
            (motion_sensitivity_profile_t)stored.sensitivity_profile,
        .detector = {
            .window_size = stored.window_size,
            .calibration_samples = stored.calibration_samples,
            .algorithm = (motion_feature_algorithm_t)stored.algorithm,
            .baseline_mode =
                (motion_baseline_mode_t)stored.baseline_mode,
            .sigma_multiplier = stored.sigma_multiplier,
            .minimum_threshold = stored.minimum_threshold,
            .release_threshold_ratio = stored.release_threshold_ratio,
            .baseline_update_alpha = stored.baseline_update_alpha,
            .trigger_consecutive = stored.trigger_consecutive,
            .clear_consecutive = stored.clear_consecutive,
        },
    };
    memcpy(migrated.wifi_ssid, stored.wifi_ssid, sizeof(migrated.wifi_ssid));
    memcpy(migrated.wifi_password,
           stored.wifi_password,
           sizeof(migrated.wifi_password));
    if (!app_config_valid(&migrated)) {
        return ESP_ERR_INVALID_STATE;
    }
    error = app_config_save(&migrated);
    if (error != ESP_OK) {
        return error;
    }
    error = nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (error == ESP_OK) {
        esp_err_t erase_error = nvs_erase_key(handle, CONFIG_V3_BLOB_KEY);
        if (erase_error == ESP_OK || erase_error == ESP_ERR_NVS_NOT_FOUND) {
            erase_error = nvs_commit(handle);
        }
        nvs_close(handle);
        if (erase_error != ESP_OK) {
            return erase_error;
        }
    }
    if (error != ESP_OK) {
        return error;
    }
    *config = migrated;
    return ESP_OK;
}

static esp_err_t migrate_v2(app_runtime_config_t *config)
{
    nvs_handle_t handle;
    esp_err_t error = nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (error != ESP_OK) {
        return error;
    }

    stored_config_v2_t stored = {0};
    size_t size = sizeof(stored);
    error = nvs_get_blob(handle, CONFIG_V2_BLOB_KEY, &stored, &size);
    nvs_close(handle);
    if (error != ESP_OK) {
        return error;
    }
    if (size != sizeof(stored) || stored.magic != CONFIG_MAGIC ||
        stored.version != 2U || stored.size != sizeof(stored.config)) {
        return ESP_ERR_INVALID_STATE;
    }

    app_runtime_config_t migrated;
    app_config_defaults(&migrated);
    migrated.sample_interval_ms = stored.config.sample_interval_ms;
    migrated.sensitivity_profile = stored.config.sensitivity_profile;
    migrated.detector = stored.config.detector;
    if (!app_config_valid(&migrated)) {
        return ESP_ERR_INVALID_STATE;
    }

    error = app_config_save(&migrated);
    if (error != ESP_OK) {
        return error;
    }

    error = nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (error == ESP_OK) {
        esp_err_t erase_error = nvs_erase_key(handle, CONFIG_V2_BLOB_KEY);
        if (erase_error == ESP_OK || erase_error == ESP_ERR_NVS_NOT_FOUND) {
            erase_error = nvs_commit(handle);
        }
        nvs_close(handle);
        if (erase_error != ESP_OK) {
            return erase_error;
        }
    }
    if (error != ESP_OK) {
        return error;
    }

    *config = migrated;
    return ESP_OK;
}

esp_err_t app_config_load(app_runtime_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    app_config_defaults(config);

    nvs_handle_t handle;
    esp_err_t error = nvs_open(CONFIG_NAMESPACE, NVS_READONLY, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (error != ESP_OK) {
        return error;
    }

    stored_config_v4_t stored = {0};
    size_t size = sizeof(stored);
    error = nvs_get_blob(handle, CONFIG_BLOB_KEY, &stored, &size);
    nvs_close(handle);

    if (error == ESP_ERR_NVS_NOT_FOUND) {
        error = migrate_v3(config);
        if (error == ESP_ERR_NVS_NOT_FOUND) {
            error = migrate_v2(config);
        }
        return error == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : error;
    }
    if (error != ESP_OK) {
        return error;
    }

    if (size != sizeof(stored) || !runtime_from_stored_v4(config, &stored)) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

esp_err_t app_config_save(const app_runtime_config_t *config)
{
    if (!app_config_valid(config)) {
        return ESP_ERR_INVALID_ARG;
    }

    stored_config_v4_t stored;
    stored_v4_from_runtime(&stored, config);

    nvs_handle_t handle;
    esp_err_t error = nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (error != ESP_OK) {
        return error;
    }

    error = nvs_set_blob(handle, CONFIG_BLOB_KEY, &stored, sizeof(stored));
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    nvs_close(handle);
    return error;
}

esp_err_t app_config_erase(void)
{
    nvs_handle_t handle;
    esp_err_t error = nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (error != ESP_OK) {
        return error;
    }

    error = nvs_erase_key(handle, CONFIG_BLOB_KEY);
    if (error == ESP_OK || error == ESP_ERR_NVS_NOT_FOUND) {
        const esp_err_t v3_error = nvs_erase_key(handle, CONFIG_V3_BLOB_KEY);
        error = v3_error == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : v3_error;
    }
    if (error == ESP_OK || error == ESP_ERR_NVS_NOT_FOUND) {
        const esp_err_t v2_error = nvs_erase_key(handle, CONFIG_V2_BLOB_KEY);
        error = v2_error == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : v2_error;
    }
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    nvs_close(handle);
    return error;
}
