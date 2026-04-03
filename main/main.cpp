#include <cstdio>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <nvs_flash.h>

#include "app_config.h"
#include "display.h"
#include "lvgl_port.h"
#include "sd_card.h"
#include "bt_keyboard.h"
#include "editor.h"
#include "editor_ui.h"
#include "wifi_manager.h"
#include "git_sync.h"

static const char *TAG = "WriterDeck";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "WriterDeck - Markdown Editor for ESP32-S3-RLCD-4.2");

    /* Initialize NVS - required for WiFi and BT */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Initialize SD card */
    ESP_LOGI(TAG, "Initializing SD card...");
    sd_card_init(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN, SD_MOUNT_POINT);

    /* Initialize display */
    ESP_LOGI(TAG, "Initializing display...");
    display_init(RLCD_MOSI_PIN, RLCD_SCK_PIN, RLCD_DC_PIN,
                 RLCD_CS_PIN, RLCD_RST_PIN, LCD_WIDTH, LCD_HEIGHT);

    /* Initialize LVGL */
    ESP_LOGI(TAG, "Initializing LVGL...");
    lvgl_port_init(LCD_WIDTH, LCD_HEIGHT);

    /* Create editor UI */
    ESP_LOGI(TAG, "Creating editor UI...");
    if (lvgl_port_lock(-1)) {
        editor_ui_init();
        lvgl_port_unlock();
    }

    /* Initialize Bluetooth keyboard */
    ESP_LOGI(TAG, "Initializing Bluetooth keyboard...");
    bt_keyboard_init();

    /* Initialize WiFi manager (doesn't connect yet) */
    ESP_LOGI(TAG, "Initializing WiFi manager...");
    wifi_manager_init();

    /* Initialize Git sync */
    ESP_LOGI(TAG, "Initializing Git sync...");
    git_sync_init();

    ESP_LOGI(TAG, "WriterDeck ready. Waiting for Bluetooth keyboard...");
}
