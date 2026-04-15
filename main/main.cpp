#include <cstdio>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include "sdkconfig.h"

#include "app_config.h"
#include "display.h"
#include "lvgl_port.h"
#include "sd_card.h"
#include "ble_keyboard.h"
#include "editor.h"
#include "editor_ui.h"
#include "wifi_manager.h"
#include "git_sync.h"
#include "standby.h"

static const char *TAG = "Draftling";

extern "C" void app_main(void)
{
#if defined(CONFIG_DRAFTLING_MODEL_WAVESHARE_RLCD42)
    ESP_LOGI(TAG, "Draftling - Waveshare ESP32-S3-RLCD-4.2");
#endif

    /* Initialize NVS - required for WiFi and BT */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Initialize display */
    ESP_LOGI(TAG, "Initializing display...");
#if defined(CONFIG_DRAFTLING_MODEL_WAVESHARE_RLCD42)
    display_init(RLCD_MOSI_PIN, RLCD_SCK_PIN, RLCD_DC_PIN,
                 RLCD_CS_PIN, RLCD_RST_PIN, DISPLAY_WIDTH, DISPLAY_HEIGHT);
#endif

    /* Initialize LVGL */
    ESP_LOGI(TAG, "Initializing LVGL...");
    lvgl_port_init(DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_ROTATE);

    /* Create editor UI */
    ESP_LOGI(TAG, "Creating editor UI...");
    if (lvgl_port_lock(-1)) {
        editor_ui_init();
        lvgl_port_unlock();
    }

    /* Initialize SD card */
    ESP_LOGI(TAG, "Initializing SD card...");
    esp_err_t sd_ret = sd_card_init(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN, SD_MOUNT_POINT);
    if (sd_ret != ESP_OK) {
        ESP_LOGE(TAG, "SD card init failed: %s", esp_err_to_name(sd_ret));
        if (lvgl_port_lock(-1)) {
            editor_ui_set_status("ERROR: SD card not ready");
            lvgl_port_unlock();
        }
    }

    /* Initialize Bluetooth keyboard */
    ESP_LOGI(TAG, "Initializing Bluetooth keyboard...");
    ble_keyboard_init();

    /* Prompt user to connect keyboard if not already connected */
    if (!ble_keyboard_is_connected()) {
        if (lvgl_port_lock(-1)) {
            editor_ui_set_status("No keyboard connected. Scanning for BLE devices...");
            lvgl_port_unlock();
        }
    }

    /* Initialize WiFi manager (doesn't connect yet) */
    ESP_LOGI(TAG, "Initializing WiFi manager...");
    wifi_manager_init();

    /* Initialize Git sync */
    ESP_LOGI(TAG, "Initializing Git sync...");
    git_sync_init();

    /* Initialize standby manager (deep sleep on inactivity) */
    ESP_LOGI(TAG, "Initializing standby manager...");
    standby_init();

    ESP_LOGI(TAG, "Draftling ready. Waiting for Bluetooth keyboard...");
}
