idf_component_register(SRCS "app_ws.c"
                       INCLUDE_DIRS "include"
                       REQUIRES app_nvs app_core app_led app_btn_leds esp_websocket_client)
