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
 * Refresh strategy: each call to display_flush() compares the new
 * framebuffer against the last frame actually shown and:
 *   - skips the panel cycle entirely if nothing changed;
 *   - performs a partial refresh (~300 ms) over the byte-aligned
 *     bounding box of the diff for small or local changes;
 *   - performs a full refresh (~3 s) every
 *     CONFIG_DRAFTLING_EPD_FULL_REFRESH_INTERVAL partials, or
 *     immediately when the dirty region covers most of the screen.
 * display_full_refresh() is exposed so callers can force a clean,
 * ghost-free repaint on demand (e.g. after opening a new file).
 */

#include <cstdio>
#include <cstdlib>
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
#define UC8179_CMD_PTL        0x90  /* Partial Window */
#define UC8179_CMD_PTIN       0x91  /* Partial In */
#define UC8179_CMD_PTOUT      0x92  /* Partial Out */

static esp_lcd_panel_io_handle_t s_io_handle = NULL;
static uint8_t *s_disp_buf = NULL;       /* current frame composed by caller */
static uint8_t *s_prev_buf = NULL;       /* last frame actually shown on panel */
static int s_disp_len = 0;
static int s_width  = 0;
static int s_height = 0;
static int s_rst_pin  = -1;
static int s_busy_pin = -1;
static int s_stride   = 0;     /* bytes per row */

/* How many partial refreshes we have done since the last full refresh.
 * When this hits CONFIG_DRAFTLING_EPD_FULL_REFRESH_INTERVAL we force a
 * full refresh to clear residual ghosting. */
static int s_partial_count = 0;

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

    /* Allocate framebuffer in PSRAM (one bit per pixel). We keep two
     * buffers: s_disp_buf is the frame being composed by the LVGL flush
     * callback, s_prev_buf is the frame currently displayed on the
     * panel. The diff between them drives partial-refresh decisions. */
    s_disp_len = s_stride * height;
    s_disp_buf = (uint8_t *)heap_caps_malloc(s_disp_len, MALLOC_CAP_SPIRAM);
    s_prev_buf = (uint8_t *)heap_caps_malloc(s_disp_len, MALLOC_CAP_SPIRAM);
    assert(s_disp_buf && s_prev_buf);
    memset(s_disp_buf, 0xFF, s_disp_len);  /* white */
    memset(s_prev_buf, 0xFF, s_disp_len);  /* white */

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
    display_full_refresh();

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

/* ---- refresh helpers ---- */

/* Send the entire framebuffer using the full-update waveform.
 * Always called from display_full_refresh() and as a fallback when the
 * partial-refresh interval expires or every byte of the frame changed. */
static void epd_full_refresh(void)
{
    /* Old data buffer (DTM1) -- the previous frame so the controller
     * has a valid reference when generating the full waveform. */
    send_command(UC8179_CMD_DTM1);
    send_buffer(s_prev_buf, s_disp_len);

    /* New data buffer (DTM2) -- the frame to display. */
    send_command(UC8179_CMD_DTM2);
    send_buffer(s_disp_buf, s_disp_len);

    send_command(UC8179_CMD_DRF);
    /* Full refresh takes several seconds; wait generously. */
    vTaskDelay(pdMS_TO_TICKS(100));
    wait_busy(30000);

    memcpy(s_prev_buf, s_disp_buf, s_disp_len);
    s_partial_count = 0;
}

/* Crop bytes [byte_x0, byte_x1] (inclusive) from rows [y0, y1] (inclusive)
 * of the source buffer into a freshly allocated tightly-packed window
 * buffer. The caller must free the returned pointer. */
static uint8_t *crop_window(const uint8_t *src,
                            int byte_x0, int byte_x1,
                            int y0, int y1)
{
    int row_bytes = byte_x1 - byte_x0 + 1;
    int rows      = y1 - y0 + 1;
    size_t len    = (size_t)row_bytes * rows;
    uint8_t *out  = (uint8_t *)heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
    if (!out) return NULL;
    for (int y = 0; y < rows; y++) {
        memcpy(out + (size_t)y * row_bytes,
               src + (size_t)(y + y0) * s_stride + byte_x0,
               row_bytes);
    }
    return out;
}

/* Perform a UC8179 partial refresh covering pixel columns
 * [px_x0, px_x1] and rows [y0, y1]. px_x0 must be a multiple of 8 and
 * px_x1 must be of the form 8k+7 (byte-aligned window, as required by
 * the UC8179 PTL command). */
static void epd_partial_refresh(int px_x0, int px_x1, int y0, int y1)
{
    int byte_x0 = px_x0 >> 3;
    int byte_x1 = px_x1 >> 3;
    int row_bytes = byte_x1 - byte_x0 + 1;
    int rows      = y1 - y0 + 1;
    size_t win_len = (size_t)row_bytes * rows;

    uint8_t *old_win = crop_window(s_prev_buf, byte_x0, byte_x1, y0, y1);
    uint8_t *new_win = crop_window(s_disp_buf, byte_x0, byte_x1, y0, y1);
    if (!old_win || !new_win) {
        ESP_LOGW(TAG, "Partial window allocation failed, falling back to full refresh");
        free(old_win);
        free(new_win);
        epd_full_refresh();
        return;
    }

    send_command(UC8179_CMD_PTIN);

    /* Set partial window. PTL takes 9 bytes for the 800x480 panel:
     *   x_start_HI, x_start_LO, x_end_HI, x_end_LO,
     *   y_start_HI, y_start_LO, y_end_HI, y_end_LO,
     *   PT_SCAN. */
    send_command(UC8179_CMD_PTL);
    send_data((px_x0 >> 8) & 0xFF);
    send_data(px_x0 & 0xFF);
    send_data((px_x1 >> 8) & 0xFF);
    send_data(px_x1 & 0xFF);
    send_data((y0 >> 8) & 0xFF);
    send_data(y0 & 0xFF);
    send_data((y1 >> 8) & 0xFF);
    send_data(y1 & 0xFF);
    send_data(0x01);                       /* PT_SCAN: scan inside region */

    /* Old data for the window -- needed by the partial waveform so the
     * controller can compute the per-pixel transition. */
    send_command(UC8179_CMD_DTM1);
    send_buffer(old_win, win_len);

    /* New data for the window. */
    send_command(UC8179_CMD_DTM2);
    send_buffer(new_win, win_len);

    send_command(UC8179_CMD_DRF);
    vTaskDelay(pdMS_TO_TICKS(20));
    wait_busy(5000);

    send_command(UC8179_CMD_PTOUT);

    free(old_win);
    free(new_win);

    /* Mirror the change into the previous-frame buffer so the next diff
     * is computed against what the panel is actually showing. */
    for (int y = y0; y <= y1; y++) {
        memcpy(s_prev_buf + (size_t)y * s_stride + byte_x0,
               s_disp_buf + (size_t)y * s_stride + byte_x0,
               row_bytes);
    }
    s_partial_count++;
}

extern "C" void display_flush(void)
{
    /* Compute the bounding box of bytes that changed since the last
     * frame we actually pushed to the panel. The X bounds are recorded
     * in *byte* units (8 horizontally adjacent pixels) because the
     * UC8179 PTL command requires byte-aligned windows. */
    int byte_stride = s_stride;
    int min_by = byte_stride;   /* sentinels */
    int max_by = -1;
    int min_y  = s_height;
    int max_y  = -1;

    for (int y = 0; y < s_height; y++) {
        const uint8_t *a = s_disp_buf + y * byte_stride;
        const uint8_t *b = s_prev_buf + y * byte_stride;
        if (memcmp(a, b, byte_stride) == 0) continue;
        if (y < min_y) min_y = y;
        if (y > max_y) max_y = y;
        for (int bx = 0; bx < byte_stride; bx++) {
            if (a[bx] == b[bx]) continue;
            if (bx < min_by) min_by = bx;
            if (bx > max_by) max_by = bx;
        }
    }

    /* Nothing changed -- skip the panel cycle entirely. */
    if (max_by < 0) return;

    /* Force a periodic full refresh to clear ghosting that accumulates
     * after many consecutive partial updates. */
#ifdef CONFIG_DRAFTLING_EPD_FULL_REFRESH_INTERVAL
    int full_interval = CONFIG_DRAFTLING_EPD_FULL_REFRESH_INTERVAL;
#else
    int full_interval = 50;
#endif

    /* If the change covers the bulk of the screen, the partial refresh
     * costs more SPI traffic than a single full refresh, so just go full. */
    int total_window_bytes = (max_by - min_by + 1) * (max_y - min_y + 1);
    bool dirty_is_huge = (total_window_bytes * 4 >= s_disp_len * 3);

    if (s_partial_count >= full_interval || dirty_is_huge) {
        epd_full_refresh();
        return;
    }

    int px_x0 = min_by * 8;
    int px_x1 = max_by * 8 + 7;
    epd_partial_refresh(px_x0, px_x1, min_y, max_y);
}

extern "C" void display_full_refresh(void)
{
    epd_full_refresh();
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
