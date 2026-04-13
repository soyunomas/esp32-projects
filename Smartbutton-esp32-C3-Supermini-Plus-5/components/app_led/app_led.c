#include "app_led.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <limits.h>
#include <string.h>

// WS2812 RGB LED en GPIO 8 (ESP32-C3 SuperMini Plus)
#define LED_GPIO        GPIO_NUM_8
#define RMT_RESOLUTION  10000000  // 10 MHz → 100 ns por tick

static const char *TAG = "LED";

static rmt_channel_handle_t tx_chan = NULL;
static rmt_encoder_handle_t encoder = NULL;
static TaskHandle_t led_task_handle = NULL;

static uint8_t s_r = 0, s_g = 0, s_b = 0;
static bool led_on_state = false;
static int blink_on_ms = 0;
static int blink_off_ms = 0;
static bool solid = false;
static bool override_led = false;

static void led_set_on(void) {
    // WS2812 espera GRB
    uint8_t grb[3] = { s_g, s_r, s_b };
    rmt_transmit(tx_chan, encoder, grb, sizeof(grb), &(rmt_transmit_config_t){ .loop_count = 0 });
    rmt_tx_wait_all_done(tx_chan, portMAX_DELAY);
}

static void led_set_off(void) {
    uint8_t off[3] = { 0, 0, 0 };
    rmt_transmit(tx_chan, encoder, off, sizeof(off), &(rmt_transmit_config_t){ .loop_count = 0 });
    rmt_tx_wait_all_done(tx_chan, portMAX_DELAY);
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
    // Canal TX RMT
    rmt_tx_channel_config_t tx_cfg = {
        .gpio_num = LED_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RMT_RESOLUTION,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_cfg, &tx_chan));

    // Encoder de bytes con timings WS2812 (a 10 MHz, 1 tick = 100 ns)
    rmt_bytes_encoder_config_t enc_cfg = {
        .bit0 = {
            .duration0 = 3,   // T0H = 300 ns
            .level0 = 1,
            .duration1 = 9,   // T0L = 900 ns
            .level1 = 0,
        },
        .bit1 = {
            .duration0 = 9,   // T1H = 900 ns
            .level0 = 1,
            .duration1 = 3,   // T1L = 300 ns
            .level1 = 0,
        },
        .flags.msb_first = true,
    };
    ESP_ERROR_CHECK(rmt_new_bytes_encoder(&enc_cfg, &encoder));
    ESP_ERROR_CHECK(rmt_enable(tx_chan));

    led_set_off();
    ESP_LOGI(TAG, "WS2812 RGB LED inicializado en GPIO %d (RMT nativo)", LED_GPIO);

    xTaskCreate(led_task, "led_task", 2048, NULL, 5, &led_task_handle);
}

void app_led_update_state(system_state_t state) {
    switch (state) {
        case STATE_NO_CONFIG:
            s_r = 255; s_g = 128; s_b = 0;
            led_on_state = true;
            blink_on_ms = 1000; blink_off_ms = 1000;
            solid = false;
            break;
        case STATE_AP_MODE:
            s_r = 0; s_g = 0; s_b = 255;
            led_on_state = true;
            blink_on_ms = 1000; blink_off_ms = 1000;
            solid = false;
            break;
        case STATE_CONNECTING:
            s_r = 0; s_g = 0; s_b = 255;
            led_on_state = true;
            blink_on_ms = 200; blink_off_ms = 200;
            solid = false;
            break;
        case STATE_NORMAL:
            s_r = 0; s_g = 0; s_b = 64;
            led_on_state = true;
            solid = true;
            blink_on_ms = 0; blink_off_ms = 0;
            break;
        case STATE_HTTP_REQ:
            s_r = 0; s_g = 128; s_b = 255;
            led_on_state = true;
            blink_on_ms = 150; blink_off_ms = 150;
            solid = false;
            break;
        case STATE_RESET_WARNING:
            s_r = 255; s_g = 0; s_b = 0;
            led_on_state = true;
            blink_on_ms = 100; blink_off_ms = 100;
            solid = false;
            break;
        case STATE_FACTORY_RESET:
            s_r = 255; s_g = 0; s_b = 0;
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
    s_r = 0; s_g = 255; s_b = 0;
    led_set_on();  vTaskDelay(pdMS_TO_TICKS(1000));
    led_set_off();
    override_led = false;
    notify_led_task();
}

void app_led_signal_error(void) {
    override_led = true;
    notify_led_task();
    uint8_t prev_r = s_r, prev_g = s_g, prev_b = s_b;
    s_r = 255; s_g = 0; s_b = 0;
    for (int i = 0; i < 3; i++) {
        led_set_on();  vTaskDelay(pdMS_TO_TICKS(100));
        led_set_off(); vTaskDelay(pdMS_TO_TICKS(100));
    }
    s_r = prev_r; s_g = prev_g; s_b = prev_b;
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
    s_r = r; s_g = g; s_b = b;
    led_on_state = (r > 0 || g > 0 || b > 0);
    solid = true;
    blink_on_ms = 0; blink_off_ms = 0;
    notify_led_task();
}

void app_led_off(void) {
    solid = false;
    blink_on_ms = 0; blink_off_ms = 0;
    led_on_state = false;
    s_r = 0; s_g = 0; s_b = 0;
    notify_led_task();
}
