#pragma once

#include "sdkconfig.h"

/* Display dimensions (derived from Kconfig hardware model selection) */
#define DISPLAY_WIDTH   CONFIG_DRAFTLING_DISPLAY_WIDTH
#define DISPLAY_HEIGHT  CONFIG_DRAFTLING_DISPLAY_HEIGHT
#define DISPLAY_ROTATE  CONFIG_DRAFTLING_DISPLAY_ROTATE_ANGLE

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

#else
#error "No hardware model selected. Run idf.py menuconfig and choose a model."
#endif
