#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TELEGRAM_TOKEN_MAX_LENGTH 128U
#define TELEGRAM_CHAT_ID_MAX_LENGTH 32U

typedef struct {
    bool enabled;
    bool token_set;
    char chat_id[TELEGRAM_CHAT_ID_MAX_LENGTH + 1U];
    bool busy;
    uint32_t queue_depth;
    uint64_t sent;
    uint64_t failures;
    uint64_t dropped;
    int last_http_status;
    esp_err_t last_error;
} telegram_notifier_snapshot_t;

esp_err_t telegram_notifier_init(void);
bool telegram_notifier_token_valid(const char *token);
bool telegram_notifier_chat_id_valid(const char *chat_id);
esp_err_t telegram_notifier_update(bool enabled,
                                   const char *new_token,
                                   bool clear_token,
                                   const char *chat_id);
esp_err_t telegram_notifier_erase(void);
esp_err_t telegram_notifier_enqueue_motion(const char *source,
                                           int64_t timestamp_ms);
esp_err_t telegram_notifier_enqueue_test(void);
void telegram_notifier_get_snapshot(telegram_notifier_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif
