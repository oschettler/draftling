#pragma once

#include "sdkconfig.h"

/* Display dimensions (derived from Kconfig hardware model selection) */
#define DISPLAY_WIDTH   CONFIG_DRAFTLING_DISPLAY_WIDTH
#define DISPLAY_HEIGHT  CONFIG_DRAFTLING_DISPLAY_HEIGHT
#define DISPLAY_ROTATE  CONFIG_DRAFTLING_DISPLAY_ROTATE_ANGLE

/* Logical pixel size: every logical LVGL pixel is rendered as
 * DISPLAY_SCALE x DISPLAY_SCALE panel pixels by the display backend
 * (nearest-neighbor expansion). The editor and LVGL canvas use
 * DISPLAY_LOGICAL_WIDTH / DISPLAY_LOGICAL_HEIGHT; only the display
 * backend deals in physical panel pixels. */
#define DISPLAY_SCALE          CONFIG_DRAFTLING_DISPLAY_SCALE
#define DISPLAY_LOGICAL_WIDTH  (DISPLAY_WIDTH  / DISPLAY_SCALE)
#define DISPLAY_LOGICAL_HEIGHT (DISPLAY_HEIGHT / DISPLAY_SCALE)

/* SD Card mount point (shared across all hardware models) */
#define SD_MOUNT_POINT  "/sdcard"

#if defined(CONFIG_DRAFTLING_MODEL_WAVESHARE_RLCD42)
/* ----- Waveshare ESP32-S3-RLCD-4.2 ----- */

/* RLCD Display - SPI interface */
#define RLCD_MOSI_PIN   12
#define RLCD_SCK_PIN    11
#define RLCD_DC_PIN     5
#define RLCD_CS_PIN     40
#define RLCD_RST_PIN    41
#define RLCD_TE_PIN     6

/* SD Card - SDMMC 1-bit interface */
#define SD_CLK_PIN      38
#define SD_CMD_PIN      21
#define SD_D0_PIN       39

/* I2C Bus */
#define I2C_SDA_PIN     13
#define I2C_SCL_PIN     14

/* Battery voltage ADC (GPIO4, ADC1_CH3, 3:1 divider) */
#define BATT_ADC_PIN    4
#define BATT_EN_PIN     -1
#define BATT_DIVIDER    3

/* Deep-sleep wakeup on GPIO18 (EXT0, active-low) */
#define WAKEUP_GPIO_NUM 18

#elif defined(CONFIG_DRAFTLING_MODEL_SEEED_RETERMINAL_E1001)
/* ----- Seeed Studio reTerminal E1001 ----- */

/* UC8179 e-paper display - SPI interface
 * (SCK and MOSI are shared with the SD card on the same SPI bus.) */
#define EPD_MOSI_PIN    9
#define EPD_SCK_PIN     7
#define EPD_DC_PIN      11
#define EPD_CS_PIN      10
#define EPD_RST_PIN     12
#define EPD_BUSY_PIN    13

/* SD Card - SPI interface (shares SCK/MOSI bus with the e-paper) */
#define SD_SPI_MOSI_PIN 9
#define SD_SPI_MISO_PIN 8
#define SD_SPI_SCK_PIN  7
#define SD_SPI_CS_PIN   14
#define SD_EN_PIN       16
#define SD_DET_PIN      15

/* I2C Bus (XIAO default pads) */
#define I2C_SDA_PIN     6
#define I2C_SCL_PIN     5

/* Battery voltage ADC (GPIO1, ADC1_CH0, 2:1 divider, enable on GPIO21) */
#define BATT_ADC_PIN    1
#define BATT_EN_PIN     21
#define BATT_DIVIDER    2

/* Deep-sleep wakeup on GPIO3 (KEY0 / right green button, active-low) */
#define WAKEUP_GPIO_NUM 3

#elif defined(CONFIG_DRAFTLING_MODEL_WAVESHARE_EPD_HAT)
/* ----- Waveshare E-Paper Driver HAT (generic ESP32 host) -----
 *
 * Selectable on any BLE-capable ESP-IDF target (ESP32, S3, C2, C3,
 * C6, H2). The HAT itself is a passive SPI breakout for UC8179-class
 * panels and carries no MCU. Defaults below match the
 * ESP32-S3-DevKitC-1 wiring used in Waveshare's example projects,
 * but every pin is overridable via Kconfig (see "Waveshare E-Paper
 * Driver HAT pinout" menu) so the same model works on, e.g., a
 * classic ESP32-DevKitC or an ESP32-C6-DevKitM-1.
 */

#define EPD_MOSI_PIN    CONFIG_DRAFTLING_HAT_EPD_MOSI_PIN
#define EPD_SCK_PIN     CONFIG_DRAFTLING_HAT_EPD_SCK_PIN
#define EPD_DC_PIN      CONFIG_DRAFTLING_HAT_EPD_DC_PIN
#define EPD_CS_PIN      CONFIG_DRAFTLING_HAT_EPD_CS_PIN
#define EPD_RST_PIN     CONFIG_DRAFTLING_HAT_EPD_RST_PIN
#define EPD_BUSY_PIN    CONFIG_DRAFTLING_HAT_EPD_BUSY_PIN
/* Panel power-enable pin. Driven HIGH at boot (before display_init)
 * and LOW just before deep sleep so the panel is unpowered while the
 * MCU is asleep. Set to -1 in Kconfig if PWR is wired permanently
 * high. */
#define EPD_PWR_PIN     CONFIG_DRAFTLING_HAT_EPD_PWR_PIN

/* Optional SD card. The interface is selected via the
 * DRAFTLING_HAT_SD_INTERFACE choice (None / SPI / SDMMC 1-bit).
 * CONFIG_DRAFTLING_HAT_HAS_SD is auto-selected by Kconfig whenever
 * any non-None interface is picked, so existing checks for it
 * continue to mean "is there an SD card at all". */
#ifdef CONFIG_DRAFTLING_HAT_SD_SPI
#define SD_SPI_MOSI_PIN CONFIG_DRAFTLING_HAT_SD_MOSI_PIN
#define SD_SPI_MISO_PIN CONFIG_DRAFTLING_HAT_SD_MISO_PIN
#define SD_SPI_SCK_PIN  CONFIG_DRAFTLING_HAT_SD_SCK_PIN
#define SD_SPI_CS_PIN   CONFIG_DRAFTLING_HAT_SD_CS_PIN
#define SD_EN_PIN       -1
#endif
#ifdef CONFIG_DRAFTLING_HAT_SD_SDMMC
#define SD_CLK_PIN      CONFIG_DRAFTLING_HAT_SD_SDMMC_CLK_PIN
#define SD_CMD_PIN      CONFIG_DRAFTLING_HAT_SD_SDMMC_CMD_PIN
#define SD_D0_PIN       CONFIG_DRAFTLING_HAT_SD_SDMMC_D0_PIN
#endif

/* Optional battery monitor on a user-selected ADC1 pin. Disabled by
 * default (CONFIG_DRAFTLING_HAT_BATT_ADC_PIN = -1) since the bare HAT
 * has no battery wiring; battery_init() then becomes a no-op and the
 * editor UI hides the battery icon. Configure under
 * "Waveshare E-Paper Driver HAT pinout" in menuconfig if the host
 * board has a cell wired to an ADC1 pin through a resistive divider. */
#define BATT_ADC_PIN    CONFIG_DRAFTLING_HAT_BATT_ADC_PIN
#if CONFIG_DRAFTLING_HAT_BATT_ADC_PIN >= 0
#define BATT_EN_PIN     CONFIG_DRAFTLING_HAT_BATT_EN_PIN
#define BATT_DIVIDER    CONFIG_DRAFTLING_HAT_BATT_DIVIDER
#else
#define BATT_EN_PIN     -1
#define BATT_DIVIDER    1
#endif

/* Deep-sleep wakeup pin (active-low). User-configurable via the
 * CONFIG_DRAFTLING_HAT_WAKEUP_GPIO option (default GPIO0, the BOOT
 * button on most ESP32-S3 dev kits). */
#define WAKEUP_GPIO_NUM CONFIG_DRAFTLING_HAT_WAKEUP_GPIO

#elif defined(CONFIG_DRAFTLING_MODEL_M5STACK_PAPERS3)
/* ----- M5Stack PaperS3 -----
 *
 * The PaperS3 drives an ED047TC1 e-paper panel (panel_width=960,
 * panel_height=540 in M5GFX, with offset_rotation=3) via the
 * ESP32-S3 LCD/I80 parallel peripheral. The display driver calls
 * setRotation(1) so the user-visible dimensions are 960x540
 * landscape ("horizontal"). The panel data bus, control lines and
 * power-rail enable are configured by the m5stack/M5GFX library
 * internally (board id "M5PaperS3"); we do not redefine those GPIOs
 * here. See the M5Stack hardware reference for the full pin list:
 * https://docs.m5stack.com/en/core/papers3
 *
 * Pins listed below are the ones Draftling itself touches outside of
 * the display driver (SD card, wakeup, optional I2C).
 */

/* Onboard MicroSD on a dedicated SPI host (SPI3 - the EPD parallel bus
 * driven by M5GFX claims GPIO 6-18 plus 45/46 for its data/control
 * lines, so the SD slot must use a separate set of pins).
 *
 * Pin assignments verified against the M5Stack PaperS3 hardware
 * reference and the FastEPD/Arduino "papers3_screenshot" example:
 *   CS=47, SCK=39, MOSI=38, MISO=40. */
#define SD_SPI_MOSI_PIN 38
#define SD_SPI_MISO_PIN 40
#define SD_SPI_SCK_PIN  39
#define SD_SPI_CS_PIN   47
#define SD_EN_PIN       -1

/* I2C bus (BM8563 RTC and BMI270 IMU on the PaperS3) */
#define I2C_SDA_PIN     41
#define I2C_SCL_PIN     42

/* Battery voltage monitor.
 *
 * The PaperS3 routes the LiPo cell through a 1:2 resistive divider
 * (R_top == R_bottom, V_pin = V_bat / 2) into ADC1 channel 2 on
 * GPIO3. There is no enable transistor -- the divider is always
 * powered. Pin and divider ratio match the M5Unified Power_Class
 * configuration for board_M5PaperS3 (BAT_ADC = ADC1_GPIO3,
 * adc_ratio = 2.0). The battery component will multiply the
 * measured pin voltage by BATT_DIVIDER (=2) to recover the actual
 * cell voltage.
 *
 * GPIO4 carries the charger CHG_STAT signal on the PaperS3 (active
 * low while charging). It is not wired through the battery API yet;
 * the editor only displays the cell percentage. */
#define BATT_ADC_PIN    3
#define BATT_EN_PIN     -1
#define BATT_DIVIDER    2

/* Deep-sleep wakeup on the BOOT button (GPIO0, active-low).
 *
 * Earlier revisions tried GPIO21 (wrong -- that's the on-board
 * buzzer/speaker pin) and GPIO48 (the GT911 touch-panel INT line).
 * GPIO48 also failed: M5GFX initializes only the e-paper panel, not
 * the touch controller, so the GT911 is left uninitialized and holds
 * its INT line low (the line doubles as I2C-address selection during
 * reset). The standby manager would then see GPIO48 stuck low and
 * fire the GPIO_INTR_LOW_LEVEL wake immediately.
 *
 * GPIO0 is the only digital input button on the PaperS3 besides the
 * hardware power switch, and -- being an ESP32-S3 strapping pin with
 * a board-level pull-up -- is guaranteed high while the device is
 * idle. It is also RTC-capable, so we can use real EXT0 deep sleep
 * (matching the other supported boards) instead of the light-sleep
 * + esp_restart() workaround. */
#define WAKEUP_GPIO_NUM 0

#else
#error "No hardware model selected. Run idf.py menuconfig and choose a model."
#endif
