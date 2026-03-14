#include "app_mqtt.h"
#include "mqtt_client.h"
#include "app_core.h"
#include "app_led.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

static const char *TAG = "MQTT";

// Event bits locales para manejar la secuencia de conexión/publicación
#define MQTT_CONNECTED_BIT (1 << 0)
#define MQTT_PUBLISHED_BIT (1 << 1)
#define MQTT_ERROR_BIT     (1 << 2)

static EventGroupHandle_t mqtt_event_group;

// Handler principal para la operación asíncrona (botón físico)
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT Connected");
        xEventGroupSetBits(mqtt_event_group, MQTT_CONNECTED_BIT);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT Disconnected");
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT Published, msg_id=%d", event->msg_id);
        xEventGroupSetBits(mqtt_event_group, MQTT_PUBLISHED_BIT);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT Error type: %d", event->error_handle->error_type);
        xEventGroupSetBits(mqtt_event_group, MQTT_ERROR_BIT);
        break;
    default:
        break;
    }
}

static void mqtt_task(void *pvParameters) {
    button_config_t *btn_cfg = (button_config_t *)pvParameters;
    
    mqtt_config_t mcfg;
    app_nvs_get_mqtt(&mcfg);

    if (!mcfg.enabled || strlen(mcfg.host) == 0) {
        ESP_LOGE(TAG, "MQTT not configured or disabled");
        app_led_signal_error();
        free(btn_cfg);
        app_set_state(STATE_NORMAL);
        xEventGroupSetBits(app_event_group, EVENT_HTTP_DONE);
        vTaskDelete(NULL);
        return;
    }

    mqtt_event_group = xEventGroupCreate();
    char uri[128];
    sprintf(uri, "mqtt://%s:%d", mcfg.host, mcfg.port);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = uri,
        .credentials.username = (strlen(mcfg.user) > 0) ? mcfg.user : NULL,
        .credentials.authentication.password = (strlen(mcfg.pass) > 0) ? mcfg.pass : NULL,
        .credentials.client_id = (strlen(mcfg.client_id) > 0) ? mcfg.client_id : NULL,
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);

    // Esperar conexión
    EventBits_t bits = xEventGroupWaitBits(mqtt_event_group, MQTT_CONNECTED_BIT | MQTT_ERROR_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(5000));
    
    if (bits & MQTT_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Publishing to %s payload: %s", btn_cfg->target, btn_cfg->payload);
        
        int msg_id = esp_mqtt_client_publish(client, btn_cfg->target, btn_cfg->payload, 0, 1, 0);
        
        if (msg_id >= 0) {
            // Esperar confirmación PUBACK (QoS 1)
            bits = xEventGroupWaitBits(mqtt_event_group, MQTT_PUBLISHED_BIT | MQTT_ERROR_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(3000));
            if (bits & MQTT_PUBLISHED_BIT) {
                app_led_signal_success();
            } else {
                app_led_signal_error();
            }
        } else {
            app_led_signal_error();
        }
    } else {
        ESP_LOGE(TAG, "Could not connect to MQTT broker");
        app_led_signal_error();
    }

    esp_mqtt_client_disconnect(client); // Cierre graceful
    esp_mqtt_client_stop(client);
    esp_mqtt_client_destroy(client);
    
    vEventGroupDelete(mqtt_event_group);
    free(btn_cfg);
    
    app_set_state(STATE_NORMAL);
    xEventGroupSetBits(app_event_group, EVENT_HTTP_DONE); // Usamos el mismo bit para señalizar fin de tarea
    vTaskDelete(NULL);
}

void app_mqtt_publish_oneshot(int btn_id, button_config_t *btn_cfg) {
    app_set_state(STATE_HTTP_REQ); // Estado visual de "ocupado"
    
    // Duplicamos cfg para pasarlo al task
    button_config_t *cfg_copy = malloc(sizeof(button_config_t));
    memcpy(cfg_copy, btn_cfg, sizeof(button_config_t));

    xTaskCreate(mqtt_task, "mqtt_pub", 4096, (void*)cfg_copy, 5, NULL);
}

// Handler estático para el test síncrono (evita crash en RISC-V por función anidada)
static void mqtt_sync_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    EventGroupHandle_t target_eg = (EventGroupHandle_t)handler_args;
    if (event_id == MQTT_EVENT_CONNECTED) xEventGroupSetBits(target_eg, MQTT_CONNECTED_BIT);
    if (event_id == MQTT_EVENT_PUBLISHED) xEventGroupSetBits(target_eg, MQTT_PUBLISHED_BIT);
    if (event_id == MQTT_EVENT_ERROR) xEventGroupSetBits(target_eg, MQTT_ERROR_BIT);
}

// Test síncrono para la web
int app_mqtt_test_sync(const mqtt_config_t *mcfg, const button_config_t *bcfg) {
    if (!mcfg->enabled || strlen(mcfg->host) == 0) return -2;

    EventGroupHandle_t local_eg = xEventGroupCreate();
    
    char uri[128];
    sprintf(uri, "mqtt://%s:%d", mcfg->host, mcfg->port);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = uri,
        .credentials.username = (strlen(mcfg->user) > 0) ? mcfg->user : NULL,
        .credentials.authentication.password = (strlen(mcfg->pass) > 0) ? mcfg->pass : NULL,
        .credentials.client_id = (strlen(mcfg->client_id) > 0) ? mcfg->client_id : NULL,
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    
    // Pasamos local_eg como argumento (contexto) en lugar de usar closure/función anidada
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_sync_handler, (void*)local_eg);
    
    esp_mqtt_client_start(client);

    int result = -1;
    EventBits_t bits = xEventGroupWaitBits(local_eg, MQTT_CONNECTED_BIT | MQTT_ERROR_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(5000));
    
    if (bits & MQTT_CONNECTED_BIT) {
        esp_mqtt_client_publish(client, bcfg->target, bcfg->payload, 0, 1, 0);
        bits = xEventGroupWaitBits(local_eg, MQTT_PUBLISHED_BIT | MQTT_ERROR_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(3000));
        if (bits & MQTT_PUBLISHED_BIT) result = 200; // Simula HTTP OK
        else result = 504; // Timeout
    }

    esp_mqtt_client_stop(client);
    esp_mqtt_client_destroy(client);
    vEventGroupDelete(local_eg);
    return result;
}
