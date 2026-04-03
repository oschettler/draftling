#pragma once

/* RLCD Display - SPI interface */
#define RLCD_MOSI_PIN   12
#define RLCD_SCK_PIN    11
#define RLCD_DC_PIN     5
#define RLCD_CS_PIN     40
#define RLCD_RST_PIN    41
#define RLCD_TE_PIN     6
#define LCD_WIDTH       400
#define LCD_HEIGHT      300

/* SD Card - SDMMC interface */
#define SD_CLK_PIN      38
#define SD_CMD_PIN      21
#define SD_D0_PIN       39
#define SD_MOUNT_POINT  "/sdcard"

/* I2C Bus */
#define I2C_SDA_PIN     13
#define I2C_SCL_PIN     14
