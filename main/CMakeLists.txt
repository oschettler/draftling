idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    PRIV_REQUIRES display sd_card bt_keyboard editor wifi_manager git_sync nvs_flash
)
