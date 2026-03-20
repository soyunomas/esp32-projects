#include "app_buttons.h"
#include "driver/gpio.h"
#include "app_core.h"
#include "app_http.h"
#include "app_mqtt.h"
#include "app_led.h"
#include "app_nvs.h"
#include "app_btn_leds.h"
#include "esp_log.h"
#include "esp_timer.h"

extern volatile int g_wakeup_btn;

#define BTN1_GPIO 4
#define BTN2_GPIO 5
#define POLL_RATE_MS 50

static int s_debounce_ms = 200;
static const char *TAG = "BUTTONS";
static int64_t last_trigger_time_1 = 0;
static int64_t last_trigger_time_2 = 0;

static void trigger_action(int btn_id) {
    button_config_t cfg;
    if (app_nvs_get_button_config(btn_id, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load config for BTN%d", btn_id);
        app_led_signal_error();
        return;
    }

    ESP_LOGI(TAG, "Executing BTN%d Action Type: %d (0=HTTP, 1=MQTT)", btn_id, cfg.action_type);
    app_btn_leds_on(btn_id);

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

    int64_t now = esp_timer_get_time() / 1000;
    int64_t *last_time = (btn_id == 1) ? &last_trigger_time_1 : &last_trigger_time_2;

    if ((now - *last_time) < cfg.cooldown_ms) {
        ESP_LOGW(TAG, "Ignored: Cooldown active");
        return false;
    }

    *last_time = now;
    return true;
}

void app_buttons_simulate_press(int btn_id) {
    ESP_LOGI(TAG, "BTN%d Wakeup Triggered (Bypassing cooldown)", btn_id);
    
    int64_t now = esp_timer_get_time() / 1000;
    if (btn_id == 1) last_trigger_time_1 = now;
    else last_trigger_time_2 = now;

    trigger_action(btn_id);
}

static void button_task(void *arg) {
    uint32_t both_duration = 0;
    int prev_b1 = 1, prev_b2 = 1;
    uint32_t b1_press_time = 0, b2_press_time = 0;
    bool both_held = false;
    uint32_t reset_time_ms = 8000;
    uint32_t warn_time_ms = 5000;

    while (1) {
        int b1 = gpio_get_level(BTN1_GPIO);
        int b2 = gpio_get_level(BTN2_GPIO);

        if (b1 == 0 && b2 == 0) {
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

            // Alternate LEDs during hold (toggle every 250ms)
            bool led_phase = ((both_duration / 250) % 2) == 0;
            app_btn_leds_on(led_phase ? 1 : 2);
            app_btn_leds_off(led_phase ? 2 : 1);

            if (both_duration > reset_time_ms) {
                app_btn_leds_off_all();
                for (int i = 0; i < 5; i++) {
                    app_btn_leds_on(1);
                    app_btn_leds_on(2);
                    vTaskDelay(pdMS_TO_TICKS(100));
                    app_btn_leds_off_all();
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
                app_set_state(STATE_FACTORY_RESET);
                vTaskDelay(portMAX_DELAY);
            } else if (both_duration > warn_time_ms && app_get_state() != STATE_RESET_WARNING) {
                app_set_state(STATE_RESET_WARNING);
            }
        } else {
            if (both_held) {
                both_held = false;
                both_duration = 0;
                app_btn_leds_off_all();
                if (app_get_state() == STATE_RESET_WARNING) {
                    app_set_state(STATE_NORMAL);
                } else {
                    app_led_update_state(app_get_state());
                }
            }

            if (prev_b1 == 0 && b1 == 1 && b2 == 1) {
                if (g_wakeup_btn == 1) {
                    b1_press_time = 0;
                } else if (b1_press_time >= (uint32_t)s_debounce_ms && app_get_state() == STATE_NORMAL) {
                    if (can_trigger(1)) {
                        ESP_LOGI(TAG, "BTN1 Triggered");
                        trigger_action(1);
                    }
                }
                b1_press_time = 0;
            }

            if (prev_b2 == 0 && b2 == 1 && b1 == 1) {
                if (g_wakeup_btn == 2) {
                    b2_press_time = 0;
                } else if (b2_press_time >= (uint32_t)s_debounce_ms && app_get_state() == STATE_NORMAL) {
                    if (can_trigger(2)) {
                        ESP_LOGI(TAG, "BTN2 Triggered");
                        trigger_action(2);
                    }
                }
                b2_press_time = 0;
            }

            if (b1 == 0 && b2 == 1) b1_press_time += POLL_RATE_MS;
            if (b2 == 0 && b1 == 1) b2_press_time += POLL_RATE_MS;
        }

        prev_b1 = b1;
        prev_b2 = b2;
        vTaskDelay(pdMS_TO_TICKS(POLL_RATE_MS));
    }
}

void app_buttons_init(void) {
    admin_config_t admin;
    app_nvs_get_admin(&admin);
    s_debounce_ms = admin.debounce_ms > 0 ? admin.debounce_ms : 200;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BTN1_GPIO) | (1ULL << BTN2_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    xTaskCreate(button_task, "btn_task", 4096, NULL, 5, NULL);
}
