#include <cstdio>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <driver/spi_common.h>
#include <driver/gpio.h>
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
    /* Persist cursor/scroll metadata even when the document body
     * itself is unmodified, so reopening the file resumes at the
     * last position. No-op if no file is currently open. */
    editor_save_meta();

#if defined(CONFIG_DRAFTLING_MODEL_M5STACK_PAPERS3)
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
    /* Take the LVGL mutex if we can so the wipe does not race with
     * the LVGL task's flush_cb. The mutex is recursive
     * (lvgl_port_init), so this also works when pre_sleep_autosave
     * runs inside the LVGL task itself (the "Sleep now" menu path).
     * If for any reason the lock cannot be obtained quickly, wipe
     * anyway -- a clean white frame on the panel matters more than
     * the slim chance of a flush_cb collision right before deep
     * sleep. */
    bool locked = lvgl_port_lock(200);
    display_clear(0xFF);
    display_full_refresh();
    if (locked) {
        lvgl_port_unlock();
    } else {
        ESP_LOGW(TAG, "pre_sleep wipe: LVGL lock not acquired, proceeded anyway");
    }
#endif
}

extern "C" void app_main(void)
{
#if defined(CONFIG_DRAFTLING_MODEL_WAVESHARE_RLCD42)
    ESP_LOGI(TAG, "Draftling - Waveshare ESP32-S3-RLCD-4.2");
#elif defined(CONFIG_DRAFTLING_MODEL_M5STACK_PAPERS3)
    ESP_LOGI(TAG, "Draftling - M5Stack PaperS3");
#elif defined(CONFIG_DRAFTLING_MODEL_WAVESHARE_TOUCH_LCD_349)
    ESP_LOGI(TAG, "Draftling - Waveshare ESP32-S3-Touch-LCD-3.49");
#elif defined(CONFIG_DRAFTLING_MODEL_JC3248W535)
    ESP_LOGI(TAG, "Draftling - Guition JC3248W535");
#elif defined(CONFIG_DRAFTLING_MODEL_LILYGO_TDISPLAY_S3)
    ESP_LOGI(TAG, "Draftling - LilyGO T-Display-S3");
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
#elif defined(CONFIG_DRAFTLING_MODEL_M5STACK_PAPERS3)
    /* The PaperS3 driver is a thin shim over M5GFX which configures
     * all panel GPIOs internally based on the M5PaperS3 board id. We
     * still call display_init for API parity; pin parameters are
     * ignored by the M5GFX backend. */
    display_init(-1, -1, -1, -1, -1, -1, DISPLAY_WIDTH, DISPLAY_HEIGHT);
#elif defined(CONFIG_DRAFTLING_DISPLAY_AXS15231B)
    /* AXS15231B QSPI color LCD. Needs 9 GPIOs (CS/SCK/D0..D3/RST/TE/BL),
     * which do not fit in display_init()'s 6 pin slots, so the backend
     * exposes a dedicated struct-based init. */
    {
        display_axs15231b_config_t cfg = {};
        cfg.cs     = LCD_QSPI_CS_PIN;
        cfg.sck    = LCD_QSPI_SCK_PIN;
        cfg.d0     = LCD_QSPI_D0_PIN;
        cfg.d1     = LCD_QSPI_D1_PIN;
        cfg.d2     = LCD_QSPI_D2_PIN;
        cfg.d3     = LCD_QSPI_D3_PIN;
        cfg.rst    = LCD_RST_PIN;
        cfg.te     = LCD_TE_PIN;
        cfg.bl     = LCD_BL_PIN;
        cfg.width  = DISPLAY_WIDTH;
        cfg.height = DISPLAY_HEIGHT;
        display_axs15231b_init(&cfg);
    }
#elif defined(CONFIG_DRAFTLING_DISPLAY_ST7789)
    /* ST7789 over an 8-bit i80 parallel bus. The LilyGO T-Display-S3
     * gates the LCD's 3V3 rail with a power-enable transistor on
     * GPIO15: it must be driven HIGH before the controller will
     * respond, otherwise the panel-init reset pulse times out. */
#if defined(LCD_PWR_EN_PIN) && (LCD_PWR_EN_PIN >= 0)
    {
        gpio_config_t pwr = {};
        pwr.intr_type    = GPIO_INTR_DISABLE;
        pwr.mode         = GPIO_MODE_OUTPUT;
        pwr.pin_bit_mask = (1ULL << LCD_PWR_EN_PIN);
        gpio_config(&pwr);
        gpio_set_level((gpio_num_t)LCD_PWR_EN_PIN, 1);
    }
#endif
#if defined(LCD_RD_PIN) && (LCD_RD_PIN >= 0)
    /* The i80 driver does not toggle RD, but the panel samples it on
     * reset and will NACK if it is left floating low. Drive it HIGH. */
    {
        gpio_config_t rd = {};
        rd.intr_type    = GPIO_INTR_DISABLE;
        rd.mode         = GPIO_MODE_OUTPUT;
        rd.pin_bit_mask = (1ULL << LCD_RD_PIN);
        gpio_config(&rd);
        gpio_set_level((gpio_num_t)LCD_RD_PIN, 1);
    }
#endif
    {
        display_st7789_config_t cfg = {};
        cfg.data[0] = LCD_D0_PIN;
        cfg.data[1] = LCD_D1_PIN;
        cfg.data[2] = LCD_D2_PIN;
        cfg.data[3] = LCD_D3_PIN;
        cfg.data[4] = LCD_D4_PIN;
        cfg.data[5] = LCD_D5_PIN;
        cfg.data[6] = LCD_D6_PIN;
        cfg.data[7] = LCD_D7_PIN;
        cfg.wr      = LCD_WR_PIN;
        cfg.dc      = LCD_DC_PIN;
        cfg.cs      = LCD_CS_PIN;
        cfg.rst     = LCD_RST_PIN;
        cfg.bl      = LCD_BL_PIN;
        cfg.width   = DISPLAY_WIDTH;
        cfg.height  = DISPLAY_HEIGHT;
        /* T-Display-S3: 240x320 panel cropped to 170x320 with a
         * 35-pixel column gap; landscape orientation swaps XY and
         * mirrors X. ST7789 panels need INVON for correct colors. */
        cfg.x_gap        = 0;
        cfg.y_gap        = 35;
        cfg.swap_xy      = true;
        cfg.mirror_x     = true;
        cfg.mirror_y     = false;
        cfg.invert_color = true;
        display_st7789_init(&cfg);
    }
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
     * is a no-op when BATT_ADC_PIN is < 0 (HAT case). */
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
#elif defined(CONFIG_DRAFTLING_MODEL_M5STACK_PAPERS3)
    /* PaperS3 has an on-board MicroSD on its own SPI3 bus. */
    sd_ret = sd_card_init_spi(SPI3_HOST,
                              SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_SCK_PIN,
                              SD_SPI_CS_PIN, SD_EN_PIN,
                              SD_MOUNT_POINT);
#elif defined(CONFIG_DRAFTLING_DISPLAY_AXS15231B)
    /* The AXS15231B color-LCD boards (Waveshare 3.49, JC3248W535)
     * carry the SD card on a SPI bus separate from the QSPI display
     * bus (the display owns SPI2_HOST, so the SD slot uses SPI3). */
    sd_ret = sd_card_init_spi(SPI3_HOST,
                              SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_SCK_PIN,
                              SD_SPI_CS_PIN, SD_EN_PIN,
                              SD_MOUNT_POINT);
#elif defined(CONFIG_DRAFTLING_MODEL_LILYGO_TDISPLAY_S3)
    /* T-Display-S3 has no on-board MicroSD slot. The SD_SPI_* pins
     * defined in app_config.h describe a *recommended* external SD
     * wiring on the back GPIO header (10/11/12/13). If no SD module
     * is connected the call below fails harmlessly and the editor
     * runs in read-only mode with the "ERROR: SD card not ready"
     * status banner. */
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
