idf_component_register(SRCS "app_led.c"
                       INCLUDE_DIRS "include"
                       REQUIRES app_core
                       PRIV_REQUIRES led_strip)
