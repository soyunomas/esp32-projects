#include "csi_capture.h"

#include <stdatomic.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define CSI_CAPTURE_QUEUE_DEPTH 32U
#define CSI_CAPTURE_TASK_STACK 3072U
#define CSI_CAPTURE_TASK_PRIORITY 4U
#define CSI_CAPTURE_MAX_BYTES (CSI_FEATURE_MAX_SUBCARRIERS * 2U)

static const char *TAG = "csi_capture";

typedef struct {
    int8_t data[CSI_CAPTURE_MAX_BYTES];
    uint16_t length;
    int8_t rssi_dbm;
    uint32_t wifi_timestamp_us;
    int64_t received_at_us;
    bool first_word_invalid;
} queued_csi_frame_t;

static QueueHandle_t frame_queue;
static uint8_t expected_source_mac[6];
static atomic_uint_fast64_t frames_received;
static atomic_uint_fast64_t frames_processed;
static atomic_uint_fast64_t frames_dropped;
static portMUX_TYPE snapshot_lock = portMUX_INITIALIZER_UNLOCKED;
static csi_capture_snapshot_t latest_snapshot;
static csi_temporal_aggregator_t temporal_aggregator;
static csi_temporal_aggregator_t normalized_temporal_aggregator;
static csi_temporal_aggregator_t complex_correlation_aggregator;

static void csi_receive_callback(void *context, wifi_csi_info_t *info)
{
    (void)context;
    if (info == NULL || info->buf == NULL ||
        memcmp(info->mac, expected_source_mac, sizeof(expected_source_mac)) != 0) {
        return;
    }

    atomic_fetch_add(&frames_received, 1U);
    if (info->len == 0U || info->len > CSI_CAPTURE_MAX_BYTES) {
        atomic_fetch_add(&frames_dropped, 1U);
        return;
    }

    queued_csi_frame_t frame = {
        .length = info->len,
        .rssi_dbm = info->rx_ctrl.rssi,
        .wifi_timestamp_us = info->rx_ctrl.timestamp,
        .received_at_us = esp_timer_get_time(),
        .first_word_invalid = info->first_word_invalid,
    };
    memcpy(frame.data, info->buf, info->len);
    if (xQueueSend(frame_queue, &frame, 0) != pdTRUE) {
        atomic_fetch_add(&frames_dropped, 1U);
    }
}

static void csi_processing_task(void *argument)
{
    (void)argument;
    csi_feature_extractor_t extractor;
    csi_feature_extractor_init(&extractor);
    queued_csi_frame_t frame;

    for (;;) {
        if (xQueueReceive(frame_queue, &frame, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        const csi_frame_features_t features =
            csi_feature_extract(&extractor,
                                frame.data,
                                frame.length,
                                frame.first_word_invalid);
        if (!features.valid) {
            atomic_fetch_add(&frames_dropped, 1U);
            continue;
        }
        const uint64_t processed = atomic_fetch_add(&frames_processed, 1U) + 1U;
        const csi_capture_snapshot_t snapshot = {
            .valid = true,
            .rssi_dbm = frame.rssi_dbm,
            .csi_length = frame.length,
            .wifi_timestamp_us = frame.wifi_timestamp_us,
            .received_at_us = frame.received_at_us,
            .frames_received = atomic_load(&frames_received),
            .frames_processed = processed,
            .frames_dropped = atomic_load(&frames_dropped),
            .features = features,
        };
        taskENTER_CRITICAL(&snapshot_lock);
        latest_snapshot = snapshot;
        if (features.temporal_delta_valid) {
            csi_temporal_aggregator_push(
                &temporal_aggregator,
                features.temporal_mean_absolute_delta);
            csi_temporal_aggregator_push(
                &normalized_temporal_aggregator,
                features.temporal_normalized_mean_absolute_delta);
            csi_temporal_aggregator_push(
                &complex_correlation_aggregator,
                features.temporal_complex_correlation_distance);
        }
        taskEXIT_CRITICAL(&snapshot_lock);
    }
}

esp_err_t csi_capture_start(const uint8_t source_mac[6])
{
    if (source_mac == NULL || frame_queue != NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const wifi_csi_config_t config = {
        .lltf_en = true,
        .htltf_en = false,
        .stbc_htltf2_en = false,
        .ltf_merge_en = true,
        .channel_filter_en = true,
        .manu_scale = true,
        .shift = 1,
        .dump_ack_en = false,
    };
    esp_err_t error = esp_wifi_set_csi_config(&config);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_csi_config failed: %s", esp_err_to_name(error));
        return error;
    }

    memcpy(expected_source_mac, source_mac, sizeof(expected_source_mac));
    memset(&latest_snapshot, 0, sizeof(latest_snapshot));
    csi_temporal_aggregator_init(&temporal_aggregator);
    csi_temporal_aggregator_init(&normalized_temporal_aggregator);
    csi_temporal_aggregator_init(&complex_correlation_aggregator);
    atomic_store(&frames_received, 0U);
    atomic_store(&frames_processed, 0U);
    atomic_store(&frames_dropped, 0U);

    frame_queue = xQueueCreate(CSI_CAPTURE_QUEUE_DEPTH,
                               sizeof(queued_csi_frame_t));
    if (frame_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    TaskHandle_t processing_task = NULL;
    if (xTaskCreate(csi_processing_task,
                    "csi_processing",
                    CSI_CAPTURE_TASK_STACK,
                    NULL,
                    CSI_CAPTURE_TASK_PRIORITY,
                    &processing_task) != pdPASS) {
        vQueueDelete(frame_queue);
        frame_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    error = esp_wifi_set_csi_rx_cb(csi_receive_callback, NULL);
    if (error == ESP_OK) {
        error = esp_wifi_set_csi(true);
    }
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "CSI callback/enable failed: %s", esp_err_to_name(error));
        vTaskDelete(processing_task);
        vQueueDelete(frame_queue);
        frame_queue = NULL;
        return error;
    }
    ESP_LOGI(TAG, "CSI capture enabled for connected AP");
    return ESP_OK;
}

bool csi_capture_get_snapshot(csi_capture_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return false;
    }
    taskENTER_CRITICAL(&snapshot_lock);
    *snapshot = latest_snapshot;
    (void)csi_temporal_aggregator_take(&temporal_aggregator,
                                       &snapshot->temporal_aggregate);
    (void)csi_temporal_aggregator_take(
        &normalized_temporal_aggregator,
        &snapshot->normalized_temporal_aggregate);
    (void)csi_temporal_aggregator_take(
        &complex_correlation_aggregator,
        &snapshot->complex_correlation_aggregate);
    taskEXIT_CRITICAL(&snapshot_lock);
    snapshot->frames_received = atomic_load(&frames_received);
    snapshot->frames_processed = atomic_load(&frames_processed);
    snapshot->frames_dropped = atomic_load(&frames_dropped);
    return snapshot->valid;
}
