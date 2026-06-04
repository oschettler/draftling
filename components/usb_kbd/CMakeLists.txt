idf_component_register(
    SRCS "usb_kbd.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES ble_keyboard usb driver
)
