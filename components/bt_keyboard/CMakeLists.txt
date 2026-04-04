idf_component_register(
    SRCS "bt_keyboard.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES bt nvs_flash esp_hid
)
