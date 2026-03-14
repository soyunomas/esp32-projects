#include "app_led.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <limits.h>

// ESP32-C3 SuperMini: LED azul onboard en GPIO8, activo LOW (0=ON, 1=OFF)
#define LED_GPIO GPIO_NUM_8

static const char *TAG = "LED";

static TaskHandle_t led_task_handle = NULL;

static bool led_on_state = false;
static int blink_on_ms = 0;
static int blink_off_ms = 0;
static bool solid = false;
static bool override_led = false;

static void led_set_on(void) {
    gpio_set_level(LED_GPIO, 0); // Active LOW
}

static void led_set_off(void) {
    gpio_set_level(LED_GPIO, 1); // Active LOW
}

static void notify_led_task(void) {
    if (led_task_handle) {
        xTaskNotify(led_task_handle, 1, eSetBits);
    }
}

static void led_task(void *arg) {
    while (1) {
        if (override_led) {
            xTaskNotifyWait(0, ULONG_MAX, NULL, pdMS_TO_TICKS(100));
            continue;
        }

        if (solid && led_on_state) {
            led_set_on();
            xTaskNotifyWait(0, ULONG_MAX, NULL, pdMS_TO_TICKS(100));
        } else if (blink_on_ms > 0 && led_on_state) {
            led_set_on();
            if (xTaskNotifyWait(0, ULONG_MAX, NULL, pdMS_TO_TICKS(blink_on_ms)) == pdTRUE) continue;
            
            led_set_off();
            if (blink_off_ms > 0) {
                if (xTaskNotifyWait(0, ULONG_MAX, NULL, pdMS_TO_TICKS(blink_off_ms)) == pdTRUE) continue;
            }
        } else {
            led_set_off();
            xTaskNotifyWait(0, ULONG_MAX, NULL, pdMS_TO_TICKS(100));
        }
    }
}

void app_led_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    led_set_off();
    ESP_LOGI(TAG, "LED azul onboard inicializado en GPIO %d (active LOW)", LED_GPIO);

    xTaskCreate(led_task, "led_task", 2048, NULL, 5, &led_task_handle);
}

void app_led_update_state(system_state_t state) {
    switch (state) {
        case STATE_AP_MODE:
            // Parpadeo lento: modo AP
            led_on_state = true;
            blink_on_ms = 1000; blink_off_ms = 1000;
            solid = false;
            break;
        case STATE_CONNECTING:
            // Parpadeo rápido: conectando
            led_on_state = true;
            blink_on_ms = 200; blink_off_ms = 200;
            solid = false;
            break;
        case STATE_NORMAL:
            // Azul fijo: conectado OK
            led_on_state = true;
            solid = true;
            blink_on_ms = 0; blink_off_ms = 0;
            break;
        case STATE_HTTP_REQ:
            // Pulso súper rápido: acción en curso
            led_on_state = true;
            blink_on_ms = 150; blink_off_ms = 150;
            solid = false;
            break;
        case STATE_RESET_WARNING:
            // Parpadeo muy rápido: warning
            led_on_state = true;
            blink_on_ms = 100; blink_off_ms = 100;
            solid = false;
            break;
        case STATE_FACTORY_RESET:
            // LED fijo: reset en curso
            led_on_state = true;
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
    notify_led_task();
    led_set_off(); vTaskDelay(pdMS_TO_TICKS(100));
    led_set_on();  vTaskDelay(pdMS_TO_TICKS(1000));
    led_set_off();
    override_led = false;
    notify_led_task();
}

void app_led_signal_error(void) {
    override_led = true;
    notify_led_task();
    for (int i = 0; i < 3; i++) {
        led_set_on();  vTaskDelay(pdMS_TO_TICKS(100));
        led_set_off(); vTaskDelay(pdMS_TO_TICKS(100));
    }
    override_led = false;
    notify_led_task();
}

void app_led_set_blink(int on_ms, int off_ms) {
    led_on_state = true;
    blink_on_ms = on_ms;
    blink_off_ms = off_ms;
    solid = false;
    notify_led_task();
}

void app_led_set_color(uint8_t r, uint8_t g, uint8_t b) {
    // En C3 SuperMini solo tenemos LED azul, si cualquier color != 0, encendemos
    led_on_state = (r > 0 || g > 0 || b > 0);
    solid = true;
    blink_on_ms = 0; blink_off_ms = 0;
    notify_led_task();
}

void app_led_off(void) {
    solid = false;
    blink_on_ms = 0; blink_off_ms = 0;
    led_on_state = false;
    notify_led_task();
}
