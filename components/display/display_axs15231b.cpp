#include "sdkconfig.h"
#if defined(CONFIG_DRAFTLING_DISPLAY_AXS15231B)

/*
 * AXS15231B QSPI color-LCD driver.
 *
 * Used by:
 *   - Waveshare ESP32-S3-Touch-LCD-3.49 (640x172)
 *   - Guition JC3248W535               (480x320)
 *
 * The AXS15231B is a small color TFT controller that uses a 4-line
 * (Quad-SPI) interface for both commands and pixel data. Commands
 * are sent in single-line mode wrapped in a 32-bit "address phase"
 * preamble (0x02, 0x00, cmd, 0x00) and pixel writes are sent in QSPI
 * mode after a (0x32, 0x00, 0x2C, 0x00) preamble that selects the
 * controller's "Memory Write" command (0x2C).
 *
 * Architecture
 * ------------
 * Unlike the monochrome backends, this driver keeps its own RGB565
 * framebuffer in PSRAM (sized to width * height * 2). The LVGL
 * port's flush_cb pushes RGB565 rectangles into the framebuffer via
 * display_push_rgb565(), and display_flush() walks the accumulated
 * dirty bounding box and DMA-streams just that region to the panel
 * over QSPI. There is no per-pixel display_set_pixel fast-path on
 * color hardware (the LVGL port always takes the RGB565 path), but
 * we provide a degraded fallback that interprets 0/0xFF as
 * black/white in case the framebuffer is queried from elsewhere
 * (e.g. the splash-screen logo in editor_ui.cpp).
 *
 * Status
 * ------
 * Initial implementation. The init sequence and pin maps have been
 * cross-referenced with the boards' published schematics and the
 * widely shared LovyanGFX panel definitions for these displays, but
 * have not been verified on real hardware in-tree. Likely tweak
 * points: MADCTL byte (0x36) for rotation, and the AXS15231B-specific
 * vendor command block (0xBB / 0xA0 below).
 *
 * Pins are passed in via display_axs15231b_init() (struct-based,
 * since the controller needs 9 GPIOs -- more than the legacy
 * display_init() can carry). Call display_axs15231b_init() from
 * main.cpp; the legacy display_init() is unsupported on this
 * backend and will abort.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <driver/ledc.h>

#include "display.h"

static const char *TAG = "DisplayAXS";

/* LEDC PWM configuration for the backlight. We keep timer 0 / channel
 * 0 dedicated to the LCD backlight; nothing else in Draftling uses
 * LEDC. 5 kHz is well above any visible flicker and is comfortably
 * within the LEDC peripheral's range at 10-bit duty resolution. */
#define BL_LEDC_TIMER       LEDC_TIMER_0
#define BL_LEDC_MODE        LEDC_LOW_SPEED_MODE
#define BL_LEDC_CHANNEL     LEDC_CHANNEL_0
#define BL_LEDC_DUTY_RES    LEDC_TIMER_10_BIT
#define BL_LEDC_DUTY_MAX    ((1 << 10) - 1)
#define BL_LEDC_FREQ_HZ     5000

static void backlight_pwm_init(int bl_pin, int percent)
{
    if (bl_pin < 0) return;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    ledc_timer_config_t t = {};
    t.speed_mode      = BL_LEDC_MODE;
    t.duty_resolution = BL_LEDC_DUTY_RES;
    t.timer_num       = BL_LEDC_TIMER;
    t.freq_hz         = BL_LEDC_FREQ_HZ;
    t.clk_cfg         = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&t));

    ledc_channel_config_t c = {};
    c.gpio_num   = bl_pin;
    c.speed_mode = BL_LEDC_MODE;
    c.channel    = BL_LEDC_CHANNEL;
    c.timer_sel  = BL_LEDC_TIMER;
    c.intr_type  = LEDC_INTR_DISABLE;
    /* Start at 0 (off) -- the caller bumps the duty up to the
     * configured brightness after panel init so the user never sees a
     * black/garbage flash. */
    c.duty       = 0;
    c.hpoint     = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&c));
}

static void backlight_pwm_set_percent(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    uint32_t duty = (uint32_t)((BL_LEDC_DUTY_MAX * percent) / 100);
    ESP_ERROR_CHECK(ledc_set_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL));
}


#define AXS_SPI_HOST            SPI2_HOST
#define AXS_SPI_CLOCK_HZ        (40 * 1000 * 1000)

/* Command preamble (single-line mode). The controller expects the
 * 8-bit command in the third byte of the 32-bit "address" phase. */
#define AXS_CMD_PREAMBLE        0x02

/* Pixel-data preamble (QSPI mode). 0x32 selects QSPI, 0x2C is the
 * standard MIPI "Memory Write" command. */
#define AXS_DATA_PREAMBLE_BYTE  0x32
#define AXS_MEMORY_WRITE        0x2C

static spi_device_handle_t s_spi = NULL;
static int s_width  = 0;
static int s_height = 0;
static int s_rst_pin = -1;
static int s_te_pin  = -1;
static int s_bl_pin  = -1;

/* RGB565 framebuffer (host-side). uint16_t in little-endian; we send
 * it byte-swapped to match the panel's big-endian RGB565 wire format. */
static uint16_t *s_fb = NULL;
static size_t    s_fb_pixels = 0;

/* Accumulated dirty bounding box (inclusive) since the last flush.
 * (-1, -1, -1, -1) means clean. */
static int s_dirty_x1 = -1;
static int s_dirty_y1 = -1;
static int s_dirty_x2 = -1;
static int s_dirty_y2 = -1;

/* One-shot panel-refresh clip rectangle (set by display_set_partial_clip
 * and consumed by the next display_flush). w<=0 or h<=0 means inactive. */
static int s_clip_x = 0, s_clip_y = 0, s_clip_w = 0, s_clip_h = 0;

/* DMA scratch buffer for one row's worth of byte-swapped pixels. We
 * stream the dirty rectangle row by row to avoid allocating a full
 * intermediate copy; the largest row is 1024 px = 2 KB. */
static uint8_t *s_row_buf = NULL;

/* ---------------- Low-level SPI helpers ---------------- */

static void spi_send_cmd(uint8_t cmd, const uint8_t *params, size_t n_params)
{
    /* AXS15231B command format (single-line SPI):
     *   addr phase (32 bits): 0x02 0x00 cmd 0x00
     *   data phase: 0...n parameter bytes
     */
    spi_transaction_ext_t tx = {};
    tx.base.flags = SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_CMD;
    tx.base.cmd   = AXS_CMD_PREAMBLE;
    tx.base.addr  = ((uint32_t)cmd) << 8;
    tx.command_bits = 8;
    tx.address_bits = 24;

    if (n_params > 0) {
        tx.base.length    = n_params * 8;
        tx.base.tx_buffer = params;
    }

    ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, (spi_transaction_t *)&tx));
}

/* Send a chunk of pixel data over QSPI using the Memory Write
 * preamble. Used for the initial blank fill and for each row of the
 * dirty region. */
static void spi_send_pixels(const uint8_t *data, size_t n_bytes)
{
    spi_transaction_ext_t tx = {};
    tx.base.flags = SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_CMD |
                    SPI_TRANS_MODE_QIO;
    tx.base.cmd   = AXS_DATA_PREAMBLE_BYTE;
    tx.base.addr  = ((uint32_t)AXS_MEMORY_WRITE) << 8;
    tx.command_bits = 8;
    tx.address_bits = 24;
    tx.base.length    = n_bytes * 8;
    tx.base.tx_buffer = data;

    ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, (spi_transaction_t *)&tx));
}

static void hw_reset(void)
{
    if (s_rst_pin < 0) return;
    gpio_set_level((gpio_num_t)s_rst_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level((gpio_num_t)s_rst_pin, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level((gpio_num_t)s_rst_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
}

static void axs15231b_init_sequence(void)
{
    /* Vendor-specific init block. These two writes are required by
     * the AXS15231B before any standard MIPI commands are honoured;
     * the magic bytes were cross-referenced with the LovyanGFX
     * Panel_AXS15231B definition and the Waveshare/Guition example
     * code shipped with these boards. */
    static const uint8_t init_bb[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    spi_send_cmd(0xBB, init_bb, sizeof(init_bb));

    static const uint8_t init_a0[] = {
        0xC0, 0x10, 0x00, 0x02, 0x00, 0x00, 0x04,
        0x3F, 0x20, 0x05, 0x0F, 0x18, 0x21, 0x10
    };
    spi_send_cmd(0xA0, init_a0, sizeof(init_a0));

    /* Memory Access Control (MADCTL). 0x00 selects portrait mode
     * with RGB pixel order (no row/column swaps, no mirroring). The
     * Waveshare 3.49" board's "640x172" landscape orientation will
     * need this byte tuned (typically 0x60 or similar to set MV/MX);
     * we leave the default here and expect the per-board init to
     * override once the orientation is verified on hardware. */
    static const uint8_t madctl[] = { 0x00 };
    spi_send_cmd(0x36, madctl, sizeof(madctl));

    /* Pixel format: 16 bpp (RGB565). */
    static const uint8_t pixfmt[] = { 0x05 };
    spi_send_cmd(0x3A, pixfmt, sizeof(pixfmt));

    /* TE line on (V-blank only). */
    static const uint8_t te[] = { 0x00 };
    spi_send_cmd(0x35, te, sizeof(te));

    /* Sleep out and display on. */
    spi_send_cmd(0x11, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(120));
    spi_send_cmd(0x29, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
}

static void set_addr_window(int x, int y, int w, int h)
{
    int x2 = x + w - 1;
    int y2 = y + h - 1;

    uint8_t caset[] = {
        (uint8_t)((x  >> 8) & 0xFF), (uint8_t)(x  & 0xFF),
        (uint8_t)((x2 >> 8) & 0xFF), (uint8_t)(x2 & 0xFF)
    };
    uint8_t raset[] = {
        (uint8_t)((y  >> 8) & 0xFF), (uint8_t)(y  & 0xFF),
        (uint8_t)((y2 >> 8) & 0xFF), (uint8_t)(y2 & 0xFF)
    };
    spi_send_cmd(0x2A, caset, sizeof(caset));
    spi_send_cmd(0x2B, raset, sizeof(raset));
}

/* ---------------- Public API ---------------- */

extern "C" void display_init(int /*pin_a*/, int /*pin_b*/, int /*pin_c*/,
                             int /*pin_d*/, int /*pin_e*/, int /*pin_f*/,
                             int /*width*/, int /*height*/)
{
    /* The AXS15231B driver needs 9 GPIOs which do not fit in
     * display_init()'s 6 pin slots, so it exposes its own
     * struct-based init (display_axs15231b_init). Calling the
     * generic display_init on an AXS15231B build is a programming
     * error; abort loudly. */
    ESP_LOGE(TAG, "display_init() is not supported on AXS15231B; "
                  "call display_axs15231b_init() instead");
    abort();
}

extern "C" void display_axs15231b_init(const display_axs15231b_config_t *cfg)
{
    assert(cfg);
    s_width  = cfg->width;
    s_height = cfg->height;
    s_rst_pin = cfg->rst;
    s_te_pin  = cfg->te;
    s_bl_pin  = cfg->bl;

    /* Reset / TE / Backlight GPIOs */
    if (s_rst_pin >= 0) {
        gpio_config_t g = {};
        g.intr_type    = GPIO_INTR_DISABLE;
        g.mode         = GPIO_MODE_OUTPUT;
        g.pin_bit_mask = (1ULL << s_rst_pin);
        ESP_ERROR_CHECK(gpio_config(&g));
    }
    if (s_bl_pin >= 0) {
        /* Backlight is driven by LEDC PWM so the user can dial the
         * brightness with DRAFTLING_BACKLIGHT_PERCENT. Init the
         * channel with duty 0 (off) here; we ramp it up to the
         * configured brightness after the panel is fully
         * initialised, so there is no black/garbage flash. */
        backlight_pwm_init(s_bl_pin, CONFIG_DRAFTLING_BACKLIGHT_PERCENT);
    }
    if (s_te_pin >= 0) {
        gpio_config_t g = {};
        g.intr_type    = GPIO_INTR_DISABLE;
        g.mode         = GPIO_MODE_INPUT;
        g.pin_bit_mask = (1ULL << s_te_pin);
        g.pull_up_en   = GPIO_PULLUP_DISABLE;
        ESP_ERROR_CHECK(gpio_config(&g));
    }

    /* QSPI bus: 4 data lines (D0..D3) + SCK. CS is owned by the
     * spi_device. Max transfer = one full row (width * 2 bytes). */
    spi_bus_config_t bus_cfg = {};
    bus_cfg.data0_io_num = cfg->d0;
    bus_cfg.data1_io_num = cfg->d1;
    bus_cfg.data2_io_num = cfg->d2;
    bus_cfg.data3_io_num = cfg->d3;
    bus_cfg.sclk_io_num  = cfg->sck;
    bus_cfg.max_transfer_sz = s_width * 2 + 16;
    bus_cfg.flags        = SPICOMMON_BUSFLAG_QUAD;
    ESP_ERROR_CHECK(spi_bus_initialize(AXS_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t dev_cfg = {};
    dev_cfg.command_bits   = 8;
    dev_cfg.address_bits   = 24;
    dev_cfg.mode           = 0;
    dev_cfg.clock_speed_hz = AXS_SPI_CLOCK_HZ;
    dev_cfg.spics_io_num   = cfg->cs;
    dev_cfg.queue_size     = 7;
    dev_cfg.flags          = SPI_DEVICE_HALFDUPLEX;
    ESP_ERROR_CHECK(spi_bus_add_device(AXS_SPI_HOST, &dev_cfg, &s_spi));

    /* Allocate framebuffer (RGB565) and the per-row DMA scratch
     * buffer. The scratch must be DMA-capable internal RAM because
     * spi_device_polling_transmit() does not stream from PSRAM. */
    s_fb_pixels = (size_t)s_width * s_height;
    s_fb = (uint16_t *)heap_caps_malloc(s_fb_pixels * sizeof(uint16_t),
                                        MALLOC_CAP_SPIRAM);
    s_row_buf = (uint8_t *)heap_caps_malloc(s_width * 2,
                                            MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    assert(s_fb && s_row_buf);
    memset(s_fb, 0, s_fb_pixels * sizeof(uint16_t));

    hw_reset();
    axs15231b_init_sequence();

    /* Backlight on at the user-configured brightness. */
    if (s_bl_pin >= 0) {
        backlight_pwm_set_percent(CONFIG_DRAFTLING_BACKLIGHT_PERCENT);
    }

    /* Initial blank to avoid showing whatever junk was in panel RAM. */
    display_clear(0x00);
    display_full_refresh();

    ESP_LOGI(TAG, "AXS15231B %dx%d initialized", s_width, s_height);
}

extern "C" void display_clear(uint8_t color)
{
    /* The legacy 1-bpp API: 0xFF means "white", 0x00 means "black".
     * Map both bytes of the RGB565 pixel to the same value so memset
     * works directly on the framebuffer. */
    memset(s_fb, color ? 0xFF : 0x00, s_fb_pixels * sizeof(uint16_t));
    s_dirty_x1 = 0;
    s_dirty_y1 = 0;
    s_dirty_x2 = s_width  - 1;
    s_dirty_y2 = s_height - 1;
}

extern "C" void display_set_pixel(uint16_t x, uint16_t y, uint8_t color)
{
    if (x >= s_width || y >= s_height) return;
    s_fb[(size_t)y * s_width + x] = (color == 0) ? 0x0000 : 0xFFFF;

    if (s_dirty_x1 < 0) {
        s_dirty_x1 = s_dirty_x2 = x;
        s_dirty_y1 = s_dirty_y2 = y;
    } else {
        if ((int)x < s_dirty_x1) s_dirty_x1 = x;
        if ((int)x > s_dirty_x2) s_dirty_x2 = x;
        if ((int)y < s_dirty_y1) s_dirty_y1 = y;
        if ((int)y > s_dirty_y2) s_dirty_y2 = y;
    }
}

extern "C" bool display_push_rgb565(int x, int y, int w, int h,
                                    const void *color_map)
{
    if (x < 0 || y < 0 || w <= 0 || h <= 0) return true;
    int x2 = x + w - 1;
    int y2 = y + h - 1;
    if (x2 >= s_width)  x2 = s_width  - 1;
    if (y2 >= s_height) y2 = s_height - 1;
    int eff_w = x2 - x + 1;
    int eff_h = y2 - y + 1;
    if (eff_w <= 0 || eff_h <= 0) return true;

    const uint16_t *src = (const uint16_t *)color_map;
    for (int row = 0; row < eff_h; row++) {
        uint16_t *dst = s_fb + (size_t)(y + row) * s_width + x;
        memcpy(dst, src, eff_w * sizeof(uint16_t));
        src += w;  /* caller-supplied stride is the original w */
    }

    if (s_dirty_x1 < 0) {
        s_dirty_x1 = x;  s_dirty_y1 = y;
        s_dirty_x2 = x2; s_dirty_y2 = y2;
    } else {
        if (x  < s_dirty_x1) s_dirty_x1 = x;
        if (x2 > s_dirty_x2) s_dirty_x2 = x2;
        if (y  < s_dirty_y1) s_dirty_y1 = y;
        if (y2 > s_dirty_y2) s_dirty_y2 = y2;
    }
    return true;
}

extern "C" void display_set_partial_clip(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) {
        s_clip_w = s_clip_h = 0;
        return;
    }
    s_clip_x = x; s_clip_y = y; s_clip_w = w; s_clip_h = h;
}

extern "C" void display_flush(void)
{
    if (s_dirty_x1 < 0) return;  /* nothing dirty */

    int x1 = s_dirty_x1, y1 = s_dirty_y1;
    int x2 = s_dirty_x2, y2 = s_dirty_y2;

    /* Apply one-shot clip if pending */
    if (s_clip_w > 0 && s_clip_h > 0) {
        int cx2 = s_clip_x + s_clip_w - 1;
        int cy2 = s_clip_y + s_clip_h - 1;
        if (s_clip_x > x1) x1 = s_clip_x;
        if (s_clip_y > y1) y1 = s_clip_y;
        if (cx2      < x2) x2 = cx2;
        if (cy2      < y2) y2 = cy2;
        s_clip_w = s_clip_h = 0;
    }

    /* Reset accumulator regardless of whether the clipped region is
     * empty -- we still consumed it. */
    s_dirty_x1 = s_dirty_y1 = s_dirty_x2 = s_dirty_y2 = -1;

    if (x1 > x2 || y1 > y2) return;
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= s_width)  x2 = s_width  - 1;
    if (y2 >= s_height) y2 = s_height - 1;

    int w = x2 - x1 + 1;
    int h = y2 - y1 + 1;

    set_addr_window(x1, y1, w, h);

    /* Stream the region row-by-row, byte-swapping into the DMA buffer. */
    for (int row = 0; row < h; row++) {
        const uint16_t *src = s_fb + (size_t)(y1 + row) * s_width + x1;
        uint8_t *dst = s_row_buf;
        for (int col = 0; col < w; col++) {
            uint16_t px = src[col];
            *dst++ = (uint8_t)(px >> 8);
            *dst++ = (uint8_t)(px & 0xFF);
        }
        spi_send_pixels(s_row_buf, w * 2);
    }
}

extern "C" void display_full_refresh(void)
{
    /* Color LCDs do not accumulate ghosting; a "full refresh" is just
     * a flush of the entire framebuffer. */
    s_dirty_x1 = 0;
    s_dirty_y1 = 0;
    s_dirty_x2 = s_width  - 1;
    s_dirty_y2 = s_height - 1;
    /* Cancel any partial clip so the whole panel really gets pushed. */
    s_clip_w = s_clip_h = 0;
    display_flush();
}

extern "C" uint8_t *display_get_buffer(void)
{
    return (uint8_t *)s_fb;
}

extern "C" int display_get_buffer_size(void)
{
    return (int)(s_fb_pixels * sizeof(uint16_t));
}

#endif /* CONFIG_DRAFTLING_DISPLAY_AXS15231B */
