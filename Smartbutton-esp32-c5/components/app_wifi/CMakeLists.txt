idf_component_register(SRCS "app_wifi.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_wifi lwip app_core app_nvs cjson app_dns app_btn_leds)
