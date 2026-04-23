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
 * be redundant - and the round-trip
 *
 *     LVGL RGB565  ->  our 1-bpp buffer  ->  8-bpp grayscale row
 *                  ->  M5GFX 4-bpp framebuffer
 *
 * proved fragile (it produced unreadable 3-4 px tall text on the
 * panel). Instead, we expose the optional display_push_rgb565()
 * fast path, which lvgl_port.cpp calls from its flush_cb to push
 * the LVGL RGB565 framebuffer straight into M5GFX. M5GFX handles
 * the RGB565 -> grayscale conversion and Bayer dithering internally.
 *
 * This mirrors the proven M5GFX + LVGL bridge used by the public
 * Boisti13/papers3-dashboard project (see src/display/epd_driver.cpp
 * in that repo).
 *
 * Public API
 * ----------
 *   display_init(...)           pin params ignored - M5GFX configures
 *                               everything from the M5PaperS3 board id
 *   display_clear(color)        s_gfx.fillScreen(white|black)
 *   display_set_pixel(x,y,c)    s_gfx.drawPixel
 *   display_flush()             trigger panel refresh, wait for it
 *   display_full_refresh()      alias for display_flush() in v1
 *   display_push_rgb565(...)    fast path used by lvgl_port.cpp
 *   display_get_buffer / size   return null/0 - no 1-bpp framebuffer
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

static M5GFX s_gfx;
static int   s_width  = 0;
static int   s_height = 0;
static bool  s_dirty  = false;

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
     * leave the panel looking muddy. */
    s_gfx.setEpdMode(epd_mode_t::epd_quality);

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
    s_gfx.fillScreen(TFT_WHITE);
    s_gfx.display();
    s_gfx.waitDisplay();

    ESP_LOGI(TAG, "PaperS3 display initialized via M5GFX (%dx%d)",
             s_width, s_height);
}

extern "C" void display_clear(uint8_t color)
{
    s_gfx.fillScreen((color != 0) ? TFT_WHITE : TFT_BLACK);
    s_dirty = true;
}

extern "C" void display_set_pixel(uint16_t x, uint16_t y, uint8_t color)
{
    /* Slow per-pixel path. lvgl_port.cpp normally bypasses this via
     * the display_push_rgb565() fast path; this function exists only
     * to satisfy the legacy display.h API. */
    if ((int)x >= s_width || (int)y >= s_height) return;
    s_gfx.drawPixel(x, y, (color != 0) ? TFT_WHITE : TFT_BLACK);
    s_dirty = true;
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
    s_dirty = true;
    return true;
}

extern "C" void display_flush(void)
{
    if (!s_dirty) return;

    /* Wait for any in-flight panel refresh to finish before queuing a
     * new one. M5GFX's Panel_EPD runs its waveform on a background
     * task; if we keep calling display() while it is mid-refresh we
     * stack up overlapping updates which the user perceives as
     * flicker. Block here so the panel always reaches a steady state
     * between flushes. */
    s_gfx.waitDisplay();
    s_gfx.display();
    s_gfx.waitDisplay();

    s_dirty = false;
}

extern "C" void display_full_refresh(void)
{
    /* In v1 every refresh is a full refresh. Force the panel to
     * redraw even if nothing has been drawn since the last flush. */
    s_dirty = true;
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
