idf_component_register(
    SRCS "portal_auth.c"
    INCLUDE_DIRS "include"
    REQUIRES mbedtls nvs_flash esp_timer
)
