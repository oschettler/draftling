#include <cstdio>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <driver/spi_common.h>
#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#if defined(CONFIG_DRAFTLING_DISPLAY_EPDIY)
#include <driver/i2c_master.h>
#endif
#if defined(CONFIG_DRAFTLING_DISPLAY_MIPI_DSI)
#include "bsp/m5stack_tab5.h"
#endif
#include "sdkconfig.h"

#if defined(CONFIG_ESP_HOSTED_ENABLED)
#include "esp_hosted.h"
#include "esp_hosted_event.h"
#include "esp_event.h"
#include <freertos/event_groups.h>
#endif

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
#if defined(CONFIG_DRAFTLING_HAS_USB_HOST)
#include "usb_kbd.h"
#endif

static const char *TAG = "Draftling";

/* Called by standby just before entering deep sleep */
static void pre_sleep_autosave(void)
{
    ESP_LOGI(TAG, "Pre-sleep: autosave + EPD wipe (generic path)");
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
     * task's flush_cb. */
    /* Take the LVGL mutex if we can so the wipe does not race with
     * the LVGL task's flush_cb. The mutex is recursive
     * (draftling_lvgl_port_init), so this also works when pre_sleep_autosave
     * runs inside the LVGL task itself (the "Sleep now" menu path).
     * If for any reason the lock cannot be obtained quickly, wipe
     * anyway -- a clean white frame on the panel matters more than
     * the slim chance of a flush_cb collision right before deep
     * sleep. */
    bool locked = draftling_lvgl_port_lock(200);
    display_clear(0xFF);
    display_full_refresh();
    if (locked) {
        draftling_lvgl_port_unlock();
    } else {
        ESP_LOGW(TAG, "pre_sleep wipe: LVGL lock not acquired, proceeded anyway");
    }
#endif
}

#if defined(CONFIG_DRAFTLING_MODEL_LILYGO_T5_EPD_S3_PRO)
/* LilyGO T5 E-Paper S3 Pro / Pro Lite: deep-sleep current budget.
 *
 * Without this hook the board pulls ~30 mA in deep sleep (a 1500 mAh
 * NP-F battery dies in ~2 days). Target after this hook is < 100 uA,
 * giving > 2 weeks idle. Each step plugs one peripheral:
 *
 *   Peripheral             Sleep state we drive it to            Source
 *   --------------------   ----------------------------------    ----------
 *   GT911 touch ctrlr      cmd 0x05 -> "sleep" (~8 uA)           GT911 DS sec.6
 *   SX1262 LoRa (Pro)      SetSleep(0x00) (~600 nA cold)         SX126x DS sec.13.1.7
 *   SD card (SPI)          SPI3 bus freed; card to standby        sd_card_deinit()
 *   EPDIY (TPS65185+exp)   epd_poweroff() -> WAKEUP low (<1 uA)   epdiy README
 *   Front-light LED (G11)  driven LOW + RTC IO hold                display_deep_sleep_prepare()
 *   ESP32-S3 RTC_PERIPH    powered down                            esp_sleep_pd_config()
 *
 * The display + touch deinit must happen with the I2C bus still
 * alive, so the sequence is:
 *   touchscreen_sleep()              (uses I2C)
 *   display_deep_sleep_prepare()     (uses I2C, then tears it down)
 *   sd_card_deinit()                 (releases SPI3)
 *   SX1262 SetSleep over SPI3        (must run BEFORE spi_bus_free,
 *                                     but sd_card_deinit closes the
 *                                     bus, so we do LoRa first)
 *   esp_sleep_pd_config(RTC_PERIPH)  (final hint to PMU)
 *   gpio_deep_sleep_hold_en()        (latch all gpio_hold_en pads)
 *
 * `standby_enter_sleep()` does the actual `esp_deep_sleep_start()`
 * after this callback returns.
 */

/* SX1262 SetSleep opcode (sx126x datasheet sec.13.1.7). We issue a
 * cold-start sleep (no register retention -- we don't have any
 * persistent radio state to keep). 1-byte opcode + 1-byte param. */
static void t5_lora_sleep(void)
{
#if defined(BOARD_LORA_CS_PIN) && (BOARD_LORA_CS_PIN >= 0)
    /* The SX1262 sits on SPI3 (shared with the SD card). Talk to it
     * directly via spi_bus_add_device using the LoRa CS, then remove
     * the device when done. Failure is non-fatal -- the radio chip
     * is depopulated on the Lite variant, so a NACK / no-response is
     * the expected outcome there. */
    spi_device_handle_t lora = NULL;
    spi_device_interface_config_t devcfg = {};
    devcfg.clock_speed_hz = 1 * 1000 * 1000;  /* 1 MHz is safe for SetSleep */
    devcfg.mode           = 0;
    devcfg.spics_io_num   = BOARD_LORA_CS_PIN;
    devcfg.queue_size     = 1;
    esp_err_t err = spi_bus_add_device(SPI3_HOST, &devcfg, &lora);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "SX1262 spi_bus_add_device failed: %s",
                 esp_err_to_name(err));
        return;
    }
    uint8_t tx[2] = { 0x84 /* SetSleep */, 0x00 /* cold start */ };
    spi_transaction_t t = {};
    t.length    = sizeof(tx) * 8;
    t.tx_buffer = tx;
    err = spi_device_polling_transmit(lora, &t);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "SX1262 entered sleep mode");
    } else {
        ESP_LOGD(TAG, "SX1262 SetSleep transmit failed: %s",
                 esp_err_to_name(err));
    }
    spi_bus_remove_device(lora);
#endif
}

/* Sweep a hard-coded list of GPIOs that are not wired to any
 * peripheral on the T5 (per the LilyGO factory schematic) and
 * isolate them so they don't leak through floating inputs in deep
 * sleep. We restrict the list to RTC-capable pins because
 * rtc_gpio_isolate() is the documented low-power state for those.
 * Non-RTC pins are released to high-Z via gpio_reset_pin -- the
 * pad's analog input is already off in deep sleep, so a high-Z
 * output is the lowest-leakage state available. */
static void t5_isolate_unused_gpios(void)
{
    /* ESP32-S3 RTC-IO range is GPIO0..GPIO21. Skip pins that are
     * actively used in deep sleep:
     *   GPIO0  -- BOOT button, EXT0 wake (handled by standby.cpp)
     *   GPIO11 -- front-light, held LOW (display_deep_sleep_prepare)
     *   GPIO13 -- SD MOSI / shared SPI3 (released by sd_card_deinit)
     *   GPIO14 -- SD SCK  / shared SPI3 (released by sd_card_deinit)
     *   GPIO21 -- SD MISO / shared SPI3 (released by sd_card_deinit)
     *   GPIO12 -- SD CS (released by sd_card_deinit)
     */
    static const int rtc_unused[] = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 15, 16, 17, 18, 19, 20
    };
    for (size_t i = 0; i < sizeof(rtc_unused) / sizeof(rtc_unused[0]); ++i) {
        gpio_num_t g = (gpio_num_t)rtc_unused[i];
        if (rtc_gpio_is_valid_gpio(g)) {
            (void)rtc_gpio_isolate(g);
        }
    }
}

static void pre_sleep_t5_deinit(void)
{
    ESP_LOGI(TAG, "Pre-sleep: LilyGO T5 peripheral teardown");
    /* Run the editor autosave + EPD wipe first; that path uses the
     * touch / display / I2C / SD subsystems while they are still up. */
    pre_sleep_autosave();

    /* GT911 touch controller -> sleep (uses the shared I2C bus). */
    touchscreen_sleep();

    /* SX1262 LoRa -> sleep (Pro variant only; harmless NACK on Lite). */
    t5_lora_sleep();

    /* Release the SD card + SPI3 bus so the card itself drops to
     * standby and the bus pins float free. After this point any SPI3
     * transaction (including the LoRa one above) is illegal -- which
     * is why we do LoRa first. */
    (void)sd_card_deinit();

    /* Isolate every unused RTC-IO pin so floating inputs don't leak. */
    t5_isolate_unused_gpios();

    /* Power down the RTC peripherals domain. We have no ULP and no
     * RTC SLOW memory data to retain across wake (the SoC cold-boots
     * on wake and the editor restores from autosave), so dropping
     * this domain saves ~40 uA on ESP32-S3 over the default. */
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);

    /* Latch every pad that called gpio_hold_en() (the front-light,
     * specifically) so the level stays driven through deep sleep. */
    gpio_deep_sleep_hold_en();
}
#endif /* CONFIG_DRAFTLING_MODEL_LILYGO_T5_EPD_S3_PRO */

#if defined(CONFIG_DRAFTLING_MODEL_M5STACK_TAB5)
/* M5Stack Tab5 (ESP32-P4): pre-sleep peripheral teardown.
 *
 * The Tab5 has no LP_IO-capable user input, so deep sleep can only
 * exit via the hardware RESET button (the chip cold-boots; editor
 * state is restored from autosave). With the BSP's default state
 * left untouched the board still draws far more than it should in
 * deep sleep because:
 *
 *   - the ESP32-C6 co-processor stays fully powered through P4 deep
 *     sleep (WLAN_PWR_EN = PI4IOE5V6408 #2 P0 is latched HIGH at
 *     boot and never released; the C6 LDO keeps its CPU, BLE
 *     controller and SDIO link armed),
 *   - the MIPI-DSI panel and its backlight stay on,
 *   - the GT911 touch controller keeps polling at ~3 mA,
 *   - the RTC peripherals domain is left powered.
 *
 * This hook drives each of those into the lowest state we can reach
 * from software, in the order dictated by their dependencies (the
 * I/O expander writes must happen while bsp_i2c is still up; the
 * panel disable is safe at any time because the BSP owns it).
 *
 * Wake path is unchanged: the chip cold-boots on RESET and re-runs
 * app_main(), which re-asserts WLAN_PWR_EN and CHG_EN through the
 * same BSP / expander code used at first boot. */
static void pre_sleep_tab5_deinit(void)
{
    ESP_LOGI(TAG, "Pre-sleep: M5Stack Tab5 peripheral teardown");

    /* Editor autosave first while every bus is still up. */
    pre_sleep_autosave();

    /* GT911 -> sleep (uses the BSP-owned I2C bus, which is still up). */
    touchscreen_sleep();

    /* MIPI-DSI panel + backlight off via the BSP-managed handles. */
    display_deep_sleep_prepare();

    /* Drop the ESP32-C6 power rail. The PI4IOE5V6408 latches the
     * pin state across P4 deep sleep, so the rail stays off until
     * the next cold boot re-runs bsp_feature_enable(BSP_FEATURE_WIFI,
     * true) in app_main(). Use the BSP API rather than poking the
     * expander directly so the driver's shadow register stays in
     * sync with the chip; otherwise a later RMW on any other pin
     * on the same expander would un-clear our bit. */
    {
        esp_err_t err = bsp_feature_enable(BSP_FEATURE_WIFI, false);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Tab5: WLAN_PWR_EN dropped (ESP32-C6 off)");
        } else {
            ESP_LOGW(TAG, "Tab5: bsp_feature_enable(WIFI, false) failed: %s",
                     esp_err_to_name(err));
        }
    }

    /* Power down the RTC peripherals domain. Draftling has no ULP
     * and no RTC SLOW data to retain across wake, so dropping this
     * domain shaves a few mA on ESP32-P4 over the default. */
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);
}
#endif /* CONFIG_DRAFTLING_MODEL_M5STACK_TAB5 */

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

    /* M5Stack Tab5 GT911 I2C address selection.
     *
     * The GT911 latches its 7-bit I2C address from the level on its
     * INT pin at the *rising edge* of its internal power-on reset:
     *   INT low  during reset -> backup  address 0x14
     *   INT high during reset -> primary address 0x5D
     *
     * The Tab5 has a hardware pull-up to 3V3 on the GT911 INT
     * (ESP32-P4 GPIO23). If we leave that pull-up to win, the
     * GT911 latches 0x5D, but the BSP only ever probes the backup
     * 0x14 (and our own TOUCH_I2C_ADDR is 0x14 to match) so touch
     * is effectively dead.
     *
     * Crucially, on this board the GT911 has NO dedicated RST line
     * exposed to the SoC -- the BSP sets `rst_gpio_num = GPIO_NUM_NC`
     * and `BSP_LCD_RST = GPIO_NUM_NC`. The only way to force the
     * GT911 to re-run its internal power-on reset (and re-latch
     * its I2C address) is to power-cycle the TOUCH_EN rail driven
     * by the first PI4IOE5V6408 I/O expander (I2C 0x43, pin P5 =
     * BSP_TOUCH_EN). On a cold boot the PI4IOE5V6408 starts with
     * every pin in high-impedance, so TOUCH_EN is effectively low
     * and the GT911 is unpowered until `bsp_feature_enable(TOUCH)`
     * brings it up inside `bsp_get_board_version()`. But on a warm
     * reboot (esp_restart, panic, watchdog, wake-from-reset) the
     * PI4IOE5V6408 retains its previous register state -- TOUCH_EN
     * is already high, the GT911 already latched 0x5D against the
     * INT pull-up before we ever got control, and any amount of
     * INT-low driving afterwards has no effect.
     *
     * So do both, in this order:
     *   (a) drive GPIO23 LOW as a push-pull output (override the
     *       3V3 pull-up at the SoC pin),
     *   (b) ask the BSP for the first PI4IOE5V6408 handle, switch
     *       BSP_TOUCH_EN (P5) to push-pull output, then drive it
     *       low for >=10 ms and high again, holding INT low across
     *       the whole edge so the GT911 latches 0x14.
     *
     * `touchscreen_init()` further down reconfigures GPIO23 to
     * INPUT + pull-up for the normal "data ready" signalling.
     * By then the GT911 has already latched its address, so the
     * 3V3 idle level on INT no longer matters.
     *
     * The subsequent `bsp_feature_enable(BSP_FEATURE_TOUCH, true)`
     * call inside `bsp_get_board_version()` (which is inside
     * `display_init()` below) is idempotent: it just rewrites P5
     * to the same `high` we already set here. */
#if defined(CONFIG_DRAFTLING_TOUCHSCREEN) && TOUCH_INT_PIN >= 0
    {
        /* (a) Hold INT low at the SoC pin. */
        gpio_config_t int_cfg = {};
        int_cfg.intr_type    = GPIO_INTR_DISABLE;
        int_cfg.mode         = GPIO_MODE_OUTPUT;
        int_cfg.pin_bit_mask = (1ULL << TOUCH_INT_PIN);
        int_cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
        int_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        gpio_config(&int_cfg);
        gpio_set_level((gpio_num_t)TOUCH_INT_PIN, 0);

        /* (b) Power-cycle TOUCH_EN through the first PI4IOE5V6408
         * so the GT911 actually re-runs its internal POR and
         * latches the INT=low we just established. */
        esp_io_expander_handle_t pi4 = bsp_io_expander_init();
        if (pi4 == NULL) {
            ESP_LOGW(TAG, "Tab5 touch: bsp_io_expander_init returned NULL; "
                          "skipping GT911 power-cycle (touch may stay at 0x5D)");
        } else {
            esp_err_t err = esp_io_expander_set_dir(
                pi4, BSP_TOUCH_EN, IO_EXPANDER_OUTPUT);
            if (err == ESP_OK) {
                err = esp_io_expander_set_output_mode(
                    pi4, BSP_TOUCH_EN, IO_EXPANDER_OUTPUT_MODE_PUSH_PULL);
            }
            if (err == ESP_OK) {
                err = esp_io_expander_set_level(pi4, BSP_TOUCH_EN, 0);
            }
            if (err == ESP_OK) {
                /* GT911 datasheet: hold reset (here: VDD removed) for
                 * at least 10 ms to drain rail capacitance and ensure
                 * the internal POR actually fires on the next rise. */
                vTaskDelay(pdMS_TO_TICKS(20));
                err = esp_io_expander_set_level(pi4, BSP_TOUCH_EN, 1);
            }
            if (err == ESP_OK) {
                /* GT911 typical boot time is ~50 ms; the BSP itself
                 * waits 500 ms before probing 0x14. We just need the
                 * chip to have latched the address before we release
                 * the INT line as input later in touchscreen_init(),
                 * so a short delay here is enough -- the BSP's own
                 * 500 ms inside bsp_get_board_version() covers the
                 * rest. */
                vTaskDelay(pdMS_TO_TICKS(60));
                ESP_LOGI(TAG, "Tab5 GT911: TOUCH_EN power-cycled with "
                              "INT held low (expecting I2C 0x14)");
            } else {
                ESP_LOGW(TAG, "Tab5 GT911: TOUCH_EN power-cycle failed: %s",
                         esp_err_to_name(err));
            }
        }
    }
#endif

    /* M5Stack Tab5 battery charger enable (CHG_EN, P7 on the second
     * PI4IOE5V6408) is asserted later, AFTER `bsp_feature_enable()`
     * has had a chance to bring up the second I/O expander (its
     * driver's reset() routine wipes the output register and forces
     * every pin into high-impedance, which would clobber any CHG_EN
     * setting we did here). See the dedicated block further down,
     * immediately after the BSP_FEATURE_WIFI bring-up. */
#endif

#if defined(CONFIG_DRAFTLING_DISPLAY_RLCD)
    display_init(RLCD_MOSI_PIN, RLCD_SCK_PIN, RLCD_DC_PIN,
                 RLCD_CS_PIN, RLCD_RST_PIN, -1,
                 DISPLAY_WIDTH, DISPLAY_HEIGHT);
#elif defined(CONFIG_DRAFTLING_DISPLAY_EPDIY)
    /* vroland/epdiy backend. Owns all panel GPIOs via its board
     * definition (epdiy's built-in epd_board_v7 on the LilyGO T5
     * E-Paper S3 Pro / Pro Lite, the in-tree epd_board_papers3 on
     * the M5Stack PaperS3). Pin parameters are ignored. */
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
    draftling_lvgl_port_init(DISPLAY_LOGICAL_WIDTH, DISPLAY_LOGICAL_HEIGHT, DISPLAY_ROTATE);

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
#elif defined(CONFIG_DRAFTLING_BATTERY_INA226)
    /* M5Stack Tab5: TI INA226 current/voltage monitor at I2C 0x41 on
     * the shared system bus owned by the espressif/m5stack_tab5 BSP
     * (bsp_i2c_init() is run earlier by display_init() via the MIPI
     * DSI backend). Battery pack is a 2S NP-F550 (~6.0-8.4 V); the
     * INA226 backend divides bus voltage by cell count before
     * looking up the per-cell SoC table. */
    if (battery_init_ina226(bsp_i2c_get_handle(), 0x41, 2) != 0) {
        ESP_LOGW(TAG, "INA226 init failed; battery indicator disabled");
    }
#else
    battery_init(BATT_ADC_PIN, BATT_EN_PIN, BATT_DIVIDER);
#endif

#if defined(CONFIG_DRAFTLING_CHARGER_BQ25896)
    /* LilyGO T5 E-Paper S3 Pro / Pro Lite: the on-board BQ25896
     * charger powers up with USB-SDP-class settings (500 mA IINLIM,
     * AUTO_DPDM re-detecting on every plug-in, 40 s I2C watchdog
     * that reverts host writes) and would otherwise charge the cell
     * at ~500 mA regardless of the wall adapter rating. Lift the
     * input current limit, raise the fast-charge current and
     * disable the watchdog so the settings persist. Uses the same
     * shared I2C bus as the BQ27220 fuel gauge (chip at 0x6B). */
    if (battery_init_bq25896(shared_i2c_bus) != 0) {
        ESP_LOGW(TAG, "BQ25896 charger init failed; "
                      "device will charge at reduced rate");
    }
#endif

    /* Create editor UI */
    ESP_LOGI(TAG, "Creating editor UI...");
    if (draftling_lvgl_port_lock(-1)) {
        editor_ui_init();
        draftling_lvgl_port_unlock();
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
        if (draftling_lvgl_port_lock(-1)) {
            editor_ui_set_status("ERROR: SD card not ready");
            draftling_lvgl_port_unlock();
        }
    }

    /* Bring up the ESP-Hosted SDIO link to the on-board ESP32-C6
     * co-processor before Bluetooth or Wi-Fi. The C6 provides the
     * BLE controller (attached to Bluedroid via VHCI in
     * ble_keyboard.cpp) and the 2.4 GHz Wi-Fi radio (used through
     * esp_wifi_remote by wifi_manager.cpp).
     *
     * esp_hosted_init() / esp_hosted_connect_to_slave() return
     * success even when the SDIO slave does not respond (the
     * underlying transport_drv_reconfigure() failure is logged via
     * ESP_ERROR_CHECK_WITHOUT_ABORT and swallowed). We therefore
     * subscribe to the ESP_HOSTED_EVENT_TRANSPORT_UP event and
     * require it to fire within a short timeout before continuing.
     * If it doesn't, the most common cause is that the on-board
     * ESP32-C6 is not flashed with the matching ESP-Hosted slave
     * firmware (see docs/tab5-esp-hosted.md). In that case we show a
     * persistent on-screen message and halt instead of letting later
     * BLE/Wi-Fi initialisation abort the firmware. */
#if defined(CONFIG_ESP_HOSTED_ENABLED)
    {
#if defined(CONFIG_DRAFTLING_DISPLAY_MIPI_DSI)
        /* On the M5Stack Tab5 the ESP32-C6 co-processor's 3V3 rail is
         * gated by WLAN_PWR_EN (PI4IOE5V6408 #2, address 0x44, P0).
         * Until this pin is driven HIGH the C6 has no power, so the
         * host's reset pulse on GPIO15 below targets a dead chip and
         * the SDIO handshake never completes. Drive the rail high via
         * the espressif/m5stack_tab5 BSP and wait briefly for the C6
         * LDO to settle and the ROM bootloader to come up. bsp_i2c_init()
         * has already been called above (display path), and
         * bsp_feature_enable() is idempotent.
         *
         * A 50 ms post-enable delay was empirically insufficient on cold
         * boot (SDIO CMD5 then times out with sdmmc_init_ocr: send_op_cond
         * returned 0x107 even though the host pulses Slave_Reset on GPIO15
         * afterwards). 200 ms gives the C6 LDO + CHIP_PU + ROM bootloader
         * comfortable headroom before SDIO probing starts. */
        esp_err_t pwr_err = bsp_feature_enable(BSP_FEATURE_WIFI, true);
        if (pwr_err != ESP_OK) {
            ESP_LOGE(TAG, "bsp_feature_enable(BSP_FEATURE_WIFI) failed: %s",
                     esp_err_to_name(pwr_err));
        } else {
            ESP_LOGI(TAG, "ESP32-C6 power rail (WLAN_PWR_EN) enabled");
        }
        vTaskDelay(pdMS_TO_TICKS(200));

#if defined(CONFIG_DRAFTLING_MODEL_M5STACK_TAB5)
        /* M5Stack Tab5: enable battery charging from the USB-C input.
         *
         * The Tab5 carries an IP2326 autonomous Li-ion charger gated
         * by the CHG_EN signal, which is wired to P7 of the second
         * PI4IOE5V6408 I/O expander (I2C address 0x44). The
         * espressif/m5stack_tab5 BSP does not expose the charger pin
         * in `bsp_feature_enable()`, and M5Stack's reference
         * `bsp_io_expander_pi4ioe_init()` deliberately boots with
         * CHG_EN = 0, which is why the battery never charges when
         * USB is plugged in.
         *
         * This block must run AFTER `bsp_feature_enable(BSP_FEATURE_WIFI)`
         * above. That call goes through `bsp_io_expander1_init()`,
         * which constructs the espressif/esp_io_expander_pi4ioe5v6408
         * driver; that driver's reset() routine issues a chip-reset
         * and then re-writes the default register values
         * (OUT_SET = 0x00, OUT_H_IM = 0xFF, IO_DIR = 0xFF), which
         * would clobber any direct I2C write we did before the BSP
         * initialised the expander.
         *
         * We deliberately use the BSP-managed `esp_io_expander_handle_t`
         * (rather than raw I2C writes) so the driver's cached shadow
         * registers stay in sync with the chip. Otherwise a later
         * BSP `esp_io_expander_set_level()` call on any OTHER pin
         * (the BSP performs read-modify-write on its own shadow,
         * never reading back the chip) would overwrite CHG_EN
         * because the shadow still believes P7 is 0.
         *
         * Switching the pin to PUSH_PULL is required: the
         * PI4IOE5V6408 powers up with every pin in high-impedance
         * (OUT_H_IM = 0xFF), and the driver's reset() restores that
         * default. Even with IO_DIR = output and OUT_SET = 1 the
         * pin would otherwise stay floating and the IP2326 would
         * never see CHG_EN go high.
         */
        {
            esp_io_expander_handle_t pi4 = bsp_io_expander1_init();
            if (pi4 == NULL) {
                ESP_LOGW(TAG, "Tab5 CHG_EN: bsp_io_expander1_init returned NULL");
            } else {
                esp_err_t err = esp_io_expander_set_dir(
                    pi4, IO_EXPANDER_PIN_NUM_7, IO_EXPANDER_OUTPUT);
                if (err == ESP_OK) {
                    err = esp_io_expander_set_output_mode(
                        pi4, IO_EXPANDER_PIN_NUM_7,
                        IO_EXPANDER_OUTPUT_MODE_PUSH_PULL);
                }
                if (err == ESP_OK) {
                    err = esp_io_expander_set_level(
                        pi4, IO_EXPANDER_PIN_NUM_7, 1);
                }
                if (err == ESP_OK) {
                    ESP_LOGI(TAG, "Tab5 CHG_EN asserted (PI4IOE5V6408 #2 P7 = 1)");
                } else {
                    ESP_LOGW(TAG, "Tab5 CHG_EN setup failed: %s",
                             esp_err_to_name(err));
                }
            }
        }
#endif /* CONFIG_DRAFTLING_MODEL_M5STACK_TAB5 */
#endif
        ESP_LOGI(TAG, "Initializing ESP-Hosted link to ESP32-C6...");

        /* esp_hosted_init() posts ESP_HOSTED_EVENT_TRANSPORT_UP via
         * the default event loop, so it must exist before we call
         * into ESP-Hosted. wifi_manager_init() creates it later; here
         * we just ensure it's already up. ESP_ERR_INVALID_STATE means
         * "already created", which is fine. */
        esp_err_t loop_err = esp_event_loop_create_default();
        if (loop_err != ESP_OK && loop_err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "esp_event_loop_create_default failed: %s",
                     esp_err_to_name(loop_err));
        }

        static EventGroupHandle_t s_hosted_evt = NULL;
        s_hosted_evt = xEventGroupCreate();
        constexpr EventBits_t HOSTED_UP_BIT = BIT0;

        auto hosted_event_cb = [](void *arg, esp_event_base_t /*base*/,
                                  int32_t id, void * /*data*/) {
            if (id == ESP_HOSTED_EVENT_TRANSPORT_UP) {
                xEventGroupSetBits(reinterpret_cast<EventGroupHandle_t>(arg),
                                   HOSTED_UP_BIT);
            }
        };

        esp_err_t reg_err = esp_event_handler_register(
            ESP_HOSTED_EVENT, ESP_EVENT_ANY_ID,
            hosted_event_cb, s_hosted_evt);
        if (reg_err != ESP_OK) {
            ESP_LOGE(TAG, "esp_event_handler_register(ESP_HOSTED_EVENT) "
                          "failed: %s", esp_err_to_name(reg_err));
        }

        int hosted_err = esp_hosted_init();
        if (hosted_err == 0) {
            hosted_err = esp_hosted_connect_to_slave();
        }

        /* Wait up to 5 seconds for the SDIO transport to come up. The
         * normal handshake completes in well under a second on a
         * healthy Tab5; 5 s leaves generous margin for slower retries
         * without keeping the user staring at an empty screen for
         * long. */
        const TickType_t timeout = pdMS_TO_TICKS(5000);
        EventBits_t bits = xEventGroupWaitBits(
            s_hosted_evt, HOSTED_UP_BIT,
            pdFALSE /* clear */, pdTRUE /* wait all */, timeout);

        esp_event_handler_unregister(ESP_HOSTED_EVENT, ESP_EVENT_ANY_ID,
                                     hosted_event_cb);

        if ((bits & HOSTED_UP_BIT) == 0) {
            ESP_LOGE(TAG, "ESP-Hosted link did not come up "
                          "(hosted_err=%d). The on-board ESP32-C6 is "
                          "probably not flashed with the matching "
                          "ESP-Hosted slave firmware. Halting.",
                     hosted_err);
            const char *fatal_msg =
                "ESP32-C6 not responding.\n\n"
                "The on-board ESP32-C6 co-processor is not "
                "responding over the ESP-Hosted SDIO link. "
                "This usually means it has not been flashed "
                "with the matching ESP-Hosted slave firmware.\n\n"
                "Please flash the ESP32-C6 with the ESP-Hosted "
                "slave firmware, then power-cycle the device.\n\n"
                "See docs/tab5-esp-hosted.md for instructions.";
            if (draftling_lvgl_port_lock(-1)) {
                editor_ui_show_fatal(fatal_msg);
                draftling_lvgl_port_unlock();
            }
            /* Stop here. Do not proceed to ble_keyboard_init() /
             * wifi_manager / etc.: those abort the firmware when the
             * hosted link is down, which would wipe the message we
             * just put on screen. */
            while (true) {
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        }
        ESP_LOGI(TAG, "ESP-Hosted link up");
    }
#endif

    /* Bring up USB Host + HID keyboard driver (Tab5: USB-A port).
     * If a USB keyboard is enumerated within the probe window we
     * disable BLE so the wired keyboard is the sole input source;
     * if the user later unplugs the USB keyboard, the disconnect
     * handler in usb_kbd.cpp calls ble_keyboard_enable() and BLE
     * resumes scanning. BLE is brought up unconditionally below
     * so this hand-off works in both directions.
     */
    bool usb_kbd_present = false;
#if defined(CONFIG_DRAFTLING_HAS_USB_HOST)
    ESP_LOGI(TAG, "Starting USB host...");
    esp_err_t usb_err = bsp_usb_host_start(BSP_USB_HOST_POWER_MODE_USB_DEV,
                                            true /* limit to 500 mA */);
    if (usb_err != ESP_OK) {
        ESP_LOGW(TAG, "bsp_usb_host_start failed: %s",
                 esp_err_to_name(usb_err));
    } else if (usb_kbd_init() == 0) {
        const int probe_ms = CONFIG_DRAFTLING_USB_KBD_PROBE_MS;
        ESP_LOGI(TAG, "Probing for USB keyboard (%d ms)...", probe_ms);
        /* Poll every 50 ms so we exit as soon as enumeration
         * completes rather than always waiting the full window. */
        for (int waited = 0; waited < probe_ms; waited += 50) {
            if (usb_kbd_is_connected()) break;
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        usb_kbd_present = usb_kbd_is_connected();
    }
#endif

    /* Initialize Bluetooth keyboard.
     *
     * We always bring BLE up so it can take over as soon as the
     * user unplugs the USB keyboard (the USB disconnect handler in
     * usb_kbd.cpp calls ble_keyboard_enable() which restarts
     * scanning). When a USB keyboard is already present at boot
     * we initialise BLE and immediately disable it; the BT
     * controller stays up but the host stops scanning and stops
     * dispatching key events / status text. */
    if (usb_kbd_present) {
        ESP_LOGI(TAG, "USB keyboard detected; BLE will stay idle "
                      "until the USB keyboard is unplugged");
        /* editor_ui_init() left the "Searching for BLE keyboard..."
         * prompt screen active because at that point we did not yet
         * know whether a USB keyboard would enumerate. Jump straight
         * to the file browser; the BLE connect callback will not
         * fire while BLE is disabled. */
        if (draftling_lvgl_port_lock(-1)) {
            editor_ui_show_file_browser();
            draftling_lvgl_port_unlock();
        }
        ble_keyboard_init();
        ble_keyboard_disable();
    } else {
        ESP_LOGI(TAG, "Initializing Bluetooth keyboard...");
        /* Now that we know we are going down the BLE path, replace
         * the neutral "Initializing..." caption editor_ui_init()
         * put on the boot screen with the actual "Searching for BLE
         * keyboard..." prompt. Doing it here (rather than in
         * editor_ui_init) keeps that message off the screen on the
         * USB-keyboard path. */
        if (draftling_lvgl_port_lock(-1)) {
            editor_ui_set_ble_prompt_text(
                "Searching for BLE keyboard...\nPlease turn on your keyboard");
            draftling_lvgl_port_unlock();
        }
        ble_keyboard_init();
    }

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
     * On boards with a tight internal heap (e.g. M5Stack PaperS3,
     * ~138 KB free) eagerly calling esp_wifi_init() here fails with
     * ESP_ERR_NO_MEM because the WiFi static RX/TX buffers (DMA-
     * capable, must live in internal RAM) cannot fit alongside the
     * Bluedroid stack that ble_keyboard_init() just brought up. */

    /* Initialize Git sync */
    ESP_LOGI(TAG, "Initializing Git sync...");
    git_sync_init();

    /* Initialize standby manager (deep sleep on inactivity) */
    ESP_LOGI(TAG, "Initializing standby manager...");
    standby_init();
#if defined(CONFIG_DRAFTLING_MODEL_LILYGO_T5_EPD_S3_PRO)
    standby_set_pre_sleep_cb(pre_sleep_t5_deinit);
#elif defined(CONFIG_DRAFTLING_MODEL_M5STACK_TAB5)
    standby_set_pre_sleep_cb(pre_sleep_tab5_deinit);
#else
    standby_set_pre_sleep_cb(pre_sleep_autosave);
#endif

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
