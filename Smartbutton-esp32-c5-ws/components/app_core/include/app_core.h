#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

typedef enum {
    STATE_INIT,
    STATE_NO_CONFIG,
    STATE_AP_MODE,
    STATE_CONNECTING,
    STATE_NORMAL,
    STATE_HTTP_REQ,
    STATE_RESET_WARNING,
    STATE_FACTORY_RESET,
    STATE_ERROR
} system_state_t;

#define EVENT_WIFI_CONNECTED    (1 << 0)
#define EVENT_WIFI_LOST         (1 << 1)
#define EVENT_BUTTON_TRIGGER    (1 << 2)
#define EVENT_FACTORY_RESET     (1 << 3)
#define EVENT_HTTP_DONE         (1 << 4)

typedef void (*state_change_cb_t)(system_state_t);

extern EventGroupHandle_t app_event_group;

void app_set_state(system_state_t new_state);
system_state_t app_get_state(void);
void app_set_state_callback(state_change_cb_t cb);
