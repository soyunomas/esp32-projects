idf_component_register(
    SRCS "probe_config_codec.c" "probe_config_nvs.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash reference_selector reference_store
)
