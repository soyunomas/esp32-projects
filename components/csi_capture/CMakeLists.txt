idf_component_register(
    SRCS "csi_capture.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_timer csi_features
)
