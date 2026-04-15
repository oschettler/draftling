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

/* Deep-sleep wakeup on GPIO18 (EXT0, active-low) */
#define WAKEUP_GPIO_NUM 18

#else
#error "No hardware model selected. Run idf.py menuconfig and choose a model."
#endif
