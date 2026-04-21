#include "sdkconfig.h"
#if defined(CONFIG_DRAFTLING_MODEL_SEEED_RETERMINAL_E1001)

/*
 * UC8179 e-paper display driver for the Seeed reTerminal E1001
 * (800 x 480, 1 bit per pixel, full-screen refresh).
 *
 * The driver implements the same public API as the Waveshare RLCD
 * driver so that the LVGL port (lvgl_port.cpp) can stay generic:
 *
 *   display_init(mosi, sck, dc, cs, rst, width, height)
 *   display_clear(color)
 *   display_set_pixel(x, y, color)
 *   display_flush()
 *
 * Two parameters are not present in the public signature but are
 * still required for the e-paper: BUSY (input) and the SPI host id.
 * The reTerminal E1001 wires BUSY to GPIO13 and shares the SPI bus
 * with the SD card (HSPI / SPI2), so we pin them as compile-time
 * constants here.
 *
 * Frame buffer layout (UC8179 in B/W mode):
 *   - 1 bit per pixel, 8 horizontally adjacent pixels per byte,
 *     MSB = leftmost pixel, packed row-major.
 *   - White pixel = bit set (1), black pixel = bit clear (0).
 *
 * Refresh strategy: this initial implementation uses the full-update
 * waveform on every flush. Refresh latency is ~3-5 seconds, which is
 * acceptable for a distraction-free Markdown editor between explicit
 * "page" actions but quite slow for character-by-character input. A
 * later patch can add partial refresh.
 */

#include <cstdio>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_lcd_panel_io.h>

#include "display.h"

static const char *TAG = "DisplayEPD";

/* The reTerminal E1001 shares the SPI bus between the e-paper and the
 * SD card. We use HSPI (SPI2) so SPI3 is left free for any future
 * peripheral (and to mirror the upstream Seeed_GFX setup). */
#define EPD_SPI_HOST    SPI2_HOST

/* BUSY input from the panel (active-low). Hard-coded for the only board
 * that currently uses this driver; if a future board reuses the UC8179
 * with different wiring, promote this to an init-time parameter. */
#define EPD_BUSY_PIN    13

/* UC8179 commands used in this driver */
#define UC8179_CMD_PSR        0x00  /* Panel Setting */
#define UC8179_CMD_PWR        0x01  /* Power Setting */
#define UC8179_CMD_PON        0x04  /* Power ON */
#define UC8179_CMD_POF        0x02  /* Power OFF */
#define UC8179_CMD_BTST       0x06  /* Booster Soft Start */
#define UC8179_CMD_DSLP       0x07  /* Deep Sleep */
#define UC8179_CMD_DTM1       0x10  /* Display Start Transmission 1 (old) */
#define UC8179_CMD_DRF        0x12  /* Display Refresh */
#define UC8179_CMD_DTM2       0x13  /* Display Start Transmission 2 (new) */
#define UC8179_CMD_PLL        0x30  /* PLL Control */
#define UC8179_CMD_CDI        0x50  /* VCOM and data interval setting */
#define UC8179_CMD_TCON       0x60  /* TCON Setting */
#define UC8179_CMD_TRES       0x61  /* Resolution Setting */
#define UC8179_CMD_VDCS       0x82  /* VCM_DC Setting */

static esp_lcd_panel_io_handle_t s_io_handle = NULL;
static uint8_t *s_disp_buf = NULL;
static int s_disp_len = 0;
static int s_width  = 0;
static int s_height = 0;
static int s_rst_pin  = -1;
static int s_busy_pin = -1;
static int s_stride   = 0;     /* bytes per row */

/* ---- low-level helpers ---- */

static void send_command(uint8_t cmd)
{
    esp_lcd_panel_io_tx_param(s_io_handle, cmd, NULL, 0);
}

static void send_data(uint8_t data)
{
    esp_lcd_panel_io_tx_param(s_io_handle, -1, &data, 1);
}

static void send_buffer(const uint8_t *data, int len)
{
    esp_lcd_panel_io_tx_color(s_io_handle, -1, data, len);
}

static void hw_reset(void)
{
    gpio_set_level((gpio_num_t)s_rst_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level((gpio_num_t)s_rst_pin, 0);
    vTaskDelay(pdMS_TO_TICKS(5));
    gpio_set_level((gpio_num_t)s_rst_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
}

/* The UC8179 BUSY line goes LOW while the controller is busy and
 * returns HIGH when it is ready to accept the next command. */
static void wait_busy(int timeout_ms)
{
    const TickType_t step  = pdMS_TO_TICKS(10);
    int waited = 0;
    while (gpio_get_level((gpio_num_t)s_busy_pin) == 0) {
        vTaskDelay(step);
        waited += 10;
        if (timeout_ms > 0 && waited >= timeout_ms) {
            ESP_LOGW(TAG, "BUSY timeout after %d ms", waited);
            return;
        }
    }
}

/* ---- public API ---- */

extern "C" void display_init(int mosi, int sck, int dc, int cs, int rst,
                             int width, int height)
{
    s_width   = width;
    s_height  = height;
    s_stride  = (width + 7) / 8;
    s_rst_pin = rst;
    s_busy_pin = EPD_BUSY_PIN;

    /* Initialize the SPI bus (HSPI). The SD card driver may attach to
     * the same bus afterwards as a second device. */
    spi_bus_config_t bus_cfg = {};
    bus_cfg.miso_io_num     = -1;
    bus_cfg.mosi_io_num     = mosi;
    bus_cfg.sclk_io_num     = sck;
    bus_cfg.quadwp_io_num   = -1;
    bus_cfg.quadhd_io_num   = -1;
    bus_cfg.max_transfer_sz = (width * height) / 8 + 32;
    esp_err_t bus_ret = spi_bus_initialize(EPD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (bus_ret != ESP_OK && bus_ret != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(bus_ret);
    }

    /* Create LCD panel IO over SPI */
    esp_lcd_panel_io_spi_config_t io_cfg = {};
    io_cfg.dc_gpio_num       = dc;
    io_cfg.cs_gpio_num       = cs;
    io_cfg.pclk_hz           = 10 * 1000 * 1000;
    io_cfg.lcd_cmd_bits      = 8;
    io_cfg.lcd_param_bits    = 8;
    io_cfg.spi_mode          = 0;
    io_cfg.trans_queue_depth = 10;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)EPD_SPI_HOST, &io_cfg, &s_io_handle));

    /* Configure RST as output and BUSY as input */
    gpio_config_t out_cfg = {};
    out_cfg.intr_type    = GPIO_INTR_DISABLE;
    out_cfg.mode         = GPIO_MODE_OUTPUT;
    out_cfg.pin_bit_mask = (1ULL << rst);
    out_cfg.pull_up_en   = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK(gpio_config(&out_cfg));

    gpio_config_t in_cfg = {};
    in_cfg.intr_type    = GPIO_INTR_DISABLE;
    in_cfg.mode         = GPIO_MODE_INPUT;
    in_cfg.pin_bit_mask = (1ULL << s_busy_pin);
    in_cfg.pull_up_en   = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK(gpio_config(&in_cfg));

    /* Allocate framebuffer in PSRAM (one bit per pixel) */
    s_disp_len = s_stride * height;
    s_disp_buf = (uint8_t *)heap_caps_malloc(s_disp_len, MALLOC_CAP_SPIRAM);
    assert(s_disp_buf);
    memset(s_disp_buf, 0xFF, s_disp_len);  /* white */

    /* Hardware reset and UC8179 init sequence (B/W mode, full update) */
    hw_reset();

    send_command(UC8179_CMD_PWR);
    send_data(0x07);                       /* Internal DC/DC, VGH/VGL/VSH/VSL */
    send_data(0x07);
    send_data(0x3F);                       /* VDH = 15 V */
    send_data(0x3F);                       /* VDL = -15 V */

    send_command(UC8179_CMD_PON);
    wait_busy(5000);

    send_command(UC8179_CMD_PSR);
    send_data(0x1F);                       /* KW-BF, KWR-AF, Scan up, Shift right */

    send_command(UC8179_CMD_TRES);
    send_data((s_width >> 8) & 0xFF);
    send_data(s_width & 0xFF);
    send_data((s_height >> 8) & 0xFF);
    send_data(s_height & 0xFF);

    send_command(UC8179_CMD_VDCS);
    send_data(0x22);                       /* VCOM_DC */

    send_command(UC8179_CMD_TCON);
    send_data(0x22);

    send_command(UC8179_CMD_CDI);
    send_data(0x10);                       /* Border = white, default interval */
    send_data(0x07);

    send_command(UC8179_CMD_PLL);
    send_data(0x06);                       /* 50 Hz */

    /* Push the white framebuffer once so the panel is in a known state */
    display_flush();

    ESP_LOGI(TAG, "UC8179 e-paper %dx%d initialized", width, height);
}

extern "C" void display_clear(uint8_t color)
{
    /* Convert "8-bit gray" caller convention (0x00 = black, 0xFF = white)
     * into our 1bpp packed buffer where 1 = white, 0 = black. */
    memset(s_disp_buf, color ? 0xFF : 0x00, s_disp_len);
}

extern "C" void display_set_pixel(uint16_t x, uint16_t y, uint8_t color)
{
    if (x >= s_width || y >= s_height) return;
    int idx = y * s_stride + (x >> 3);
    uint8_t mask = 0x80 >> (x & 7);
    if (color) {
        s_disp_buf[idx] |= mask;          /* white */
    } else {
        s_disp_buf[idx] &= ~mask;         /* black */
    }
}

extern "C" void display_flush(void)
{
    /* Old data buffer (DTM1) -- send same content so the controller has a
     * valid "previous frame" reference when generating the waveform. */
    send_command(UC8179_CMD_DTM1);
    send_buffer(s_disp_buf, s_disp_len);

    /* New data buffer (DTM2) -- the actual frame to display. */
    send_command(UC8179_CMD_DTM2);
    send_buffer(s_disp_buf, s_disp_len);

    send_command(UC8179_CMD_DRF);
    /* Refresh can take several seconds; wait generously. */
    vTaskDelay(pdMS_TO_TICKS(100));
    wait_busy(30000);
}

extern "C" uint8_t *display_get_buffer(void)
{
    return s_disp_buf;
}

extern "C" int display_get_buffer_size(void)
{
    return s_disp_len;
}

#endif /* CONFIG_DRAFTLING_MODEL_SEEED_RETERMINAL_E1001 */
