#include <cstdio>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <driver/spi_common.h>
#include <driver/gpio.h>
#if defined(CONFIG_DRAFTLING_DISPLAY_EPDIY)
#include <driver/i2c_master.h>
#endif
#if defined(CONFIG_DRAFTLING_DISPLAY_MIPI_DSI)
#include <driver/i2c_master.h>
#include "bsp/m5stack_tab5.h"
#endif
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
#include "touchscreen.h"
#include "power.h"

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

#if defined(CONFIG_DRAFTLING_DISPLAY_EPD)
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
    ESP_LOGI(TAG, "Draftling - %s", BOARD_NAME);

    /* Initialize NVS - required for WiFi and BT */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

#if defined(CONFIG_DRAFTLING_HAS_POWER_LATCH)
    /* Close the hardware power latch first thing after NVS so the
     * battery rail stays alive when the user releases the boot-time
     * PWR button press. power_init() also arms a polling timer that
     * watches the PWR button for a long-press (= power off). The
     * pre-off callback is wired up after the editor is created so
     * the long-press save can flush dirty document state. */
    ESP_LOGI(TAG, "Initializing power latch...");
    {
        power_config_t pcfg = {};
        pcfg.pwr_button_gpio = PWR_BUTTON_GPIO;
        pcfg.i2c_sda         = PWR_I2C_SDA_PIN;
        pcfg.i2c_scl         = PWR_I2C_SCL_PIN;
        pcfg.tca9554_addr    = PWR_TCA9554_ADDR;
        pcfg.latch_bit       = PWR_TCA9554_LATCH_BIT;
        power_init(&pcfg);
    }
#endif

    /* Initialize display */
    ESP_LOGI(TAG, "Initializing display...");
#if defined(CONFIG_DRAFTLING_DISPLAY_EPDIY)
    /* The LilyGO T5 E-Paper S3 Pro / Pro Lite shares its on-board
     * I2C bus between epdiy (TPS65185 EPD power IC + PCA9535 IO
     * expander), the GT911 capacitive touch controller and the
     * BQ27220 battery fuel gauge (battery_init_bq27220 further
     * below). All consumers use driver-NG (driver/i2c_master.h), and
     * ESP-IDF only allows one i2c_new_master_bus() per port. We
     * therefore create the bus here, ahead of any consumer, and hand
     * the handle to each: display_set_shared_i2c_bus() before
     * display_init() (epdiy routes through epd_init_with_config() +
     * EpdInitConfig.i2c.bus_handle), battery_init_bq27220() right
     * after, and touchscreen_config_t.i2c_bus before
     * touchscreen_init() further below. This is the resolution for
     * the legacy/driver-NG conflict that previously forced touch off
     * on these boards; see components/display/idf_component.yml for
     * why epdiy is pinned to a git commit. */
    i2c_master_bus_handle_t shared_i2c_bus = NULL;
    {
        i2c_master_bus_config_t bus_cfg = {};
        bus_cfg.i2c_port          = 0;
        bus_cfg.sda_io_num        = (gpio_num_t)I2C_SDA_PIN;
        bus_cfg.scl_io_num        = (gpio_num_t)I2C_SCL_PIN;
        bus_cfg.clk_source        = I2C_CLK_SRC_DEFAULT;
        bus_cfg.glitch_ignore_cnt = 7;
        bus_cfg.flags.enable_internal_pullup = true;
        esp_err_t bus_err = i2c_new_master_bus(&bus_cfg, &shared_i2c_bus);
        if (bus_err != ESP_OK) {
            ESP_LOGE(TAG, "Shared I2C bus init failed: %s -- "
                          "falling back to per-component buses (touch "
                          "will be disabled to avoid conflict)",
                     esp_err_to_name(bus_err));
            shared_i2c_bus = NULL;
        } else {
            ESP_LOGI(TAG, "Shared I2C bus created (port 0, SDA=%d SCL=%d)",
                     I2C_SDA_PIN, I2C_SCL_PIN);
        }
    }
    display_set_shared_i2c_bus(shared_i2c_bus);
#endif
#if defined(CONFIG_DRAFTLING_DISPLAY_MIPI_DSI)
    /* M5Stack Tab5 shares its on-board I2C bus (SDA=31, SCL=32)
     * between the PI4IOE5V6408 I/O expander (LCD_EN, TOUCH_EN, ...),
     * the GT911 / ST7123 touch controller, the BMI270 IMU and the
     * audio codec. The espressif/m5stack_tab5 BSP creates and owns
     * the driver-NG bus inside bsp_i2c_init(); the touchscreen
     * component picks it up further below via bsp_i2c_get_handle()
     * + touchscreen_config_t.i2c_bus. Call bsp_i2c_init() once
     * here, before display_init(), so the panel auto-detect probe
     * inside the BSP can issue the read on a ready bus. */
    {
        esp_err_t bus_err = bsp_i2c_init();
        if (bus_err != ESP_OK) {
            ESP_LOGE(TAG, "bsp_i2c_init failed: %s", esp_err_to_name(bus_err));
        }
    }
#endif

#if defined(CONFIG_DRAFTLING_DISPLAY_RLCD)
    display_init(RLCD_MOSI_PIN, RLCD_SCK_PIN, RLCD_DC_PIN,
                 RLCD_CS_PIN, RLCD_RST_PIN, -1,
                 DISPLAY_WIDTH, DISPLAY_HEIGHT);
#elif defined(CONFIG_DRAFTLING_DISPLAY_EDS3)
    /* The PaperS3 driver is a thin shim over M5GFX which configures
     * all panel GPIOs internally based on the M5PaperS3 board id. We
     * still call display_init for API parity; pin parameters are
     * ignored by the M5GFX backend. */
    display_init(-1, -1, -1, -1, -1, -1, DISPLAY_WIDTH, DISPLAY_HEIGHT);
#elif defined(CONFIG_DRAFTLING_DISPLAY_EPDIY)
    /* The LilyGO T5 E-Paper S3 Pro / Pro Lite display backend is a
     * thin shim over vroland/epdiy which owns all panel GPIOs via
     * its `epd_board_v7` configuration (8-bit parallel data bus on
     * direct GPIOs plus TPS65185 power management via a PCA9535 IO
     * expander on I2C). Pin parameters are ignored. */
    display_init(-1, -1, -1, -1, -1, -1, DISPLAY_WIDTH, DISPLAY_HEIGHT);
#elif defined(CONFIG_DRAFTLING_DISPLAY_MIPI_DSI)
    /* M5Stack Tab5 MIPI-DSI panel. All panel GPIOs, the MIPI-DSI
     * PHY LDO, the I/O expander wiring and the LEDC backlight
     * channel are owned by the espressif/m5stack_tab5 BSP, which
     * also auto-detects board v1 (ILI9881C + GT911) vs board v2
     * (ST7123 + ST7123). display_init's pin parameters are ignored
     * by this backend. */
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
        /* The Guition JC3248W535's AXS15231B silicon ignores the
         * MADCTL MV (swap-XY) bit (same observation as Tactility's
         * driver for this board), so the backend has to software-
         * rotate at flush time to present the 320x480 portrait
         * panel as 480x320 landscape. Waveshare's Touch-LCD-3.49
         * is mounted with the same AXS15231B silicon but a 172x640
         * portrait panel; its reference firmware also software-
         * rotates to 640x172 landscape (USER_DISP_ROT_90), so we
         * take the same path. */
#if defined(CONFIG_DRAFTLING_MODEL_JC3248W535) || \
    defined(CONFIG_DRAFTLING_MODEL_WAVESHARE_TOUCH_LCD_349)
        cfg.swap_xy = true;
#else
        cfg.swap_xy = false;
#endif
        /* The hand-coded vendor-register block in display_axs15231b.cpp
         * was adapted from the Guition JC3248W535 reference and is
         * required to drive that 480x320 panel out of its weak factory
         * defaults. The Waveshare Touch-LCD-3.49's 172x640 panel
         * already ships with correct factory POR values -- the
         * upstream Espressif esp_lcd_axs15231b driver lets users
         * supply only {SLPOUT, DISPON} for that board, which is what
         * Waveshare's reference firmware does. Sending the JC3248W535
         * recipe to that panel clobbers its correct defaults and
         * leaves the display black on cold boot until the user
         * presses RESET. Skip the vendor block on Touch-LCD-3.49. */
#if defined(CONFIG_DRAFTLING_MODEL_WAVESHARE_TOUCH_LCD_349)
        cfg.skip_vendor_init = true;
#else
        cfg.skip_vendor_init = false;
#endif
        cfg.bl_deep_sleep_cut = LCD_BL_DEEP_SLEEP_CUT_PIN;
        cfg.bl_active_low     = (LCD_BL_ACTIVE_LOW != 0);
        display_axs15231b_init(&cfg);
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
#if defined(CONFIG_DRAFTLING_BATTERY_BQ27220)
    /* LilyGO T5 E-Paper S3 Pro / Pro Lite: BQ27220YZFR fuel gauge
     * lives on the same I2C bus we already created above for epdiy
     * + GT911. The ADC backend has no pin (BATT_ADC_PIN == -1) on
     * these boards; battery_init() would early-return, so we drive
     * the BQ27220 backend directly. */
    if (battery_init_bq27220(shared_i2c_bus) != 0) {
        ESP_LOGW(TAG, "BQ27220 fuel gauge init failed; "
                      "falling back to ADC backend");
        battery_init(BATT_ADC_PIN, BATT_EN_PIN, BATT_DIVIDER);
    }
#else
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
    esp_err_t sd_ret = ESP_FAIL;
#if defined(CONFIG_DRAFTLING_SD_SDMMC)
    /* Boards with the SD slot wired to the on-chip SDMMC peripheral
     * (Waveshare RLCD-4.2). Uses 1-bit mode to keep the pin count
     * down. */
    sd_ret = sd_card_init(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN, SD_MOUNT_POINT);
#else
#if defined(BOARD_LORA_CS_PIN) && (BOARD_LORA_CS_PIN >= 0)
    /* LilyGO T5 E-Paper S3 Pro: the on-board MicroSD slot shares its
     * SPI bus with the SX1262 LoRa radio. If the LoRa CS line is
     * left floating, SD init can fail intermittently because the
     * LoRa chip latches onto SPI traffic intended for the SD card
     * (LilyGO T5S3-4.7-e-paper-PRO issue #3). Drive LoRa CS HIGH
     * here -- on the Pro Lite variant the LoRa silicon is
     * depopulated, so this is just a harmless drive on an
     * unconnected output. */
    {
        gpio_config_t lora_cs = {};
        lora_cs.intr_type    = GPIO_INTR_DISABLE;
        lora_cs.mode         = GPIO_MODE_OUTPUT;
        lora_cs.pin_bit_mask = (1ULL << BOARD_LORA_CS_PIN);
        gpio_config(&lora_cs);
        gpio_set_level((gpio_num_t)BOARD_LORA_CS_PIN, 1);
    }
#endif
    /* Every other supported board carries the SD card on a generic
     * SPI bus separate from the display:
     *   - PaperS3:                on-board MicroSD on SPI3
     *   - AXS15231B color LCDs:   SPI3 (display owns SPI2 QSPI)
     *   - LilyGO T5 E-Paper S3 Pro: on-board MicroSD on SPI3 (shared
     *     with the SX1262 LoRa radio CS; we drive LoRa CS HIGH above
     *     so the radio does not snoop the SD traffic).
     * sd_card_init_spi() returns gracefully if no card is present. */
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

#if defined(CONFIG_DRAFTLING_TOUCHSCREEN)
    /* Initialize the touchscreen and register the LVGL pointer
     * indev. Must run after the display (LVGL needs the default
     * display) and before standby_init (standby queries the touch
     * INT GPIO when CONFIG_DRAFTLING_STANDBY_WAKE_ON_TOUCH is set).
     *
     * Touch native orientation and mirror flags come from
     * app_config.h's per-board block (TOUCH_NATIVE_W / _H /
     * TOUCH_SWAP_XY / TOUCH_MIRROR_X / TOUCH_MIRROR_Y), so this
     * code stays board-agnostic. logical_width / logical_height
     * are the DISPLAY_SCALE-aware logical pixel counts that LVGL
     * itself uses.
     *
     * On the LilyGO T5 E-Paper S3 Pro / Pro Lite the GT911 sits on
     * the same physical I2C bus as epdiy's PCA9535 + TPS65185, and
     * ESP-IDF allows only one driver-NG i2c_new_master_bus() per
     * port. We therefore reuse the `shared_i2c_bus` handle created
     * above and hand it to the touchscreen component via
     * tcfg.i2c_bus; on every other board tcfg.i2c_bus stays NULL
     * and the component creates its own bus from sda/scl as before. */
    ESP_LOGI(TAG, "Initializing touchscreen...");
    {
        touchscreen_config_t tcfg = {};
        tcfg.sda      = I2C_SDA_PIN;
        tcfg.scl      = I2C_SCL_PIN;
        tcfg.rst      = TOUCH_RST_PIN;
        tcfg.intr     = TOUCH_INT_PIN;
        tcfg.i2c_addr = TOUCH_I2C_ADDR;
        tcfg.i2c_port = 0;
        tcfg.i2c_hz   = 400000;
        tcfg.native_width   = TOUCH_NATIVE_W;
        tcfg.native_height  = TOUCH_NATIVE_H;
        tcfg.logical_width  = DISPLAY_LOGICAL_WIDTH;
        tcfg.logical_height = DISPLAY_LOGICAL_HEIGHT;
        tcfg.swap_xy  = TOUCH_SWAP_XY  ? true : false;
        tcfg.mirror_x = TOUCH_MIRROR_X ? true : false;
        tcfg.mirror_y = TOUCH_MIRROR_Y ? true : false;
        tcfg.user_rotate_deg = DISPLAY_ROTATE;
#if defined(CONFIG_DRAFTLING_DISPLAY_EPDIY)
        tcfg.i2c_bus = (void *)shared_i2c_bus;
#elif defined(CONFIG_DRAFTLING_DISPLAY_MIPI_DSI)
        /* The m5stack_tab5 BSP owns the I2C master bus (created
         * by bsp_i2c_init() before display_init() above); hand
         * its handle to the touchscreen component so we do not
         * collide on i2c_new_master_bus() for the same port. */
        tcfg.i2c_bus = (void *)bsp_i2c_get_handle();
#endif
        touchscreen_init(&tcfg);
    }
#endif

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

#if defined(CONFIG_DRAFTLING_HAS_POWER_LATCH)
    /* On PWR long-press, run the full shutdown sequence via the
     * standby manager: auto-save (pre_sleep_cb above), backlight
     * cut, power-latch cut (battery: rail drops here; USB: no-op),
     * then esp_deep_sleep_start. This is the same sequence used on
     * inactivity timeout, so the behaviour is identical regardless
     * of which path triggers the shutdown. */
    power_set_long_press_cb(standby_enter_sleep);
#endif

    ESP_LOGI(TAG, "Draftling ready. Waiting for Bluetooth keyboard...");
}
