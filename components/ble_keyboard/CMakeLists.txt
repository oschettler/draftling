## ble_keyboard
##
## On targets without a usable Bluedroid host (e.g. an ESP-IDF port
## that compiles `bt` only as controller stubs, or an sdkconfig with
## Bluedroid turned off) the host-side headers (`esp_bt_main.h`,
## `esp_gap_ble_api.h`, `esp_gattc_api.h`, ...) are not installed by
## the `bt` component and the `esp_hid` component is not built. The
## source file already compiles to no-op stubs in that case (see
## ble_keyboard.cpp `#if !defined(CONFIG_BT_BLUEDROID_ENABLED)`), but
## the CMake-level requires must also be conditional on the same
## symbol or the project fails to configure.
##
## When CONFIG_BT_BLUEDROID_ENABLED is set but CONFIG_BT_CONTROLLER_DISABLED
## is also set (the Tab5 / ESP32-P4 build), we additionally need
## the `espressif/esp_hosted` component for the hosted-VHCI HCI
## driver that gets attached to Bluedroid in ble_keyboard_init().
##
## On ESP32-P4 specifically we MUST have the Bluedroid host enabled
## (controller comes from the on-board C6 via ESP-Hosted VHCI). If
## sdkconfig has CONFIG_BT_BLUEDROID_ENABLED=n on this target, the
## BLE keyboard silently collapses to a no-op stub and the user
## loses keyboard input. Fail the build early with an actionable
## message instead -- the most common cause is a stale sdkconfig
## that did not pick up sdkconfig.defaults.esp32p4 (defaults are
## only applied to a freshly-created sdkconfig).
if(CONFIG_IDF_TARGET_ESP32P4 AND NOT CONFIG_BT_BLUEDROID_ENABLED)
    message(FATAL_ERROR
        "Tab5 (ESP32-P4) requires CONFIG_BT_BLUEDROID_ENABLED=y for the "
        "BLE keyboard over ESP-Hosted VHCI, but sdkconfig has it unset. "
        "Your sdkconfig is likely stale: delete sdkconfig + sdkconfig.old "
        "and rerun `idf.py set-target esp32p4` so sdkconfig.defaults.esp32p4 "
        "is reapplied. See the Bluetooth block in sdkconfig.defaults.esp32p4 "
        "for details.")
endif()

set(BLE_KB_REQS nvs_flash)
if(CONFIG_BT_BLUEDROID_ENABLED)
    list(APPEND BLE_KB_REQS bt esp_hid)
    if(CONFIG_BT_CONTROLLER_DISABLED)
        list(APPEND BLE_KB_REQS espressif__esp_hosted)
    endif()
endif()

idf_component_register(
    SRCS "ble_keyboard.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES ${BLE_KB_REQS}
)
