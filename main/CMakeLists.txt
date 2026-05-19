idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    PRIV_REQUIRES display sd_card ble_keyboard editor wifi_manager git_sync standby battery touchscreen nvs_flash bt esp_hid driver
)
