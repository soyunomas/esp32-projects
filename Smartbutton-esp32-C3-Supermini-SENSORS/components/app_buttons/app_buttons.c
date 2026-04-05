#include "app_buttons.h"
#include "driver/gpio.h"
#include "app_core.h"
#include "app_http.h"
#include "app_mqtt.h"
#include "app_led.h"
#include "app_nvs.h"
#include "esp_log.h"
#include "esp_timer.h"

#define MAX_INPUTS 3
#define POLL_RATE_MS 50

static const char *TAG = "BUTTONS";
static int s_debounce_ms = 200;

typedef struct {
    int btn_id;
    int gpio_num;
    int input_mode;
    int stabilization_ms;
    bool is_reset_btn;
    bool enabled;
    int prev_level;
    uint32_t press_time;
    int64_t last_trigger_time;
    int64_t boot_time;
} input_state_t;

static input_state_t inputs[MAX_INPUTS];
static int num_inputs = 0;
static int reset_input_idx = -1;

static void trigger_action(int btn_id) {
    button_config_t cfg;
    if (app_nvs_get_button_config(btn_id, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load config for BTN%d", btn_id);
        app_led_signal_error();
        return;
    }

    ESP_LOGI(TAG, "Executing BTN%d Action Type: %d (0=HTTP, 1=MQTT)", btn_id, cfg.action_type);

    if (cfg.action_type == 1) {
        app_mqtt_publish_oneshot(btn_id, &cfg);
    } else {
        app_http_trigger(btn_id);
    }
}

static bool can_trigger(int btn_id, input_state_t *inp) {
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
    if ((now - inp->last_trigger_time) < cfg.cooldown_ms) {
        ESP_LOGW(TAG, "Ignored: Cooldown active");
        return false;
    }

    inp->last_trigger_time = now;
    return true;
}

static bool is_active(input_state_t *inp, int level) {
    if (inp->input_mode == INPUT_MODE_SENSOR) {
        return level == 1;
    }
    return level == 0;
}

static bool is_stabilized(input_state_t *inp) {
    if (inp->stabilization_ms <= 0) return true;
    int64_t now = esp_timer_get_time() / 1000;
    return (now - inp->boot_time) >= inp->stabilization_ms;
}

static void button_task(void *arg) {
    uint32_t reset_hold_time = 0;
    uint32_t reset_time_ms = 8000;
    uint32_t warn_time_ms = 5000;

    while (1) {
        // Reset logic: single designated button
        if (reset_input_idx >= 0) {
            input_state_t *ri = &inputs[reset_input_idx];
            int level = gpio_get_level(ri->gpio_num);

            if (is_active(ri, level)) {
                if (reset_hold_time == 0) {
                    admin_config_t admin;
                    app_nvs_get_admin(&admin);
                    reset_time_ms = admin.reset_time_ms > 0 ? admin.reset_time_ms : 8000;
                    warn_time_ms = reset_time_ms > 3000 ? reset_time_ms - 3000 : reset_time_ms / 2;
                    app_led_set_blink(500, 500);
                }
                reset_hold_time += POLL_RATE_MS;

                if (reset_hold_time > reset_time_ms) {
                    app_set_state(STATE_FACTORY_RESET);
                    vTaskDelay(portMAX_DELAY);
                } else if (reset_hold_time > warn_time_ms && app_get_state() != STATE_RESET_WARNING) {
                    app_set_state(STATE_RESET_WARNING);
                }
            } else {
                if (reset_hold_time > 0) {
                    reset_hold_time = 0;
                    if (app_get_state() == STATE_RESET_WARNING) {
                        app_set_state(STATE_NORMAL);
                    } else {
                        app_led_update_state(app_get_state());
                    }
                }
            }
        }

        // Input polling
        for (int i = 0; i < num_inputs; i++) {
            input_state_t *inp = &inputs[i];

            // Skip disabled inputs (reset button always polls via reset logic above)
            if (!inp->enabled && !inp->is_reset_btn) {
                continue;
            }

            int level = gpio_get_level(inp->gpio_num);

            if (!is_stabilized(inp)) {
                inp->prev_level = level;
                continue;
            }

            if (inp->input_mode == INPUT_MODE_SENSOR) {
                // Sensor: detect rising edge (inactive -> active)
                if (!is_active(inp, inp->prev_level) && is_active(inp, level)) {
                    if (app_get_state() == STATE_NORMAL) {
                        if (can_trigger(inp->btn_id, inp)) {
                            ESP_LOGI(TAG, "INPUT%d (Sensor GPIO%d) Triggered", inp->btn_id, inp->gpio_num);
                            trigger_action(inp->btn_id);
                        }
                    }
                }
            } else {
                // Button: detect release after debounce hold (active -> inactive)
                if (is_active(inp, level)) {
                    inp->press_time += POLL_RATE_MS;
                } else {
                    if (is_active(inp, inp->prev_level) && inp->press_time >= (uint32_t)s_debounce_ms) {
                        // Skip if this is the reset button and was held long
                        if (inp->is_reset_btn && reset_hold_time > 0) {
                            // Already handled by reset logic
                        } else if (app_get_state() == STATE_NORMAL) {
                            if (can_trigger(inp->btn_id, inp)) {
                                ESP_LOGI(TAG, "INPUT%d (Button GPIO%d) Triggered", inp->btn_id, inp->gpio_num);
                                trigger_action(inp->btn_id);
                            }
                        }
                    }
                    inp->press_time = 0;
                }
            }

            inp->prev_level = level;
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_RATE_MS));
    }
}

void app_buttons_init(void) {
    admin_config_t admin;
    app_nvs_get_admin(&admin);
    s_debounce_ms = admin.debounce_ms > 0 ? admin.debounce_ms : 200;

    int64_t now = esp_timer_get_time() / 1000;
    num_inputs = 0;
    reset_input_idx = -1;

    for (int i = 1; i <= MAX_INPUTS; i++) {
        button_config_t cfg;
        if (app_nvs_get_button_config(i, &cfg) != ESP_OK) continue;
        if (cfg.gpio_num < 0) continue;

        input_state_t *inp = &inputs[num_inputs];
        inp->btn_id = i;
        inp->gpio_num = cfg.gpio_num;
        inp->input_mode = cfg.input_mode;
        inp->stabilization_ms = cfg.stabilization_ms;
        inp->is_reset_btn = cfg.is_reset_btn;
        inp->enabled = cfg.enabled;
        inp->prev_level = 0;
        inp->press_time = 0;
        inp->last_trigger_time = 0;
        inp->boot_time = now;

        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << cfg.gpio_num),
            .mode = GPIO_MODE_INPUT,
            .intr_type = GPIO_INTR_DISABLE
        };

        if (cfg.input_mode == INPUT_MODE_SENSOR) {
            io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
            io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
        } else {
            io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
            io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        }

        gpio_config(&io_conf);
        inp->prev_level = gpio_get_level(cfg.gpio_num);

        if (cfg.is_reset_btn) {
            reset_input_idx = num_inputs;
        }

        ESP_LOGI(TAG, "Input %d: GPIO%d mode=%s stab=%dms reset=%s en=%s",
                 i, cfg.gpio_num,
                 cfg.input_mode == INPUT_MODE_SENSOR ? "SENSOR" : "BUTTON",
                 cfg.stabilization_ms,
                 cfg.is_reset_btn ? "YES" : "NO",
                 cfg.enabled ? "YES" : "NO");

        num_inputs++;
    }

    if (num_inputs > 0) {
        xTaskCreate(button_task, "btn_task", 4096, NULL, 5, NULL);
    } else {
        ESP_LOGW(TAG, "No inputs configured");
    }
}
