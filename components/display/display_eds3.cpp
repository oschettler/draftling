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
    /* Bring up the M5GFX panel first. M5GFX auto-detects the PaperS3
     * board and configures the LCD/I80 peripheral, control GPIOs and
     * power rail internally.
     *
     * The PaperS3 panel is configured by M5GFX with
     *   panel_width=960, panel_height=540, offset_rotation=3
     * Per LovyanGFX's Panel_HasBuffer::setRotation, the user-visible
     * dimensions are:
     *   internal_rotation = (user_rotation + offset_rotation) & 3
     *   width = panel_width, height = panel_height
     *   if (internal_rotation & 1) swap(width, height)
     * setRotation(1) -> internal_rotation=0 -> width=960, height=540
     * (landscape, "horizontal" -- what the user expects). */
    s_gfx.init();
    s_gfx.setRotation(1);
    /* epd_quality runs the full grayscale waveform on every refresh.
     * It is slower than epd_text/epd_fast, but it fully drives each
     * pixel to its target level so the panel reaches a clean white
     * instead of accumulating ghosting / grey residue. The faster
     * modes are designed for incremental partial updates over an
     * already-clean baseline; with our LVGL FULL render mode every
     * flush rewrites the whole screen, so the partial-update modes
     * leave the panel looking muddy and "flickering". */
    s_gfx.setEpdMode(epd_mode_t::epd_quality);

    int gfx_w = s_gfx.width();
    int gfx_h = s_gfx.height();

    /* Use the panel's own reported dimensions as authoritative. If
     * Kconfig (CONFIG_DRAFTLING_DISPLAY_WIDTH/HEIGHT) is stale (e.g.
     * an old sdkconfig still has the historical 540x960 portrait
     * defaults), trusting Kconfig means drawPixel() calls outside the
     * panel bounds get silently clipped while pixels inside the
     * "missing" portion are never written -- producing a tiny corner
     * of UI on a mostly-grey screen. Override silently if needed. */
    if (gfx_w != width || gfx_h != height) {
        ESP_LOGW(TAG,
                 "Configured framebuffer size %dx%d does not match M5GFX "
                 "panel size %dx%d; using panel size. Update "
                 "CONFIG_DRAFTLING_DISPLAY_WIDTH/HEIGHT (delete sdkconfig "
                 "and rebuild to pick up the new defaults).",
                 width, height, gfx_w, gfx_h);
    }
    s_width  = gfx_w;
    s_height = gfx_h;
    s_stride = (s_width + 7) / 8;

    /* Allocate framebuffer in PSRAM (1 bit per pixel) */
    s_disp_len = s_stride * s_height;
    s_disp_buf = (uint8_t *)heap_caps_malloc(s_disp_len, MALLOC_CAP_SPIRAM);
    assert(s_disp_buf);
    /* Start clean (all white) */
    memset(s_disp_buf, 0xFF, s_disp_len);

    /* Initial full white refresh so the panel leaves its muddy
     * power-on state. */
    s_gfx.fillScreen(TFT_WHITE);
    s_gfx.display();
    s_gfx.waitDisplay();

    ESP_LOGI(TAG, "PaperS3 display initialized via M5GFX (%dx%d, 1 bpp)",
             s_width, s_height);
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

    /* Wait for any in-flight panel refresh to finish before queuing a
     * new one. M5GFX's Panel_EPD runs its waveform on a background
     * task; if we keep calling display() while it is mid-refresh we
     * stack up overlapping updates which the user perceives as
     * flicker. Block here so the panel always reaches a steady state
     * between flushes. */
    s_gfx.waitDisplay();

    /* Push the 1-bpp framebuffer one row at a time as 8-bpp grayscale.
     * Panel_EPD always operates in grayscale_8bit, so passing a
     * grayscale_t* row buffer through pushImage avoids the per-pixel
     * call overhead of drawPixel and lets LovyanGFX use its straight
     * row-copy fast path. The row buffer lives in PSRAM and is
     * recycled across flushes. */
    static lgfx::v1::grayscale_t *row_buf = nullptr;
    static int row_buf_w = 0;
    if (row_buf == nullptr || row_buf_w != s_width) {
        if (row_buf) heap_caps_free(row_buf);
        row_buf = (lgfx::v1::grayscale_t *)heap_caps_malloc(
            sizeof(lgfx::v1::grayscale_t) * s_width, MALLOC_CAP_SPIRAM);
        assert(row_buf);
        row_buf_w = s_width;
    }

    s_gfx.startWrite();
    for (int y = 0; y < s_height; y++) {
        const uint8_t *src = s_disp_buf + y * s_stride;
        for (int x = 0; x < s_width; x++) {
            uint8_t mask = (uint8_t)(0x80 >> (x & 7));
            row_buf[x].raw = (src[x >> 3] & mask) ? 0xFF : 0x00;
        }
        s_gfx.pushImage(0, y, s_width, 1, row_buf);
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
