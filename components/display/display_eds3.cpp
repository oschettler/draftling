#include "sdkconfig.h"
#if defined(CONFIG_DRAFTLING_MODEL_M5STACK_PAPERS3)

/*
 * M5Stack PaperS3 e-paper display driver (thin shim over M5GFX).
 *
 * The PaperS3 drives a 540x960 ED047TC1 panel through the ESP32-S3
 * LCD/I80 parallel peripheral with multi-pass grayscale waveforms.
 * Re-implementing that from scratch is several hundred lines of
 * timing-critical code, so this driver delegates all panel access to
 * the m5stack/M5GFX managed component (https://github.com/m5stack/M5GFX),
 * which detects the PaperS3 board automatically and exposes a simple
 * drawing API.
 *
 * Architecture
 * ------------
 * Unlike the RLCD and UC8179 backends, the PaperS3 backend does NOT
 * keep its own 1-bpp framebuffer. M5GFX already maintains a 4-bpp
 * grayscale framebuffer internally, so an extra 1-bpp buffer would
 * be redundant. We expose the optional display_push_rgb565() fast
 * path, which lvgl_port.cpp calls from its flush_cb to push the
 * LVGL RGB565 framebuffer straight into M5GFX. M5GFX handles the
 * RGB565 -> grayscale conversion and Bayer dithering internally.
 *
 * Refresh strategy
 * ----------------
 * lvgl_port.cpp uses LV_DISPLAY_RENDER_MODE_PARTIAL on this backend,
 * so each display_push_rgb565() call corresponds to a single LVGL
 * invalidated rectangle (cursor blink, edited line, status bar,
 * etc.). We accumulate those rectangles into a dirty bounding box
 * and, on display_flush(), call M5GFX's region-scoped
 *   s_gfx.display(x, y, w, h)
 * with the fast `epd_text` waveform. That repaints only the changed
 * pixels in ~150-300 ms instead of running the full grayscale
 * waveform over all 540x960 pixels (~700-900 ms) on every keystroke.
 *
 * E-paper partial refreshes accumulate ghosting, so every
 * CONFIG_DRAFTLING_EPD_FULL_REFRESH_INTERVAL partial updates we
 * promote the next refresh to a full-screen `epd_quality` pass to
 * reset the panel to a clean baseline. display_clear() and
 * display_full_refresh() also trigger a full quality refresh.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_heap_caps.h>

#include <M5GFX.h>

#include "display.h"

static const char *TAG = "DisplayEDS3";

static M5GFX s_gfx;
static int   s_width  = 0;
static int   s_height = 0;

/* Dirty bounding box accumulated since the last display_flush(). */
static int  s_dx0 = 0, s_dy0 = 0, s_dx1 = -1, s_dy1 = -1;
/* True if the next flush must be a full-screen quality refresh. */
static bool s_force_full = true;
/* Number of partial refreshes since the last full refresh. When this
 * reaches CONFIG_DRAFTLING_EPD_FULL_REFRESH_INTERVAL we force a full
 * refresh to clear residual ghosting. */
static int  s_partial_count = 0;

#ifdef CONFIG_DRAFTLING_EPD_FULL_REFRESH_INTERVAL
#define EDS3_FULL_REFRESH_INTERVAL CONFIG_DRAFTLING_EPD_FULL_REFRESH_INTERVAL
#else
#define EDS3_FULL_REFRESH_INTERVAL 50
#endif

static inline void mark_dirty_rect(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) return;
    int x0 = x;
    int y0 = y;
    int x1 = x + w - 1;
    int y1 = y + h - 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= s_width)  x1 = s_width  - 1;
    if (y1 >= s_height) y1 = s_height - 1;
    if (x1 < x0 || y1 < y0) return;
    if (s_dx1 < s_dx0 || s_dy1 < s_dy0) {
        /* Empty -> initialize. */
        s_dx0 = x0; s_dy0 = y0; s_dx1 = x1; s_dy1 = y1;
    } else {
        if (x0 < s_dx0) s_dx0 = x0;
        if (y0 < s_dy0) s_dy0 = y0;
        if (x1 > s_dx1) s_dx1 = x1;
        if (y1 > s_dy1) s_dy1 = y1;
    }
}

static inline void clear_dirty(void)
{
    s_dx0 = 0; s_dy0 = 0; s_dx1 = -1; s_dy1 = -1;
}

/* ---- public API ---- */

extern "C" void display_init(int /*pin_a*/, int /*pin_b*/, int /*pin_c*/,
                             int /*pin_d*/, int /*pin_e*/, int /*pin_f*/,
                             int width, int height)
{
    /* Bring up the M5GFX panel. M5GFX auto-detects the PaperS3 board
     * and configures the LCD/I80 peripheral, control GPIOs and power
     * rail internally.
     *
     * The PaperS3 panel is configured by M5GFX with
     *   panel_width=960, panel_height=540, offset_rotation=3
     * setRotation(1) -> internal_rotation=0 -> width=960, height=540
     * (landscape, "horizontal" -- what the user expects). */
    s_gfx.init();
    s_gfx.setRotation(1);

    s_width  = s_gfx.width();
    s_height = s_gfx.height();
    if (s_width != width || s_height != height) {
        ESP_LOGW(TAG,
                 "Configured framebuffer size %dx%d does not match M5GFX "
                 "panel size %dx%d; using panel size. Update "
                 "CONFIG_DRAFTLING_DISPLAY_WIDTH/HEIGHT (delete sdkconfig "
                 "and rebuild to pick up the new defaults).",
                 width, height, s_width, s_height);
    }

    /* Initial full white refresh so the panel leaves its muddy
     * power-on state. */
    s_gfx.setEpdMode(epd_mode_t::epd_quality);
    s_gfx.fillScreen(TFT_WHITE);
    s_gfx.display();
    s_gfx.waitDisplay();

    clear_dirty();
    s_force_full = false;
    s_partial_count = 0;

    ESP_LOGI(TAG, "PaperS3 display initialized via M5GFX (%dx%d), "
                  "partial refresh every flush, full refresh every "
                  "%d partials",
             s_width, s_height, EDS3_FULL_REFRESH_INTERVAL);
}

extern "C" void display_clear(uint8_t color)
{
    s_gfx.fillScreen((color != 0) ? TFT_WHITE : TFT_BLACK);
    /* A full-screen clear deserves a clean full-screen refresh -
     * partial waveforms would leave the previous frame ghosted
     * underneath the new background. */
    mark_dirty_rect(0, 0, s_width, s_height);
    s_force_full = true;
}

extern "C" void display_set_pixel(uint16_t x, uint16_t y, uint8_t color)
{
    /* Slow per-pixel path. lvgl_port.cpp normally bypasses this via
     * the display_push_rgb565() fast path; this function exists only
     * to satisfy the legacy display.h API. */
    if ((int)x >= s_width || (int)y >= s_height) return;
    s_gfx.drawPixel(x, y, (color != 0) ? TFT_WHITE : TFT_BLACK);
    mark_dirty_rect(x, y, 1, 1);
}

extern "C" bool display_push_rgb565(int x, int y, int w, int h,
                                    const void *color_map)
{
    if (color_map == nullptr) return false;
    if (w <= 0 || h <= 0) return false;
    /* M5GFX's pushImage accepts an rgb565_t* and writes into its
     * internal grayscale framebuffer. It does NOT trigger a panel
     * refresh - that happens in display_flush(). */
    s_gfx.pushImage(x, y, w, h,
                    (const lgfx::v1::rgb565_t *)color_map);
    mark_dirty_rect(x, y, w, h);
    return true;
}

extern "C" void display_flush(void)
{
    /* Nothing changed since the last flush -> nothing to do. */
    if (s_dx1 < s_dx0 || s_dy1 < s_dy0) return;

    /* Wait for any in-flight panel refresh to finish before queuing a
     * new one. M5GFX's Panel_EPD runs its waveform on a background
     * task; if we keep calling display() while it is mid-refresh we
     * stack up overlapping updates which the user perceives as
     * flicker. */
    s_gfx.waitDisplay();

    int dx0 = s_dx0, dy0 = s_dy0, dx1 = s_dx1, dy1 = s_dy1;
    int dw  = dx1 - dx0 + 1;
    int dh  = dy1 - dy0 + 1;

    /* If the dirty area covers most of the screen, a full refresh is
     * cheaper (and cleaner) than a giant partial. */
    bool huge = ((long)dw * dh) * 4 > ((long)s_width * s_height) * 3;

    bool do_full = s_force_full || huge ||
                   s_partial_count >= EDS3_FULL_REFRESH_INTERVAL;

    if (do_full) {
        s_gfx.setEpdMode(epd_mode_t::epd_quality);
        s_gfx.display();
        s_partial_count = 0;
        s_force_full = false;
    } else {
        /* epd_text is the fastest waveform: a short two-pass update
         * that only drives the pixels in the supplied rectangle.
         * Ideal for cursor blinks and single-line text edits. */
        s_gfx.setEpdMode(epd_mode_t::epd_text);
        s_gfx.display(dx0, dy0, dw, dh);
        s_partial_count++;
    }

    s_gfx.waitDisplay();
    clear_dirty();
}

extern "C" void display_full_refresh(void)
{
    /* Force the next flush to be a full-screen quality refresh, even
     * if nothing is dirty (caller wants to clean up ghosting). */
    mark_dirty_rect(0, 0, s_width, s_height);
    s_force_full = true;
    display_flush();
}

extern "C" uint8_t *display_get_buffer(void)
{
    /* No 1-bpp framebuffer on this backend. */
    return nullptr;
}

extern "C" int display_get_buffer_size(void)
{
    return 0;
}

#endif /* CONFIG_DRAFTLING_MODEL_M5STACK_PAPERS3 */
