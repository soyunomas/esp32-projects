idf_component_register(
    SRCS "app_config.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash motion_detector
)
