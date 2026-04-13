idf_component_register(SRCS "app_web.c"
INCLUDE_DIRS "include"
REQUIRES esp_http_server app_wifi app_nvs app_core app_http app_mqtt app_ws
PRIV_REQUIRES cjson esp_system mbedtls app_update spi_flash)
