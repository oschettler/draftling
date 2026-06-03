#include "sdkconfig.h"
#if defined(CONFIG_DRAFTLING_DISPLAY_EPDIY)

/*
 * LilyGO T5 E-Paper S3 Pro / Pro Lite e-paper display driver
 * (thin shim over vroland/epdiy).
 *
 * Hardware
 * --------
 * Both Pro and Pro Lite carry the same 4.7" 960x540 ED047TC1 e-paper
 * panel (landscape), driven by an 8-bit parallel data bus on direct
 * ESP32-S3 GPIOs plus a TPS65185 high-voltage supply commanded over
 * I2C through a PCA9535PW I/O expander. All those pins are owned by
 * the epdiy library's `epd_board_v7` configuration -- the matching
 * board id documented in vroland/epdiy's README "LilyGo Boards"
 * table for the "LilyGo T5 S3 E-Paper Pro".
 *
 * Architecture
 * ------------
 * epdiy's high-level API (`epd_hl_*`) keeps a 4-bpp grayscale
 * framebuffer (one nibble per panel pixel, ~253 KB at 960x540) in
 * PSRAM. We expose the optional display_push_rgb565() fast path that
 * lvgl_port.cpp uses on color/M5GFX backends and convert the LVGL
 * RGB565 chunks straight into that grayscale framebuffer, scaling
 * each logical LVGL pixel into a SCALE x SCALE block of panel
 * pixels (CONFIG_DRAFTLING_DISPLAY_SCALE = 2 by default on this
 * board, matching the M5Stack PaperS3 backend). Pushes are batched
 * into a dirty bounding box; display_flush() turns on the panel
 * power rail, runs an `epd_hl_update_area` partial update over the
 * bbox, then powers the rail back off. Every
 * CONFIG_DRAFTLING_EPD_FULL_REFRESH_INTERVAL partial updates we
 * promote the next refresh to a full-screen `MODE_GC16` flashing
 * update to clear residual ghosting.
 *
 * Deep sleep
 * ----------
 * Per the epdiy README and the documented issue #14 ("battery drain
 * after deep sleep"), epd_poweroff() alone is NOT enough; the I2C
 * bus and the LCD-peripheral ISR must also be released with
 * epd_deinit() before esp_deep_sleep_start(). standby_enter_sleep
 * calls display_deep_sleep_prepare() right before the actual sleep
 * call, so this backend implements that as poweroff + deinit. The
 * panel keeps the previous image displayed without power.
 *
 * VCOM
 * ----
 * Each individual ED047TC1 panel has a slightly different optimal
 * VCOM voltage; epdiy's lilygo_board_s3.c hard-codes 1600 mV as the
 * default and the factory firmware exposes a calibration setting in
 * NVS. We use the same 1600 mV default; visible "fading" or uneven
 * grays during partial updates would indicate the value needs to be
 * tuned per board (e.g. via a future Draftling settings entry).
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

#include "epdiy.h"
#include "epd_init_config.h"
#include <driver/i2c_master.h>

#include "display.h"

static const char *TAG = "DisplayEPDIY";

/* Caller-supplied I2C master bus handle (created in main.cpp before
 * display_init), so epdiy and the GT911 touch driver can share the
 * single on-board I2C port. NULL means "let epdiy create its own
 * bus internally" -- matches epdiy's default behaviour and the
 * pre-shared-bus code path. */
static i2c_master_bus_handle_t s_shared_i2c_bus = NULL;

/* High-level state object owned by epdiy (front+back 4-bpp
 * framebuffers, allocated by epd_hl_init). */
static EpdiyHighlevelState s_hl;
static uint8_t            *s_fb     = nullptr;  /* shortcut: epd_hl_get_framebuffer(&s_hl) */
static int                 s_width  = 0;        /* panel pixels */
static int                 s_height = 0;        /* panel pixels */
static bool                s_initialized = false;

/* Logical-to-panel scale (every logical LVGL pixel is rendered as a
 * SCALE x SCALE block of panel pixels). */
#ifdef CONFIG_DRAFTLING_DISPLAY_SCALE
#define EPDIY_SCALE CONFIG_DRAFTLING_DISPLAY_SCALE
#else
#define EPDIY_SCALE 1
#endif

#ifdef CONFIG_DRAFTLING_EPD_FULL_REFRESH_INTERVAL
#define EPDIY_FULL_REFRESH_INTERVAL CONFIG_DRAFTLING_EPD_FULL_REFRESH_INTERVAL
#else
#define EPDIY_FULL_REFRESH_INTERVAL 30
#endif

/* Dirty bounding box accumulated since the last display_flush() (in
 * panel pixels). */
static int  s_dx0 = 0, s_dy0 = 0, s_dx1 = -1, s_dy1 = -1;
static bool s_force_full = true;
static int  s_partial_count = 0;

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

/* Write a single panel pixel into the 4-bpp framebuffer. Pixels are
 * packed two-per-byte: low nibble = even x, high nibble = odd x
 * (epdiy convention, see vroland/epdiy:src/output_common/render.c
 * pixel layout). color is the 4-bit gray level (0x0 = black,
 * 0xF = white) in the low nibble of the passed byte. */
static inline void put_panel_pixel(int x, int y, uint8_t gray4)
{
    if ((unsigned)x >= (unsigned)s_width)  return;
    if ((unsigned)y >= (unsigned)s_height) return;
    size_t idx = (size_t)y * (size_t)(s_width / 2) + (size_t)(x >> 1);
    uint8_t b = s_fb[idx];
    if (x & 1) {
        b = (uint8_t)((b & 0x0F) | ((gray4 & 0x0F) << 4));
    } else {
        b = (uint8_t)((b & 0xF0) | (gray4 & 0x0F));
    }
    s_fb[idx] = b;
}

/* Fill a (panel-pixel) rectangle in the 4-bpp framebuffer with a
 * solid gray. Equivalent to per-pixel put_panel_pixel but unrolled
 * for the common case of contiguous runs. */
static void fill_panel_rect(int x, int y, int w, int h, uint8_t gray4)
{
    if (w <= 0 || h <= 0) return;
    int x0 = (x < 0) ? 0 : x;
    int y0 = (y < 0) ? 0 : y;
    int x1 = x + w; if (x1 > s_width)  x1 = s_width;
    int y1 = y + h; if (y1 > s_height) y1 = s_height;
    if (x1 <= x0 || y1 <= y0) return;
    uint8_t packed = (uint8_t)(((gray4 & 0x0F) << 4) | (gray4 & 0x0F));
    int row_bytes = s_width / 2;
    for (int py = y0; py < y1; py++) {
        uint8_t *row = s_fb + (size_t)py * row_bytes;
        int px = x0;
        /* Leading odd byte (high nibble only). */
        if (px & 1) {
            uint8_t &b = row[px >> 1];
            b = (uint8_t)((b & 0x0F) | ((gray4 & 0x0F) << 4));
            px++;
        }
        /* Whole bytes. */
        int end_aligned = x1 & ~1;
        if (end_aligned > px) {
            memset(row + (px >> 1), packed, (size_t)((end_aligned - px) >> 1));
            px = end_aligned;
        }
        /* Trailing pixel (low nibble only). */
        if (px < x1) {
            uint8_t &b = row[px >> 1];
            b = (uint8_t)((b & 0xF0) | (gray4 & 0x0F));
        }
    }
}

/* Threshold an RGB565 word to a 4-bit gray level. We render mostly
 * black-on-white (or white-on-black) UI so a binary threshold on
 * luminance is adequate; intermediate gray values produced by LVGL's
 * font antialiasing collapse to the nearest of pure black / pure
 * white, matching the M5GFX backend's effective behaviour after its
 * own dithering passes. */
static inline uint8_t rgb565_to_gray4(uint16_t v)
{
    /* Quick luma approximation: take the green channel as a proxy.
     * 6-bit green > 31 means "bright" -> white. */
    return ((v >> 5) & 0x3F) >= 32 ? 0x0F : 0x00;
}

/* ---- public API ---- */

extern "C" void display_set_shared_i2c_bus(void *bus_handle)
{
    /* Called by main.cpp before display_init() on epdiy boards. We
     * stash the handle; display_init() reads it and routes epdiy
     * through epd_init_with_config(). Idempotent and NULL-safe. */
    s_shared_i2c_bus = (i2c_master_bus_handle_t)bus_handle;
}

extern "C" void display_init(int /*pin_a*/, int /*pin_b*/, int /*pin_c*/,
                             int /*pin_d*/, int /*pin_e*/, int /*pin_f*/,
                             int width, int height)
{
    if (s_initialized) return;

    /* Bring up the EPD board + display. epd_board_v7 + ED047TC1 is
     * the documented combination for the LilyGO T5 E-Paper S3 Pro
     * (see vroland/epdiy README "LilyGo Boards" table and the
     * factory firmware Xinyuan-LilyGO/T5S3-4.7-e-paper-PRO
     * examples/factory/main/main.cpp `#define DEMO_BOARD epd_board_v7`).
     */
    /* LUT size: on ESP32-S3 epdiy uses RENDER_METHOD_LCD with the
     * optimized PIE vector lookup, which only ever touches the first
     * 1 KB of the conversion LUT (epdiy logs "only 1k of 65536 LUT
     * in use!" with EPD_LUT_64K). The 64K LUT is allocated from
     * MALLOC_CAP_INTERNAL and permanently reserves 64 KB of internal
     * DRAM (src/render.c init_lut_table), which on this board is
     * scarce -- once Bluedroid, LVGL, and the epdiy framebuffers are
     * up, the remaining internal heap falls below the threshold
     * esp_wifi_init() needs for its DMA-only static RX buffers and
     * WiFi connect fails with ESP_ERR_NO_MEM ("Expected to init 4
     * rx buffer, actual is 0"). EPD_LUT_1K matches what the S3
     * vector path actually uses and frees the other 63 KB for WiFi. */

    /* If main.cpp created the shared I2C bus for us, hand it to
     * epdiy via the post-PR-#475 entry point so epdiy adds its
     * TPS65185 / PCA9535 devices to that bus instead of creating
     * its own. Without this the touchscreen component (which also
     * uses driver-NG) cannot coexist on the same physical bus.
     * `epd_init_with_config` supersedes `epd_init` and is called
     * INSTEAD of it; we only reach this branch when a shared bus
     * was published before display_init(). */
    if (s_shared_i2c_bus) {
        EpdI2cConfig  i2c_cfg = {};
        i2c_cfg.bus_handle    = s_shared_i2c_bus;
        EpdInitConfig cfg     = {};
        cfg.i2c               = &i2c_cfg;
        epd_init_with_config(&epd_board_v7, &ED047TC1, EPD_LUT_1K, &cfg);
    } else {
        epd_init(&epd_board_v7, &ED047TC1, EPD_LUT_1K);
    }

    /* Per-panel VCOM calibration would ideally come from NVS. 1600
     * mV is the epdiy default and the value baked into
     * lilygo_board_s3.c; symptoms of wrong VCOM are fading partials
     * and uneven grays. */
    epd_set_vcom(1600);

    /* Allocate front+back 4-bpp grayscale framebuffers in PSRAM. */
    s_hl = epd_hl_init(EPD_BUILTIN_WAVEFORM);
    s_fb = epd_hl_get_framebuffer(&s_hl);

    /* Present the panel as 960x540 landscape (USB-C on the right
     * matches EPD_ROT_LANDSCAPE for this board family). */
    epd_set_rotation(EPD_ROT_LANDSCAPE);

    s_width  = epd_rotated_display_width();
    s_height = epd_rotated_display_height();
    if (s_width != width || s_height != height) {
        ESP_LOGW(TAG,
                 "Configured framebuffer size %dx%d does not match epdiy "
                 "panel size %dx%d; using panel size. Update "
                 "CONFIG_DRAFTLING_DISPLAY_WIDTH/HEIGHT (delete sdkconfig "
                 "and rebuild to pick up the new defaults).",
                 width, height, s_width, s_height);
    }

    /* Start with a clean white panel. */
    fill_panel_rect(0, 0, s_width, s_height, 0x0F);
    epd_poweron();
    epd_clear();
    epd_hl_update_screen(&s_hl, MODE_GC16, 25);
    epd_poweroff();

    clear_dirty();
    s_force_full     = true;
    s_partial_count  = 0;
    s_initialized    = true;

    ESP_LOGI(TAG, "T5 E-Paper S3 Pro display initialized via epdiy "
                  "(%dx%d panel, scale=%d -> %dx%d logical), full refresh "
                  "every %d partials",
             s_width, s_height, EPDIY_SCALE,
             s_width / EPDIY_SCALE, s_height / EPDIY_SCALE,
             EPDIY_FULL_REFRESH_INTERVAL);
}

extern "C" void display_clear(uint8_t color)
{
    if (!s_initialized) return;
    uint8_t g = (color != 0) ? 0x0F : 0x00;
    fill_panel_rect(0, 0, s_width, s_height, g);
    mark_dirty_rect(0, 0, s_width, s_height);
    s_force_full = true;
}

extern "C" void display_set_pixel(uint16_t x, uint16_t y, uint8_t color)
{
    if (!s_initialized) return;
    /* Coordinates are *logical* pixels -- expand to a SCALE x SCALE
     * block of panel pixels. */
    int px = (int)x * EPDIY_SCALE;
    int py = (int)y * EPDIY_SCALE;
    fill_panel_rect(px, py, EPDIY_SCALE, EPDIY_SCALE,
                    (color != 0) ? 0x0F : 0x00);
    mark_dirty_rect(px, py, EPDIY_SCALE, EPDIY_SCALE);
}

extern "C" bool display_push_rgb565(int x, int y, int w, int h,
                                    const void *color_map)
{
    if (!s_initialized || color_map == nullptr) return false;
    if (w <= 0 || h <= 0) return false;
    const uint16_t *src = (const uint16_t *)color_map;

    /* Per-logical-pixel: write a SCALE x SCALE block into the
     * grayscale framebuffer. */
    for (int sy = 0; sy < h; sy++) {
        for (int sx = 0; sx < w; sx++) {
            uint16_t v = src[(size_t)sy * w + sx];
            uint8_t g = rgb565_to_gray4(v);
            int px = (x + sx) * EPDIY_SCALE;
            int py = (y + sy) * EPDIY_SCALE;
            fill_panel_rect(px, py, EPDIY_SCALE, EPDIY_SCALE, g);
        }
    }

    int dx = x * EPDIY_SCALE;
    int dy = y * EPDIY_SCALE;
    int dw = w * EPDIY_SCALE;
    int dh = h * EPDIY_SCALE;
    mark_dirty_rect(dx, dy, dw, dh);
    return true;
}

extern "C" void display_flush(void)
{
    if (!s_initialized) return;
    if (s_dx1 < s_dx0 || s_dy1 < s_dy0) return;

    int dx0 = s_dx0, dy0 = s_dy0, dx1 = s_dx1, dy1 = s_dy1;
    int dw  = dx1 - dx0 + 1;
    int dh  = dy1 - dy0 + 1;

    /* If the dirty area covers most of the screen, a full refresh is
     * cheaper (and cleaner) than a giant partial. */
    bool huge = ((long)dw * dh) * 4 > ((long)s_width * s_height) * 3;

    /* Under rapid back-to-back flushes (e.g. typing at the top of a
     * long document, which reflows nearly every line), epdiy's LCD
     * render path on the ESP32-S3 has been observed to wedge both
     * `epd_prep` feeder tasks busy-spinning at top priority on both
     * cores, starving IDLE0 and tripping the task watchdog with no
     * recovery. The trigger is large MODE_GL16 partials issued
     * back-to-back before the EPD rail has fully settled. Promote
     * near-full partials to a MODE_GC16 full refresh whenever the
     * previous flush finished less than `FAST_SCROLL_GAP_MS` ago:
     * one ~430 ms full refresh is more predictable than a sequence
     * of ~430 ms 60-90% partials, and the flash is barely visible
     * during fast scrolling because the content is changing anyway.
     * Threshold of 40% of screen area is well below the 75% `huge`
     * cutoff above, so steady-state small partials (cursor blink,
     * status bar) are unaffected. */
    static const int     FAST_SCROLL_GAP_MS       = 800;
    static const long    FAST_SCROLL_AREA_NUM     = 2;   /* >= 2/5 ... */
    static const long    FAST_SCROLL_AREA_DEN     = 5;   /* ... of screen */
    static int64_t       s_last_flush_us          = 0;
    int64_t now_us = esp_timer_get_time();
    bool fast_scroll =
        (s_last_flush_us != 0) &&
        ((now_us - s_last_flush_us) < (int64_t)FAST_SCROLL_GAP_MS * 1000) &&
        (((long)dw * dh) * FAST_SCROLL_AREA_DEN >
         ((long)s_width * s_height) * FAST_SCROLL_AREA_NUM);

    bool do_full = s_force_full || huge || fast_scroll ||
                   s_partial_count >= EPDIY_FULL_REFRESH_INTERVAL;

    epd_poweron();
    if (do_full) {
        /* MODE_GC16: full-screen 16-gray flashing update, clears any
         * residual ghosting. */
        epd_hl_update_screen(&s_hl, MODE_GC16, 25);
        s_partial_count = 0;
        s_force_full = false;
    } else {
        /* MODE_GL16: 16-gray non-flashing partial update. Good for
         * UI changes where the user should not see a black flash. */
        EpdRect r = { .x = dx0, .y = dy0, .width = dw, .height = dh };
        epd_hl_update_area(&s_hl, MODE_GL16, 25, r);
        s_partial_count++;
    }
    epd_poweroff();

    s_last_flush_us = esp_timer_get_time();
    clear_dirty();
}

extern "C" void display_full_refresh(void)
{
    if (!s_initialized) return;
    mark_dirty_rect(0, 0, s_width, s_height);
    s_force_full = true;
    display_flush();
}

extern "C" void display_set_partial_clip(int /*x*/, int /*y*/,
                                         int /*w*/, int /*h*/)
{
    /* No-op on this backend; the dirty bbox already drives the
     * refresh region. The clip hint is an optimisation specific to
     * the M5GFX backend's debounce machinery. */
}

extern "C" uint8_t *display_get_buffer(void)
{
    /* No external 1-bpp framebuffer; epdiy owns the 4-bpp one. */
    return nullptr;
}

extern "C" int display_get_buffer_size(void)
{
    return 0;
}

extern "C" void display_set_backlight(int /*percent*/)
{
    /* E-paper has no controllable backlight. */
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
    if (!s_initialized) return;
    /* epdiy README + issue #14: epd_poweroff() alone leaks battery
     * after deep sleep -- the I2C bus and LCD-peripheral ISR must
     * also be released via epd_deinit() before esp_deep_sleep_start.
     * The wake from deep sleep is a hard reset that re-runs
     * display_init() from scratch, so it is safe to fully release
     * here. */
    epd_poweroff();
    epd_deinit();
    s_initialized = false;
}

#endif /* CONFIG_DRAFTLING_DISPLAY_EPDIY */
