#include "app_ws.h"
#include "esp_websocket_client.h"
#include "app_core.h"
#include "app_led.h"
#include "app_btn_leds.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "WS";

#define WS_CONNECTED_BIT (1 << 0)
#define WS_SENT_BIT      (1 << 1)
#define WS_ERROR_BIT     (1 << 2)

static EventGroupHandle_t ws_event_group;

static void ws_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WS Connected");
        xEventGroupSetBits(ws_event_group, WS_CONNECTED_BIT);
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "WS Disconnected");
        break;
    case WEBSOCKET_EVENT_DATA:
        if (data->op_code == 0x01 || data->op_code == 0x02) {
            ESP_LOGI(TAG, "WS Received %d bytes", data->data_len);
        }
        break;
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "WS Error");
        xEventGroupSetBits(ws_event_group, WS_ERROR_BIT);
        break;
    default:
        break;
    }
}

typedef struct {
    button_config_t cfg;
    int btn_id;
} ws_task_params_t;

static int ws_connect_send(const button_config_t *cfg) {
    ws_event_group = xEventGroupCreate();

    esp_websocket_client_config_t ws_cfg = {
        .uri = cfg->target,
        .reconnect_timeout_ms = 0,
        .network_timeout_ms = cfg->timeout_ms > 0 ? cfg->timeout_ms : 5000,
    };

    esp_websocket_client_handle_t client = esp_websocket_client_init(&ws_cfg);
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, ws_event_handler, NULL);
    esp_websocket_client_start(client);

    EventBits_t bits = xEventGroupWaitBits(ws_event_group,
        WS_CONNECTED_BIT | WS_ERROR_BIT, pdFALSE, pdFALSE,
        pdMS_TO_TICKS(cfg->timeout_ms > 0 ? cfg->timeout_ms : 5000));

    int result = -1;
    if (bits & WS_CONNECTED_BIT) {
        const char *msg = (strlen(cfg->payload) > 0) ? cfg->payload : "1";
        int len = strlen(msg);
        int sent = esp_websocket_client_send_text(client, msg, len, pdMS_TO_TICKS(3000));
        if (sent == len) {
            ESP_LOGI(TAG, "WS Sent %d bytes to %s", len, cfg->target);
            result = 200;
        } else {
            ESP_LOGE(TAG, "WS Send failed");
            result = 504;
        }
    } else {
        ESP_LOGE(TAG, "WS Connection failed to %s", cfg->target);
    }

    esp_websocket_client_close(client, pdMS_TO_TICKS(1000));
    esp_websocket_client_stop(client);
    esp_websocket_client_destroy(client);
    vEventGroupDelete(ws_event_group);

    return result;
}

static void ws_task(void *pvParameters) {
    ws_task_params_t *params = (ws_task_params_t *)pvParameters;
    int btn_id = params->btn_id;

    int result = ws_connect_send(&params->cfg);

    if (result == 200) {
        app_led_signal_success();
    } else {
        app_led_signal_error();
    }

    app_btn_leds_off(btn_id);
    free(params);
    app_set_state(STATE_NORMAL);
    xEventGroupSetBits(app_event_group, EVENT_HTTP_DONE);
    vTaskDelete(NULL);
}

void app_ws_send_oneshot(int btn_id, button_config_t *cfg) {
    app_set_state(STATE_HTTP_REQ);

    ws_task_params_t *params = malloc(sizeof(ws_task_params_t));
    memcpy(&params->cfg, cfg, sizeof(button_config_t));
    params->btn_id = btn_id;

    xTaskCreate(ws_task, "ws_send", 6144, (void *)params, 5, NULL);
}

int app_ws_test_sync(const button_config_t *cfg) {
    if (strlen(cfg->target) < 5) return -1;
    return ws_connect_send(cfg);
}
