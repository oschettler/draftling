#include <cstdio>
#include <cstring>
#include "sdkconfig.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include "lvgl.h"

#include "lvgl_port.h"
#include "display.h"

static const char *TAG = "LvglPort";

#define LVGL_TICK_PERIOD_MS    5
#define LVGL_TASK_MAX_DELAY_MS 500
#define LVGL_TASK_MIN_DELAY_MS 50
#define BYTES_PER_PIXEL        2   /* LV_COLOR_FORMAT_RGB565 */

static SemaphoreHandle_t s_lvgl_mux = NULL;

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *color_map)
{
    int w = area->x2 - area->x1 + 1;
    int h = area->y2 - area->y1 + 1;

    /* Fast path: if the backend supports it, push the LVGL RGB565
     * framebuffer directly to the panel without going through our
     * per-pixel 1-bpp conversion. The PaperS3 (M5GFX) backend uses
     * this; RLCD and UC8179 return false and we fall back to the
     * legacy per-pixel path below. */
    if (display_push_rgb565(area->x1, area->y1, w, h, color_map)) {
        /* LVGL may slice a single dirty region into multiple
         * draw-buffer-sized chunks (in PARTIAL render mode) or invalidate
         * several disjoint regions in one cycle. Only trigger the panel
         * refresh on the final chunk so all pushes accumulate into a
         * single dirty bounding box and produce a single e-paper
         * refresh - otherwise the user sees the same screen update
         * twice (once for the cursor and once for the title bar) on
         * every keystroke. */
        if (lv_display_flush_is_last(disp)) {
            display_flush();
        }
        lv_disp_flush_ready(disp);
        return;
    }

    uint16_t *buf = (uint16_t *)color_map;
    for (int y = area->y1; y <= area->y2; y++) {
        for (int x = area->x1; x <= area->x2; x++) {
            uint8_t color = (*buf < 0x7FFF) ? 0x00 : 0xFF;
            display_set_pixel(x, y, color);
            buf++;
        }
    }
    if (lv_display_flush_is_last(disp)) {
        display_flush();
    }
    lv_disp_flush_ready(disp);
}

static void tick_cb(void *arg)
{
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static void lvgl_task(void *arg)
{
    uint32_t delay_ms = LVGL_TASK_MAX_DELAY_MS;
    for (;;) {
        if (lvgl_port_lock(-1)) {
            delay_ms = lv_timer_handler();
            lvgl_port_unlock();
        }
        if (delay_ms > LVGL_TASK_MAX_DELAY_MS) delay_ms = LVGL_TASK_MAX_DELAY_MS;
        if (delay_ms < LVGL_TASK_MIN_DELAY_MS) delay_ms = LVGL_TASK_MIN_DELAY_MS;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

extern "C" void lvgl_port_init(int width, int height, int rotate_deg)
{
    /* Recursive mutex so a task that already holds it can call
     * lvgl_port_lock() again without deadlocking. The "Sleep now"
     * menu path is the motivating case: it runs inside the LVGL
     * timer/key handler (mutex held) and then calls
     * standby_enter_sleep() -> pre_sleep_autosave(), which itself
     * takes lvgl_port_lock() to wipe the panel to white before deep
     * sleep. With a plain mutex the second take would block forever
     * and the screen would never get the white frame. */
    s_lvgl_mux = xSemaphoreCreateRecursiveMutex();
    lv_init();

    lv_display_t *disp = lv_display_create(width, height);
    /* LVGL v9 defaults to LV_COLOR_FORMAT_RGB888 (4 bytes per pixel).
     * Our flush path (and the PaperS3 fast path that hands the
     * framebuffer to M5GFX as rgb565_t*) expect RGB565, and
     * BYTES_PER_PIXEL below is sized for RGB565. Declare it
     * explicitly before allocating buffers; otherwise LVGL writes
     * 4-byte pixels into a half-sized buffer and the panel renders
     * mangled, ~4x-shrunk content. */
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, flush_cb);

    /* Apply display rotation */
    lv_display_rotation_t rot = LV_DISPLAY_ROTATION_0;
    switch (rotate_deg) {
    case 90:  rot = LV_DISPLAY_ROTATION_90;  break;
    case 180: rot = LV_DISPLAY_ROTATION_180; break;
    case 270: rot = LV_DISPLAY_ROTATION_270; break;
    default:  rot = LV_DISPLAY_ROTATION_0;   break;
    }
    lv_display_set_rotation(disp, rot);

#if defined(CONFIG_DRAFTLING_MODEL_M5STACK_PAPERS3)
    /* PaperS3 / M5GFX path: use PARTIAL render mode so flush_cb is
     * called once per invalidated rectangle with a tightly-packed
     * pixel buffer. This lets the display backend issue a partial
     * e-paper refresh of just the changed region (typically the
     * cursor or a single edited line) instead of repainting all
     * 540x960 pixels and running the full grayscale waveform on
     * every keystroke.
     *
     * A draw buffer sized to ~1/8 of the screen is enough to hold any
     * single LVGL invalidated region the editor produces (status bar,
     * a line of text, a dialog box) in one flush. */
    size_t buf_size = (size_t)width * height * BYTES_PER_PIXEL / 8;
    uint8_t *buf1 = (uint8_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    uint8_t *buf2 = (uint8_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    assert(buf1 && buf2);
    lv_display_set_buffers(disp, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
#else
    size_t buf_size = (size_t)width * height * BYTES_PER_PIXEL;
    uint8_t *buf1 = (uint8_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    uint8_t *buf2 = (uint8_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    assert(buf1 && buf2);
    lv_display_set_buffers(disp, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_FULL);
#endif

    /* Tick timer */
    esp_timer_create_args_t timer_args = {};
    timer_args.callback = tick_cb;
    timer_args.name     = "lvgl_tick";
    esp_timer_handle_t timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, LVGL_TICK_PERIOD_MS * 1000));

    xTaskCreatePinnedToCore(lvgl_task, "LVGL", 8 * 1024, NULL, 5, NULL, 0);
    ESP_LOGI(TAG, "LVGL port initialized (%dx%d, rotation=%d)", width, height, rotate_deg);
}

extern "C" bool lvgl_port_lock(int timeout_ms)
{
    TickType_t t = (timeout_ms == -1) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(s_lvgl_mux, t) == pdTRUE;
}

extern "C" void lvgl_port_unlock(void)
{
    xSemaphoreGiveRecursive(s_lvgl_mux);
}
