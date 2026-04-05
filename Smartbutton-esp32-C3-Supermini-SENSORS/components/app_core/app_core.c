#include "app_core.h"
#include "esp_log.h"
#include <stddef.h>

static const char *TAG = "CORE";
system_state_t current_state = STATE_INIT;
EventGroupHandle_t app_event_group;

static state_change_cb_t state_cb = NULL;

void app_set_state_callback(state_change_cb_t cb) {
    state_cb = cb;
}

void app_set_state(system_state_t new_state) {
    if (current_state != new_state) {
        current_state = new_state;
        ESP_LOGI(TAG, "State Transition -> %d", new_state);
        // Notificamos inmediatamente al callback registrado (LED en este caso)
        if (state_cb) {
            state_cb(new_state);
        }
    }
}

system_state_t app_get_state(void) {
    return current_state;
}
