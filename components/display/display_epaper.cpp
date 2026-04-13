/*
 * E-Paper display driver for M5Stack PaperS3 (IT8951 controller).
 *
 * Implements the same display.h API as the RLCD driver so that the
 * rest of the firmware (LVGL port, editor UI) works unchanged.
 *
 * The IT8951 communicates over SPI using 16-bit transfers with a
 * preamble word that selects command / write-data / read-data mode.
 * A HRDY (host-ready) pin indicates when the controller is ready to
 * accept the next transaction.
 */

#include <cstdio>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>

#include "display.h"

static const char *TAG = "EPD";

/* IT8951 SPI preamble words */
#define IT8951_PREAMBLE_CMD   0x6000
#define IT8951_PREAMBLE_WRITE 0x0000
#define IT8951_PREAMBLE_READ  0x1000

/* IT8951 host commands */
#define IT8951_CMD_SYS_RUN       0x0001
#define IT8951_CMD_STANDBY       0x0002
#define IT8951_CMD_SLEEP         0x0003
#define IT8951_CMD_REG_RD        0x0010
#define IT8951_CMD_REG_WR        0x0011
#define IT8951_CMD_LD_IMG_AREA   0x0021
#define IT8951_CMD_LD_IMG_END    0x0022
#define IT8951_CMD_DPY_AREA      0x0034
#define IT8951_CMD_GET_DEV_INFO  0x0302

/* IT8951 display update modes */
#define IT8951_MODE_INIT  0  /* full clear / init */
#define IT8951_MODE_DU    1  /* direct update (fast, 1-bit) */
#define IT8951_MODE_GC16  2  /* 16-level grayscale (slow, high quality) */

/* IT8951 device-info structure returned by GET_DEV_INFO */
typedef struct {
    uint16_t panel_w;
    uint16_t panel_h;
    uint16_t img_buf_addr_l;
    uint16_t img_buf_addr_h;
    char     fw_version[16];
    char     lut_version[16];
} it8951_dev_info_t;

static spi_device_handle_t s_spi       = NULL;
static int                 s_busy_pin  = -1;
static int                 s_rst_pin   = -1;
static uint8_t            *s_disp_buf  = NULL;
static int                 s_disp_len  = 0;
static int                 s_width     = 0;
static int                 s_height    = 0;
static uint32_t            s_img_buf_addr = 0;  /* IT8951 on-chip image buffer */

/* ---- low-level SPI helpers ---- */

static void wait_busy(void)
{
    if (s_busy_pin < 0) return;
    while (gpio_get_level((gpio_num_t)s_busy_pin) == 0) {
        vTaskDelay(1);
    }
}

static void spi_write16(uint16_t val)
{
    uint8_t tx[2] = { (uint8_t)(val >> 8), (uint8_t)(val & 0xFF) };
    spi_transaction_t t = {};
    t.length    = 16;
    t.tx_buffer = tx;
    spi_device_transmit(s_spi, &t);
}

static uint16_t spi_read16(void)
{
    uint8_t rx[2] = {};
    spi_transaction_t t = {};
    t.length    = 16;
    t.rxlength  = 16;
    t.rx_buffer = rx;
    spi_device_transmit(s_spi, &t);
    return ((uint16_t)rx[0] << 8) | rx[1];
}

static void it8951_write_cmd(uint16_t cmd)
{
    wait_busy();
    spi_write16(IT8951_PREAMBLE_CMD);
    wait_busy();
    spi_write16(cmd);
}

static void it8951_write_data(uint16_t data)
{
    wait_busy();
    spi_write16(IT8951_PREAMBLE_WRITE);
    wait_busy();
    spi_write16(data);
}

static uint16_t it8951_read_data(void)
{
    wait_busy();
    spi_write16(IT8951_PREAMBLE_READ);
    wait_busy();
    spi_read16();  /* dummy read */
    return spi_read16();
}

static void it8951_write_reg(uint16_t addr, uint16_t val)
{
    it8951_write_cmd(IT8951_CMD_REG_WR);
    it8951_write_data(addr);
    it8951_write_data(val);
}

/* ---- IT8951 high-level helpers ---- */

static void it8951_get_dev_info(it8951_dev_info_t *info)
{
    it8951_write_cmd(IT8951_CMD_GET_DEV_INFO);

    wait_busy();
    spi_write16(IT8951_PREAMBLE_READ);
    wait_busy();
    spi_read16();  /* dummy */

    uint16_t *raw = (uint16_t *)info;
    /* The struct is 20 uint16_t words (40 bytes) */
    for (int i = 0; i < (int)(sizeof(it8951_dev_info_t) / 2); i++) {
        raw[i] = spi_read16();
    }
}

static void it8951_set_target_mem_addr(uint32_t addr)
{
    /* LISAR register pair at 0x0200 (high) and 0x0208 (low) -- see IT8951 docs */
    it8951_write_reg(0x0208, (uint16_t)(addr & 0xFFFF));
    it8951_write_reg(0x0200, (uint16_t)((addr >> 16) & 0xFFFF));
}

static void it8951_load_image_area(int x, int y, int w, int h,
                                   const uint8_t *data)
{
    it8951_set_target_mem_addr(s_img_buf_addr);

    /* LD_IMG_AREA command args: endian-type/pixel-format/rotate, x, y, w, h */
    it8951_write_cmd(IT8951_CMD_LD_IMG_AREA);
    it8951_write_data(0x0003);  /* 8bpp, no rotate, little-endian */
    it8951_write_data((uint16_t)x);
    it8951_write_data((uint16_t)y);
    it8951_write_data((uint16_t)w);
    it8951_write_data((uint16_t)h);

    /* Stream pixel data as 16-bit words (two 8-bit pixels per word) */
    int total = w * h;
    wait_busy();
    spi_write16(IT8951_PREAMBLE_WRITE);
    for (int i = 0; i < total; i += 2) {
        uint16_t word = ((uint16_t)data[i] << 8);
        if (i + 1 < total) word |= data[i + 1];
        wait_busy();
        spi_write16(word);
    }

    it8951_write_cmd(IT8951_CMD_LD_IMG_END);
}

static void it8951_display_area(int x, int y, int w, int h, int mode)
{
    it8951_write_cmd(IT8951_CMD_DPY_AREA);
    it8951_write_data((uint16_t)x);
    it8951_write_data((uint16_t)y);
    it8951_write_data((uint16_t)w);
    it8951_write_data((uint16_t)h);
    it8951_write_data((uint16_t)mode);
}

/* ---- public API (display.h) ---- */

extern "C" void display_init(int mosi, int sck, int dc_or_cs, int cs_or_rst,
                              int rst_or_busy, int width, int height)
{
    /*
     * For the IT8951-based e-paper the parameters are mapped as:
     *   mosi         = SPI MOSI
     *   sck          = SPI SCK
     *   dc_or_cs     = SPI CS (IT8951 has no D/C line)
     *   cs_or_rst    = RST  (-1 if unused)
     *   rst_or_busy  = BUSY / HRDY
     */
    int cs   = dc_or_cs;
    s_rst_pin  = cs_or_rst;
    s_busy_pin = rst_or_busy;
    s_width  = width;
    s_height = height;

    /* Configure BUSY pin as input */
    if (s_busy_pin >= 0) {
        gpio_config_t io = {};
        io.intr_type    = GPIO_INTR_DISABLE;
        io.mode         = GPIO_MODE_INPUT;
        io.pin_bit_mask = (1ULL << s_busy_pin);
        io.pull_up_en   = GPIO_PULLUP_ENABLE;
        gpio_config(&io);
    }

    /* Optional HW reset */
    if (s_rst_pin >= 0) {
        gpio_config_t io = {};
        io.intr_type    = GPIO_INTR_DISABLE;
        io.mode         = GPIO_MODE_OUTPUT;
        io.pin_bit_mask = (1ULL << s_rst_pin);
        io.pull_up_en   = GPIO_PULLUP_ENABLE;
        gpio_config(&io);
        gpio_set_level((gpio_num_t)s_rst_pin, 1);
        vTaskDelay(pdMS_TO_TICKS(50));
        gpio_set_level((gpio_num_t)s_rst_pin, 0);
        vTaskDelay(pdMS_TO_TICKS(20));
        gpio_set_level((gpio_num_t)s_rst_pin, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    /* Initialize SPI bus */
    spi_bus_config_t bus_cfg = {};
    bus_cfg.miso_io_num   = -1;
    bus_cfg.mosi_io_num   = mosi;
    bus_cfg.sclk_io_num   = sck;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = width * height;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t dev = {};
    dev.clock_speed_hz = 12 * 1000 * 1000;  /* 12 MHz */
    dev.mode           = 0;
    dev.spics_io_num   = cs;
    dev.queue_size     = 1;
    ESP_ERROR_CHECK(spi_bus_add_device(SPI3_HOST, &dev, &s_spi));

    /* Wake IT8951 */
    it8951_write_cmd(IT8951_CMD_SYS_RUN);
    vTaskDelay(pdMS_TO_TICKS(100));

    /* Read device info to get the on-chip image buffer address */
    it8951_dev_info_t info = {};
    it8951_get_dev_info(&info);
    s_img_buf_addr = ((uint32_t)info.img_buf_addr_h << 16) | info.img_buf_addr_l;

    ESP_LOGI(TAG, "IT8951 panel=%ux%u, img_buf=0x%08lx",
             info.panel_w, info.panel_h, (unsigned long)s_img_buf_addr);
    ESP_LOGI(TAG, "FW: %.16s  LUT: %.16s", info.fw_version, info.lut_version);

    /* Allocate local 8-bpp shadow framebuffer in PSRAM */
    s_disp_len = width * height;
    s_disp_buf = (uint8_t *)heap_caps_malloc(s_disp_len, MALLOC_CAP_SPIRAM);
    assert(s_disp_buf);
    memset(s_disp_buf, 0xFF, s_disp_len);  /* white */

    /* Perform an INIT-mode full refresh to clear the panel */
    it8951_load_image_area(0, 0, width, height, s_disp_buf);
    it8951_display_area(0, 0, width, height, IT8951_MODE_INIT);
    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_LOGI(TAG, "E-Paper %dx%d initialized (IT8951)", width, height);
}

extern "C" void display_clear(uint8_t color)
{
    memset(s_disp_buf, color, s_disp_len);
}

extern "C" void display_set_pixel(uint16_t x, uint16_t y, uint8_t color)
{
    if (x >= s_width || y >= s_height) return;
    /* 8-bpp grayscale: 0x00 = black, 0xFF = white */
    s_disp_buf[(int)y * s_width + (int)x] = color ? 0xFF : 0x00;
}

extern "C" void display_flush(void)
{
    it8951_load_image_area(0, 0, s_width, s_height, s_disp_buf);
    /* Use DU (direct update) for fast monochrome refresh */
    it8951_display_area(0, 0, s_width, s_height, IT8951_MODE_DU);
}

extern "C" uint8_t *display_get_buffer(void)
{
    return s_disp_buf;
}

extern "C" int display_get_buffer_size(void)
{
    return s_disp_len;
}
