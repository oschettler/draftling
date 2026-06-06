set(touch_reqs driver esp_driver_i2c esp_driver_gpio lvgl__lvgl)

## On the M5Stack Tab5 (ESP32-P4) the touchscreen backend delegates
## to the espressif/m5stack_tab5 BSP's bsp_touch_new() and polls
## through the generic esp_lcd_touch API, so both v1 (GT911) and
## v2 (ST7123) variants work transparently. Pull in the BSP and
## the esp_lcd_touch core only on that target; non-Tab5 builds
## stay on the existing direct-I2C GT911 / AXS5106L paths and do
## not need either component.
if(CONFIG_DRAFTLING_MODEL_M5STACK_TAB5)
    list(APPEND touch_reqs espressif__m5stack_tab5 espressif__esp_lcd_touch)
endif()

idf_component_register(
    SRCS "touchscreen.cpp"
    INCLUDE_DIRS "include"
    REQUIRES ${touch_reqs}
)
