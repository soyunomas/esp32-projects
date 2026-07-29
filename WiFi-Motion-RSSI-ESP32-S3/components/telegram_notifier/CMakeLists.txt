idf_component_register(
    SRCS "telegram_notifier.c" "telegram_validation.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_client esp_timer mbedtls nvs_flash espressif__cjson
)
