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

#elif defined(CONFIG_DRAFTLING_MODEL_LILYGO_T5_EPD_S3_PRO) || \
      defined(CONFIG_DRAFTLING_MODEL_LILYGO_T5_EPD_S3_PRO_LITE)
/* ----- LilyGO T5 E-Paper S3 Pro / Pro Lite -----
 *
 * 4.7" ED047TC1 e-paper panel, 960 x 540 landscape, driven by the
 * vroland/epdiy library's `epd_board_v7` configuration: 8-bit
 * parallel data bus + control lines on direct ESP32-S3 GPIOs, plus
 * a TPS65185 high-voltage power-management IC commanded over I2C via
 * a PCA9535PW I/O expander (I2C addr 0x20). All EPD pin assignments
 * are encapsulated inside epdiy's `epd_board_v7.c` so they are NOT
 * redefined here -- Draftling only needs to know about the
 * peripherals it touches itself (SD card, I2C touch, BOOT button).
 *
 * The "Pro" and "Pro Lite" SKUs share the same H752-01 schematic.
 * The only difference is that the Lite variant depopulates the
 * SX1262 LoRa radio (CS = GPIO 46) and the MIA-M10Q GPS receiver;
 * neither is used by Draftling, so the pin map below is identical
 * between the two variants.
 *
 * References:
 *   https://github.com/Xinyuan-LilyGO/T5S3-4.7-e-paper-PRO
 *     docs/pin_define.md, docs/pinmap.md, examples/factory/main/utilities.h
 *   https://github.com/vroland/epdiy
 *     src/board/epd_board_v7.c, README.md "LilyGo Boards" table
 *     (LilyGo T5 S3 E-Paper Pro -> ED047TC1 -> epd_board_v7).
 */
#if defined(CONFIG_DRAFTLING_MODEL_LILYGO_T5_EPD_S3_PRO)
#define BOARD_NAME      "LilyGO T5 E-Paper S3 Pro"
#else
#define BOARD_NAME      "LilyGO T5 E-Paper S3 Pro Lite"
#endif

/* On-board MicroSD card on a dedicated SPI bus.
 *
 * Pin assignments per the LilyGO reference:
 *   examples/factory/main/utilities.h
 *     #define BOARD_SD_MOSI 13
 *     #define BOARD_SD_MISO 21
 *     #define BOARD_SD_SCK  14
 *     #define BOARD_SD_CS   12
 *
 * The SPI bus (GPIO 13/21/14) is *shared* with the SX1262 LoRa
 * radio (CS = GPIO 46) on the Pro variant. The Lite variant does
 * not have LoRa installed, so the bus is SD-only there. To keep
 * the same code path on both SKUs and avoid the well-known
 * "intermittent SD init failure with LoRa CS floating" problem
 * (LilyGO T5S3-4.7-e-paper-PRO issue #3), main.cpp drives LoRa CS
 * HIGH before sd_card_init_spi() runs. On the Lite the GPIO is
 * just an unconnected output which is harmless. */
#define SD_SPI_MOSI_PIN 13
#define SD_SPI_MISO_PIN 21
#define SD_SPI_SCK_PIN  14
#define SD_SPI_CS_PIN   12
#define SD_EN_PIN       -1

/* SPI CS of the SX1262 LoRa radio (Pro variant). main.cpp drives
 * this HIGH before SD init to prevent the LoRa chip from
 * intercepting SPI traffic intended for the SD slot. Harmless on
 * the Lite variant where the LoRa silicon is depopulated. */
#define BOARD_LORA_CS_PIN 46

/* I2C bus carrying the GT911 capacitive touch controller, the
 * PCF8563TS RTC, the BQ27220 fuel gauge, the BQ25896 charger, the
 * PCA9535 I/O expander (also used by epdiy for EPD power-rail
 * control) and the TPS65185 EPD power IC. */
#define I2C_SDA_PIN     39
#define I2C_SCL_PIN     40

/* GT911 capacitive touch controller (present on both Pro and Lite
 * per the LilyGO docs/pin_define.md hardware section, even though
 * Draftling defaults to TOUCHSCREEN=y only on the Pro SKU).
 *
 * Native panel coordinate range as reported by the GT911 is
 * 540 wide x 960 tall (portrait); the display backend presents the
 * panel as 960x540 landscape via epd_set_rotation(EPD_ROT_LANDSCAPE),
 * so the touch driver swaps XY to match (same convention as the
 * M5Stack PaperS3 GT911 block above).
 *
 * NOTE: the GT911 lives on the same I2C bus as epdiy's PCA9535 +
 * TPS65185. epdiy creates its own bus on I2C port 0 inside
 * epd_init(), so the touchscreen component cannot create a second
 * bus on the same port. Until the touchscreen component grows
 * support for an externally-supplied bus handle, touch on this
 * board may fail to initialise at runtime (logged as a touchscreen
 * init error; firmware continues without touch). */
#define TOUCH_I2C_ADDR  0x5D
#define TOUCH_INT_PIN   CONFIG_DRAFTLING_TOUCH_INT_GPIO
#define TOUCH_RST_PIN   CONFIG_DRAFTLING_TOUCH_RST_GPIO
#define TOUCH_NATIVE_W  540
#define TOUCH_NATIVE_H  960
#define TOUCH_SWAP_XY   1
#define TOUCH_MIRROR_X  1
#define TOUCH_MIRROR_Y  0

/* Battery monitor.
 *
 * The T5 E-Paper S3 Pro routes the LiPo cell through a BQ25896
 * charger + BQ27220YZFR fuel gauge, both on the shared I2C bus
 * (BQ27220 at 0x55, BQ25896 at 0x6B). There is NO dedicated GPIO
 * ADC pin for the cell voltage. The ADC-only battery component
 * (components/battery/battery.cpp) therefore cannot read the
 * battery on this board; we set BATT_ADC_PIN=-1 (battery_init
 * becomes a no-op) and DRAFTLING_HAS_BATTERY is left unset so the
 * editor UI hides its battery indicator. A future fuel-gauge-aware
 * battery backend would replace this stub. */
#define BATT_ADC_PIN    -1
#define BATT_EN_PIN     -1
#define BATT_DIVIDER    1

/* Deep-sleep wakeup on the BOOT button (GPIO 0, active-low strapping
 * pin with a board-level pull-up; RTC-capable so EXT0 wake works).
 * Matches BOARD_BOOT_BTN in examples/factory/main/utilities.h. */
#define WAKEUP_GPIO_NUM 0

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
 * 3.49" IPS color LCD, 172 x 640 native portrait (presented as
 * 640 x 172 landscape via software rotation), AXS15231B controller
 * over QSPI. Pin assignments below are taken verbatim from the
 * official Waveshare reference firmware
 * (Examples/Arduino/09_LVGL_V8_Test/user_config.h at
 * https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.49); they
 * differ from the placeholder values used during early bring-up.
 *
 * Touch input is an Allystar AXS5106-family capacitive controller
 * sharing the AXS15231B's I2C protocol family. INT and RST are not
 * wired to the ESP32 on this board (Waveshare's own example also
 * sets both to -1).
 */
#define BOARD_NAME          "Waveshare ESP32-S3-Touch-LCD-3.49"

/* AXS15231B QSPI display interface */
#define LCD_QSPI_CS_PIN     9
#define LCD_QSPI_SCK_PIN    10
#define LCD_QSPI_D0_PIN     11
#define LCD_QSPI_D1_PIN     12
#define LCD_QSPI_D2_PIN     13
#define LCD_QSPI_D3_PIN     14
#define LCD_RST_PIN         21
#define LCD_TE_PIN          -1   /* TE not wired on this board */
/* GPIO 8 drives the BL boost-converter enable, and is wired
 * **active LOW**: a logical LOW on the pin turns the backlight ON
 * at full brightness, a logical HIGH turns it OFF. This is the
 * polarity used by Waveshare's own reference firmware
 * (Examples/ESP-IDF/10_LVGL_V9_Test/components/lcd_bl_pwm_bsp/
 * lcd_bl_pwm_bsp.h), where `LCD_PWM_MODE_255` (the brightest level)
 * is defined as `(0xff - 255) = 0` and `LCD_PWM_MODE_0` (off) is
 * `(0xff - 0) = 255`.
 *
 * We mirror that reference exactly: an LEDC channel on GPIO 8
 * driven at duty 0 keeps the boost circuit on at full brightness,
 * and intermediate duties dim the panel proportionally. The pin is
 * pre-configured as an output with the internal pull-up enabled
 * before the LEDC channel is bound so the line never floats during
 * the boot-to-LEDC handoff. `LCD_BL_ACTIVE_LOW = 1` tells the
 * AXS15231B backend to invert the percent->duty mapping (and to
 * drive the pin HIGH for deep-sleep BL cut).
 *
 * Earlier bring-up attempts on this board left the pin high-Z
 * (LCD_BL_PIN = -1) because we had inverted the polarity and the
 * "100 % brightness" duty was actually OFF, producing a "screen
 * dark all session, flashes only at deep-sleep entry" symptom.
 * Verified working against Waveshare's 10_LVGL_V9_Test example
 * with `Backlight_Testing` enabled (the brightness loop cycles
 * 255 -> 175 -> 125 -> 0 raw duties and the panel dims
 * accordingly). */
#define LCD_BL_PIN          8
#define LCD_BL_ACTIVE_LOW   1

/* On-board MicroSD card slot on a dedicated SPI bus. Pin assignments
 * match the official Waveshare ESP32-S3-Touch-LCD-3.49 pin map
 * (https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-3.49): SD_CS=38,
 * SD_MOSI=39, SD_MISO=40, SD_SCLK=41. The earlier placeholder values
 * on GPIO 4-7 were both wrong (the board really does have an on-board
 * SD slot) and conflicted with the on-board BAT_ADC on IO4. */
#define SD_SPI_MOSI_PIN     39
#define SD_SPI_MISO_PIN     40
#define SD_SPI_SCK_PIN      41
#define SD_SPI_CS_PIN       38
#define SD_EN_PIN           -1

/* I2C bus carrying the AXS15231-family touch controller. Pins match
 * Touch_SDA_NUM / Touch_SCL_NUM in Waveshare's user_config.h. */
#define I2C_SDA_PIN         17
#define I2C_SCL_PIN         18

/* AXS5106-family touch controller. INT and RST are not wired on
 * this board (both -1 in Waveshare's reference). The controller
 * reports coordinates in the panel's landscape orientation
 * (640 wide x 172 tall) but with both axes flipped relative to
 * the LCD scan-out (per Waveshare's USER_DISP_ROT_90 touch handler:
 * data->point.x = LCD_H_RES - pointX, data->point.y = LCD_V_RES -
 * pointY), so mirror both X and Y. */
#define TOUCH_I2C_ADDR      0x3B
#define TOUCH_INT_PIN       CONFIG_DRAFTLING_TOUCH_INT_GPIO
#define TOUCH_RST_PIN       CONFIG_DRAFTLING_TOUCH_RST_GPIO
#define TOUCH_NATIVE_W      640
#define TOUCH_NATIVE_H      172
#define TOUCH_SWAP_XY       0
#define TOUCH_MIRROR_X      1
#define TOUCH_MIRROR_Y      1

/* On-board battery monitor.
 *
 * The Touch-LCD-3.49 wires the LiPo cell through a ~3:1 resistive
 * divider into GPIO 4 (ADC1_CH3). Matches the Waveshare BAT_ADC pin
 * map in the official wiki and the `01_ADC_Test/adc_bsp.c` reference
 * firmware, which recovers the cell voltage with
 * `*value = 0.001 * vol * 3` -- i.e. BATT_DIVIDER = 3, not the 2
 * we initially guessed (a 2 multiplier produced a ~18 % reading on
 * a fully charged cell, since the table maps ~2.8 V to ~0 %). */
#define BATT_ADC_PIN        4
#define BATT_EN_PIN         -1
#define BATT_DIVIDER        3

/* Power management.
 *
 * The Touch-LCD-3.49 has a hardware power latch that keeps the
 * battery rail alive after the user releases the PWR button: a
 * TCA9554 I2C IO-expander pin 6 must be driven HIGH at boot to keep
 * the latch closed, and driving it LOW cuts the battery rail and
 * fully powers the board off. The PWR button itself is wired to
 * GPIO 16 (active LOW) -- a momentary press on the unpowered board
 * applies VBAT just long enough for the firmware to latch IO6 HIGH;
 * holding the same button while powered triggers the firmware's
 * long-press handler which auto-saves and cuts the latch.
 *
 * The TCA9554 lives on a *second* I2C bus on GPIO 47 (SDA) / GPIO 48
 * (SCL), shared with the on-board BM8563 RTC and BMI270 IMU; the
 * touch-controller I2C bus on GPIO 17 / 18 above is separate and is
 * owned by the touchscreen component.
 *
 * Pin assignments and the TCA9554 latch bit match the Waveshare
 * reference firmware (Examples/ESP-IDF/07_BATT_PWR_Test/components/
 * user_app/user_app.cpp and i2c_bsp/i2c_bsp.c). */
#define PWR_BUTTON_GPIO     16
#define PWR_I2C_SDA_PIN     47
#define PWR_I2C_SCL_PIN     48
#define PWR_TCA9554_ADDR    0x20  /* A2=A1=A0=0 */
#define PWR_TCA9554_LATCH_BIT 6

/* Deep-sleep wakeup on BOOT (GPIO0, active-low strapping pin).
 * Used only on USB power: on battery, the inactivity timer cuts the
 * power latch (via the `power` component) instead of entering deep
 * sleep, so the wake source is then the PWR button on GPIO 16
 * (which re-applies VBAT and cold-boots the firmware). */
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

/* Default: no deep-sleep-only BL cut pin. Boards whose BL cannot
 * be cut via the normal LCD_BL_PIN path (because the BL boost
 * circuit does not tolerate the ESP32 driving the BL enable pin
 * during normal operation) may override this to drive a separate
 * pin LOW only at deep-sleep entry. No current board uses this
 * fallback; kept available for future boards that need it. */
#ifndef LCD_BL_DEEP_SLEEP_CUT_PIN
#define LCD_BL_DEEP_SLEEP_CUT_PIN   -1
#endif

/* Default: BL pin is active HIGH (LEDC duty MAX = full brightness,
 * duty 0 = off). Boards whose BL boost circuit is active LOW
 * (LEDC duty 0 = full brightness, duty MAX = off) override this
 * to 1 -- see the Waveshare ESP32-S3-Touch-LCD-3.49 block above. */
#ifndef LCD_BL_ACTIVE_LOW
#define LCD_BL_ACTIVE_LOW   0
#endif
