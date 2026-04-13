idf_component_register(SRCS "app_buttons.c"
INCLUDE_DIRS "include"
REQUIRES esp_driver_gpio app_core app_led app_nvs app_http app_mqtt)
