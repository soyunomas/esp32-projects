idf_component_register(
    SRCS "csi_traffic.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_netif lwip
)
