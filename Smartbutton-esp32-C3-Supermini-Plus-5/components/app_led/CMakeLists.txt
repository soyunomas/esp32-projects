idf_component_register(SRCS "app_led.c"
                       INCLUDE_DIRS "include"
                       REQUIRES app_core esp_driver_gpio esp_driver_rmt)
