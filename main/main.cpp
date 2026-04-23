#include <cstdio>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
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

#if defined(CONFIG_DRAFTLING_MODEL_SEEED_RETERMINAL_E1001) || \
    defined(CONFIG_DRAFTLING_MODEL_WAVESHARE_EPD_HAT) || \
    defined(CONFIG_DRAFTLING_MODEL_M5STACK_PAPERS3)
    /* E-paper retains its image without power. Wipe the panel to a
     * clean white frame so the user does not see the editor frozen on
     * the display while the MCU is in deep sleep. We do this even when
     * CONFIG_DRAFTLING_EPD_BLACK_BACKGROUND is enabled -- the panel is
     * easier to read at a glance when blank, and a black frame held
     * indefinitely is worse for the e-paper than a white one.
     *
     * Take the LVGL mutex first so this does not race with the LVGL
     * task's flush_cb (the PaperS3 backend in particular requires
     * exclusive access to its M5GFX instance). */
    if (lvgl_port_lock(-1)) {
        display_clear(0xFF);
        display_full_refresh();
        lvgl_port_unlock();
    }
#endif

#if defined(CONFIG_DRAFTLING_MODEL_WAVESHARE_EPD_HAT)
    /* Drop the e-paper PWR rail so the panel draws no current while
     * the MCU is in deep sleep. The image already on the panel is
     * retained without power. */
    if (EPD_PWR_PIN >= 0) {
        gpio_set_level((gpio_num_t)EPD_PWR_PIN, 0);
    }
#endif
}

extern "C" void app_main(void)
{
#if defined(CONFIG_DRAFTLING_MODEL_WAVESHARE_RLCD42)
    ESP_LOGI(TAG, "Draftling - Waveshare ESP32-S3-RLCD-4.2");
#elif defined(CONFIG_DRAFTLING_MODEL_SEEED_RETERMINAL_E1001)
    ESP_LOGI(TAG, "Draftling - Seeed Studio reTerminal E1001");
#elif defined(CONFIG_DRAFTLING_MODEL_WAVESHARE_EPD_HAT)
    ESP_LOGI(TAG, "Draftling - Waveshare E-Paper Driver HAT (%dx%d)",
             DISPLAY_WIDTH, DISPLAY_HEIGHT);
#elif defined(CONFIG_DRAFTLING_MODEL_M5STACK_PAPERS3)
    ESP_LOGI(TAG, "Draftling - M5Stack PaperS3");
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
                 RLCD_CS_PIN, RLCD_RST_PIN, -1,
                 DISPLAY_WIDTH, DISPLAY_HEIGHT);
#elif defined(CONFIG_DRAFTLING_MODEL_SEEED_RETERMINAL_E1001) || \
      defined(CONFIG_DRAFTLING_MODEL_WAVESHARE_EPD_HAT)
#  if defined(CONFIG_DRAFTLING_MODEL_WAVESHARE_EPD_HAT)
    /* Power the e-paper panel on before initialising the SPI driver.
     * EPD_PWR_PIN gates the panel's power rail on the Waveshare HAT;
     * driving it high here turns the panel on, and the standby
     * pre-sleep callback drops it again before deep sleep. -1 means
     * the user wired PWR permanently high and no MCU pin is needed. */
    if (EPD_PWR_PIN >= 0) {
        gpio_config_t pwr_io = {};
        pwr_io.pin_bit_mask = 1ULL << EPD_PWR_PIN;
        pwr_io.mode         = GPIO_MODE_OUTPUT;
        pwr_io.pull_up_en   = GPIO_PULLUP_DISABLE;
        pwr_io.pull_down_en = GPIO_PULLDOWN_DISABLE;
        pwr_io.intr_type    = GPIO_INTR_DISABLE;
        gpio_config(&pwr_io);
        gpio_set_level((gpio_num_t)EPD_PWR_PIN, 1);
    }
#  endif
    display_init(EPD_MOSI_PIN, EPD_SCK_PIN, EPD_DC_PIN,
                 EPD_CS_PIN, EPD_RST_PIN, EPD_BUSY_PIN,
                 DISPLAY_WIDTH, DISPLAY_HEIGHT);
#elif defined(CONFIG_DRAFTLING_MODEL_M5STACK_PAPERS3)
    /* The PaperS3 driver is a thin shim over M5GFX which configures
     * all panel GPIOs internally based on the M5PaperS3 board id. We
     * still call display_init for API parity; pin parameters are
     * ignored by the M5GFX backend. */
    display_init(-1, -1, -1, -1, -1, -1, DISPLAY_WIDTH, DISPLAY_HEIGHT);
#endif

    /* Initialize LVGL.
     *
     * LVGL renders in *logical* pixels (panel size / DISPLAY_SCALE).
     * The display backend is responsible for scaling each logical
     * pixel to a SCALE x SCALE block of physical panel pixels. With
     * SCALE = 1 the logical and panel dimensions are identical. */
    ESP_LOGI(TAG, "Initializing LVGL...");
    lvgl_port_init(DISPLAY_LOGICAL_WIDTH, DISPLAY_LOGICAL_HEIGHT, DISPLAY_ROTATE);

    /* Initialize battery voltage monitor before the UI so the editor
     * status bar can show the battery level immediately. battery_init
     * is a no-op when BATT_ADC_PIN is < 0 (HAT and PaperS3 cases). */
    ESP_LOGI(TAG, "Initializing battery monitor...");
    battery_init(BATT_ADC_PIN, BATT_EN_PIN, BATT_DIVIDER);

    /* Create editor UI */
    ESP_LOGI(TAG, "Creating editor UI...");
    if (lvgl_port_lock(-1)) {
        editor_ui_init();
        lvgl_port_unlock();
    }

    /* Initialize SD card */
    ESP_LOGI(TAG, "Initializing SD card...");
    esp_err_t sd_ret = ESP_FAIL;
#if defined(CONFIG_DRAFTLING_MODEL_WAVESHARE_RLCD42)
    sd_ret = sd_card_init(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN, SD_MOUNT_POINT);
#elif defined(CONFIG_DRAFTLING_MODEL_SEEED_RETERMINAL_E1001)
    /* The e-paper driver has already initialized the SPI2 bus, so we
     * pass -1 for sck/mosi and just attach the SD card as a second
     * device on that bus. */
    sd_ret = sd_card_init_spi(SPI2_HOST,
                              SD_SPI_MISO_PIN, -1, -1,
                              SD_SPI_CS_PIN, SD_EN_PIN,
                              SD_MOUNT_POINT);
#elif defined(CONFIG_DRAFTLING_MODEL_WAVESHARE_EPD_HAT)
#  if defined(CONFIG_DRAFTLING_HAT_SD_SPI)
    /* HAT has no on-board SD; user opted in to a separate SD on SPI3
     * with its own dedicated pinout. */
    sd_ret = sd_card_init_spi(SPI3_HOST,
                              SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_SCK_PIN,
                              SD_SPI_CS_PIN, SD_EN_PIN,
                              SD_MOUNT_POINT);
#  elif defined(CONFIG_DRAFTLING_HAT_SD_SDMMC)
    /* SDMMC 1-bit (CLK/CMD/D0) on the chip's dedicated SDMMC peripheral.
     * Faster than SPI; available on ESP32 and ESP32-S3 only. Pinout is
     * user-configurable via the HAT pinout menu and defaults to the
     * Freenove ESP32-S3 example wiring (CLK=14, CMD=15, D0=2). */
    sd_ret = sd_card_init(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN, SD_MOUNT_POINT);
#  else
    ESP_LOGW(TAG, "HAT model built without SD support "
                  "(CONFIG_DRAFTLING_HAT_SD_NONE)");
    sd_ret = ESP_ERR_NOT_SUPPORTED;
#  endif
#elif defined(CONFIG_DRAFTLING_MODEL_M5STACK_PAPERS3)
    /* PaperS3 has an on-board MicroSD on its own SPI3 bus. */
    sd_ret = sd_card_init_spi(SPI3_HOST,
                              SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_SCK_PIN,
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

    /* WiFi is lazy-initialized on first wifi_manager_connect() call.
     * On boards with a tight internal heap (e.g. M5Stack PaperS3, ~138 KB
     * free after M5GFX statics) eagerly calling esp_wifi_init() here
     * fails with ESP_ERR_NO_MEM because the WiFi static RX/TX buffers
     * (DMA-capable, must live in internal RAM) cannot fit alongside the
     * Bluedroid stack that ble_keyboard_init() just brought up. */

    /* Initialize Git sync */
    ESP_LOGI(TAG, "Initializing Git sync...");
    git_sync_init();

    /* Initialize standby manager (deep sleep on inactivity) */
    ESP_LOGI(TAG, "Initializing standby manager...");
    standby_init();
    standby_set_pre_sleep_cb(pre_sleep_autosave);

    ESP_LOGI(TAG, "Draftling ready. Waiting for Bluetooth keyboard...");
}
