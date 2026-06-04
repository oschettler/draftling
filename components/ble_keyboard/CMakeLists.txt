## ble_keyboard
##
## On targets without a usable native Bluetooth controller (e.g.
## ESP32-P4, whose BLE arrives via an ESP32-C6 co-processor and
## ESP-Hosted rather than on-chip) the ESP-IDF `bt` and `esp_hid`
## components are not part of the build. The source file already
## compiles to no-op stubs in that case (see ble_keyboard.cpp
## #if !defined(CONFIG_BT_ENABLED)), but the CMake-level requires
## must also be conditional or the project fails to configure.
set(BLE_KB_REQS nvs_flash)
if(CONFIG_BT_ENABLED)
    list(APPEND BLE_KB_REQS bt esp_hid)
endif()

idf_component_register(
    SRCS "ble_keyboard.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES ${BLE_KB_REQS}
)
