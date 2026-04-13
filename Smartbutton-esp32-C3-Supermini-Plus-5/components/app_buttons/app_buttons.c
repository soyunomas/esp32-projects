#include "app_buttons.h"
#include "driver/gpio.h"
#include "app_core.h"
#include "app_http.h"
#include "app_mqtt.h"
#include "app_led.h"
#include "app_nvs.h"
#include "esp_log.h"
#include "esp_timer.h"

extern volatile int g_wakeup_btn;

#define NUM_BUTTONS 5
#define POLL_RATE_MS 50

static const int BTN_GPIO[NUM_BUTTONS] = {4, 5, 6, 7, 10};

static int s_debounce_ms = 200;
static const char *TAG = "BUTTONS";
static int64_t last_trigger_time[NUM_BUTTONS] = {0};

static void trigger_action(int btn_id) {
    button_config_t cfg;
    if (app_nvs_get_button_config(btn_id, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load config for BTN%d", btn_id);
        app_led_signal_error();
        return;
    }

    ESP_LOGI(TAG, "Executing BTN%d Action Type: %d (0=HTTP, 1=MQTT)", btn_id, cfg.action_type);

    if (cfg.action_type == 1) {
        // MQTT
        app_mqtt_publish_oneshot(btn_id, &cfg);
    } else {
        // HTTP (Default)
        app_http_trigger(btn_id);
    }
}

static bool can_trigger(int btn_id) {
    if (app_get_state() == STATE_HTTP_REQ) {
        ESP_LOGW(TAG, "Ignored: System Busy");
        app_led_set_blink(50, 50); 
        vTaskDelay(pdMS_TO_TICKS(200)); 
        app_led_update_state(app_get_state());
        return false;
    }

    button_config_t cfg;
    if (app_nvs_get_button_config(btn_id, &cfg) != ESP_OK) {
        cfg.cooldown_ms = 2000; 
    }

    int idx = btn_id - 1;
    if (idx < 0 || idx >= NUM_BUTTONS) return false;

    int64_t now = esp_timer_get_time() / 1000;
    if ((now - last_trigger_time[idx]) < cfg.cooldown_ms) {
        ESP_LOGW(TAG, "Ignored: Cooldown active");
        return false;
    }

    last_trigger_time[idx] = now;
    return true;
}

void app_buttons_simulate_press(int btn_id) {
    ESP_LOGI(TAG, "BTN%d Wakeup Triggered (Bypassing cooldown)", btn_id);
    
    int idx = btn_id - 1;
    if (idx >= 0 && idx < NUM_BUTTONS) {
        last_trigger_time[idx] = esp_timer_get_time() / 1000;
    }

    trigger_action(btn_id);
}

static bool all_others_released(const int *levels, int except) {
    for (int i = 0; i < NUM_BUTTONS; i++) {
        if (i != except && levels[i] == 0) return false;
    }
    return true;
}

static int s_reset_btns[2] = {-1, -1};

static void load_reset_buttons(void) {
    int count = 0;
    s_reset_btns[0] = -1;
    s_reset_btns[1] = -1;
    for (int i = 1; i <= NUM_BUTTONS && count < 2; i++) {
        button_config_t cfg;
        if (app_nvs_get_button_config(i, &cfg) == ESP_OK && cfg.is_reset) {
            s_reset_btns[count++] = i - 1;
        }
    }
}

static void button_task(void *arg) {
    uint32_t both_duration = 0;
    int prev[NUM_BUTTONS];
    uint32_t press_time[NUM_BUTTONS];
    bool both_held = false;
    uint32_t reset_time_ms = 8000;
    uint32_t warn_time_ms = 5000;

    load_reset_buttons();

    for (int i = 0; i < NUM_BUTTONS; i++) {
        prev[i] = 1;
        press_time[i] = 0;
    }

    while (1) {
        int cur[NUM_BUTTONS];
        for (int i = 0; i < NUM_BUTTONS; i++) {
            cur[i] = gpio_get_level(BTN_GPIO[i]);
        }

        // Factory reset: dos botones marcados como reset pulsados simultáneamente
        bool reset_combo = (s_reset_btns[0] >= 0 && s_reset_btns[1] >= 0 &&
                            cur[s_reset_btns[0]] == 0 && cur[s_reset_btns[1]] == 0);
        if (reset_combo) {
            if (!both_held) {
                both_held = true;
                both_duration = 0;
                
                admin_config_t admin;
                app_nvs_get_admin(&admin);
                reset_time_ms = admin.reset_time_ms > 0 ? admin.reset_time_ms : 8000;
                warn_time_ms = reset_time_ms > 3000 ? reset_time_ms - 3000 : reset_time_ms / 2;
                
                app_led_set_blink(500, 500);
            }
            both_duration += POLL_RATE_MS;

            if (both_duration > reset_time_ms) {
                app_set_state(STATE_FACTORY_RESET);
                vTaskDelay(portMAX_DELAY);
            } else if (both_duration > warn_time_ms && app_get_state() != STATE_RESET_WARNING) {
                app_set_state(STATE_RESET_WARNING);
            }
        } else {
            if (both_held) {
                both_held = false;
                both_duration = 0;
                if (app_get_state() == STATE_RESET_WARNING) {
                    app_set_state(STATE_NORMAL);
                } else {
                    app_led_update_state(app_get_state());
                }
            }

            for (int i = 0; i < NUM_BUTTONS; i++) {
                int btn_id = i + 1;
                if (prev[i] == 0 && cur[i] == 1 && all_others_released(cur, i)) {
                    if (g_wakeup_btn == btn_id) {
                        press_time[i] = 0;
                    } else if (press_time[i] >= (uint32_t)s_debounce_ms && app_get_state() == STATE_NORMAL) {
                        if (can_trigger(btn_id)) {
                            ESP_LOGI(TAG, "BTN%d Triggered", btn_id);
                            trigger_action(btn_id);
                        }
                    }
                    press_time[i] = 0;
                }

                if (cur[i] == 0 && all_others_released(cur, i)) {
                    press_time[i] += POLL_RATE_MS;
                }
            }
        }

        for (int i = 0; i < NUM_BUTTONS; i++) {
            prev[i] = cur[i];
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_RATE_MS));
    }
}

void app_buttons_init(void) {
    admin_config_t admin;
    app_nvs_get_admin(&admin);
    s_debounce_ms = admin.debounce_ms > 0 ? admin.debounce_ms : 200;

    uint64_t pin_mask = 0;
    for (int i = 0; i < NUM_BUTTONS; i++) {
        pin_mask |= (1ULL << BTN_GPIO[i]);
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = pin_mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    xTaskCreate(button_task, "btn_task", 4096, NULL, 5, NULL);
}
