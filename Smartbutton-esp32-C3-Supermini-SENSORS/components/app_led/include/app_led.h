#pragma once
#include "app_core.h"
#include <stdint.h>

void app_led_init(void);
void app_led_update_state(system_state_t state);
void app_led_signal_success(void);
void app_led_signal_error(void);
void app_led_set_blink(int on_ms, int off_ms);
void app_led_set_color(uint8_t r, uint8_t g, uint8_t b);
void app_led_off(void);
