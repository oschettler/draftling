idf_component_register(
    SRCS "git_sync.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_http_client sd_card wifi_manager nvs_flash
)
