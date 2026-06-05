#include "sdkconfig.h"
#if defined(CONFIG_DRAFTLING_TOUCHSCREEN)

/*
 * I2C touchscreen driver + LVGL pointer input device.
 *
 * Supports two controllers, picked at build time via
 * CONFIG_DRAFTLING_TOUCH_CONTROLLER:
 *
 *   * AXS5106L (Allystar, e.g. Guition JC3248W535):
 *     Speaks a small "magic packet" I2C protocol -- the host writes
 *     an 8-byte read-request packet and reads back 8 bytes
 *     containing a touch count and a single point. The protocol is
 *     shared across Allystar's AXS5106 / AXS5106L variants and
 *     matches the public LovyanGFX / Tactility / Arduino
 *     implementations widely cited for this controller.
 *
 *   * GT911 (Goodix, e.g. M5Stack PaperS3):
 *     Register-addressed protocol -- 16-bit register address sent
 *     MSB-first, status byte at 0x814E (bit 7 = buffer ready, low
 *     nibble = touch-point count), point data at 0x814F+ (8 bytes
 *     per point, X / Y little-endian). After reading, the host
 *     must clear 0x814E so the controller can fill the next frame.
 *     The GT911 listens on either 0x5D or 0x14 depending on the
 *     INT level seen during its internal power-on reset; we probe
 *     both before binding the I2C device. On boards where RST is
 *     not wired to an ESP32 GPIO (PaperS3) the address-select
 *     reset sequence cannot be performed, but probing covers it.
 *
 * In both cases the driver registers an LVGL pointer indev which
 * feeds touch coordinates to the LVGL event system, so widgets
 * receive standard click / press / gesture events. The transform
 * from the controller's native coordinate space to LVGL logical
 * coordinates is parameterised in touchscreen_config_t
 * (mirror_x / mirror_y / swap_xy + native / logical sizes), so the
 * same code works for panels mounted in different orientations
 * relative to the LCD.
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

static bool s_initialized = false;
static touchscreen_config_t s_cfg;
static i2c_master_bus_handle_t s_bus = NULL;
/* True if we created s_bus ourselves and must free it on (future)
 * deinit. False when the bus was supplied by the caller (shared
 * with another driver-NG consumer such as epdiy on the LilyGO T5
 * E-Paper S3 Pro). Mirrors epdiy's own `owns_bus` flag. */
static bool s_owns_bus = false;
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

    lx = clamp(lx, 0, s_cfg.logical_width  - 1);
    ly = clamp(ly, 0, s_cfg.logical_height - 1);

    /* Apply the user-requested display rotation so the reported
     * coordinates match the rotated UI seen on screen. */
    int rot = s_cfg.user_rotate_deg;
    if (rot < 0) rot = ((rot % 360) + 360) % 360;
    else        rot = rot % 360;
    switch (rot) {
    case 90:
        *ox = ly;
        *oy = (s_cfg.logical_width  - 1) - lx;
        break;
    case 180:
        *ox = (s_cfg.logical_width  - 1) - lx;
        *oy = (s_cfg.logical_height - 1) - ly;
        break;
    case 270:
        *ox = (s_cfg.logical_height - 1) - ly;
        *oy = lx;
        break;
    default:
        *ox = lx;
        *oy = ly;
        break;
    }
}

/* ---- AXS5106L (magic-packet) driver ---- */
#if defined(CONFIG_DRAFTLING_TOUCH_AXS5106L)

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

/* Issue a single read of the controller. Returns true if a touch is
 * currently active and fills *out_x / *out_y with logical coords. */
static bool poll_axs5106l(int *out_x, int *out_y)
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
     * "frame valid" flag in resp[0]. Accept either:
     *  - resp[1] in [1..5] (explicit point count), OR
     *  - resp[0] == 0 (no gesture) AND (x | y) != 0 (LilyGo's check
     *    in their official AXS15231B driver -- the controller does
     *    not always populate the point-count byte).
     * If none of the above, treat as "finger up". */
    uint8_t points  = resp[1] & 0x0F;
    uint8_t gesture = resp[0];

    int nx = ((int)(resp[2] & 0x0F) << 8) | resp[3];
    int ny = ((int)(resp[4] & 0x0F) << 8) | resp[5];

    bool valid = (points >= 1 && points <= 5) ||
                 (gesture == 0 && (nx != 0 || ny != 0));
    if (!valid) {
        return false;
    }

    int lx, ly;
    native_to_logical(nx, ny, &lx, &ly);
    if (out_x) *out_x = lx;
    if (out_y) *out_y = ly;
    return true;
}

#endif /* CONFIG_DRAFTLING_TOUCH_AXS5106L */

/* ---- GT911 (register-based) driver ---- */
#if defined(CONFIG_DRAFTLING_TOUCH_GT911)

/* GT911 register addresses (datasheet section 7). Sent over I2C as
 * two bytes, high-byte first. */
#define GT911_REG_STATUS    0x814E  /* status / buffer-ready / point count */
#define GT911_REG_POINT1    0x814F  /* first touch point (8 bytes) */
#define GT911_REG_COMMAND   0x8040  /* command register (0x05 = sleep) */
#define GT911_CMD_SLEEP     0x05    /* enter low-power sleep mode */

static esp_err_t gt911_read_reg(uint16_t reg, uint8_t *buf, size_t len)
{
    uint8_t addr[2] = { (uint8_t)((reg >> 8) & 0xFF),
                        (uint8_t)(reg & 0xFF) };
    return i2c_master_transmit_receive(
        s_dev, addr, sizeof(addr), buf, len, 50 /* ms */);
}

static esp_err_t gt911_write_reg(uint16_t reg, uint8_t val)
{
    uint8_t packet[3] = { (uint8_t)((reg >> 8) & 0xFF),
                          (uint8_t)(reg & 0xFF),
                          val };
    return i2c_master_transmit(s_dev, packet, sizeof(packet), 50 /* ms */);
}

static bool poll_gt911(int *out_x, int *out_y)
{
    if (!s_dev) return false;

    uint8_t status = 0;
    if (gt911_read_reg(GT911_REG_STATUS, &status, 1) != ESP_OK) {
        ESP_LOGD(TAG, "gt911 status read failed");
        return false;
    }

    /* Buffer-ready bit (0x80) not set -> no new frame. The controller
     * sets this bit when it has populated 0x814F+ with fresh point
     * data; we must clear it after reading so the next frame can be
     * written. Until then there is no usable data to fetch. */
    if ((status & 0x80) == 0) {
        /* Nothing to clear -- the controller has not flagged a new
         * frame, so writing 0 is a no-op but also wastes an I2C
         * transaction. Just return. */
        return false;
    }

    bool pressed = false;
    uint8_t cnt = status & 0x0F;
    if (cnt >= 1 && cnt <= 5) {
        /* Read only the first point (index 0). Each point is 8 bytes:
         *   [0]    track id
         *   [1..2] x (little-endian)
         *   [3..4] y (little-endian)
         *   [5..6] strength (LE, unused)
         *   [7]    reserved
         */
        uint8_t pt[8] = { 0 };
        if (gt911_read_reg(GT911_REG_POINT1, pt, sizeof(pt)) == ESP_OK) {
            int nx = ((int)pt[2] << 8) | pt[1];
            int ny = ((int)pt[4] << 8) | pt[3];
            int lx, ly;
            native_to_logical(nx, ny, &lx, &ly);
            if (out_x) *out_x = lx;
            if (out_y) *out_y = ly;
            pressed = true;
        } else {
            ESP_LOGD(TAG, "gt911 point read failed");
        }
    }

    /* Always clear the buffer-ready flag so the controller can fill
     * the next frame -- even if we decided the frame was invalid. */
    gt911_write_reg(GT911_REG_STATUS, 0x00);
    return pressed;
}

#endif /* CONFIG_DRAFTLING_TOUCH_GT911 */

/* Dispatch to the controller-specific poll. */
static bool poll_controller(int *out_x, int *out_y)
{
#if defined(CONFIG_DRAFTLING_TOUCH_GT911)
    return poll_gt911(out_x, out_y);
#elif defined(CONFIG_DRAFTLING_TOUCH_AXS5106L)
    return poll_axs5106l(out_x, out_y);
#else
#  error "No touch controller driver selected (CONFIG_DRAFTLING_TOUCH_*)"
#endif
}

/* INT is a brief per-frame pulse on the AXS15231B (not held low
 * while a finger is down), so we cannot use it to gate the I2C
 * poll: by the time LVGL's read_cb runs the pulse is long gone.
 * We therefore poll on every read_cb invocation and rely on the
 * controller's response (gesture / point-count / zero coords) to
 * decide whether a finger is currently down. The INT GPIO is still
 * used by the standby manager as the EXT0 deep-sleep wake source. */

static void indev_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;

    int x = s_x_latch;
    int y = s_y_latch;
    bool pressed = false;

    if (s_mux && xSemaphoreTake(s_mux, 0) == pdTRUE) {
        pressed = poll_controller(&x, &y);
        if (pressed) {
            s_x_latch = x;
            s_y_latch = y;
        }
        s_pressed_latch = pressed;
        xSemaphoreGive(s_mux);
    } else {
        /* Mutex contended -- report release for this frame; LVGL will
         * call us again next tick. */
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

    /* I2C master bus + device handle (ESP-IDF v5.x i2c_master API).
     *
     * If the caller passed an existing driver-NG bus handle via
     * s_cfg.i2c_bus we adopt it instead of creating our own. This
     * is the only way to coexist with another driver-NG consumer
     * on the same physical I2C port (currently used by the LilyGO
     * T5 E-Paper S3 Pro / Pro Lite, where vroland/epdiy owns the
     * on-board I2C bus shared with the GT911). */
    if (s_cfg.i2c_bus) {
        s_bus      = (i2c_master_bus_handle_t)s_cfg.i2c_bus;
        s_owns_bus = false;
    } else {
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
        s_owns_bus = true;
    }

#if defined(CONFIG_DRAFTLING_TOUCH_GT911)
    /* GT911 selects its I2C address (0x5D or 0x14) based on the INT
     * level seen during its internal power-on reset. On boards where
     * the GT911 RST line is not wired to an ESP32-S3 GPIO (M5Stack
     * PaperS3) we cannot drive the address-select reset sequence,
     * so probe both possible addresses and stick with whichever one
     * acknowledges. The configured value (s_cfg.i2c_addr, typically
     * 0x5D) is tried first; the alternate is the fallback. */
    {
        uint8_t primary = s_cfg.i2c_addr;
        uint8_t alt     = (primary == 0x5D) ? 0x14 :
                          (primary == 0x14) ? 0x5D : primary;
        if (i2c_master_probe(s_bus, primary, 100) != ESP_OK &&
            alt != primary &&
            i2c_master_probe(s_bus, alt, 100) == ESP_OK) {
            ESP_LOGI(TAG, "GT911 responded at 0x%02X (configured 0x%02X)",
                     alt, primary);
            s_cfg.i2c_addr = alt;
        }
    }
#endif

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address  = s_cfg.i2c_addr;
    dev_cfg.scl_speed_hz    = s_cfg.i2c_hz ? s_cfg.i2c_hz : 400000;

    esp_err_t err = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device failed: %s", esp_err_to_name(err));
        /* Only tear down the bus if we created it ourselves --
         * a caller-supplied shared bus is still owned (and used)
         * by the other consumer. */
        if (s_owns_bus) {
            i2c_del_master_bus(s_bus);
        }
        s_bus      = NULL;
        s_owns_bus = false;
        return;
    }

    /* Register LVGL pointer indev. lv_init() was already called by
     * draftling_lvgl_port_init(); we just attach the new device to the default
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
#if defined(CONFIG_DRAFTLING_TOUCH_GT911)
    const char *ctrl = "GT911";
#elif defined(CONFIG_DRAFTLING_TOUCH_AXS5106L)
    const char *ctrl = "AXS5106L";
#else
    const char *ctrl = "?";
#endif
    ESP_LOGI(TAG, "%s touchscreen initialized "
                  "(I2C addr=0x%02X, INT=%d, native=%dx%d, logical=%dx%d, "
                  "mirror_x=%d mirror_y=%d swap_xy=%d)",
             ctrl,
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
        pressed = poll_controller(out_x ? out_x : NULL,
                                  out_y ? out_y : NULL);
        if (pressed) {
            if (out_x) s_x_latch = *out_x;
            if (out_y) s_y_latch = *out_y;
        }
        s_pressed_latch = pressed;
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

extern "C" void touchscreen_sleep(void)
{
    if (!s_initialized || !s_dev) return;
#if defined(CONFIG_DRAFTLING_TOUCH_GT911)
    /* GT911 datasheet, section 6 (command register 0x8040):
     * writing 0x05 puts the controller into sleep mode (typical
     * standby current < 10 µA). It wakes when the INT pin is
     * driven high by the host, or on the next reset. We hold the
     * mutex so we don't race a concurrent poll_controller(). */
    if (s_mux && xSemaphoreTake(s_mux, pdMS_TO_TICKS(50)) == pdTRUE) {
        esp_err_t err = gt911_write_reg(GT911_REG_COMMAND, GT911_CMD_SLEEP);
        xSemaphoreGive(s_mux);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "GT911 entered sleep mode");
        } else {
            ESP_LOGW(TAG, "GT911 sleep cmd failed: %s",
                     esp_err_to_name(err));
        }
    }
#else
    /* AXS5106L has no documented sleep command in the public
     * register map; it idles to ~100 µA on its own once polling
     * stops, which is acceptable on the Tab5 (separate rail). */
#endif
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
extern "C" void touchscreen_sleep(void)           { }

#endif /* CONFIG_DRAFTLING_TOUCHSCREEN */
