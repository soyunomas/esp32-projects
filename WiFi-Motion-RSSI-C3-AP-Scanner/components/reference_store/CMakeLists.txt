idf_component_register(
    SRCS "reference_store_codec.c" "reference_store_nvs.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash reference_selector
)
