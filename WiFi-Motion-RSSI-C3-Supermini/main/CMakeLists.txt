idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_event esp_netif esp_timer nvs_flash esp_driver_gpio esp_driver_usb_serial_jtag motion_detector app_config sample_metrics event_marker recovery_button csi_capture csi_traffic captive_dns config_portal telegram_notifier
)
