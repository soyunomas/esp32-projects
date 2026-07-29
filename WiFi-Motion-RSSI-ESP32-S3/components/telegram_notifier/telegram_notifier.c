#include "telegram_notifier.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

#define TELEGRAM_NAMESPACE "telegram"
#define TELEGRAM_BLOB_KEY "config_v1"
#define TELEGRAM_MAGIC 0x54474D31UL
#define TELEGRAM_VERSION 1U
#define TELEGRAM_QUEUE_LENGTH 6U
#define TELEGRAM_MESSAGE_MAX_LENGTH 160U
#define TELEGRAM_COOLDOWN_MS 30000LL
#define TELEGRAM_HTTP_TIMEOUT_MS 8000

static const char *TAG = "telegram";

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint8_t enabled;
    char token[TELEGRAM_TOKEN_MAX_LENGTH + 1U];
    char chat_id[TELEGRAM_CHAT_ID_MAX_LENGTH + 1U];
} stored_telegram_config_t;

typedef struct {
    char message[TELEGRAM_MESSAGE_MAX_LENGTH + 1U];
} telegram_queue_item_t;

static stored_telegram_config_t current_config;
static telegram_notifier_snapshot_t current_snapshot;
static SemaphoreHandle_t config_mutex;
static SemaphoreHandle_t snapshot_mutex;
static QueueHandle_t notification_queue;
static int64_t last_motion_queued_ms = -TELEGRAM_COOLDOWN_MS;

static bool stored_config_valid(const stored_telegram_config_t *config)
{
    if (config->magic != TELEGRAM_MAGIC ||
        config->version != TELEGRAM_VERSION ||
        config->size != sizeof(*config) || config->enabled > 1U ||
        strnlen(config->token, sizeof(config->token)) >= sizeof(config->token) ||
        strnlen(config->chat_id, sizeof(config->chat_id)) >=
            sizeof(config->chat_id)) {
        return false;
    }
    const bool has_token = config->token[0] != '\0';
    const bool has_chat_id = config->chat_id[0] != '\0';
    return (!has_token || telegram_notifier_token_valid(config->token)) &&
           (!has_chat_id || telegram_notifier_chat_id_valid(config->chat_id)) &&
           (!config->enabled || (has_token && has_chat_id));
}

static esp_err_t persist_config(const stored_telegram_config_t *config)
{
    nvs_handle_t handle;
    esp_err_t error = nvs_open(TELEGRAM_NAMESPACE, NVS_READWRITE, &handle);
    if (error != ESP_OK) {
        return error;
    }
    error = nvs_set_blob(handle, TELEGRAM_BLOB_KEY, config, sizeof(*config));
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    nvs_close(handle);
    return error;
}

static void update_result(bool success, esp_err_t error, int http_status)
{
    if (xSemaphoreTake(snapshot_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        current_snapshot.busy = false;
        current_snapshot.last_error = error;
        current_snapshot.last_http_status = http_status;
        if (success) {
            ++current_snapshot.sent;
        } else {
            ++current_snapshot.failures;
        }
        current_snapshot.queue_depth = uxQueueMessagesWaiting(notification_queue);
        xSemaphoreGive(snapshot_mutex);
    }
}

static esp_err_t send_message(const telegram_queue_item_t *item,
                              int *http_status)
{
    char token[TELEGRAM_TOKEN_MAX_LENGTH + 1U];
    char chat_id[TELEGRAM_CHAT_ID_MAX_LENGTH + 1U];
    if (xSemaphoreTake(config_mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    strlcpy(token, current_config.token, sizeof(token));
    strlcpy(chat_id, current_config.chat_id, sizeof(chat_id));
    xSemaphoreGive(config_mutex);
    if (!telegram_notifier_token_valid(token) ||
        !telegram_notifier_chat_id_valid(chat_id)) {
        memset(token, 0, sizeof(token));
        return ESP_ERR_INVALID_STATE;
    }

    char url[TELEGRAM_TOKEN_MAX_LENGTH + 64U];
    const int url_length = snprintf(url,
                                    sizeof(url),
                                    "https://api.telegram.org/bot%s/sendMessage",
                                    token);
    memset(token, 0, sizeof(token));
    if (url_length < 0 || (size_t)url_length >= sizeof(url)) {
        memset(url, 0, sizeof(url));
        return ESP_ERR_INVALID_SIZE;
    }

    cJSON *json = cJSON_CreateObject();
    if (json == NULL ||
        cJSON_AddStringToObject(json, "chat_id", chat_id) == NULL ||
        cJSON_AddStringToObject(json, "text", item->message) == NULL) {
        cJSON_Delete(json);
        memset(chat_id, 0, sizeof(chat_id));
        memset(url, 0, sizeof(url));
        return ESP_ERR_NO_MEM;
    }
    memset(chat_id, 0, sizeof(chat_id));
    char *body = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (body == NULL) {
        memset(url, 0, sizeof(url));
        return ESP_ERR_NO_MEM;
    }

    const esp_http_client_config_t http_config = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = TELEGRAM_HTTP_TIMEOUT_MS,
        .disable_auto_redirect = true,
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == NULL) {
        cJSON_free(body);
        memset(url, 0, sizeof(url));
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, strlen(body));
    const esp_err_t error = esp_http_client_perform(client);
    *http_status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    cJSON_free(body);
    memset(url, 0, sizeof(url));
    return error;
}

static void telegram_worker(void *argument)
{
    (void)argument;
    telegram_queue_item_t item;
    for (;;) {
        if (xQueueReceive(notification_queue, &item, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (xSemaphoreTake(snapshot_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            current_snapshot.busy = true;
            current_snapshot.queue_depth = uxQueueMessagesWaiting(notification_queue);
            xSemaphoreGive(snapshot_mutex);
        }
        int http_status = 0;
        const esp_err_t error = send_message(&item, &http_status);
        const bool success = error == ESP_OK && http_status == 200;
        update_result(success, error, http_status);
        if (success) {
            ESP_LOGI(TAG, "Telegram notification sent");
        } else {
            ESP_LOGW(TAG,
                     "Telegram notification failed: error=%s http=%d",
                     esp_err_to_name(error),
                     http_status);
        }
        memset(&item, 0, sizeof(item));
    }
}

esp_err_t telegram_notifier_init(void)
{
    config_mutex = xSemaphoreCreateMutex();
    snapshot_mutex = xSemaphoreCreateMutex();
    notification_queue = xQueueCreate(TELEGRAM_QUEUE_LENGTH,
                                      sizeof(telegram_queue_item_t));
    if (config_mutex == NULL || snapshot_mutex == NULL ||
        notification_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    memset(&current_config, 0, sizeof(current_config));
    current_config.magic = TELEGRAM_MAGIC;
    current_config.version = TELEGRAM_VERSION;
    current_config.size = sizeof(current_config);
    nvs_handle_t handle;
    esp_err_t error = nvs_open(TELEGRAM_NAMESPACE, NVS_READONLY, &handle);
    if (error == ESP_OK) {
        size_t size = sizeof(current_config);
        error = nvs_get_blob(handle, TELEGRAM_BLOB_KEY, &current_config, &size);
        nvs_close(handle);
        if (error != ESP_OK || size != sizeof(current_config) ||
            !stored_config_valid(&current_config)) {
            ESP_LOGW(TAG, "Ignoring invalid persisted Telegram configuration");
            memset(&current_config, 0, sizeof(current_config));
            current_config.magic = TELEGRAM_MAGIC;
            current_config.version = TELEGRAM_VERSION;
            current_config.size = sizeof(current_config);
        }
    } else if (error != ESP_ERR_NVS_NOT_FOUND) {
        return error;
    }
    current_snapshot.enabled = current_config.enabled != 0U;
    current_snapshot.token_set = current_config.token[0] != '\0';
    strlcpy(current_snapshot.chat_id,
            current_config.chat_id,
            sizeof(current_snapshot.chat_id));
    if (xTaskCreate(telegram_worker,
                    "telegram",
                    8192,
                    NULL,
                    4,
                    NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t telegram_notifier_update(bool enabled,
                                   const char *new_token,
                                   bool clear_token,
                                   const char *chat_id)
{
    if (chat_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(config_mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    stored_telegram_config_t candidate = current_config;
    candidate.enabled = enabled ? 1U : 0U;
    if (clear_token) {
        memset(candidate.token, 0, sizeof(candidate.token));
    } else if (new_token != NULL && new_token[0] != '\0') {
        strlcpy(candidate.token, new_token, sizeof(candidate.token));
    }
    strlcpy(candidate.chat_id, chat_id, sizeof(candidate.chat_id));
    if (!stored_config_valid(&candidate)) {
        xSemaphoreGive(config_mutex);
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t error = persist_config(&candidate);
    if (error == ESP_OK) {
        current_config = candidate;
    }
    xSemaphoreGive(config_mutex);
    if (error == ESP_OK &&
        xSemaphoreTake(snapshot_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        current_snapshot.enabled = candidate.enabled != 0U;
        current_snapshot.token_set = candidate.token[0] != '\0';
        strlcpy(current_snapshot.chat_id,
                candidate.chat_id,
                sizeof(current_snapshot.chat_id));
        xSemaphoreGive(snapshot_mutex);
    }
    return error;
}

esp_err_t telegram_notifier_erase(void)
{
    nvs_handle_t handle;
    esp_err_t error = nvs_open(TELEGRAM_NAMESPACE, NVS_READWRITE, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        error = ESP_OK;
    } else if (error == ESP_OK) {
        error = nvs_erase_key(handle, TELEGRAM_BLOB_KEY);
        if (error == ESP_ERR_NVS_NOT_FOUND) {
            error = ESP_OK;
        }
        if (error == ESP_OK) {
            error = nvs_commit(handle);
        }
        nvs_close(handle);
    }
    if (error == ESP_OK) {
        (void)telegram_notifier_update(false, NULL, true, "");
    }
    return error;
}

static esp_err_t enqueue_message(const char *message)
{
    telegram_queue_item_t item = {0};
    strlcpy(item.message, message, sizeof(item.message));
    if (xQueueSend(notification_queue, &item, 0) != pdTRUE) {
        if (xSemaphoreTake(snapshot_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            ++current_snapshot.dropped;
            xSemaphoreGive(snapshot_mutex);
        }
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(snapshot_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        current_snapshot.queue_depth = uxQueueMessagesWaiting(notification_queue);
        xSemaphoreGive(snapshot_mutex);
    }
    return ESP_OK;
}

esp_err_t telegram_notifier_enqueue_motion(const char *source,
                                           int64_t timestamp_ms)
{
    bool enabled = false;
    if (xSemaphoreTake(config_mutex, 0) == pdTRUE) {
        enabled = current_config.enabled != 0U;
        xSemaphoreGive(config_mutex);
    }
    if (!enabled) {
        return ESP_ERR_INVALID_STATE;
    }
    if (timestamp_ms - last_motion_queued_ms < TELEGRAM_COOLDOWN_MS) {
        return ESP_ERR_INVALID_STATE;
    }
    char message[TELEGRAM_MESSAGE_MAX_LENGTH + 1U];
    snprintf(message,
             sizeof(message),
             "Movimiento detectado por %s. Tiempo activo: %.1f s.",
             source != NULL ? source : "Wi-Fi",
             (double)timestamp_ms / 1000.0);
    const esp_err_t error = enqueue_message(message);
    if (error == ESP_OK) {
        last_motion_queued_ms = timestamp_ms;
    }
    return error;
}

esp_err_t telegram_notifier_enqueue_test(void)
{
    bool configured = false;
    if (xSemaphoreTake(config_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        configured = telegram_notifier_token_valid(current_config.token) &&
                     telegram_notifier_chat_id_valid(current_config.chat_id);
        xSemaphoreGive(config_mutex);
    }
    return configured
        ? enqueue_message("Mensaje de prueba de WiFi Motion RSSI.")
        : ESP_ERR_INVALID_STATE;
}

void telegram_notifier_get_snapshot(telegram_notifier_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    if (xSemaphoreTake(snapshot_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        *snapshot = current_snapshot;
        snapshot->queue_depth = uxQueueMessagesWaiting(notification_queue);
        xSemaphoreGive(snapshot_mutex);
    }
}
