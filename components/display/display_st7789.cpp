#include "sdkconfig.h"
#if defined(CONFIG_DRAFTLING_DISPLAY_ST7789)

/*
 * ST7789 i80 (8-bit parallel) color-LCD driver.
 *
 * Used by:
 *   - LilyGO T-Display-S3 (320x170, ST7789V over LCD_CAM i80 bus)
 *
 * Architecture
 * ------------
 * Mirrors the AXS15231B color backend: keep an RGB565 framebuffer
 * in PSRAM (sized to width * height * 2). The LVGL port's flush_cb
 * pushes RGB565 rectangles into the framebuffer via
 * display_push_rgb565(), and display_flush() walks the accumulated
 * dirty bounding box and DMA-streams just that region to the panel
 * over the i80 bus using esp_lcd_panel_draw_bitmap(). There is no
 * per-pixel display_set_pixel fast-path on color hardware (the LVGL
 * port always takes the RGB565 path), but we provide a degraded
 * fallback that interprets 0/0xFF as black/white in case the
 * framebuffer is queried from elsewhere (e.g. the splash-screen
 * logo in editor_ui.cpp).
 *
 * Status
 * ------
 * Initial implementation. The pin map and gap/MADCTL parameters were
 * cross-referenced with the LilyGO T-Display-S3 schematic and the
 * widely shared TFT_eSPI / LovyanGFX configurations for that board,
 * but have not been verified on real hardware in-tree. Likely tweak
 * points: x_gap (column offset, 35 for 170 px landscape on a 240 px
 * panel) and the swap_xy / mirror_* flags for rotation.
 *
 * Pins are passed in via display_st7789_init() (struct-based, since
 * the controller needs more GPIOs than the legacy display_init() can
 * carry). Call display_st7789_init() from main.cpp; the legacy
 * display_init() is unsupported on this backend and will abort.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <algorithm>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_attr.h>
#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_ops.h>

#include "display.h"

static const char *TAG = "DisplayST7789";

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

/* Backlight GPIO captured at init(); forward-declared here so
 * display_set_backlight() (below) can reference it before the rest
 * of the static state is defined further down. */
static int s_bl_pin = -1;
static int s_bl_last_pct = 100;
static bool s_panel_asleep = false;

static void backlight_pwm_init(int bl_pin)
{
    if (bl_pin < 0) return;

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
    c.duty       = 0;
    c.hpoint     = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&c));
}

extern "C" void display_set_backlight(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    /* Only cache the value once we know the hardware was actually
     * driven; otherwise display_wake() would later "restore" a
     * brightness that never made it out to the panel. */
    if (s_bl_pin < 0) return;
    s_bl_last_pct = percent;
    uint32_t duty = (uint32_t)((BL_LEDC_DUTY_MAX * percent) / 100);
    ESP_ERROR_CHECK(ledc_set_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL));
}

/* The i80 driver's PCLK ceiling depends on the GPIOs in use; 20 MHz
 * is the conservative figure recommended in the ESP-IDF examples for
 * boards that route the LCD bus through standard GPIOs (no dedicated
 * IO MUX). Bumping this is worthwhile once the panel is verified. */
#define ST7789_PCLK_HZ          (20 * 1000 * 1000)

/* Trans queue depth for esp_lcd. We keep this small (2) and wait
 * for a per-transfer "done" callback after each draw_bitmap so the
 * shared s_dma_buf scratch is never overwritten while DMA still
 * reads from it. See display_flush() for the synchronization
 * sequence (draw_bitmap -> sem take). */
#define ST7789_TRANS_QUEUE      2

static esp_lcd_panel_io_handle_t s_io     = NULL;
static esp_lcd_panel_handle_t    s_panel  = NULL;
static int s_width  = 0;
static int s_height = 0;
/* s_bl_pin is defined near the top of this file (above
 * display_set_backlight); kept logically with the other s_* state. */

/* RGB565 framebuffer (host-side). uint16_t in little-endian; the
 * esp_lcd ST7789 driver swaps to the panel's wire format internally
 * when LCD_CMD_BITS / LCD_PARAM_BITS are configured below. */
static uint16_t *s_fb = NULL;
static size_t    s_fb_pixels = 0;

/* Logical-to-panel pixel scale. Read from Kconfig at compile time.
 * Each logical LVGL pixel is rendered as SCALE x SCALE physical
 * panel pixels via nearest-neighbor expansion in display_push_rgb565().
 * LVGL renders into a logical canvas of size (s_width / SCALE) by
 * (s_height / SCALE), so without this scaling the LVGL output would
 * occupy only the top-left 1/SCALE^2 of the panel. */
#ifdef CONFIG_DRAFTLING_DISPLAY_SCALE
#define ST7789_SCALE CONFIG_DRAFTLING_DISPLAY_SCALE
#else
#define ST7789_SCALE 1
#endif

/* Accumulated dirty bounding box (inclusive) since the last flush.
 * (-1, -1, -1, -1) means clean. */
static int s_dirty_x1 = -1;
static int s_dirty_y1 = -1;
static int s_dirty_x2 = -1;
static int s_dirty_y2 = -1;

/* One-shot panel-refresh clip rectangle (set by display_set_partial_clip
 * and consumed by the next display_flush). w<=0 or h<=0 means inactive. */
static int s_clip_x = 0, s_clip_y = 0, s_clip_w = 0, s_clip_h = 0;

/* DMA scratch buffer for the dirty rectangle. Sized to the largest
 * rectangle the LVGL port can produce in a single flush (at most
 * width * 64 px, since lvgl_port allocates buf_size = w * h / 8 and
 * LVGL chunks accordingly). For 320x170 that is ~40 KB. We size to
 * a generous fraction of the framebuffer to avoid splitting flushes;
 * if allocation fails we degrade to row-by-row streaming. */
static uint16_t *s_dma_buf      = NULL;
static size_t    s_dma_buf_pixels = 0;

/* Binary semaphore signalling that the most recent draw_bitmap()
 * DMA has fully drained the scratch buffer. esp_lcd's
 * draw_bitmap is asynchronous, so we wait on this before reusing
 * s_dma_buf or returning from display_flush(). Initial state is
 * "given" so the very first display_flush() does not block. */
static SemaphoreHandle_t s_trans_done = NULL;

static bool IRAM_ATTR on_color_trans_done(esp_lcd_panel_io_handle_t /*io*/,
                                          esp_lcd_panel_io_event_data_t * /*ed*/,
                                          void * /*user_ctx*/)
{
    BaseType_t hp_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(s_trans_done, &hp_task_woken);
    return hp_task_woken == pdTRUE;
}

extern "C" void display_init(int /*pin_a*/, int /*pin_b*/, int /*pin_c*/,
                             int /*pin_d*/, int /*pin_e*/, int /*pin_f*/,
                             int /*width*/, int /*height*/)
{
    /* The ST7789 i80 driver needs 14 GPIOs which do not fit in
     * display_init()'s 6 pin slots, so it exposes its own
     * struct-based init (display_st7789_init). Calling the generic
     * display_init on an ST7789 build is a programming error;
     * abort loudly. */
    ESP_LOGE(TAG, "display_init() is not supported on ST7789; "
                  "call display_st7789_init() instead");
    abort();
}

extern "C" void display_st7789_init(const display_st7789_config_t *cfg)
{
    assert(cfg);
    s_width  = cfg->width;
    s_height = cfg->height;
    s_bl_pin = cfg->bl;

    /* Create the trans-done semaphore in the "given" state so the
     * first display_flush() doesn't block. The DMA-done callback
     * registered below gives it from ISR after each draw_bitmap. */
    s_trans_done = xSemaphoreCreateBinary();
    if (!s_trans_done) {
        ESP_LOGE(TAG, "Failed to create trans-done semaphore (out of memory)");
        abort();
    }
    xSemaphoreGive(s_trans_done);

    /* ---- i80 bus ---- */
    esp_lcd_i80_bus_handle_t bus = NULL;
    esp_lcd_i80_bus_config_t bus_cfg = {};
    bus_cfg.dc_gpio_num    = cfg->dc;
    bus_cfg.wr_gpio_num    = cfg->wr;
    bus_cfg.clk_src        = LCD_CLK_SRC_DEFAULT;
    for (int i = 0; i < 8; i++) bus_cfg.data_gpio_nums[i] = cfg->data[i];
    bus_cfg.bus_width      = 8;
    /* Maximum bytes transferred in one esp_lcd_panel_draw_bitmap()
     * call. We size it to the full framebuffer so a worst-case
     * full-screen flush goes through in one DMA chain. */
    bus_cfg.max_transfer_bytes = (size_t)s_width * s_height * sizeof(uint16_t) + 16;
    /* Allow the i80 bus's DMA descriptors to address PSRAM (the LVGL
     * draw buffers and our s_fb live there). The legacy
     * psram_trans_align / sram_trans_align fields were deprecated in
     * ESP-IDF 5.3 in favour of a single dma_burst_size value (in
     * bytes); 64 keeps the cache-line alignment requirement for
     * PSRAM-backed buffers satisfied. */
    bus_cfg.dma_burst_size      = 64;
    ESP_ERROR_CHECK(esp_lcd_new_i80_bus(&bus_cfg, &bus));

    /* ---- panel-IO ---- */
    esp_lcd_panel_io_i80_config_t io_cfg = {};
    io_cfg.cs_gpio_num     = cfg->cs;
    io_cfg.pclk_hz         = ST7789_PCLK_HZ;
    io_cfg.trans_queue_depth = ST7789_TRANS_QUEUE;
    io_cfg.lcd_cmd_bits    = 8;
    io_cfg.lcd_param_bits  = 8;
    io_cfg.dc_levels.dc_idle_level     = 0;
    io_cfg.dc_levels.dc_cmd_level      = 0;
    io_cfg.dc_levels.dc_dummy_level    = 0;
    io_cfg.dc_levels.dc_data_level     = 1;
    /* Byte-swap on the way out so our little-endian RGB565
     * framebuffer is shipped to the panel as big-endian, which is
     * what the ST7789 expects when COLMOD = 0x55 (RGB565). */
    io_cfg.flags.swap_color_bytes = 1;
    io_cfg.on_color_trans_done    = on_color_trans_done;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i80(bus, &io_cfg, &s_io));

    /* ---- ST7789 panel ---- */
    esp_lcd_panel_dev_config_t panel_cfg = {};
    panel_cfg.reset_gpio_num = cfg->rst;
    /* rgb_endian / rgb_ele_order defaults to RGB on a zero-init
     * struct, which matches the ST7789's MADCTL.BGR=0 default; do
     * not set the field explicitly here because it was renamed
     * between ESP-IDF 5.3 and 5.4 (rgb_endian -> rgb_ele_order). */
    panel_cfg.bits_per_pixel = 16;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(s_io, &panel_cfg, &s_panel));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    if (cfg->invert_color) {
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, true));
    }
    if (cfg->swap_xy) {
        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(s_panel, true));
    }
    if (cfg->mirror_x || cfg->mirror_y) {
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel, cfg->mirror_x, cfg->mirror_y));
    }
    if (cfg->x_gap || cfg->y_gap) {
        ESP_ERROR_CHECK(esp_lcd_panel_set_gap(s_panel, cfg->x_gap, cfg->y_gap));
    }
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    /* ---- Framebuffer + DMA scratch ---- */
    s_fb_pixels = (size_t)s_width * s_height;
    s_fb = (uint16_t *)heap_caps_malloc(s_fb_pixels * sizeof(uint16_t),
                                        MALLOC_CAP_SPIRAM);
    if (!s_fb) {
        ESP_LOGE(TAG, "Framebuffer alloc failed (%u bytes from PSRAM)",
                 (unsigned)(s_fb_pixels * sizeof(uint16_t)));
        abort();
    }
    memset(s_fb, 0, s_fb_pixels * sizeof(uint16_t));

    /* Pre-allocate a DMA-capable scratch buffer in PSRAM matching the
     * full panel; if PSRAM is exhausted, fall back to a small chunk
     * (one row) so display_flush() can still stream row-by-row. */
    s_dma_buf_pixels = s_fb_pixels;
    s_dma_buf = (uint16_t *)heap_caps_malloc(s_dma_buf_pixels * sizeof(uint16_t),
                                             MALLOC_CAP_SPIRAM);
    if (!s_dma_buf) {
        ESP_LOGW(TAG, "PSRAM scratch alloc failed, falling back to single-row");
        s_dma_buf_pixels = s_width;
        s_dma_buf = (uint16_t *)heap_caps_malloc(s_dma_buf_pixels * sizeof(uint16_t),
                                                 MALLOC_CAP_SPIRAM);
        if (!s_dma_buf) {
            ESP_LOGE(TAG, "Single-row scratch alloc failed (%u bytes); cannot continue",
                     (unsigned)(s_dma_buf_pixels * sizeof(uint16_t)));
            abort();
        }
    }

    /* Backlight LEDC channel is configured here with duty 0 (off);
     * the editor calls display_set_backlight() with the user-
     * configured (NVS-persisted) percent after this returns, so we
     * do not show a black/garbage flash. */
    if (s_bl_pin >= 0) {
        backlight_pwm_init(s_bl_pin);
    }

    /* Initial blank to avoid showing whatever junk was in panel RAM. */
    display_clear(0x00);
    display_full_refresh();

    ESP_LOGI(TAG, "ST7789 i80 %dx%d initialized", s_width, s_height);
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
    /* Caller passes *logical* coordinates; expand to a SCALE x SCALE
     * block of panel pixels to match display_push_rgb565(). */
    int px = (int)x * ST7789_SCALE;
    int py = (int)y * ST7789_SCALE;
    if (px >= s_width || py >= s_height) return;
    uint16_t v = (color == 0) ? 0x0000 : 0xFFFF;
    int x_end = px + ST7789_SCALE; if (x_end > s_width)  x_end = s_width;
    int y_end = py + ST7789_SCALE; if (y_end > s_height) y_end = s_height;
    for (int yy = py; yy < y_end; yy++) {
        uint16_t *row = s_fb + (size_t)yy * s_width + px;
        for (int xx = px; xx < x_end; xx++) *row++ = v;
    }

    int x2 = x_end - 1;
    int y2 = y_end - 1;
    if (s_dirty_x1 < 0) {
        s_dirty_x1 = px;  s_dirty_y1 = py;
        s_dirty_x2 = x2;  s_dirty_y2 = y2;
    } else {
        if (px < s_dirty_x1) s_dirty_x1 = px;
        if (x2 > s_dirty_x2) s_dirty_x2 = x2;
        if (py < s_dirty_y1) s_dirty_y1 = py;
        if (y2 > s_dirty_y2) s_dirty_y2 = y2;
    }
}

extern "C" bool display_push_rgb565(int x, int y, int w, int h,
                                    const void *color_map)
{
    if (w <= 0 || h <= 0) return true;
    /* Caller passes *logical* coordinates and a tightly-packed
     * (logical w * logical h) RGB565 buffer. Nearest-neighbor expand
     * each source pixel into a SCALE x SCALE panel-pixel block in the
     * framebuffer. */
    int px = x * ST7789_SCALE;
    int py = y * ST7789_SCALE;
    int pw = w * ST7789_SCALE;
    int ph = h * ST7789_SCALE;
    if (px < 0 || py < 0) return true;
    int x2 = px + pw - 1;
    int y2 = py + ph - 1;
    if (x2 >= s_width)  x2 = s_width  - 1;
    if (y2 >= s_height) y2 = s_height - 1;
    int eff_w = x2 - px + 1;
    int eff_h = y2 - py + 1;
    if (eff_w <= 0 || eff_h <= 0) return true;

    const uint16_t *src = (const uint16_t *)color_map;
    if (ST7789_SCALE == 1) {
        for (int row = 0; row < eff_h; row++) {
            uint16_t *dst = s_fb + (size_t)(py + row) * s_width + px;
            memcpy(dst, src, eff_w * sizeof(uint16_t));
            src += w;
        }
    } else {
        /* Expand each source row into one panel row, then memcpy-replicate
         * (ST7789_SCALE - 1) more times. */
        for (int sy = 0; sy < h; sy++) {
            int dy0 = py + sy * ST7789_SCALE;
            if (dy0 >= s_height) break;
            uint16_t *drow0 = s_fb + (size_t)dy0 * s_width + px;
            int eff_w_row = eff_w;
            uint16_t *p = drow0;
            int written = 0;
            for (int sx = 0; sx < w && written < eff_w_row; sx++) {
                uint16_t v = src[(size_t)sy * w + sx];
                for (int k = 0; k < ST7789_SCALE && written < eff_w_row; k++) {
                    *p++ = v;
                    written++;
                }
            }
            for (int k = 1; k < ST7789_SCALE; k++) {
                int dy = dy0 + k;
                if (dy >= s_height) break;
                memcpy(s_fb + (size_t)dy * s_width + px, drow0,
                       (size_t)eff_w_row * sizeof(uint16_t));
            }
        }
    }

    if (s_dirty_x1 < 0) {
        s_dirty_x1 = px;  s_dirty_y1 = py;
        s_dirty_x2 = x2;  s_dirty_y2 = y2;
    } else {
        if (px < s_dirty_x1) s_dirty_x1 = px;
        if (x2 > s_dirty_x2) s_dirty_x2 = x2;
        if (py < s_dirty_y1) s_dirty_y1 = py;
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
    /* Caller passes *logical* coordinates; the dirty bbox and the
     * panel-write window operate in panel pixels, so scale up. */
    s_clip_x = x * ST7789_SCALE;
    s_clip_y = y * ST7789_SCALE;
    s_clip_w = w * ST7789_SCALE;
    s_clip_h = h * ST7789_SCALE;
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

    /* Pack the dirty rectangle into a contiguous DMA-friendly buffer
     * before handing it to esp_lcd_panel_draw_bitmap(). The framebuffer
     * stride (s_width) does not match the rect width unless x1==0 and
     * w==s_width, so a memcpy-per-row is required. If the scratch is
     * the full framebuffer we can fit the whole rect in one go;
     * otherwise we fall back to one row at a time. */
    size_t need = (size_t)w * h;
    if (need <= s_dma_buf_pixels) {
        /* Wait for the previous DMA to finish before reusing s_dma_buf. */
        xSemaphoreTake(s_trans_done, portMAX_DELAY);
        for (int row = 0; row < h; row++) {
            const uint16_t *src = s_fb + (size_t)(y1 + row) * s_width + x1;
            uint16_t       *dst = s_dma_buf + (size_t)row * w;
            memcpy(dst, src, (size_t)w * sizeof(uint16_t));
        }
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(
            s_panel, x1, y1, x2 + 1, y2 + 1, s_dma_buf));
        /* Block here too so display_flush() is fully synchronous from
         * the caller's point of view (matches the AXS15231B and RLCD
         * backends) and the next flush always finds the semaphore in
         * the "given" state. */
        xSemaphoreTake(s_trans_done, portMAX_DELAY);
        xSemaphoreGive(s_trans_done);
    } else {
        for (int row = 0; row < h; row++) {
            xSemaphoreTake(s_trans_done, portMAX_DELAY);
            const uint16_t *src = s_fb + (size_t)(y1 + row) * s_width + x1;
            memcpy(s_dma_buf, src, (size_t)w * sizeof(uint16_t));
            ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(
                s_panel, x1, y1 + row, x2 + 1, y1 + row + 1, s_dma_buf));
            xSemaphoreTake(s_trans_done, portMAX_DELAY);
            xSemaphoreGive(s_trans_done);
        }
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

extern "C" void display_sleep(void)
{
    if (s_panel_asleep) return;
    s_panel_asleep = true;
    /* Capture the user's brightness BEFORE display_set_backlight(0)
     * overwrites s_bl_last_pct, so display_wake() can restore it
     * accurately. */
    int saved_pct = s_bl_last_pct;
    display_set_backlight(0);
    if (s_panel) {
        esp_lcd_panel_disp_on_off(s_panel, false);
    }
    s_bl_last_pct = saved_pct ? saved_pct : 100;
}

extern "C" void display_wake(void)
{
    if (!s_panel_asleep) return;
    if (s_panel) {
        esp_lcd_panel_disp_on_off(s_panel, true);
    }
    s_panel_asleep = false;
    int pct = s_bl_last_pct > 0 ? s_bl_last_pct : 100;
    display_set_backlight(pct);
    display_full_refresh();
}

extern "C" uint8_t *display_get_buffer(void)
{
    return (uint8_t *)s_fb;
}

extern "C" int display_get_buffer_size(void)
{
    return (int)(s_fb_pixels * sizeof(uint16_t));
}

extern "C" void display_deep_sleep_prepare(void)
{
    /* Blank the panel via DISPOFF + SLPIN and cut the backlight.
     * On the T-Display-S3 the BL is wired through an external
     * MOSFET driven from GPIO; LEDC duty 0 holds the pin LOW and
     * the MOSFET turns the BL off. We do not bother latching the
     * pin through deep sleep here because the T-Display-S3 has no
     * external pull-up on its BL line -- once the IO mux releases
     * the pin it floats low and the BL stays off. */
    display_sleep();
}

#endif /* CONFIG_DRAFTLING_DISPLAY_ST7789 */
