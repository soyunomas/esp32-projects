idf_component_register(SRCS "app_mqtt.c"
                       INCLUDE_DIRS "include"
                       REQUIRES mqtt app_nvs app_core app_led app_btn_leds)
