#include "sdkconfig.h"
#if defined(CONFIG_DRAFTLING_MODEL_M5STACK_PAPERS3)

/*
 * M5Stack PaperS3 e-paper display driver (1-bit B/W shim over M5GFX).
 *
 * The PaperS3 drives a 540x960 ED047TC1 panel through the ESP32-S3
 * LCD/I80 parallel peripheral with multi-pass grayscale waveforms.
 * Re-implementing that from scratch is several hundred lines of
 * timing-critical code, so this driver delegates all panel access to
 * the m5stack/M5GFX managed component (https://github.com/m5stack/M5GFX),
 * which detects the PaperS3 board automatically and exposes a simple
 * drawing API.
 *
 * Draftling's UI is 1 bpp, so we keep an internal 1-bpp framebuffer
 * (same packed layout as the UC8179 driver: 1 bit per pixel, 8 pixels
 * per byte, MSB = leftmost, packed row-major; bit set = white) and
 * push it to M5GFX at flush time. v1 always performs a full refresh;
 * partial refresh and grayscale are TODO.
 *
 * The driver implements the same public API as the other backends
 * so that the LVGL port (lvgl_port.cpp) can stay generic:
 *
 *   display_init(...)             -- pin params ignored, M5GFX
 *                                    configures everything from the
 *                                    M5PaperS3 board id
 *   display_clear(color)
 *   display_set_pixel(x, y, color)
 *   display_flush()               -- full refresh
 *   display_full_refresh()        -- alias for display_flush() in v1
 *   display_get_buffer / size     -- exposes the 1-bpp framebuffer
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_heap_caps.h>

#include <M5GFX.h>

#include "display.h"

static const char *TAG = "DisplayEDS3";

static M5GFX     s_gfx;
static uint8_t  *s_disp_buf = nullptr;
static int       s_disp_len = 0;
static int       s_width    = 0;
static int       s_height   = 0;
static int       s_stride   = 0;
static bool      s_dirty    = false;

/* ---- public API ---- */

extern "C" void display_init(int /*pin_a*/, int /*pin_b*/, int /*pin_c*/,
                             int /*pin_d*/, int /*pin_e*/, int /*pin_f*/,
                             int width, int height)
{
    s_width  = width;
    s_height = height;
    s_stride = (width + 7) / 8;

    /* Allocate framebuffer in PSRAM (1 bit per pixel) */
    s_disp_len = s_stride * height;
    s_disp_buf = (uint8_t *)heap_caps_malloc(s_disp_len, MALLOC_CAP_SPIRAM);
    assert(s_disp_buf);
    /* Start clean (all white) */
    memset(s_disp_buf, 0xFF, s_disp_len);

    /* Bring up the M5GFX panel. M5GFX auto-detects the PaperS3 board
     * and configures the LCD/I80 peripheral, control GPIOs and power
     * rail internally. The PaperS3 panel is registered as 960x540
     * landscape (offset_rotation=3); with setRotation(0) the GFX
     * reports width()=960, height()=540. Our framebuffer dimensions
     * MUST agree, otherwise drawPixel() calls past the panel bounds
     * are silently clipped and only the top-left fits on screen. */
    s_gfx.init();
    s_gfx.setEpdMode(epd_mode_t::epd_text);
    s_gfx.setRotation(0);

    int gfx_w = s_gfx.width();
    int gfx_h = s_gfx.height();
    if (gfx_w != s_width || gfx_h != s_height) {
        ESP_LOGE(TAG,
                 "Framebuffer size %dx%d does not match M5GFX panel size %dx%d; "
                 "pixels outside the panel will be clipped. Adjust "
                 "CONFIG_DRAFTLING_DISPLAY_WIDTH/HEIGHT to match.",
                 s_width, s_height, gfx_w, gfx_h);
    }

    s_gfx.fillScreen(TFT_WHITE);
    s_gfx.display();

    ESP_LOGI(TAG, "PaperS3 display initialized via M5GFX (%dx%d, 1 bpp)",
             width, height);
}

extern "C" void display_clear(uint8_t color)
{
    if (!s_disp_buf) return;
    /* color != 0 means white in our 1-bpp convention (bit set = white) */
    memset(s_disp_buf, (color != 0) ? 0xFF : 0x00, s_disp_len);
    s_dirty = true;
}

extern "C" void display_set_pixel(uint16_t x, uint16_t y, uint8_t color)
{
    if (!s_disp_buf) return;
    if (x >= s_width || y >= s_height) return;
    int idx = (int)y * s_stride + (x >> 3);
    uint8_t mask = (uint8_t)(0x80 >> (x & 7));
    uint8_t prev = s_disp_buf[idx];
    uint8_t next;
    if (color != 0) {
        next = prev | mask;       /* white */
    } else {
        next = prev & ~mask;      /* black */
    }
    if (next != prev) {
        s_disp_buf[idx] = next;
        s_dirty = true;
    }
}

extern "C" void display_flush(void)
{
    if (!s_disp_buf || !s_dirty) return;

    /* Push the 1-bpp framebuffer to M5GFX. We iterate every pixel and
     * call drawPixel() inside a startWrite/endWrite batch. drawPixel
     * has per-call overhead but it is unambiguously available across
     * every M5GFX/LovyanGFX version, and the e-paper refresh itself
     * (3-5 s) dominates the overall flush time. A future optimization
     * is to switch to setAddrWindow + pushPixels with a packed
     * grayscale row buffer. */
    s_gfx.startWrite();
    for (int y = 0; y < s_height; y++) {
        const uint8_t *src = s_disp_buf + y * s_stride;
        for (int x = 0; x < s_width; x++) {
            uint8_t mask = (uint8_t)(0x80 >> (x & 7));
            uint32_t color = (src[x >> 3] & mask) ? TFT_WHITE : TFT_BLACK;
            s_gfx.drawPixel(x, y, color);
        }
    }
    s_gfx.endWrite();
    s_gfx.display();

    s_dirty = false;
}

extern "C" void display_full_refresh(void)
{
    /* In v1 every refresh is a full refresh. Force the panel to redraw
     * even if the framebuffer hasn't changed. */
    s_dirty = true;
    display_flush();
}

extern "C" uint8_t *display_get_buffer(void)
{
    return s_disp_buf;
}

extern "C" int display_get_buffer_size(void)
{
    return s_disp_len;
}

#endif /* CONFIG_DRAFTLING_MODEL_M5STACK_PAPERS3 */
