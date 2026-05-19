#include "sdkconfig.h"
#if defined(CONFIG_DRAFTLING_TOUCHSCREEN)

/*
 * AXS5106L touchscreen driver + LVGL pointer input device.
 *
 * The AXS5106L (Guition JC3248W535) speaks a small "magic packet"
 * I2C protocol: the host writes an 8-byte read-request packet and
 * reads back 8 bytes containing a touch count and a single touch
 * point. The protocol is shared across Allystar's AXS5106 / AXS5106L
 * variants and matches the public LovyanGFX / Tactility / Arduino
 * implementations widely cited for this controller.
 *
 * The driver also registers an LVGL pointer indev which feeds touch
 * coordinates to the LVGL event system, so widgets receive standard
 * click / press / gesture events. When an INT GPIO is provided, the
 * driver only polls I2C when INT is low -- the controller pulses
 * INT for each frame of active touch, so we avoid the bus traffic
 * (and the I2C-mutex contention with other devices on the same bus)
 * when no finger is down.
 *
 * The transform from the controller's native portrait coordinates to
 * LVGL logical coordinates is parameterised in touchscreen_config_t
 * (mirror_x/mirror_y/swap_xy + native/logical sizes), so the same
 * driver works on touch panels mounted in different orientations
 * relative to the LCD without code changes.
 */

#include <cstring>
#include <cstdio>
#include <esp_log.h>
#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "lvgl.h"

#include "touchscreen.h"

static const char *TAG = "Touch";

/* AXS5106L "read 1 touch point" magic packet. Bytes 0-3 are the
 * vendor preamble (0xB5, 0xAB, 0xA5, 0x5A); the trailing 0x08 is
 * the number of bytes to return. The controller responds with 8
 * bytes laid out as:
 *   [0]      gesture id (unused)
 *   [1]      touch points (0 or 1)
 *   [2]      event | x_high (high nibble of x in bits 0..3)
 *   [3]      x_low
 *   [4]      finger id | y_high
 *   [5]      y_low
 *   [6..7]   weight / area (ignored)
 */
static const uint8_t AXS5106_READ_CMD[8] = {
    0xB5, 0xAB, 0xA5, 0x5A, 0x00, 0x00, 0x00, 0x08
};

static bool s_initialized = false;
static touchscreen_config_t s_cfg;
static i2c_master_bus_handle_t s_bus = NULL;
static i2c_master_dev_handle_t s_dev = NULL;
static SemaphoreHandle_t s_mux = NULL;
static lv_indev_t *s_indev = NULL;

/* Latched "current touch" state, updated on every successful poll. */
static volatile bool s_pressed_latch = false;
static volatile int  s_x_latch = 0;
static volatile int  s_y_latch = 0;

static inline int clamp(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* Map a raw (native_x, native_y) reading to logical LVGL coordinates
 * using the configured orientation flags. */
static void native_to_logical(int nx, int ny, int *ox, int *oy)
{
    if (s_cfg.mirror_x) nx = (s_cfg.native_width  - 1) - nx;
    if (s_cfg.mirror_y) ny = (s_cfg.native_height - 1) - ny;

    int sx_native = s_cfg.native_width;
    int sy_native = s_cfg.native_height;
    int rx = nx;
    int ry = ny;
    if (s_cfg.swap_xy) {
        int t = rx; rx = ry; ry = t;
        t = sx_native; sx_native = sy_native; sy_native = t;
    }

    /* Scale native -> logical. Guard against zero. */
    if (sx_native <= 0) sx_native = 1;
    if (sy_native <= 0) sy_native = 1;
    int lx = (int)((int64_t)rx * s_cfg.logical_width  / sx_native);
    int ly = (int)((int64_t)ry * s_cfg.logical_height / sy_native);

    *ox = clamp(lx, 0, s_cfg.logical_width  - 1);
    *oy = clamp(ly, 0, s_cfg.logical_height - 1);
}

/* Issue a single read of the controller. Returns true if a touch is
 * currently active and fills *out_x / *out_y with logical coords. */
static bool poll_controller(int *out_x, int *out_y)
{
    if (!s_dev) return false;

    uint8_t resp[8] = { 0 };
    esp_err_t err = i2c_master_transmit_receive(
        s_dev,
        AXS5106_READ_CMD, sizeof(AXS5106_READ_CMD),
        resp, sizeof(resp),
        50 /* ms */);
    if (err != ESP_OK) {
        /* I2C transient -- silently drop the sample. Log at DEBUG so
         * we do not flood the console when no controller is wired. */
        ESP_LOGD(TAG, "i2c read failed: %s", esp_err_to_name(err));
        return false;
    }

    /* Some firmwares report the touch count in resp[1]; others put a
     * "frame valid" flag in resp[0]. Treat anything with the low
     * nibble of resp[1] in [1..5] as "finger down". */
    uint8_t points = resp[1] & 0x0F;
    if (points == 0 || points > 5) {
        return false;
    }

    int nx = ((int)(resp[2] & 0x0F) << 8) | resp[3];
    int ny = ((int)(resp[4] & 0x0F) << 8) | resp[5];

    int lx, ly;
    native_to_logical(nx, ny, &lx, &ly);
    if (out_x) *out_x = lx;
    if (out_y) *out_y = ly;
    return true;
}

/* INT line is active-low: poll the controller only when it is held
 * low (the controller pulses it for each touch frame). When no INT
 * GPIO is wired, fall back to polling on every read_cb invocation. */
static bool int_active(void)
{
    if (s_cfg.intr < 0) return true;
    return gpio_get_level((gpio_num_t)s_cfg.intr) == 0;
}

static void indev_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;

    int x = s_x_latch;
    int y = s_y_latch;
    bool pressed = false;

    if (int_active() && s_mux && xSemaphoreTake(s_mux, 0) == pdTRUE) {
        pressed = poll_controller(&x, &y);
        if (pressed) {
            s_x_latch = x;
            s_y_latch = y;
        }
        s_pressed_latch = pressed;
        xSemaphoreGive(s_mux);
    } else {
        /* INT high -- finger up. Honour the latch so we report the
         * release on the next call without another I2C read. */
        s_pressed_latch = false;
    }

    if (s_pressed_latch) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = s_x_latch;
        data->point.y = s_y_latch;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
        data->point.x = s_x_latch;
        data->point.y = s_y_latch;
    }
}

extern "C" void touchscreen_init(const touchscreen_config_t *cfg)
{
    if (s_initialized || !cfg) return;

    s_cfg = *cfg;
    s_mux = xSemaphoreCreateMutex();
    if (!s_mux) {
        ESP_LOGE(TAG, "mutex alloc failed");
        return;
    }

    /* Optional dedicated reset line. (On the JC3248W535 the touch
     * reset is tied to the LCD reset, so this is usually -1.) */
    if (s_cfg.rst >= 0) {
        gpio_config_t g = {};
        g.intr_type    = GPIO_INTR_DISABLE;
        g.mode         = GPIO_MODE_OUTPUT;
        g.pin_bit_mask = (1ULL << s_cfg.rst);
        gpio_config(&g);
        gpio_set_level((gpio_num_t)s_cfg.rst, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level((gpio_num_t)s_cfg.rst, 1);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    /* INT line as plain input with pull-up. We do *not* attach a
     * GPIO ISR -- LVGL's read_cb polls the level cheaply and it is
     * easier to share the pin with the standby manager's EXT0 wake
     * configuration when we are not contending for the GPIO IRQ. */
    if (s_cfg.intr >= 0) {
        gpio_config_t g = {};
        g.intr_type    = GPIO_INTR_DISABLE;
        g.mode         = GPIO_MODE_INPUT;
        g.pin_bit_mask = (1ULL << s_cfg.intr);
        g.pull_up_en   = GPIO_PULLUP_ENABLE;
        g.pull_down_en = GPIO_PULLDOWN_DISABLE;
        gpio_config(&g);
    }

    /* I2C master bus + device handle (ESP-IDF v5.x i2c_master API). */
    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port          = s_cfg.i2c_port;
    bus_cfg.sda_io_num        = (gpio_num_t)s_cfg.sda;
    bus_cfg.scl_io_num        = (gpio_num_t)s_cfg.scl;
    bus_cfg.clk_source        = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = true;

    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
        return;
    }

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address  = s_cfg.i2c_addr;
    dev_cfg.scl_speed_hz    = s_cfg.i2c_hz ? s_cfg.i2c_hz : 400000;

    err = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device failed: %s", esp_err_to_name(err));
        i2c_del_master_bus(s_bus);
        s_bus = NULL;
        return;
    }

    /* Register LVGL pointer indev. lv_init() was already called by
     * lvgl_port_init(); we just attach the new device to the default
     * display. */
    s_indev = lv_indev_create();
    if (s_indev) {
        lv_indev_set_type(s_indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(s_indev, indev_read_cb);
        lv_indev_set_display(s_indev, lv_display_get_default());
    } else {
        ESP_LOGW(TAG, "lv_indev_create failed (LVGL not initialized?)");
    }

    s_initialized = true;
    ESP_LOGI(TAG, "AXS5106L touchscreen initialized "
                  "(I2C addr=0x%02X, INT=%d, native=%dx%d, logical=%dx%d, "
                  "mirror_x=%d mirror_y=%d swap_xy=%d)",
             s_cfg.i2c_addr, s_cfg.intr,
             s_cfg.native_width, s_cfg.native_height,
             s_cfg.logical_width, s_cfg.logical_height,
             (int)s_cfg.mirror_x, (int)s_cfg.mirror_y, (int)s_cfg.swap_xy);
}

extern "C" bool touchscreen_is_initialized(void)
{
    return s_initialized;
}

extern "C" int touchscreen_get_int_gpio(void)
{
    if (!s_initialized) return -1;
    return s_cfg.intr;
}

extern "C" bool touchscreen_read(int *out_x, int *out_y)
{
    if (!s_initialized || !s_mux) return false;
    bool pressed = false;
    if (xSemaphoreTake(s_mux, pdMS_TO_TICKS(20)) == pdTRUE) {
        if (int_active()) {
            pressed = poll_controller(out_x ? out_x : NULL,
                                      out_y ? out_y : NULL);
            if (pressed) {
                if (out_x) s_x_latch = *out_x;
                if (out_y) s_y_latch = *out_y;
            }
            s_pressed_latch = pressed;
        } else {
            s_pressed_latch = false;
        }
        xSemaphoreGive(s_mux);
    }
    if (!pressed) {
        if (out_x) *out_x = s_x_latch;
        if (out_y) *out_y = s_y_latch;
    }
    return pressed;
}

extern "C" bool touchscreen_is_pressed(void)
{
    return s_pressed_latch;
}

#else /* !CONFIG_DRAFTLING_TOUCHSCREEN */

/* Provide weak stubs so callers can compile-and-link without
 * #ifdef'ing every call site. The header still hides the
 * configuration struct behind the same Kconfig guard, so these are
 * only reached via translation units that explicitly check for
 * touchscreen support; keeping them keeps the link graph simple. */

#include "touchscreen.h"

extern "C" void touchscreen_init(const touchscreen_config_t *cfg) { (void)cfg; }
extern "C" bool touchscreen_is_initialized(void) { return false; }
extern "C" int  touchscreen_get_int_gpio(void)    { return -1; }
extern "C" bool touchscreen_read(int *x, int *y)  { (void)x; (void)y; return false; }
extern "C" bool touchscreen_is_pressed(void)      { return false; }

#endif /* CONFIG_DRAFTLING_TOUCHSCREEN */
