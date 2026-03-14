idf_component_register(SRCS "app_http.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_http_client app_nvs app_led app_core
                       PRIV_REQUIRES mbedtls)
