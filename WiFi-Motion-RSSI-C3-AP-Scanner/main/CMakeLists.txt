idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_event
        esp_driver_gpio
        esp_timer
        esp_wifi
        maintenance_portal
        nvs_flash
        multiref_detector
        observation_store
        probe_config
        event_marker
        probe_format
        reference_selector
        reference_store
        scan_scheduler
)
