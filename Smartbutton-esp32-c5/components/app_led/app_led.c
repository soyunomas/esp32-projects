#include "app_led.h"
#include "led_strip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <limits.h>

#define LED_GPIO 27
static const char *TAG = "LED";

static led_strip_handle_t strip = NULL;
static TaskHandle_t led_task_handle = NULL;

static uint8_t cur_r = 0, cur_g = 0, cur_b = 0;
static int blink_on_ms = 0;
static int blink_off_ms = 0;
static bool solid = false;
static bool override_led = false;

static void led_set(uint8_t r, uint8_t g, uint8_t b) {
    led_strip_set_pixel(strip, 0, r, g, b);
    led_strip_refresh(strip);
}

static void led_off(void) {
    led_strip_clear(strip);
}

static void notify_led_task(void) {
    if (led_task_handle) {
        xTaskNotify(led_task_handle, 1, eSetBits);
    }
}

static void led_task(void *arg) {
    while (1) {
        if (override_led) {
            // Dormimos el task de estado general hasta que pase el evento visual síncrono
            xTaskNotifyWait(0, ULONG_MAX, NULL, pdMS_TO_TICKS(100));
            continue;
        }

        if (solid) {
            led_set(cur_r, cur_g, cur_b);
            // xTaskNotifyWait permite abortar el delay instantáneamente
            xTaskNotifyWait(0, ULONG_MAX, NULL, pdMS_TO_TICKS(100));
        } else if (blink_on_ms > 0) {
            led_set(cur_r, cur_g, cur_b);
            if (xTaskNotifyWait(0, ULONG_MAX, NULL, pdMS_TO_TICKS(blink_on_ms)) == pdTRUE) continue;
            
            led_off();
            if (blink_off_ms > 0) {
                if (xTaskNotifyWait(0, ULONG_MAX, NULL, pdMS_TO_TICKS(blink_off_ms)) == pdTRUE) continue;
            }
        } else {
            led_off();
            xTaskNotifyWait(0, ULONG_MAX, NULL, pdMS_TO_TICKS(100));
        }
    }
}

void app_led_init(void) {
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = 1,
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000,
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &strip));
    led_off();
    ESP_LOGI(TAG, "LED RGB (WS2812) inicializado en GPIO %d", LED_GPIO);

    xTaskCreate(led_task, "led_task", 2048, NULL, 5, &led_task_handle);
}

void app_led_update_state(system_state_t state) {
    switch (state) {
        case STATE_AP_MODE:
            // Azul lento
            cur_r = 0; cur_g = 0; cur_b = 30;
            blink_on_ms = 1000; blink_off_ms = 1000;
            solid = false;
            break;
        case STATE_CONNECTING:
            // Amarillo rapido
            cur_r = 30; cur_g = 20; cur_b = 0;
            blink_on_ms = 200; blink_off_ms = 200;
            solid = false;
            break;
        case STATE_NORMAL:
            // Verde fijo
            cur_r = 0; cur_g = 25; cur_b = 0;
            solid = true;
            blink_on_ms = 0; blink_off_ms = 0;
            break;
        case STATE_HTTP_REQ:
            // Cyan pulso super rapido (Para confirmar pulsación instantánea)
            cur_r = 0; cur_g = 20; cur_b = 30;
            blink_on_ms = 150; blink_off_ms = 150;
            solid = false;
            break;
        case STATE_RESET_WARNING:
            // Rojo rápido
            cur_r = 40; cur_g = 0; cur_b = 0;
            blink_on_ms = 100; blink_off_ms = 100;
            solid = false;
            break;
        case STATE_FACTORY_RESET:
            // Rojo sólido para cuando va a matar todo
            cur_r = 40; cur_g = 0; cur_b = 0;
            solid = true;
            blink_on_ms = 0; blink_off_ms = 0;
            break;
        default:
            break;
    }
    notify_led_task();
}

void app_led_signal_success(void) {
    override_led = true;
    notify_led_task(); // Corta cualquier estado en curso
    led_off(); vTaskDelay(pdMS_TO_TICKS(100));
    led_set(0, 40, 0); vTaskDelay(pdMS_TO_TICKS(1000));
    led_off();
    override_led = false;
    notify_led_task();
}

void app_led_signal_error(void) {
    override_led = true;
    notify_led_task(); // Corta cualquier estado en curso
    for (int i = 0; i < 3; i++) {
        led_set(40, 0, 0); vTaskDelay(pdMS_TO_TICKS(100));
        led_off(); vTaskDelay(pdMS_TO_TICKS(100));
    }
    override_led = false;
    notify_led_task();
}

void app_led_set_blink(int on_ms, int off_ms) {
    blink_on_ms = on_ms;
    blink_off_ms = off_ms;
    solid = false;
    notify_led_task();
}

void app_led_set_color(uint8_t r, uint8_t g, uint8_t b) {
    cur_r = r; cur_g = g; cur_b = b;
    solid = true;
    blink_on_ms = 0; blink_off_ms = 0;
    notify_led_task();
}

void app_led_off(void) {
    solid = false;
    blink_on_ms = 0; blink_off_ms = 0;
    cur_r = 0; cur_g = 0; cur_b = 0;
    notify_led_task();
}
