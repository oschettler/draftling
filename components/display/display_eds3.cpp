#include "sdkconfig.h"
#if defined(CONFIG_DRAFTLING_DISPLAY_EDS3)

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
 * Unlike the RLCD backend, the PaperS3 backend does NOT
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
 * with the single-pulse `epd_fast` waveform. That repaints only the
 * changed pixels in ~80-150 ms with one visible flash instead of the
 * two-pass `epd_text` flicker (~150-300 ms) we used previously, and
 * is much cheaper than the full grayscale waveform over all 540x960
 * pixels (~700-900 ms).
 *
 * Two further optimizations cut typing latency:
 *
 *  - Flush debounce. When display_flush() is called less than
 *    EDS3_DEBOUNCE_MS (120 ms) after the previous panel refresh we
 *    schedule a deferred flush via esp_timer instead of driving the
 *    panel right away. Subsequent pushes within the window fold into
 *    the dirty bbox and the deferred flush picks them up in one go.
 *    A burst of fast keystrokes now yields one panel refresh instead
 *    of one per character.
 *
 *  - One-shot panel-refresh clip via display_set_partial_clip().
 *    The editor uses this to narrow the next refresh to the area
 *    around the typed character (cursor + edited columns) when it
 *    knows the rest of the LVGL-pushed line pixels are unchanged.
 *    The framebuffer is still updated over the full LVGL dirty bbox
 *    -- only the e-paper refresh region is clipped.
 *
 * E-paper partial refreshes accumulate ghosting, so every
 * CONFIG_DRAFTLING_EPD_FULL_REFRESH_INTERVAL partial updates we
 * promote the next refresh to a full-screen `epd_quality` pass to
 * reset the panel to a clean baseline. display_clear() and
 * display_full_refresh() also trigger a full quality refresh and
 * bypass the debounce so user-visible "clear screen" actions remain
 * instantaneous.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>

#include <M5GFX.h>

#include "display.h"
#include "lvgl_port.h"

static const char *TAG = "DisplayEDS3";

static M5GFX s_gfx;
static int   s_width  = 0;
static int   s_height = 0;

/* Logical-to-panel pixel scale. Read from Kconfig at compile time.
 * Each logical LVGL pixel is rendered as SCALE x SCALE physical
 * panel pixels. */
#ifdef CONFIG_DRAFTLING_DISPLAY_SCALE
#define EDS3_SCALE CONFIG_DRAFTLING_DISPLAY_SCALE
#else
#define EDS3_SCALE 1
#endif

/* Dirty bounding box accumulated since the last display_flush(). */
static int  s_dx0 = 0, s_dy0 = 0, s_dx1 = -1, s_dy1 = -1;
/* True if the next flush must be a full-screen quality refresh. */
static bool s_force_full = true;
/* Number of partial refreshes since the last full refresh. When this
 * reaches CONFIG_DRAFTLING_EPD_FULL_REFRESH_INTERVAL we force a full
 * refresh to clear residual ghosting. */
static int  s_partial_count = 0;

/* One-shot panel refresh clip set by display_set_partial_clip(). The
 * caller (typically the editor) uses it to narrow the next flush to
 * the area where pixels actually changed (e.g. a single typed glyph
 * plus the cursor) when the LVGL framebuffer was repainted over a
 * larger region whose extra pixels are unchanged from what's already
 * on the panel. Consumed (cleared) by display_flush(). */
static int  s_clip_x0 = 0, s_clip_y0 = 0, s_clip_x1 = -1, s_clip_y1 = -1;
static bool s_clip_set = false;

/* ---- Flush debounce ----
 * On fast typing each keystroke triggers an editor_ui_refresh() and an
 * LVGL flush. If we drove the panel for every one of them the user
 * would see a stutter of 1-pulse epd_fast updates that obscures the
 * cursor and slows perceived input by ~80-120 ms each. Coalesce
 * flushes that arrive close together: when display_flush() runs less
 * than EDS3_DEBOUNCE_MS after the previous panel refresh, defer it via
 * an esp_timer. The deferred timer reacquires the LVGL mutex (held by
 * the LVGL task during a normal flush_cb) before calling back into
 * display_flush(), so M5GFX is never accessed concurrently. Subsequent
 * pushes that arrive while the deferral is pending simply accumulate
 * into the dirty bbox and the deferred flush picks them all up in one
 * shot. */
#define EDS3_DEBOUNCE_MS 120
static int64_t            s_last_flush_us = 0;     /* monotonic, microseconds */
static esp_timer_handle_t s_deferred_timer = nullptr;
static bool               s_deferred_pending = false;
/* True while the deferred-timer callback is calling back into
 * display_flush(); used to bypass the debounce check exactly once. */
static bool               s_in_deferred_flush = false;

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
    /* The first LVGL render after init redraws the entire UI from a
     * blank slate. Force it to be a single full-screen quality
     * refresh - otherwise it goes through the partial path (which
     * looks dim and is slow when LVGL slices the screen into many
     * small chunks). */
    s_force_full = true;
    s_partial_count = 0;
    /* Initialize the debounce reference time so the first call to
     * display_flush() never spuriously enters the deferred-flush
     * branch even if it happens within 120 ms of boot (the
     * s_force_full flag would bypass the check anyway, but this
     * keeps the post-init steady state unambiguous). */
    s_last_flush_us = esp_timer_get_time();

    ESP_LOGI(TAG, "PaperS3 display initialized via M5GFX (%dx%d panel, "
                  "scale=%d -> %dx%d logical), partial refresh every "
                  "flush, full refresh every %d partials",
             s_width, s_height, EDS3_SCALE,
             s_width / EDS3_SCALE, s_height / EDS3_SCALE,
             EDS3_FULL_REFRESH_INTERVAL);
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
     * to satisfy the legacy display.h API. Coordinates are *logical*
     * pixels -- expand to a SCALE x SCALE block of panel pixels. */
    int px = (int)x * EDS3_SCALE;
    int py = (int)y * EDS3_SCALE;
    if (px >= s_width || py >= s_height) return;
    int pw = EDS3_SCALE;
    int ph = EDS3_SCALE;
    if (px + pw > s_width)  pw = s_width  - px;
    if (py + ph > s_height) ph = s_height - py;
    s_gfx.fillRect(px, py, pw, ph,
                   (color != 0) ? TFT_WHITE : TFT_BLACK);
    mark_dirty_rect(px, py, pw, ph);
}

/* Scale a logical RGB565 image into a freshly-allocated panel-pixel
 * RGB565 buffer using nearest-neighbor (each source pixel becomes a
 * SCALE x SCALE block). Returns the new buffer (caller frees with
 * heap_caps_free), or nullptr on allocation failure. */
static uint16_t *scale_rgb565_2x_or_more(const uint16_t *src,
                                         int sw, int sh, int scale)
{
    int dw = sw * scale;
    int dh = sh * scale;
    size_t bytes = (size_t)dw * dh * sizeof(uint16_t);
    uint16_t *dst = (uint16_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    if (dst == nullptr) {
        /* Fall back to internal RAM for tiny buffers (e.g. the cursor
         * blink) when PSRAM is exhausted. */
        dst = (uint16_t *)heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
        if (dst == nullptr) return nullptr;
    }
    /* Two-step expand: first row-replicate horizontally into the
     * destination's first row of each block, then memcpy the rest of
     * each block from that row. This is cache-friendlier than a
     * naive nested loop and amortises the per-pixel branch. */
    for (int sy = 0; sy < sh; sy++) {
        uint16_t *drow0 = dst + (size_t)sy * scale * dw;
        const uint16_t *srow = src + (size_t)sy * sw;
        /* Horizontal expansion into the first row of the block. */
        uint16_t *p = drow0;
        for (int sx = 0; sx < sw; sx++) {
            uint16_t v = srow[sx];
            for (int k = 0; k < scale; k++) *p++ = v;
        }
        /* Replicate that row scale-1 more times. */
        for (int k = 1; k < scale; k++) {
            memcpy(drow0 + (size_t)k * dw, drow0,
                   (size_t)dw * sizeof(uint16_t));
        }
    }
    return dst;
}

extern "C" bool display_push_rgb565(int x, int y, int w, int h,
                                    const void *color_map)
{
    if (color_map == nullptr) return false;
    if (w <= 0 || h <= 0) return false;
    /* M5GFX's pushImage accepts an rgb565_t* and writes into its
     * internal grayscale framebuffer. It does NOT trigger a panel
     * refresh - that happens in display_flush().
     *
     * Coordinates and the source buffer are in *logical* pixels;
     * scale up to panel pixels using nearest-neighbor expansion. */
    int px = x * EDS3_SCALE;
    int py = y * EDS3_SCALE;
    int pw = w * EDS3_SCALE;
    int ph = h * EDS3_SCALE;
    if (EDS3_SCALE == 1) {
        s_gfx.pushImage(px, py, pw, ph,
                        (const lgfx::v1::rgb565_t *)color_map);
    } else {
        uint16_t *scaled = scale_rgb565_2x_or_more(
            (const uint16_t *)color_map, w, h, EDS3_SCALE);
        if (scaled == nullptr) {
            ESP_LOGE(TAG,
                     "scale_rgb565: out of memory for %dx%d -> %dx%d",
                     w, h, pw, ph);
            return false;
        }
        s_gfx.pushImage(px, py, pw, ph,
                        (const lgfx::v1::rgb565_t *)scaled);
        heap_caps_free(scaled);
    }
    mark_dirty_rect(px, py, pw, ph);
    return true;
}

extern "C" void display_flush(void);

/* esp_timer callback for the debounced deferred flush. Runs on the
 * esp_timer task, so we must take the LVGL mutex before touching
 * M5GFX (which is otherwise only accessed from the LVGL task). */
static void deferred_flush_cb(void *arg)
{
    (void)arg;
    if (!draftling_lvgl_port_lock(-1)) return;
    s_deferred_pending = false;
    s_in_deferred_flush = true;
    display_flush();
    s_in_deferred_flush = false;
    draftling_lvgl_port_unlock();
}

extern "C" void display_flush(void)
{
    /* Nothing changed since the last flush -> nothing to do. */
    if (s_dx1 < s_dx0 || s_dy1 < s_dy0) {
        /* If a clip was set but no pixels were ever pushed, drop it
         * so it doesn't apply to a later, unrelated flush. */
        s_clip_set = false;
        return;
    }

    /* ---- Flush debounce ----
     * If the previous panel refresh finished less than EDS3_DEBOUNCE_MS
     * ago, schedule a deferred flush instead of driving the panel
     * immediately. The deferred timer reacquires the LVGL mutex and
     * calls back into display_flush() with s_in_deferred_flush=true to
     * bypass this check. Force-full requests (typically from
     * display_clear() / display_full_refresh()) skip the debounce so
     * user-visible "clear screen" actions remain instantaneous. */
    if (!s_in_deferred_flush && !s_force_full) {
        int64_t now_us = esp_timer_get_time();
        int64_t elapsed_ms = (now_us - s_last_flush_us) / 1000;
        if (elapsed_ms < EDS3_DEBOUNCE_MS) {
            int64_t remaining_ms = EDS3_DEBOUNCE_MS - elapsed_ms;
            if (s_deferred_timer == nullptr) {
                esp_timer_create_args_t targs = {};
                targs.callback = deferred_flush_cb;
                targs.name     = "eds3_flush";
                if (esp_timer_create(&targs, &s_deferred_timer) != ESP_OK) {
                    s_deferred_timer = nullptr;
                }
            }
            if (s_deferred_timer != nullptr) {
                if (s_deferred_pending) {
                    /* Already pending -> let it fire on schedule and
                     * fold in the new dirty pixels. */
                } else {
                    if (esp_timer_start_once(s_deferred_timer,
                                             remaining_ms * 1000) == ESP_OK) {
                        s_deferred_pending = true;
                        return;
                    }
                }
                if (s_deferred_pending) return;
            }
            /* Fallback: timer creation failed; flush immediately. */
        }
    }

    /* Wait for any in-flight panel refresh to finish before queuing a
     * new one. M5GFX's Panel_EPD runs its waveform on a background
     * task; if we keep calling display() while it is mid-refresh we
     * stack up overlapping updates which the user perceives as
     * flicker. */
    s_gfx.waitDisplay();

    int dx0 = s_dx0, dy0 = s_dy0, dx1 = s_dx1, dy1 = s_dy1;

    /* Apply one-shot panel-refresh clip, if set. The framebuffer was
     * already updated over the full dirty bbox (LVGL pushed all the
     * pixels), but the caller knows that only pixels inside the clip
     * actually changed. Refresh just that intersection. */
    if (s_clip_set) {
        if (dx0 < s_clip_x0) dx0 = s_clip_x0;
        if (dy0 < s_clip_y0) dy0 = s_clip_y0;
        if (dx1 > s_clip_x1) dx1 = s_clip_x1;
        if (dy1 > s_clip_y1) dy1 = s_clip_y1;
        s_clip_set = false;
        if (dx1 < dx0 || dy1 < dy0) {
            /* Empty intersection -> nothing to refresh on the panel.
             * Still consume the dirty bbox so the next flush starts
             * clean. */
            clear_dirty();
            return;
        }
    }

    int dw  = dx1 - dx0 + 1;
    int dh  = dy1 - dy0 + 1;

    /* If the dirty area covers most of the screen, a full refresh is
     * cheaper (and cleaner) than a giant partial. */
    bool huge = ((long)dw * dh) * 4 > ((long)s_width * s_height) * 3;

    bool do_full = s_force_full || huge ||
                   s_partial_count >= EDS3_FULL_REFRESH_INTERVAL;

    if (do_full) {
        s_gfx.setEpdMode(epd_mode_t::epd_quality);
        /* Pass the explicit full-panel rect: M5GFX's
         * Panel_EPDiy::display() with no arguments early-outs when its
         * internal _range_mod is empty, which it always is right
         * after a flush -- so a Ctrl+R "force full refresh" issued
         * with nothing dirty in the framebuffer would do nothing at
         * all. Expanding _range_mod via an explicit rect makes
         * M5GFX actually drive the panel. */
        s_gfx.display(0, 0, s_width, s_height);
        s_partial_count = 0;
        s_force_full = false;
    } else {
        /* epd_fast is a single-pulse waveform: one drive pass per
         * dirty rectangle, no correction pass. The user sees a
         * single quick flash instead of the two-pass flicker of
         * epd_text. Ghosting accumulates a bit faster, which is why
         * we lowered the default full-refresh interval to compensate
         * (see Kconfig DRAFTLING_EPD_FULL_REFRESH_INTERVAL). */
        s_gfx.setEpdMode(epd_mode_t::epd_fast);
        s_gfx.display(dx0, dy0, dw, dh);
        s_partial_count++;
    }

    s_gfx.waitDisplay();
    clear_dirty();
    s_last_flush_us = esp_timer_get_time();
}

extern "C" void display_full_refresh(void)
{
    /* Force the next flush to be a full-screen quality refresh, even
     * if nothing is dirty (caller wants to clean up ghosting). Drop
     * any pending one-shot clip so the entire screen actually
     * refreshes. */
    s_clip_set = false;
    mark_dirty_rect(0, 0, s_width, s_height);
    s_force_full = true;
    display_flush();
}

extern "C" void display_set_partial_clip(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) {
        s_clip_set = false;
        return;
    }
    /* Caller passes *logical* coordinates; the dirty-bbox machinery
     * (and ultimately M5GFX::display) works in panel coordinates, so
     * scale the clip rect up before clamping/unioning. */
    int x0 = x * EDS3_SCALE;
    int y0 = y * EDS3_SCALE;
    int x1 = (x + w) * EDS3_SCALE - 1;
    int y1 = (y + h) * EDS3_SCALE - 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= s_width)  x1 = s_width  - 1;
    if (y1 >= s_height) y1 = s_height - 1;
    if (x1 < x0 || y1 < y0) {
        s_clip_set = false;
        return;
    }
    if (s_clip_set) {
        /* Union with the existing clip. Multiple key events drained
         * in one lv_timer_handler() pass each push their own clip
         * before the single batched flush runs; without this union
         * only the last event's region would be refreshed and the
         * earlier ones would stay stale on the panel. */
        if (x0 < s_clip_x0) s_clip_x0 = x0;
        if (y0 < s_clip_y0) s_clip_y0 = y0;
        if (x1 > s_clip_x1) s_clip_x1 = x1;
        if (y1 > s_clip_y1) s_clip_y1 = y1;
    } else {
        s_clip_x0 = x0; s_clip_y0 = y0;
        s_clip_x1 = x1; s_clip_y1 = y1;
        s_clip_set = true;
    }
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

extern "C" void display_set_backlight(int /*percent*/)
{
    /* E-paper has no backlight. No-op. */
}

extern "C" void display_sleep(void)
{
    /* E-paper retains its image without power. No-op (the standby
     * manager wipes the panel to white separately before deep sleep). */
}

extern "C" void display_wake(void)
{
    /* No-op (see display_sleep). */
}

extern "C" void display_deep_sleep_prepare(void)
{
    /* E-paper retains its image without power and has no
     * backlight. The standby manager wipes the panel to white via
     * the pre_sleep_cb before this call, which is the visible
     * "off" state on e-paper. No-op. */
}

extern "C" void display_set_shared_i2c_bus(void * /*bus_handle*/)
{
    /* PaperS3 (M5GFX) does not expose an I2C bus to share with
     * the touchscreen; the GT911 has its own bus. No-op. */
}

#endif /* CONFIG_DRAFTLING_DISPLAY_EDS3 */
