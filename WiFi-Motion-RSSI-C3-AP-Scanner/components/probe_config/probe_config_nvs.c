#include "probe_config.h"

#include <string.h>

#include "esp_err.h"
#include "nvs.h"

#define PROBE_CONFIG_NAMESPACE "probe_cfg"
#define PROBE_CONFIG_KEY "settings"

probe_config_status_t probe_config_load(probe_config_blob_t *config)
{
    if (config == NULL) {
        return PROBE_CONFIG_INVALID;
    }
    nvs_handle_t handle;
    esp_err_t error =
        nvs_open(PROBE_CONFIG_NAMESPACE, NVS_READONLY, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        return PROBE_CONFIG_NOT_FOUND;
    }
    if (error != ESP_OK) {
        return PROBE_CONFIG_IO_ERROR;
    }
    size_t size = sizeof(*config);
    memset(config, 0, sizeof(*config));
    error = nvs_get_blob(handle, PROBE_CONFIG_KEY, config, &size);
    nvs_close(handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        return PROBE_CONFIG_NOT_FOUND;
    }
    if (error != ESP_OK || size != sizeof(*config)) {
        return error == ESP_OK ? PROBE_CONFIG_INVALID
                               : PROBE_CONFIG_IO_ERROR;
    }
    return probe_config_validate(config) ? PROBE_CONFIG_OK
                                         : PROBE_CONFIG_INVALID;
}

probe_config_status_t probe_config_save(probe_config_blob_t *config)
{
    if (config == NULL) {
        return PROBE_CONFIG_INVALID;
    }
    probe_config_finalize(config);
    if (!probe_config_validate(config)) {
        return PROBE_CONFIG_INVALID;
    }
    nvs_handle_t handle;
    esp_err_t error =
        nvs_open(PROBE_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (error != ESP_OK) {
        return PROBE_CONFIG_IO_ERROR;
    }
    error = nvs_set_blob(handle, PROBE_CONFIG_KEY, config, sizeof(*config));
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    nvs_close(handle);
    return error == ESP_OK ? PROBE_CONFIG_OK
                           : PROBE_CONFIG_IO_ERROR;
}
