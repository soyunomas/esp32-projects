#include "app_nvs.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h> 

static const char *TAG = "NVS";

void app_nvs_init(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

void app_nvs_clear_all(void) {
    ESP_LOGW(TAG, "Erasing all NVS data");
    nvs_flash_erase();
    app_nvs_init();
}

esp_err_t app_nvs_save_wifi(const char* ssid, const char* pass) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("wifi_conf", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return err;

    nvs_set_str(my_handle, "ssid", ssid);
    nvs_set_str(my_handle, "pass", pass);
    nvs_commit(my_handle);
    nvs_close(my_handle);
    return ESP_OK;
}

bool app_nvs_get_wifi_config(nvs_wifi_config_t *config) {
    nvs_handle_t my_handle;
    if (nvs_open("wifi_conf", NVS_READONLY, &my_handle) != ESP_OK) return false;

    size_t required_size = sizeof(config->ssid);
    if (nvs_get_str(my_handle, "ssid", config->ssid, &required_size) != ESP_OK) {
        nvs_close(my_handle);
        return false;
    }

    required_size = sizeof(config->password);
    if (nvs_get_str(my_handle, "pass", config->password, &required_size) != ESP_OK) {
        config->password[0] = 0; 
    }
    
    nvs_close(my_handle);
    return true;
}

esp_err_t app_nvs_save_mqtt(const mqtt_config_t *config) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("mqtt_conf", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return err;

    nvs_set_str(my_handle, "host", config->host);
    nvs_set_i32(my_handle, "port", config->port);
    nvs_set_str(my_handle, "user", config->user);
    nvs_set_str(my_handle, "pass", config->pass);
    nvs_set_str(my_handle, "clid", config->client_id);
    nvs_set_u8(my_handle, "en", config->enabled ? 1 : 0);

    nvs_commit(my_handle);
    nvs_close(my_handle);
    return ESP_OK;
}

void app_nvs_get_mqtt(mqtt_config_t *config) {
    nvs_handle_t my_handle;
    // Defaults
    config->host[0] = 0;
    config->port = 1883;
    config->user[0] = 0;
    config->pass[0] = 0;
    config->client_id[0] = 0;
    config->enabled = false;

    if (nvs_open("mqtt_conf", NVS_READONLY, &my_handle) != ESP_OK) return;

    size_t size = sizeof(config->host); nvs_get_str(my_handle, "host", config->host, &size);
    int32_t p = 1883; nvs_get_i32(my_handle, "port", &p); config->port = p;
    size = sizeof(config->user); nvs_get_str(my_handle, "user", config->user, &size);
    size = sizeof(config->pass); nvs_get_str(my_handle, "pass", config->pass, &size);
    size = sizeof(config->client_id); nvs_get_str(my_handle, "clid", config->client_id, &size);
    
    uint8_t en = 0;
    if (nvs_get_u8(my_handle, "en", &en) == ESP_OK) config->enabled = (en == 1);

    nvs_close(my_handle);
}

esp_err_t app_nvs_save_button(int btn_id, button_config_t *config) {
    nvs_handle_t my_handle;
    char namespace[16];
    sprintf(namespace, "btn_%d", btn_id);
    
    esp_err_t err = nvs_open(namespace, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return err;

    nvs_set_i32(my_handle, "atype", config->action_type);
    nvs_set_str(my_handle, "url", config->target); // Key legacy "url", usamos para target
    nvs_set_i32(my_handle, "method", config->method);
    nvs_set_str(my_handle, "payload", config->payload);
    nvs_set_i32(my_handle, "timeout", config->timeout_ms);
    nvs_set_i32(my_handle, "cooldown", config->cooldown_ms);
    nvs_set_u8(my_handle, "nocache", config->no_cache ? 1 : 0);
    nvs_set_str(my_handle, "name", config->name);
    nvs_set_u8(my_handle, "is_reset", config->is_reset ? 1 : 0);
    nvs_commit(my_handle);
    nvs_close(my_handle);
    return ESP_OK;
}

esp_err_t app_nvs_get_button_config(int btn_id, button_config_t *config) {
    nvs_handle_t my_handle;
    char namespace[16];
    sprintf(namespace, "btn_%d", btn_id);

    if (nvs_open(namespace, NVS_READONLY, &my_handle) != ESP_OK) return ESP_FAIL;

    int32_t atype = 0;
    nvs_get_i32(my_handle, "atype", &atype);
    config->action_type = atype;

    size_t size = sizeof(config->target);
    nvs_get_str(my_handle, "url", config->target, &size);
    
    int32_t method = 0;
    nvs_get_i32(my_handle, "method", &method);
    config->method = method;

    size = sizeof(config->payload);
    if(nvs_get_str(my_handle, "payload", config->payload, &size) != ESP_OK) {
        config->payload[0] = 0;
    }

    int32_t val = 5000;
    if (nvs_get_i32(my_handle, "timeout", &val) == ESP_OK) {
        config->timeout_ms = val;
    } else {
        config->timeout_ms = 5000;
    }

    val = 2000;
    if (nvs_get_i32(my_handle, "cooldown", &val) == ESP_OK) {
        config->cooldown_ms = val;
    } else {
        config->cooldown_ms = 2000;
    }

    uint8_t nc = 0;
    if (nvs_get_u8(my_handle, "nocache", &nc) == ESP_OK) {
        config->no_cache = (nc == 1);
    } else {
        config->no_cache = false;
    }

    size = sizeof(config->name);
    if (nvs_get_str(my_handle, "name", config->name, &size) != ESP_OK) {
        config->name[0] = 0;
    }

    uint8_t ir = 0;
    if (nvs_get_u8(my_handle, "is_reset", &ir) == ESP_OK) {
        config->is_reset = (ir == 1);
    } else {
        config->is_reset = false;
    }

    nvs_close(my_handle);
    return ESP_OK;
}

esp_err_t app_nvs_save_admin(const admin_config_t *config) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("admin_conf", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return err;

    nvs_set_str(my_handle, "user", config->user);
    nvs_set_str(my_handle, "pass", config->pass);
    nvs_set_i32(my_handle, "reset_ms", config->reset_time_ms);
    nvs_set_str(my_handle, "ap_ssid", config->ap_ssid);
    nvs_set_str(my_handle, "ap_pass", config->ap_pass);
    nvs_set_u8(my_handle, "pure_cli", config->pure_client ? 1 : 0);
    nvs_set_u8(my_handle, "deep_slp", config->deep_sleep ? 1 : 0);
    nvs_set_i32(my_handle, "sta_retr", config->sta_max_retries);
    nvs_set_i32(my_handle, "ap_chan", config->ap_channel);
    nvs_set_i32(my_handle, "wk_tout", config->wakeup_timeout_s);
    nvs_set_i32(my_handle, "cfg_awake", config->config_awake_s);
    nvs_set_i32(my_handle, "debounce", config->debounce_ms);
    
    nvs_commit(my_handle);
    nvs_close(my_handle);
    return ESP_OK;
}

void app_nvs_get_admin(admin_config_t *config) {
    nvs_handle_t my_handle;
    strlcpy(config->user, "admin", sizeof(config->user));
    strlcpy(config->pass, "admin", sizeof(config->pass));
    config->reset_time_ms = 8000;
    config->ap_ssid[0] = '\0';
    strlcpy(config->ap_pass, "smartbutton", sizeof(config->ap_pass));
    config->pure_client = false;
    config->deep_sleep = false;
    config->sta_max_retries = 5;
    config->ap_channel = 1;
    config->wakeup_timeout_s = 30;
    config->config_awake_s = 180;
    config->debounce_ms = 200;

    if (nvs_open("admin_conf", NVS_READONLY, &my_handle) != ESP_OK) return;

    size_t size = sizeof(config->user); nvs_get_str(my_handle, "user", config->user, &size);
    size = sizeof(config->pass); nvs_get_str(my_handle, "pass", config->pass, &size);
    
    int32_t temp_reset_ms = 8000;
    if (nvs_get_i32(my_handle, "reset_ms", &temp_reset_ms) == ESP_OK) {
        config->reset_time_ms = (int)temp_reset_ms;
    }
    
    size = sizeof(config->ap_ssid); nvs_get_str(my_handle, "ap_ssid", config->ap_ssid, &size);
    size = sizeof(config->ap_pass); nvs_get_str(my_handle, "ap_pass", config->ap_pass, &size);
    
    uint8_t pc = 0;
    if (nvs_get_u8(my_handle, "pure_cli", &pc) == ESP_OK) {
        config->pure_client = (pc == 1);
    }

    uint8_t ds = 0;
    if (nvs_get_u8(my_handle, "deep_slp", &ds) == ESP_OK) {
        config->deep_sleep = (ds == 1);
    }

    int32_t tmp;
    if (nvs_get_i32(my_handle, "sta_retr", &tmp) == ESP_OK) config->sta_max_retries = (int)tmp;
    if (nvs_get_i32(my_handle, "ap_chan", &tmp) == ESP_OK) config->ap_channel = (int)tmp;
    if (nvs_get_i32(my_handle, "wk_tout", &tmp) == ESP_OK) config->wakeup_timeout_s = (int)tmp;
    if (nvs_get_i32(my_handle, "cfg_awake", &tmp) == ESP_OK) config->config_awake_s = (int)tmp;
    if (nvs_get_i32(my_handle, "debounce", &tmp) == ESP_OK) config->debounce_ms = (int)tmp;

    nvs_close(my_handle);
}
