#include <inttypes.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "captive_dns.h"
#include "config_portal.h"
#include "csi_capture.h"
#include "csi_traffic.h"
#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "event_marker.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "motion_detector.h"
#include "recovery_button.h"
#include "sample_metrics.h"
#include "sdkconfig.h"
#include "telegram_notifier.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAILED_BIT BIT1
#define WIFI_MAXIMUM_RETRY 10
#define MOTION_SAMPLING_TASK_PRIORITY 6U
#define RECOVERY_BUTTON_HOLD_MS 5000U
#define RECOVERY_BOOT_MAGIC 0x52454356UL

static const char *TAG = "wifi_motion";
static EventGroupHandle_t wifi_event_group;
static esp_netif_t *wifi_sta_netif;
static esp_netif_t *wifi_ap_netif;
static int wifi_retry_count;
static bool wifi_recovery_requested;
static atomic_bool wifi_has_connected;
static atomic_uint_fast32_t wifi_disconnect_count;
static atomic_uint_fast32_t wifi_reconnect_count;
static event_marker_t physical_event_marker;
static atomic_bool physical_marker_active;
static atomic_uint_fast32_t physical_marker_event_id;
static atomic_int physical_marker_transition;
static recovery_button_t physical_recovery_button;
RTC_NOINIT_ATTR static uint32_t recovery_boot_magic;
RTC_NOINIT_ATTR static uint32_t recovery_boot_magic_inverse;

static void init_telemetry_output(void)
{
    usb_serial_jtag_driver_config_t config = {
        .tx_buffer_size = 4096,
        .rx_buffer_size = 256,
    };
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&config));
    usb_serial_jtag_vfs_use_driver();
}

static void telemetry_write(const char *data, size_t length)
{
    if (data == NULL || length == 0U ||
        !usb_serial_jtag_is_driver_installed()) {
        return;
    }
    (void)usb_serial_jtag_write_bytes(data, length, 0);
}

static void set_motion_led(bool enabled)
{
#if CONFIG_MOTION_LED_GPIO >= 0
#if CONFIG_MOTION_LED_ACTIVE_LOW
    const int level = !enabled;
#else
    const int level = enabled;
#endif
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

static bool event_marker_pressed(void)
{
#if CONFIG_MOTION_MARKER_GPIO >= 0
    return gpio_get_level((gpio_num_t)CONFIG_MOTION_MARKER_GPIO) == 0;
#else
    return false;
#endif
}

static void event_marker_task(void *argument)
{
    (void)argument;
    for (;;) {
        const int64_t timestamp_ms = esp_timer_get_time() / 1000;
        const recovery_button_action_t action = recovery_button_update(
            &physical_recovery_button,
            event_marker_pressed(),
            timestamp_ms);
        if (action == RECOVERY_BUTTON_SHORT_PRESS) {
            const event_marker_result_t result = event_marker_update(
                &physical_event_marker, true, timestamp_ms);
            (void)event_marker_update(&physical_event_marker,
                                      false,
                                      timestamp_ms);
            atomic_store(&physical_marker_active, result.active);
            atomic_store(&physical_marker_event_id, result.event_id);
            if (result.transition != EVENT_MARKER_NO_TRANSITION) {
                atomic_store(&physical_marker_transition, result.transition);
            }
        } else if (action == RECOVERY_BUTTON_ARMED) {
            ESP_LOGW(TAG,
                     "Recovery mode armed; release BOOT to restart");
        } else if (action == RECOVERY_BUTTON_REQUESTED) {
            ESP_LOGW(TAG, "Restarting once in WiFi recovery mode");
            recovery_boot_magic = RECOVERY_BOOT_MAGIC;
            recovery_boot_magic_inverse = ~RECOVERY_BOOT_MAGIC;
            vTaskDelay(pdMS_TO_TICKS(100));
            esp_restart();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void init_event_marker(void)
{
#if CONFIG_MOTION_MARKER_GPIO >= 0
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << CONFIG_MOTION_MARKER_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&config));
    const bool initially_pressed =
        gpio_get_level((gpio_num_t)CONFIG_MOTION_MARKER_GPIO) == 0;
    event_marker_init(&physical_event_marker,
                      false,
                      CONFIG_MOTION_MARKER_DEBOUNCE_MS);
    recovery_button_init(&physical_recovery_button,
                         initially_pressed,
                         RECOVERY_BUTTON_HOLD_MS);
#else
    event_marker_init(&physical_event_marker,
                      false,
                      CONFIG_MOTION_MARKER_DEBOUNCE_MS);
    recovery_button_init(&physical_recovery_button,
                         false,
                         RECOVERY_BUTTON_HOLD_MS);
#endif
    ESP_ERROR_CHECK(
        xTaskCreate(event_marker_task,
                    "event_marker",
                    2048,
                    NULL,
                    5,
                    NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}

static event_marker_result_t event_marker_snapshot(void)
{
    const event_marker_result_t result = {
        .active = atomic_load(&physical_marker_active),
        .event_id = atomic_load(&physical_marker_event_id),
        .transition = atomic_exchange(&physical_marker_transition,
                                      EVENT_MARKER_NO_TRANSITION),
    };
    return result;
}

static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START &&
        !wifi_recovery_requested) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_STA_DISCONNECTED) {
        atomic_fetch_add(&wifi_disconnect_count, 1U);
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);

        const bool reconnect_forever = atomic_load(&wifi_has_connected);
        if (reconnect_forever || wifi_retry_count < WIFI_MAXIMUM_RETRY) {
            wifi_retry_count++;
            atomic_fetch_add(&wifi_reconnect_count, 1U);
            esp_wifi_connect();
            if (reconnect_forever) {
                ESP_LOGW(TAG, "WiFi reconnect attempt %d", wifi_retry_count);
            } else {
                ESP_LOGW(TAG, "WiFi reconnect attempt %d/%d",
                         wifi_retry_count, WIFI_MAXIMUM_RETRY);
            }
        } else {
            xEventGroupSetBits(wifi_event_group, WIFI_FAILED_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        wifi_retry_count = 0;
        atomic_store(&wifi_has_connected, true);
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t start_recovery_ap(void)
{
    uint8_t mac[6];
    ESP_RETURN_ON_ERROR(esp_wifi_get_mac(WIFI_IF_STA, mac), TAG,
                        "Unable to read WiFi MAC");

    wifi_config_t ap_config = {0};
    snprintf((char *)ap_config.ap.ssid,
             sizeof(ap_config.ap.ssid),
             "WiFi-Motion-%02X%02X%02X",
             mac[3], mac[4], mac[5]);
    ap_config.ap.ssid_len = strlen((char *)ap_config.ap.ssid);
    ap_config.ap.channel = 1;
    ap_config.ap.max_connection = 4;
    const size_t password_length = strlen(CONFIG_MOTION_PORTAL_AP_PASSWORD);
    if (password_length >= 8U && password_length <= 63U) {
        strlcpy((char *)ap_config.ap.password,
                CONFIG_MOTION_PORTAL_AP_PASSWORD,
                sizeof(ap_config.ap.password));
        ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
        ESP_LOGW(TAG, "Recovery AP has no password");
    }

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG,
                        "Unable to start recovery AP");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap_config), TAG,
                        "Unable to configure recovery AP");
    const esp_err_t dns_error = captive_dns_start(wifi_ap_netif);
    if (dns_error != ESP_OK) {
        ESP_LOGW(TAG,
                 "Recovery AP started without captive DNS: %s",
                 esp_err_to_name(dns_error));
    }
    ESP_LOGW(TAG,
             "Recovery portal: SSID=%s URL=http://192.168.4.1",
             ap_config.ap.ssid);
    return ESP_OK;
}

static esp_err_t wifi_connect(const app_runtime_config_t *runtime_config,
                              bool *provisioning_mode,
                              bool force_recovery)
{
    if (runtime_config == NULL || provisioning_mode == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *provisioning_mode = false;
    wifi_recovery_requested = force_recovery;
    wifi_event_group = xEventGroupCreate();
    if (wifi_event_group == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_sta_netif = esp_netif_create_default_wifi_sta();
    if (wifi_sta_netif == NULL) {
        return ESP_ERR_NO_MEM;
    }
    wifi_ap_netif = esp_netif_create_default_wifi_ap();
    if (wifi_ap_netif == NULL) {
        return ESP_ERR_NO_MEM;
    }

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

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    wifi_config_t wifi_config = {0};
    if (runtime_config->wifi_ssid[0] != '\0') {
        strlcpy((char *)wifi_config.sta.ssid,
                runtime_config->wifi_ssid,
                sizeof(wifi_config.sta.ssid));
        strlcpy((char *)wifi_config.sta.password,
                runtime_config->wifi_password,
                sizeof(wifi_config.sta.password));
        wifi_config.sta.threshold.authmode =
            runtime_config->wifi_password[0] == '\0' ?
                WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
        wifi_config.sta.pmf_cfg.capable = true;
        wifi_config.sta.pmf_cfg.required = false;
    } else {
        ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_STA, &wifi_config));
        if (wifi_config.sta.ssid[0] == '\0') {
            ESP_LOGW(TAG, "No WiFi credentials; starting recovery portal");
            memset(&wifi_config, 0, sizeof(wifi_config));
        }
        if (wifi_config.sta.ssid[0] != '\0') {
            ESP_LOGI(TAG, "Using saved WiFi STA configuration");
        }
    }

    if (wifi_config.sta.ssid[0] != '\0') {
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    }
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    if (force_recovery) {
        ESP_LOGW(TAG, "BOOT requested WiFi recovery portal");
        *provisioning_mode = true;
        return start_recovery_ap();
    }

    if (wifi_config.sta.ssid[0] == '\0') {
        *provisioning_mode = true;
        return start_recovery_ap();
    }

    const EventBits_t bits = xEventGroupWaitBits(
        wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAILED_BIT,
        pdFALSE,
        pdFALSE,
        portMAX_DELAY);

    if ((bits & WIFI_CONNECTED_BIT) != 0U) {
        ESP_LOGI(TAG, "Connected to configured WiFi AP");
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Unable to connect; enabling recovery portal");
    *provisioning_mode = true;
    return start_recovery_ap();
}

static void format_bssid(const uint8_t bssid[6], char output[18])
{
    snprintf(output, 18, "%02x:%02x:%02x:%02x:%02x:%02x",
             (unsigned)bssid[0], (unsigned)bssid[1], (unsigned)bssid[2],
             (unsigned)bssid[3], (unsigned)bssid[4], (unsigned)bssid[5]);
}

static void emit_sample(int64_t timestamp_ms,
                        bool sample_ok,
                        int rssi,
                        const char *read_error,
                        const wifi_ap_record_t *ap_info,
                        const motion_detector_result_t *result,
                        const sample_observation_t *observation,
                        const sample_metrics_t *metrics,
                        const event_marker_result_t *marker,
                        const csi_capture_snapshot_t *csi,
                        const motion_detector_result_t *csi_result,
                        bool csi_detector_sampled,
                        const csi_traffic_snapshot_t *traffic,
                        const app_runtime_config_t *runtime_config)
{
    static char line[1280];
    char bssid[18] = "";
    if (ap_info != NULL) {
        format_bssid(ap_info->bssid, bssid);
    }
    const int64_t csi_age_ms = csi->valid
        ? (esp_timer_get_time() - csi->received_at_us) / 1000
        : -1;

#if CONFIG_MOTION_OUTPUT_JSON
    char rssi_value[16];
    if (sample_ok) {
        snprintf(rssi_value, sizeof(rssi_value), "%d", rssi);
    } else {
        strlcpy(rssi_value, "null", sizeof(rssi_value));
    }
    const int length = snprintf(
           line,
           sizeof(line),
           "{\"uptime_ms\":%" PRId64 ",\"sample_ok\":%s,\"rssi_dbm\":%s"
           ",\"read_error\":\"%s\",\"rssi_changed\":%s"
           ",\"detection_source\":\"%s\""
           ",\"algorithm\":\"%s\",\"baseline_mode\":\"%s\""
           ",\"profile\":\"%s\""
           ",\"query_interval_ms\":%" PRId64
           ",\"rssi_change_interval_ms\":%" PRId64
           ",\"rssi_unchanged_ms\":%" PRId64
           ",\"baseline\":%.4f,\"baseline_stddev\":%.4f"
           ",\"score\":%.4f,\"threshold\":%.4f,\"release_threshold\":%.4f"
           ",\"state\":\"%s\",\"transition\":%s,\"calibrated\":%s"
           ",\"marker_event_id\":%" PRIu32
           ",\"marker_active\":%s,\"marker_transition\":%d"
           ",\"csi_valid\":%s,\"csi_age_ms\":%" PRId64
           ",\"csi_rssi_dbm\":%d,\"csi_length\":%u"
           ",\"csi_subcarriers\":%u"
           ",\"csi_mean_amplitude\":%.4f"
           ",\"csi_amplitude_variance\":%.4f"
           ",\"csi_amplitude_range\":%.4f"
           ",\"csi_mean_energy\":%.4f"
           ",\"csi_temporal_delta\":%.4f"
           ",\"csi_temporal_delta_mean\":%.4f"
           ",\"csi_temporal_delta_peak\":%.4f"
           ",\"csi_interval_frames\":%" PRIu32
           ",\"csi_normalized_delta\":%.4f"
           ",\"csi_normalized_delta_mean\":%.4f"
           ",\"csi_normalized_delta_peak\":%.4f"
           ",\"csi_complex_distance\":%.4f"
           ",\"csi_complex_distance_mean\":%.4f"
           ",\"csi_complex_distance_peak\":%.4f"
           ",\"csi_frames_received\":%" PRIu64
           ",\"csi_frames_processed\":%" PRIu64
           ",\"csi_frames_dropped\":%" PRIu64
           ",\"csi_detector_sampled\":%s"
           ",\"csi_detector_calibrated\":%s"
           ",\"csi_detector_score\":%.4f"
           ",\"csi_detector_threshold\":%.4f"
           ",\"csi_detector_state\":\"%s\""
           ",\"csi_detector_transition\":%s"
           ",\"csi_traffic_running\":%s"
           ",\"csi_traffic_interval_ms\":%" PRIu32
           ",\"csi_traffic_payload_bytes\":%" PRIu32
           ",\"csi_traffic_requests\":%" PRIu64
           ",\"csi_traffic_replies\":%" PRIu64
           ",\"csi_traffic_timeouts\":%" PRIu64
           ",\"queries\":%" PRIu64 ",\"samples_ok\":%" PRIu64
           ",\"read_errors\":%" PRIu64 ",\"schedule_misses\":%" PRIu64
           ",\"repeated_samples\":%" PRIu64 ",\"rssi_changes\":%" PRIu64
           ",\"disconnects\":%" PRIuFAST32 ",\"reconnects\":%" PRIuFAST32
           ",\"channel\":%u,\"bssid\":\"%s\"}\n",
           timestamp_ms,
           sample_ok ? "true" : "false",
           rssi_value,
           read_error,
           observation->rssi_changed ? "true" : "false",
           app_detection_source_name(runtime_config->detection_source),
           motion_feature_algorithm_name(runtime_config->detector.algorithm),
           motion_baseline_mode_name(runtime_config->detector.baseline_mode),
           motion_sensitivity_profile_name(runtime_config->sensitivity_profile),
           observation->query_interval_ms,
           observation->rssi_change_interval_ms,
           observation->rssi_unchanged_ms,
           result->baseline_mean,
           result->baseline_stddev,
           result->score,
           result->threshold,
           result->release_threshold,
           motion_detector_state_name(result->state),
           result->state_changed ? "true" : "false",
           result->calibrated ? "true" : "false",
           marker->event_id,
           marker->active ? "true" : "false",
           (int)marker->transition,
           csi->valid ? "true" : "false",
           csi_age_ms,
           csi->rssi_dbm,
           (unsigned)csi->csi_length,
           (unsigned)csi->features.subcarrier_count,
           csi->features.mean_amplitude,
           csi->features.amplitude_variance,
           csi->features.amplitude_range,
           csi->features.mean_energy,
           csi->features.temporal_mean_absolute_delta,
           csi->temporal_aggregate.mean,
           csi->temporal_aggregate.peak,
           csi->temporal_aggregate.sample_count,
           csi->features.temporal_normalized_mean_absolute_delta,
           csi->normalized_temporal_aggregate.mean,
           csi->normalized_temporal_aggregate.peak,
           csi->features.temporal_complex_correlation_distance,
           csi->complex_correlation_aggregate.mean,
           csi->complex_correlation_aggregate.peak,
           csi->frames_received,
           csi->frames_processed,
           csi->frames_dropped,
           csi_detector_sampled ? "true" : "false",
           csi_result->calibrated ? "true" : "false",
           csi_result->score,
           csi_result->threshold,
           motion_detector_state_name(csi_result->state),
           csi_result->state_changed ? "true" : "false",
           traffic->running ? "true" : "false",
           traffic->interval_ms,
           traffic->payload_bytes,
           traffic->requests,
           traffic->replies,
           traffic->timeouts,
           metrics->queries,
           metrics->successful_samples,
           metrics->read_errors,
           metrics->schedule_misses,
           metrics->repeated_samples,
           metrics->rssi_changes,
           atomic_load(&wifi_disconnect_count),
           atomic_load(&wifi_reconnect_count),
           ap_info != NULL ? ap_info->primary : 0U,
           bssid);
#else
    const int length = snprintf(
           line,
           sizeof(line),
           "%" PRId64 ",%d,%d,%s,%d,%s,%s,%s,%s,%" PRId64 ",%" PRId64
           ",%" PRId64 ",%.4f,%.4f,%.4f,%.4f,%.4f,%s,%d,%d"
           ",%" PRIu32 ",%d,%d"
           ",%d,%" PRId64 ",%d,%u,%u,%.4f,%.4f,%.4f,%.4f,%.4f"
           ",%.4f,%.4f,%" PRIu32
           ",%.4f,%.4f,%.4f"
           ",%.4f,%.4f,%.4f"
           ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
           ",%d,%d,%.4f,%.4f,%s,%d"
           ",%d,%" PRIu32 ",%" PRIu32 ",%" PRIu64 ",%" PRIu64
           ",%" PRIu64
           ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
           ",%" PRIu64 ",%" PRIu64 ",%" PRIuFAST32 ",%" PRIuFAST32
           ",%u,%s\n",
           timestamp_ms,
           sample_ok ? 1 : 0,
           rssi,
           read_error,
           observation->rssi_changed ? 1 : 0,
           app_detection_source_name(runtime_config->detection_source),
           motion_feature_algorithm_name(runtime_config->detector.algorithm),
           motion_baseline_mode_name(runtime_config->detector.baseline_mode),
           motion_sensitivity_profile_name(runtime_config->sensitivity_profile),
           observation->query_interval_ms,
           observation->rssi_change_interval_ms,
           observation->rssi_unchanged_ms,
           result->baseline_mean,
           result->baseline_stddev,
           result->score,
           result->threshold,
           result->release_threshold,
           motion_detector_state_name(result->state),
           result->state_changed ? 1 : 0,
           result->calibrated ? 1 : 0,
           marker->event_id,
           marker->active ? 1 : 0,
           (int)marker->transition,
           csi->valid ? 1 : 0,
           csi_age_ms,
           csi->rssi_dbm,
           (unsigned)csi->csi_length,
           (unsigned)csi->features.subcarrier_count,
           csi->features.mean_amplitude,
           csi->features.amplitude_variance,
           csi->features.amplitude_range,
           csi->features.mean_energy,
           csi->features.temporal_mean_absolute_delta,
           csi->temporal_aggregate.mean,
           csi->temporal_aggregate.peak,
           csi->temporal_aggregate.sample_count,
           csi->features.temporal_normalized_mean_absolute_delta,
           csi->normalized_temporal_aggregate.mean,
           csi->normalized_temporal_aggregate.peak,
           csi->features.temporal_complex_correlation_distance,
           csi->complex_correlation_aggregate.mean,
           csi->complex_correlation_aggregate.peak,
           csi->frames_received,
           csi->frames_processed,
           csi->frames_dropped,
           csi_detector_sampled ? 1 : 0,
           csi_result->calibrated ? 1 : 0,
           csi_result->score,
           csi_result->threshold,
           motion_detector_state_name(csi_result->state),
           csi_result->state_changed ? 1 : 0,
           traffic->running ? 1 : 0,
           traffic->interval_ms,
           traffic->payload_bytes,
           traffic->requests,
           traffic->replies,
           traffic->timeouts,
           metrics->queries,
           metrics->successful_samples,
           metrics->read_errors,
           metrics->schedule_misses,
           metrics->repeated_samples,
           metrics->rssi_changes,
           atomic_load(&wifi_disconnect_count),
           atomic_load(&wifi_reconnect_count),
           ap_info != NULL ? ap_info->primary : 0U,
           bssid);
#endif
    if (length > 0 && (size_t)length < sizeof(line)) {
        telemetry_write(line, (size_t)length);
    }
}

static void update_portal_diagnostics(
    int64_t timestamp_ms,
    bool sample_ok,
    int rssi,
    const wifi_ap_record_t *ap_info,
    const motion_detector_result_t *result,
    const motion_detector_result_t *csi_result,
    const sample_metrics_t *metrics)
{
    config_portal_diagnostics_t diagnostics = {
        .uptime_ms = timestamp_ms,
        .rssi_dbm = rssi,
        .sample_ok = sample_ok,
        .channel = ap_info != NULL ? ap_info->primary : 0U,
        .detector = *result,
        .csi_detector = *csi_result,
        .queries = metrics->queries,
        .samples_ok = metrics->successful_samples,
        .read_errors = metrics->read_errors,
        .schedule_misses = metrics->schedule_misses,
        .disconnects = atomic_load(&wifi_disconnect_count),
        .reconnects = atomic_load(&wifi_reconnect_count),
    };
    if (ap_info != NULL) {
        format_bssid(ap_info->bssid, diagnostics.bssid);
    }
    config_portal_update_diagnostics(&diagnostics);
}

static motion_detector_config_t make_csi_detector_config(void)
{
    return (motion_detector_config_t){
        .window_size = 20U,
        .calibration_samples = 120U,
        .algorithm = MOTION_FEATURE_MEAN_ABSOLUTE_DIFFERENCE,
        .baseline_mode = MOTION_BASELINE_MEDIAN_MAD,
        .sigma_multiplier = 6.0f,
        .minimum_threshold = 0.05f,
        .release_threshold_ratio = 0.75f,
        .baseline_update_alpha = 0.01f,
        .trigger_consecutive = 3U,
        .clear_consecutive = 8U,
    };
}

static bool selected_detector_active(
    app_detection_source_t source,
    const motion_detector_result_t *rssi,
    const motion_detector_result_t *csi)
{
    const bool rssi_active =
        rssi->calibrated && rssi->state == MOTION_DETECTOR_ACTIVE;
    const bool csi_active =
        csi->calibrated && csi->state == MOTION_DETECTOR_ACTIVE;
    return app_detection_source_active(source, rssi_active, csi_active);
}

void app_main(void)
{
    const bool force_recovery =
        recovery_boot_magic == RECOVERY_BOOT_MAGIC &&
        recovery_boot_magic_inverse == ~RECOVERY_BOOT_MAGIC;
    recovery_boot_magic = 0U;
    recovery_boot_magic_inverse = 0U;
    ESP_ERROR_CHECK(app_config_init());
    init_telemetry_output();
    init_motion_led();
    init_event_marker();

    app_runtime_config_t runtime_config;
    esp_err_t config_error = app_config_load(&runtime_config);
    if (config_error != ESP_OK) {
        ESP_LOGW(TAG, "Invalid persisted config (%s); using defaults",
                 esp_err_to_name(config_error));
        app_config_defaults(&runtime_config);
    }

    static motion_detector_t detector;
    if (!motion_detector_init(&detector, &runtime_config.detector)) {
        ESP_LOGE(TAG, "Detector configuration is invalid");
        return;
    }
    static motion_detector_t csi_detector;
    const motion_detector_config_t csi_detector_config =
        make_csi_detector_config();
    if (!motion_detector_init(&csi_detector, &csi_detector_config)) {
        ESP_LOGE(TAG, "CSI detector configuration is invalid");
        return;
    }

    bool provisioning_mode = false;
    ESP_ERROR_CHECK(wifi_connect(&runtime_config,
                                 &provisioning_mode,
                                 force_recovery));
    ESP_ERROR_CHECK(telegram_notifier_init());
    ESP_ERROR_CHECK(config_portal_start(&runtime_config, provisioning_mode));

#if CONFIG_MOTION_CSI_ENABLED
    if (!provisioning_mode) {
        wifi_ap_record_t csi_ap_info;
        ESP_ERROR_CHECK(esp_wifi_sta_get_ap_info(&csi_ap_info));
        const esp_err_t csi_error = csi_capture_start(csi_ap_info.bssid);
        if (csi_error != ESP_OK) {
            ESP_LOGE(TAG,
                     "CSI unavailable; continuing with RSSI only: %s",
                     esp_err_to_name(csi_error));
        }
#if CONFIG_MOTION_CSI_TRAFFIC_ENABLED
        if (csi_error == ESP_OK) {
            const esp_err_t traffic_error = csi_traffic_start(
                wifi_sta_netif,
                CONFIG_MOTION_CSI_TRAFFIC_INTERVAL_MS,
                CONFIG_MOTION_CSI_TRAFFIC_TIMEOUT_MS,
                CONFIG_MOTION_CSI_TRAFFIC_PAYLOAD_BYTES);
            if (traffic_error != ESP_OK) {
                ESP_LOGE(TAG,
                         "Controlled CSI traffic unavailable: %s",
                         esp_err_to_name(traffic_error));
            }
        }
#endif
    }
#endif

#if CONFIG_MOTION_OUTPUT_CSV
    static const char csv_header[] =
        "uptime_ms,sample_ok,rssi_dbm,read_error,rssi_changed,"
        "detection_source,"
        "algorithm,baseline_mode,profile,"
        "query_interval_ms,rssi_change_interval_ms,rssi_unchanged_ms,"
        "baseline,baseline_spread,score,threshold,release_threshold,"
        "state,transition,"
        "calibrated,marker_event_id,marker_active,marker_transition,"
        "csi_valid,csi_age_ms,csi_rssi_dbm,csi_length,csi_subcarriers,"
        "csi_mean_amplitude,csi_amplitude_variance,csi_amplitude_range,"
        "csi_mean_energy,csi_temporal_delta,csi_temporal_delta_mean,"
        "csi_temporal_delta_peak,csi_interval_frames,"
        "csi_normalized_delta,csi_normalized_delta_mean,"
        "csi_normalized_delta_peak,"
        "csi_complex_distance,csi_complex_distance_mean,"
        "csi_complex_distance_peak,"
        "csi_frames_received,csi_frames_processed,csi_frames_dropped,"
        "csi_detector_sampled,csi_detector_calibrated,"
        "csi_detector_score,csi_detector_threshold,csi_detector_state,"
        "csi_detector_transition,"
        "csi_traffic_running,csi_traffic_interval_ms,"
        "csi_traffic_payload_bytes,csi_traffic_requests,"
        "csi_traffic_replies,csi_traffic_timeouts,"
        "queries,samples_ok,read_errors,schedule_misses,"
        "repeated_samples,rssi_changes,disconnects,reconnects,channel,"
        "bssid\n";
    telemetry_write(csv_header, sizeof(csv_header) - 1U);
#endif

    sample_metrics_t metrics;
    sample_metrics_init(&metrics);
    motion_detector_result_t latest_result = {
        .state = MOTION_DETECTOR_CALIBRATING,
        .threshold = detector.threshold,
        .release_threshold = detector.release_threshold,
    };
    motion_detector_result_t latest_csi_result = {
        .state = MOTION_DETECTOR_CALIBRATING,
        .threshold = csi_detector.threshold,
        .release_threshold = csi_detector.release_threshold,
    };
    uint64_t last_csi_frame_processed = 0U;
    wifi_ap_record_t latest_ap_info = {0};
    bool has_ap_info = false;
    bool selected_motion_was_active = false;

    vTaskPrioritySet(NULL, MOTION_SAMPLING_TASK_PRIORITY);
    TickType_t next_wakeup = xTaskGetTickCount();
    for (;;) {
        wifi_ap_record_t ap_info;
        const esp_err_t error = esp_wifi_sta_get_ap_info(&ap_info);
        const int64_t timestamp_ms = esp_timer_get_time() / 1000;
        const bool detector_inputs_paused =
            config_portal_wifi_scan_active(timestamp_ms);
        if (config_portal_take_calibration_request(timestamp_ms)) {
            motion_detector_reset(&detector);
            motion_detector_reset(&csi_detector);
            latest_result = (motion_detector_result_t) {
                .state = MOTION_DETECTOR_CALIBRATING,
                .threshold = detector.threshold,
                .release_threshold = detector.release_threshold,
            };
            latest_csi_result = (motion_detector_result_t) {
                .state = MOTION_DETECTOR_CALIBRATING,
                .threshold = csi_detector.threshold,
                .release_threshold = csi_detector.release_threshold,
            };
            last_csi_frame_processed = 0U;
            set_motion_led(false);
            ESP_LOGI(TAG, "RSSI and CSI detector recalibration started");
        }
        const event_marker_result_t marker_result = event_marker_snapshot();
        csi_capture_snapshot_t csi_snapshot = {0};
        csi_traffic_snapshot_t traffic_snapshot = {0};
#if CONFIG_MOTION_CSI_ENABLED
        (void)csi_capture_get_snapshot(&csi_snapshot);
#endif
        const bool csi_detector_sampled =
            !detector_inputs_paused &&
            csi_snapshot.valid &&
            csi_snapshot.temporal_aggregate.sample_count > 0U &&
            csi_snapshot.frames_processed != last_csi_frame_processed;
        if (csi_detector_sampled) {
            latest_csi_result = motion_detector_push(
                &csi_detector,
                csi_snapshot.complex_correlation_aggregate.peak);
            last_csi_frame_processed = csi_snapshot.frames_processed;
        }
#if CONFIG_MOTION_CSI_TRAFFIC_ENABLED
        csi_traffic_get_snapshot(&traffic_snapshot);
#endif
        if (marker_result.transition != EVENT_MARKER_NO_TRANSITION) {
            ESP_LOGI(TAG,
                     "Experiment marker event=%" PRIu32 " %s",
                     marker_result.event_id,
                     marker_result.active ? "started" : "finished");
        }
        if (error == ESP_OK) {
            if (!detector_inputs_paused) {
                latest_result =
                    motion_detector_push(&detector, ap_info.rssi);
            }
            latest_ap_info = ap_info;
            has_ap_info = true;
            const sample_observation_t observation =
                sample_metrics_record_success(&metrics,
                                              timestamp_ms,
                                              ap_info.rssi,
                                              runtime_config.sample_interval_ms);
            const bool selected_motion_active = selected_detector_active(
                runtime_config.detection_source,
                &latest_result,
                &latest_csi_result);
            set_motion_led(!detector_inputs_paused && selected_motion_active);
            if (!detector_inputs_paused) {
                if (selected_motion_active && !selected_motion_was_active) {
                    (void)telegram_notifier_enqueue_motion(
                        app_detection_source_name(runtime_config.detection_source),
                        timestamp_ms);
                }
                selected_motion_was_active = selected_motion_active;
            }
            emit_sample(timestamp_ms,
                        true,
                        ap_info.rssi,
                        "",
                        &latest_ap_info,
                        &latest_result,
                        &observation,
                        &metrics,
                        &marker_result,
                        &csi_snapshot,
                        &latest_csi_result,
                        csi_detector_sampled,
                        &traffic_snapshot,
                        &runtime_config);
            update_portal_diagnostics(timestamp_ms,
                                      true,
                                      ap_info.rssi,
                                      &latest_ap_info,
                                      &latest_result,
                                      &latest_csi_result,
                                      &metrics);

            if (latest_result.state_changed) {
                ESP_LOGI(TAG,
                         "State=%s score=%.3f threshold=%.3f baseline=%.3f±%.3f",
                         motion_detector_state_name(latest_result.state),
                         latest_result.score,
                         latest_result.threshold,
                         latest_result.baseline_mean,
                         latest_result.baseline_stddev);
            }
            latest_result.state_changed = false;
            latest_csi_result.state_changed = false;
        } else {
            set_motion_led(selected_detector_active(
                runtime_config.detection_source,
                &latest_result,
                &latest_csi_result));
            const sample_observation_t observation =
                sample_metrics_record_error(&metrics,
                                            timestamp_ms,
                                            runtime_config.sample_interval_ms);
            emit_sample(timestamp_ms,
                        false,
                        0,
                        esp_err_to_name(error),
                        has_ap_info ? &latest_ap_info : NULL,
                        &latest_result,
                        &observation,
                        &metrics,
                        &marker_result,
                        &csi_snapshot,
                        &latest_csi_result,
                        csi_detector_sampled,
                        &traffic_snapshot,
                        &runtime_config);
            update_portal_diagnostics(timestamp_ms,
                                      false,
                                      0,
                                      has_ap_info ? &latest_ap_info : NULL,
                                      &latest_result,
                                      &latest_csi_result,
                                      &metrics);
            latest_csi_result.state_changed = false;
            ESP_LOGW(TAG, "RSSI read failed: %s", esp_err_to_name(error));
        }

        vTaskDelayUntil(&next_wakeup,
                        pdMS_TO_TICKS(runtime_config.sample_interval_ms));
    }
}
