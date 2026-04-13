#pragma once

void app_btn_leds_init(void);
void app_btn_leds_on(int btn_id);
void app_btn_leds_off(int btn_id);
void app_btn_leds_off_all(void);
void app_btn_leds_blink(int btn_id, int count, int on_ms, int off_ms);
void app_btn_leds_wifi_connecting_start(void);
void app_btn_leds_wifi_connecting_stop(void);
void app_btn_leds_wifi_result(bool success);
