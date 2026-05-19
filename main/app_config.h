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
#define BOARD_NAME      "Waveshare ESP32-S3-RLCD-4.2"

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
#define BOARD_NAME      "M5Stack PaperS3"

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

/* I2C bus (BM8563 RTC, BMI270 IMU and GT911 capacitive touch
 * controller on the PaperS3) */
#define I2C_SDA_PIN     41
#define I2C_SCL_PIN     42

/* GT911 capacitive touch controller.
 *
 * Pin assignments match the M5GFX board definition for M5PaperS3
 * (Touch_GT911 config: pin_int=GPIO48, pin_sda=GPIO41,
 * pin_scl=GPIO42, no RST GPIO). The GT911 reset line is not wired
 * to any ESP32-S3 pin on this board -- it is released by the
 * power-rail RC, so the touchscreen driver cannot perform the
 * INT-driven address-select reset sequence and instead probes
 * both possible I2C addresses (0x5D primary, 0x14 backup).
 *
 * Native panel coordinate range as reported by the GT911 is
 * 540 wide x 960 tall (portrait). The display driver calls M5GFX
 * setRotation(1) to present a 960x540 landscape framebuffer.
 *
 * Mirrors in native_to_logical() are applied to the raw native
 * axes *before* swap_xy, so with swap_xy=1:
 *   - mirror_x flips native_x, which (after the swap) flips
 *     logical_y (the screen's vertical axis);
 *   - mirror_y flips native_y, which (after the swap) flips
 *     logical_x (the screen's horizontal axis).
 * Verified on hardware (PaperS3 in landscape, USB-C on the right,
 * the orientation M5GFX setRotation(1) produces): only the
 * vertical screen axis needs to be inverted, which after the swap
 * corresponds to flipping native_x -- so mirror_x=1, mirror_y=0. */
#define TOUCH_I2C_ADDR  0x5D
#define TOUCH_INT_PIN   CONFIG_DRAFTLING_TOUCH_INT_GPIO
#define TOUCH_RST_PIN   CONFIG_DRAFTLING_TOUCH_RST_GPIO
#define TOUCH_NATIVE_W  540
#define TOUCH_NATIVE_H  960
#define TOUCH_SWAP_XY   1
#define TOUCH_MIRROR_X  1
#define TOUCH_MIRROR_Y  0

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

#elif defined(CONFIG_DRAFTLING_MODEL_WAVESHARE_TOUCH_LCD_349)
/* ----- Waveshare ESP32-S3-Touch-LCD-3.49 -----
 *
 * 3.49" IPS color LCD, 640 x 172, AXS15231B controller over QSPI.
 * Pin assignments below match the Waveshare wiki schematic
 * (https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-3.49); they
 * may need adjustment if Waveshare revises the board.
 *
 * Touch input (CST816 / I2C) is not used by Draftling.
 */
#define BOARD_NAME          "Waveshare ESP32-S3-Touch-LCD-3.49"

/* AXS15231B QSPI display interface */
#define LCD_QSPI_CS_PIN     10
#define LCD_QSPI_SCK_PIN    12
#define LCD_QSPI_D0_PIN     11
#define LCD_QSPI_D1_PIN     13
#define LCD_QSPI_D2_PIN     14
#define LCD_QSPI_D3_PIN     9
#define LCD_RST_PIN         17
#define LCD_TE_PIN          18
#define LCD_BL_PIN          2

/* SD card on a dedicated SPI bus */
#define SD_SPI_MOSI_PIN     6
#define SD_SPI_MISO_PIN     5
#define SD_SPI_SCK_PIN      4
#define SD_SPI_CS_PIN       7
#define SD_EN_PIN           -1

/* I2C bus (touch controller and other peripherals) */
#define I2C_SDA_PIN         15
#define I2C_SCL_PIN         16

/* No on-board battery monitor on this dev board */
#define BATT_ADC_PIN        -1
#define BATT_EN_PIN         -1
#define BATT_DIVIDER        1

/* Deep-sleep wakeup on BOOT (GPIO0, active-low strapping pin). */
#define WAKEUP_GPIO_NUM     0

#elif defined(CONFIG_DRAFTLING_MODEL_JC3248W535)
/* ----- Guition JC3248W535 -----
 *
 * 3.5" IPS color LCD, 480 x 320, AXS15231B controller over QSPI.
 * Pin assignments below come from the JC3248W535 reference schematic
 * widely shared in the LovyanGFX / LVGL community for this board;
 * verify against the actual silk-screen of your unit if init fails.
 *
 * Touch input (AXS5106L capacitive controller, I2C address 0x3B)
 * is used as a secondary input device on this board (the JC3248W535
 * has no user buttons besides the power switch). The touch INT line
 * doubles as the EXT0 deep-sleep wake source. The touch pins are
 * configurable via menuconfig under "DRAFTLING Configuration ->
 * Touchscreen INT/RST GPIO" so a non-stock wiring can be supported
 * without code changes.
 */
#define BOARD_NAME          "Guition JC3248W535"

/* AXS15231B QSPI display interface */
#define LCD_QSPI_CS_PIN     45
#define LCD_QSPI_SCK_PIN    47
#define LCD_QSPI_D0_PIN     21
#define LCD_QSPI_D1_PIN     48
#define LCD_QSPI_D2_PIN     40
#define LCD_QSPI_D3_PIN     39
/* The AXS15231B has no dedicated external reset pin on this
 * board (the Arduino reference driver passes GFX_NOT_DEFINED);
 * the controller is reset by its vendor unlock/lock SPI sequence.
 * GPIO4 is in fact the touch I2C SDA line -- driving it as a
 * push-pull RST output during display init would hold touch SDA
 * low and prevent the I2C bus from ever coming up. */
#define LCD_RST_PIN         -1
#define LCD_TE_PIN          38
#define LCD_BL_PIN          1

/* SD card on a dedicated SPI bus */
#define SD_SPI_MOSI_PIN     11
#define SD_SPI_MISO_PIN     13
#define SD_SPI_SCK_PIN      12
#define SD_SPI_CS_PIN       10
#define SD_EN_PIN           -1

/* I2C bus carrying the AXS15231B integrated touch controller.
 * Per the JC3248W535 board pinout (Tactility, Yoradio, edgerun
 * and multiple community references): SDA=GPIO4, SCL=GPIO8. */
#define I2C_SDA_PIN         4
#define I2C_SCL_PIN         8

/* AXS5106L touch controller. INT defaults to GPIO3 (RTC-capable,
 * required for wake-on-touch). The reset line is tied to the LCD
 * reset on this board so we leave the dedicated touch RST at -1.
 *
 * The controller reports coordinates in the panel's native portrait
 * orientation (320 wide x 480 tall). The LCD backend software-
 * rotates to 480x320 landscape, so we swap XY and mirror X on the
 * touch side to match. */
#define TOUCH_I2C_ADDR      0x3B
#define TOUCH_INT_PIN       CONFIG_DRAFTLING_TOUCH_INT_GPIO
#define TOUCH_RST_PIN       CONFIG_DRAFTLING_TOUCH_RST_GPIO
#define TOUCH_NATIVE_W      320
#define TOUCH_NATIVE_H      480
#define TOUCH_SWAP_XY       1
#define TOUCH_MIRROR_X      1
#define TOUCH_MIRROR_Y      0

/* No on-board battery monitor on this dev board */
#define BATT_ADC_PIN        -1
#define BATT_EN_PIN         -1
#define BATT_DIVIDER        1

/* Deep-sleep wakeup. The JC3248W535 has no BOOT button on an
 * RTC-capable GPIO, so the only way to wake the device is via the
 * touch INT line -- enabled by setting both DRAFTLING_TOUCHSCREEN
 * and DRAFTLING_STANDBY_WAKE_ON_TOUCH (the per-board default).
 * WAKEUP_GPIO_NUM below is just the fallback; the standby code
 * overrides it with CONFIG_DRAFTLING_TOUCH_INT_GPIO when
 * wake-on-touch is enabled. */
#define WAKEUP_GPIO_NUM     0

#elif defined(CONFIG_DRAFTLING_MODEL_LILYGO_TDISPLAY_S3)
/* ----- LilyGO T-Display-S3 -----
 *
 * 1.9" IPS color LCD, 320 x 170 (landscape), ST7789V controller
 * driven by an 8-bit parallel (Intel 8080 / i80) bus on the
 * ESP32-S3 LCD_CAM peripheral.
 *
 * Pin assignments below match the LilyGO T-Display-S3 schematic
 * shipped on the manufacturer's repository
 * (https://github.com/Xinyuan-LilyGO/T-Display-S3). The panel data
 * bus claims GPIO 39-42 and 45-48; control lines use 5-9 plus a
 * power-enable gate on GPIO15 (must be driven HIGH before the LCD
 * powers up). The on-board buttons map BOOT to GPIO0 and KEY to
 * GPIO14; we use BOOT as the EXT0 deep-sleep wake source for
 * consistency with the other boards.
 *
 * There is no on-board MicroSD slot. To use Draftling on this board
 * the user must wire an external SD module to a free SPI bus; the
 * pin slots below are placeholders for a recommended wiring on the
 * unused GPIO 10/11/12/13 group. Adjust to taste; sd_card_init_spi
 * will fail gracefully if no SD is connected and the editor falls
 * back to read-only with the "ERROR: SD card not ready" status.
 */
#define BOARD_NAME      "LilyGO T-Display-S3"

/* ST7789 8-bit parallel (i80) display interface */
#define LCD_PWR_EN_PIN  15  /* gates the LCD's 3V3 rail; must be HIGH */
#define LCD_BL_PIN      38
#define LCD_RST_PIN     5
#define LCD_CS_PIN      6
#define LCD_DC_PIN      7
#define LCD_WR_PIN      8
#define LCD_RD_PIN      9   /* tie HIGH; the i80 driver does not read */

#define LCD_D0_PIN      39
#define LCD_D1_PIN      40
#define LCD_D2_PIN      41
#define LCD_D3_PIN      42
#define LCD_D4_PIN      45
#define LCD_D5_PIN      46
#define LCD_D6_PIN      47
#define LCD_D7_PIN      48

/* External MicroSD on a dedicated SPI bus (none on-board; recommended
 * wiring on the back GPIO header). The pin numbers are user-editable
 * in menuconfig under "LilyGO T-Display-S3 external SD card"; defaults
 * map to the unused GPIO 10/11/12/13 group. Set any pin to -1 to
 * disable SD support. */
#define SD_SPI_MOSI_PIN CONFIG_DRAFTLING_LILYGO_SD_MOSI_PIN
#define SD_SPI_MISO_PIN CONFIG_DRAFTLING_LILYGO_SD_MISO_PIN
#define SD_SPI_SCK_PIN  CONFIG_DRAFTLING_LILYGO_SD_SCK_PIN
#define SD_SPI_CS_PIN   CONFIG_DRAFTLING_LILYGO_SD_CS_PIN
#define SD_EN_PIN       -1

/* I2C bus (exposed on the GROVE / STEMMA QT connector) */
#define I2C_SDA_PIN     43
#define I2C_SCL_PIN     44

/* Battery voltage monitor: cell -> 100k:100k divider -> GPIO4 (ADC1).
 * Matches the T-Display-S3 schematic. */
#define BATT_ADC_PIN    4
#define BATT_EN_PIN     -1
#define BATT_DIVIDER    2

/* Deep-sleep wakeup on BOOT (GPIO0, active-low strapping pin with
 * board-level pull-up; RTC-capable). */
#define WAKEUP_GPIO_NUM 0

#else
#error "No hardware model selected. Run idf.py menuconfig and choose a model."
#endif
