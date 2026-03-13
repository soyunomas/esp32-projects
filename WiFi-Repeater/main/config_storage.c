#include <string.h>
#include "config_storage.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "config_storage";
#define NVS_NAMESPACE "repeater_cfg"

esp_err_t config_storage_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

void config_storage_set_defaults(repeater_config_t *config)
{
    memset(config, 0, sizeof(repeater_config_t));
    strlcpy(config->ap_ssid, "ESP32-Repeater", sizeof(config->ap_ssid));
    strlcpy(config->ap_pass, "12345678", sizeof(config->ap_pass));
    config->ap_channel = 1;
    config->ap_max_conn = 4;
}

esp_err_t config_storage_load(repeater_config_t *config)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NVS namespace not found, using defaults");
        config_storage_set_defaults(config);
        return ESP_ERR_NOT_FOUND;
    }

    size_t len;

    len = sizeof(config->sta_ssid);
    if (nvs_get_str(handle, "sta_ssid", config->sta_ssid, &len) != ESP_OK) {
        config->sta_ssid[0] = '\0';
    }

    len = sizeof(config->sta_pass);
    if (nvs_get_str(handle, "sta_pass", config->sta_pass, &len) != ESP_OK) {
        config->sta_pass[0] = '\0';
    }

    len = sizeof(config->ap_ssid);
    if (nvs_get_str(handle, "ap_ssid", config->ap_ssid, &len) != ESP_OK) {
        strlcpy(config->ap_ssid, "ESP32-Repeater", sizeof(config->ap_ssid));
    }

    len = sizeof(config->ap_pass);
    if (nvs_get_str(handle, "ap_pass", config->ap_pass, &len) != ESP_OK) {
        strlcpy(config->ap_pass, "12345678", sizeof(config->ap_pass));
    }

    uint8_t val;
    if (nvs_get_u8(handle, "ap_channel", &val) == ESP_OK) {
        config->ap_channel = val;
    } else {
        config->ap_channel = 1;
    }

    if (nvs_get_u8(handle, "ap_max_conn", &val) == ESP_OK) {
        config->ap_max_conn = val;
    } else {
        config->ap_max_conn = 4;
    }

    nvs_close(handle);
    ESP_LOGI(TAG, "Config loaded: STA='%s' AP='%s' CH=%d",
             config->sta_ssid, config->ap_ssid, config->ap_channel);
    return ESP_OK;
}

esp_err_t config_storage_save(const repeater_config_t *config)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    nvs_set_str(handle, "sta_ssid", config->sta_ssid);
    nvs_set_str(handle, "sta_pass", config->sta_pass);
    nvs_set_str(handle, "ap_ssid", config->ap_ssid);
    nvs_set_str(handle, "ap_pass", config->ap_pass);
    nvs_set_u8(handle, "ap_channel", config->ap_channel);
    nvs_set_u8(handle, "ap_max_conn", config->ap_max_conn);

    ret = nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "Config saved: STA='%s' AP='%s' CH=%d",
             config->sta_ssid, config->ap_ssid, config->ap_channel);
    return ret;
}

esp_err_t config_storage_erase(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) return ret;

    ret = nvs_erase_all(handle);
    nvs_commit(handle);
    nvs_close(handle);
    ESP_LOGI(TAG, "Config erased");
    return ret;
}
