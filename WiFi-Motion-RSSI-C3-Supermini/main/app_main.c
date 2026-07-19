#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "motion_detector.h"
#include "sdkconfig.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAILED_BIT BIT1
#define WIFI_MAXIMUM_RETRY 10

static const char *TAG = "wifi_motion";
static EventGroupHandle_t wifi_event_group;
static int wifi_retry_count;

static void set_motion_led(bool enabled)
{
#if CONFIG_MOTION_LED_GPIO >= 0
    const int level = CONFIG_MOTION_LED_ACTIVE_LOW ? !enabled : enabled;
    gpio_set_level((gpio_num_t)CONFIG_MOTION_LED_GPIO, level);
#else
    (void)enabled;
#endif
}

static void init_motion_led(void)
{
#if CONFIG_MOTION_LED_GPIO >= 0
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << CONFIG_MOTION_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&config));
    set_motion_led(false);
#endif
}

static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (wifi_retry_count < WIFI_MAXIMUM_RETRY) {
            wifi_retry_count++;
            esp_wifi_connect();
            ESP_LOGW(TAG, "WiFi reconnect attempt %d/%d",
                     wifi_retry_count, WIFI_MAXIMUM_RETRY);
        } else {
            xEventGroupSetBits(wifi_event_group, WIFI_FAILED_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        wifi_retry_count = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t wifi_connect(void)
{
    if (strlen(CONFIG_MOTION_WIFI_SSID) == 0U) {
        ESP_LOGE(TAG, "WiFi SSID is empty; run idf.py menuconfig");
        return ESP_ERR_INVALID_STATE;
    }

    wifi_event_group = xEventGroupCreate();
    if (wifi_event_group == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    const wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_config));

    esp_event_handler_instance_t wifi_handler;
    esp_event_handler_instance_t ip_handler;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL,
        &wifi_handler));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL,
        &ip_handler));

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid,
            CONFIG_MOTION_WIFI_SSID,
            sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password,
            CONFIG_MOTION_WIFI_PASSWORD,
            sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode =
        strlen(CONFIG_MOTION_WIFI_PASSWORD) == 0U ?
            WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    const EventBits_t bits = xEventGroupWaitBits(
        wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAILED_BIT,
        pdFALSE,
        pdFALSE,
        portMAX_DELAY);

    if ((bits & WIFI_CONNECTED_BIT) != 0U) {
        ESP_LOGI(TAG, "Connected to %s", CONFIG_MOTION_WIFI_SSID);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Unable to connect to %s", CONFIG_MOTION_WIFI_SSID);
    return ESP_FAIL;
}

static void emit_sample(int64_t timestamp_ms,
                        int rssi,
                        const motion_detector_result_t *result)
{
#if CONFIG_MOTION_OUTPUT_JSON
    printf("{\"t_ms\":%" PRId64
           ",\"rssi\":%d,\"score\":%.4f,\"threshold\":%.4f"
           ",\"state\":\"%s\",\"calibrated\":%s}\n",
           timestamp_ms,
           rssi,
           result->score,
           result->threshold,
           motion_detector_state_name(result->state),
           result->calibrated ? "true" : "false");
#else
    printf("%" PRId64 ",%d,%.4f,%.4f,%s,%d\n",
           timestamp_ms,
           rssi,
           result->score,
           result->threshold,
           motion_detector_state_name(result->state),
           result->calibrated ? 1 : 0);
#endif
    fflush(stdout);
}

void app_main(void)
{
    ESP_ERROR_CHECK(app_config_init());
    init_motion_led();

    app_runtime_config_t runtime_config;
    esp_err_t config_error = app_config_load(&runtime_config);
    if (config_error != ESP_OK) {
        ESP_LOGW(TAG, "Invalid persisted config (%s); using defaults",
                 esp_err_to_name(config_error));
        app_config_defaults(&runtime_config);
    }

    motion_detector_t detector;
    if (!motion_detector_init(&detector, &runtime_config.detector)) {
        ESP_LOGE(TAG, "Detector configuration is invalid");
        return;
    }

    ESP_ERROR_CHECK(wifi_connect());

#if CONFIG_MOTION_OUTPUT_CSV
    printf("t_ms,rssi_dbm,score,threshold,state,calibrated\n");
#endif

    TickType_t next_wakeup = xTaskGetTickCount();
    for (;;) {
        wifi_ap_record_t ap_info;
        const esp_err_t error = esp_wifi_sta_get_ap_info(&ap_info);
        if (error == ESP_OK) {
            const motion_detector_result_t result =
                motion_detector_push(&detector, ap_info.rssi);
            set_motion_led(result.state == MOTION_DETECTOR_ACTIVE);
            emit_sample(esp_timer_get_time() / 1000,
                        ap_info.rssi,
                        &result);

            if (result.state_changed) {
                ESP_LOGI(TAG,
                         "State=%s score=%.3f threshold=%.3f baseline=%.3f±%.3f",
                         motion_detector_state_name(result.state),
                         result.score,
                         result.threshold,
                         result.baseline_mean,
                         result.baseline_stddev);
            }
        } else {
            ESP_LOGW(TAG, "RSSI read failed: %s", esp_err_to_name(error));
        }

        vTaskDelayUntil(&next_wakeup,
                        pdMS_TO_TICKS(runtime_config.sample_interval_ms));
    }
}
