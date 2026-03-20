#include "app_btn_leds.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define LED_BTN1_GPIO  GPIO_NUM_6   // LED rojo (botón rojo GPIO4)
#define LED_BTN2_GPIO  GPIO_NUM_8   // LED verde (botón verde GPIO5)

static const char *TAG = "BTN_LEDS";
static esp_timer_handle_t wifi_blink_timer = NULL;
static bool wifi_blink_state = false;

static gpio_num_t led_gpio_for_btn(int btn_id) {
    return (btn_id == 1) ? LED_BTN1_GPIO : LED_BTN2_GPIO;
}

void app_btn_leds_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_BTN1_GPIO) | (1ULL << LED_BTN2_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    gpio_set_level(LED_BTN1_GPIO, 0);
    gpio_set_level(LED_BTN2_GPIO, 0);
    ESP_LOGI(TAG, "Button LEDs init: rojo=GPIO%d, verde=GPIO%d", LED_BTN1_GPIO, LED_BTN2_GPIO);
}

void app_btn_leds_on(int btn_id) {
    gpio_set_level(led_gpio_for_btn(btn_id), 1);
}

void app_btn_leds_off(int btn_id) {
    gpio_set_level(led_gpio_for_btn(btn_id), 0);
}

void app_btn_leds_off_all(void) {
    gpio_set_level(LED_BTN1_GPIO, 0);
    gpio_set_level(LED_BTN2_GPIO, 0);
}

void app_btn_leds_blink(int btn_id, int count, int on_ms, int off_ms) {
    gpio_num_t gpio = led_gpio_for_btn(btn_id);
    for (int i = 0; i < count; i++) {
        gpio_set_level(gpio, 1);
        vTaskDelay(pdMS_TO_TICKS(on_ms));
        gpio_set_level(gpio, 0);
        if (i < count - 1) {
            vTaskDelay(pdMS_TO_TICKS(off_ms));
        }
    }
}

static void wifi_blink_cb(void *arg) {
    wifi_blink_state = !wifi_blink_state;
    // Parpadeo alternado: rojo y verde se turnan
    gpio_set_level(LED_BTN1_GPIO, wifi_blink_state ? 1 : 0);
    gpio_set_level(LED_BTN2_GPIO, wifi_blink_state ? 0 : 1);
}

void app_btn_leds_wifi_connecting_start(void) {
    if (wifi_blink_timer != NULL) return;

    esp_timer_create_args_t timer_args = {
        .callback = wifi_blink_cb,
        .name = "wifi_blink"
    };
    esp_timer_create(&timer_args, &wifi_blink_timer);
    wifi_blink_state = false;
    esp_timer_start_periodic(wifi_blink_timer, 300 * 1000); // 300ms
    // Estado inicial
    gpio_set_level(LED_BTN1_GPIO, 1);
    gpio_set_level(LED_BTN2_GPIO, 0);
}

void app_btn_leds_wifi_connecting_stop(void) {
    if (wifi_blink_timer != NULL) {
        esp_timer_stop(wifi_blink_timer);
        esp_timer_delete(wifi_blink_timer);
        wifi_blink_timer = NULL;
    }
    app_btn_leds_off_all();
}

void app_btn_leds_wifi_result(bool success) {
    app_btn_leds_wifi_connecting_stop();
    if (success) {
        // Flash verde 2 veces
        app_btn_leds_blink(2, 2, 200, 150);
    } else {
        // Flash rojo 3 veces
        app_btn_leds_blink(1, 3, 150, 100);
    }
}
