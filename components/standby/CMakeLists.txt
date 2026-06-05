idf_component_register(
    SRCS "standby.cpp"
    INCLUDE_DIRS "include"
    ## ESP-IDF 6.0 removed the legacy `driver` component's public
    ## dependencies on the split `esp_driver_*` components (see the
    ## 6.0 peripherals migration guide). standby.cpp includes
    ## `driver/gpio.h` (now in esp_driver_gpio) and
    ## `driver/rtc_io.h` (still in the legacy `driver` component).
    PRIV_REQUIRES nvs_flash esp_timer driver esp_driver_gpio ble_keyboard display touchscreen power lvgl__lvgl
)
idf_component_optional_requires(PRIVATE usb_kbd)
