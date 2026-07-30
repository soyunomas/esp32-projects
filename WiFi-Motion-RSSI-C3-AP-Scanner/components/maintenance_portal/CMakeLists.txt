idf_component_register(
    SRCS "maintenance_portal.c"
    INCLUDE_DIRS "include"
    REQUIRES captive_dns esp_http_server esp_netif esp_timer esp_wifi mbedtls
             motion_history multiref_detector probe_config reference_store
)
