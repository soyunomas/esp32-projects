#include "app_config.h"

#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#define CONFIG_NAMESPACE "motion"
#define CONFIG_BLOB_KEY "runtime_v1"
#define CONFIG_MAGIC 0x4D4F5431UL
#define CONFIG_VERSION 1U

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    app_runtime_config_t config;
} stored_config_t;

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
        .sample_interval_ms = CONFIG_MOTION_SAMPLE_INTERVAL_MS,
        .detector = {
            .window_size = CONFIG_MOTION_WINDOW_SIZE,
            .calibration_samples = CONFIG_MOTION_CALIBRATION_SAMPLES,
            .sigma_multiplier =
                (float)CONFIG_MOTION_SIGMA_MULTIPLIER_X100 / 100.0f,
            .minimum_threshold =
                (float)CONFIG_MOTION_MIN_THRESHOLD_X100 / 100.0f,
            .trigger_consecutive = CONFIG_MOTION_TRIGGER_CONSECUTIVE,
            .clear_consecutive = CONFIG_MOTION_CLEAR_CONSECUTIVE,
        },
    };
}

bool app_config_valid(const app_runtime_config_t *config)
{
    return config != NULL &&
           config->sample_interval_ms >= 50U &&
           config->sample_interval_ms <= 5000U &&
           motion_detector_config_valid(&config->detector);
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

    stored_config_t stored = {0};
    size_t size = sizeof(stored);
    error = nvs_get_blob(handle, CONFIG_BLOB_KEY, &stored, &size);
    nvs_close(handle);

    if (error == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (error != ESP_OK) {
        return error;
    }

    if (size != sizeof(stored) ||
        stored.magic != CONFIG_MAGIC ||
        stored.version != CONFIG_VERSION ||
        stored.size != sizeof(stored.config) ||
        !app_config_valid(&stored.config)) {
        return ESP_ERR_INVALID_STATE;
    }

    *config = stored.config;
    return ESP_OK;
}

esp_err_t app_config_save(const app_runtime_config_t *config)
{
    if (!app_config_valid(config)) {
        return ESP_ERR_INVALID_ARG;
    }

    const stored_config_t stored = {
        .magic = CONFIG_MAGIC,
        .version = CONFIG_VERSION,
        .size = sizeof(*config),
        .config = *config,
    };

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
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        error = ESP_OK;
    }
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    nvs_close(handle);
    return error;
}
