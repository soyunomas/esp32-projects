idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_event esp_netif esp_timer nvs_flash esp_driver_gpio motion_detector app_config
)
