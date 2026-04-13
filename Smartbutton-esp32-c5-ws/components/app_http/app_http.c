#include "app_http.h"
#include "app_nvs.h"
#include "app_core.h"
#include "app_led.h"
#include "app_btn_leds.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_crt_bundle.h"
#include "esp_random.h"

static const char *TAG = "HTTP";

void http_execute_task(void *pvParameters) {
    int btn_id = (int)pvParameters;
    button_config_t config;
    
    if(app_nvs_get_button_config(btn_id, &config) != ESP_OK) {
        ESP_LOGE(TAG, "No config for btn %d", btn_id);
        app_led_signal_error();
        app_set_state(STATE_NORMAL);
        xEventGroupSetBits(app_event_group, EVENT_HTTP_DONE);
        vTaskDelete(NULL);
        return;
    }

    // Nota: config.target reemplaza a config.url
    if (strlen(config.target) < 5) {
        ESP_LOGE(TAG, "Invalid URL");
        app_led_signal_error();
        app_set_state(STATE_NORMAL);
        xEventGroupSetBits(app_event_group, EVENT_HTTP_DONE);
        vTaskDelete(NULL);
        return;
    }

    char final_url[560];
    strlcpy(final_url, config.target, sizeof(final_url));
    if (config.no_cache) {
        char sep = (strchr(final_url, '?') != NULL) ? '&' : '?';
        snprintf(final_url + strlen(final_url), sizeof(final_url) - strlen(final_url), "%c_cb=%u", sep, (unsigned)esp_random());
    }

    esp_http_client_config_t http_config = {
        .url = final_url, 
        .timeout_ms = config.timeout_ms > 0 ? config.timeout_ms : 5000,
        .method = (config.method == 1) ? HTTP_METHOD_POST : HTTP_METHOD_GET,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_config);

    if (config.method == 1 && strlen(config.payload) > 0) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, config.payload, strlen(config.payload));
    }
    
    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "Status = %d", status);
        if (status >= 200 && status < 300) {
            app_led_signal_success();
        } else {
            app_led_signal_error();
        }
    } else {
        ESP_LOGE(TAG, "HTTP Request failed: %s", esp_err_to_name(err));
        app_led_signal_error();
    }

    esp_http_client_cleanup(client);
    app_btn_leds_off(btn_id);
    app_set_state(STATE_NORMAL);
    xEventGroupSetBits(app_event_group, EVENT_HTTP_DONE);
    vTaskDelete(NULL);
}

void app_http_trigger(int btn_id) {
    app_set_state(STATE_HTTP_REQ);
    xTaskCreate(http_execute_task, "http_req", 12288, (void*)btn_id, 5, NULL);
}

int app_http_test_sync(button_config_t *cfg) {
    if (strlen(cfg->target) < 5) return -1;

    char final_url[560];
    strlcpy(final_url, cfg->target, sizeof(final_url));
    if (cfg->no_cache) {
        char sep = (strchr(final_url, '?') != NULL) ? '&' : '?';
        snprintf(final_url + strlen(final_url), sizeof(final_url) - strlen(final_url), "%c_cb=%u", sep, (unsigned)esp_random());
    }

    esp_http_client_config_t http_config = {
        .url = final_url,
        .timeout_ms = cfg->timeout_ms > 0 ? cfg->timeout_ms : 5000,
        .method = (cfg->method == 1) ? HTTP_METHOD_POST : HTTP_METHOD_GET,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_config);

    if (cfg->method == 1 && strlen(cfg->payload) > 0) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, cfg->payload, strlen(cfg->payload));
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = -1;
    if (err == ESP_OK) {
        status = esp_http_client_get_status_code(client);
    } else {
        ESP_LOGE(TAG, "Test failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return status;
}
