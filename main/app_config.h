#pragma once

#include "sdkconfig.h"

/* Display dimensions (derived from Kconfig hardware model selection) */
#define LCD_WIDTH   CONFIG_WRITERDECK_LCD_WIDTH
#define LCD_HEIGHT  CONFIG_WRITERDECK_LCD_HEIGHT

/* SD Card mount point (shared across all hardware models) */
#define SD_MOUNT_POINT  "/sdcard"

#if defined(CONFIG_WRITERDECK_MODEL_WAVESHARE)
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

/* Deep-sleep wakeup on GPIO18 (EXT0, active-low) */
#define WAKEUP_GPIO_NUM 18

#elif defined(CONFIG_WRITERDECK_MODEL_M5STACK)
/* ----- M5Stack PaperS3 ----- */

/* E-Paper Display - SPI interface (IT8951 controller)
 * Pin assignments below match the M5Stack PaperS3 schematic.
 * Adjust if your board revision differs. */
#define EPD_MOSI_PIN    35
#define EPD_SCK_PIN     36
#define EPD_CS_PIN      33
#define EPD_RST_PIN     -1  /* hardware reset not controlled by MCU (RC circuit) */
#define EPD_BUSY_PIN    34  /* HRDY (host-ready) signal from IT8951 */

/* SD Card - SDMMC 1-bit interface */
#define SD_CLK_PIN      39
#define SD_CMD_PIN      38
#define SD_D0_PIN       40

/* I2C Bus (AXP2101 PMIC, etc.) */
#define I2C_SDA_PIN     12
#define I2C_SCL_PIN     11

/* Deep-sleep wakeup: power button resets via PMIC, no GPIO wakeup needed */
#define WAKEUP_GPIO_NUM -1

#else
#error "No hardware model selected. Run idf.py menuconfig and choose a model."
#endif
