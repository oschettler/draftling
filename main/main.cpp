#include <cstdio>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <driver/spi_common.h>
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
#include "battery.h"

static const char *TAG = "Draftling";

/* Called by standby just before entering deep sleep */
static void pre_sleep_autosave(void)
{
    if (editor_is_modified() && editor_get_file_path()) {
        ESP_LOGI(TAG, "Auto-saving before deep sleep...");
        esp_err_t err = editor_save_file();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Auto-save failed: %s", esp_err_to_name(err));
        }
    }
}

extern "C" void app_main(void)
{
#if defined(CONFIG_DRAFTLING_MODEL_WAVESHARE_RLCD42)
    ESP_LOGI(TAG, "Draftling - Waveshare ESP32-S3-RLCD-4.2");
#elif defined(CONFIG_DRAFTLING_MODEL_SEEED_RETERMINAL_E1001)
    ESP_LOGI(TAG, "Draftling - Seeed Studio reTerminal E1001");
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
#elif defined(CONFIG_DRAFTLING_MODEL_SEEED_RETERMINAL_E1001)
    display_init(EPD_MOSI_PIN, EPD_SCK_PIN, EPD_DC_PIN,
                 EPD_CS_PIN, EPD_RST_PIN, DISPLAY_WIDTH, DISPLAY_HEIGHT);
#endif

    /* Initialize LVGL */
    ESP_LOGI(TAG, "Initializing LVGL...");
    lvgl_port_init(DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_ROTATE);

#if defined(CONFIG_DRAFTLING_MODEL_WAVESHARE_RLCD42) || \
    defined(CONFIG_DRAFTLING_MODEL_SEEED_RETERMINAL_E1001)
    /* Initialize battery voltage monitor before the UI so the editor
     * status bar can show the battery level immediately. */
    ESP_LOGI(TAG, "Initializing battery monitor...");
    battery_init(BATT_ADC_PIN, BATT_EN_PIN, BATT_DIVIDER);
#endif

    /* Create editor UI */
    ESP_LOGI(TAG, "Creating editor UI...");
    if (lvgl_port_lock(-1)) {
        editor_ui_init();
        lvgl_port_unlock();
    }

    /* Initialize SD card */
    ESP_LOGI(TAG, "Initializing SD card...");
#if defined(CONFIG_DRAFTLING_MODEL_WAVESHARE_RLCD42)
    esp_err_t sd_ret = sd_card_init(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN, SD_MOUNT_POINT);
#elif defined(CONFIG_DRAFTLING_MODEL_SEEED_RETERMINAL_E1001)
    /* The e-paper driver has already initialized the SPI2 bus, so we
     * pass -1 for sck/mosi and just attach the SD card as a second
     * device on that bus. */
    esp_err_t sd_ret = sd_card_init_spi(SPI2_HOST,
                                        SD_SPI_MISO_PIN, -1, -1,
                                        SD_SPI_CS_PIN, SD_EN_PIN,
                                        SD_MOUNT_POINT);
#endif
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

    /* Initialize WiFi manager (doesn't connect yet) */
    ESP_LOGI(TAG, "Initializing WiFi manager...");
    wifi_manager_init();

    /* Initialize Git sync */
    ESP_LOGI(TAG, "Initializing Git sync...");
    git_sync_init();

    /* Initialize standby manager (deep sleep on inactivity) */
    ESP_LOGI(TAG, "Initializing standby manager...");
    standby_init();
    standby_set_pre_sleep_cb(pre_sleep_autosave);

    ESP_LOGI(TAG, "Draftling ready. Waiting for Bluetooth keyboard...");
}
