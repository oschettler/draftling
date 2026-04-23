idf_component_register(
    SRCS "standby.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES nvs_flash esp_timer driver ble_keyboard
)
