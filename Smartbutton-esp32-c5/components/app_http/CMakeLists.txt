idf_component_register(SRCS "app_http.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_http_client app_nvs app_led app_core app_btn_leds
                       PRIV_REQUIRES mbedtls)
