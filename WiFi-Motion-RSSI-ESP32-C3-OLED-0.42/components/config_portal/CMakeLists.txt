idf_component_register(
    SRCS "config_portal.c"
    INCLUDE_DIRS "include"
    REQUIRES app_config motion_detector portal_auth telegram_notifier esp_event esp_http_server esp_wifi esp_timer espressif__cjson
)
