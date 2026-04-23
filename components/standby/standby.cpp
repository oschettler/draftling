/*
 * Standby / deep-sleep manager.
 *
 * Tracks user inactivity via an esp_timer.  When the configured
 * timeout expires the board enters deep sleep (or a light-sleep
 * equivalent followed by esp_restart() on boards whose wake pin is
 * not RTC-capable).
 *
 *  - Waveshare ESP32-S3-RLCD-4.2: wakes via EXT0 on GPIO18 (active-low).
 *    The RLCD is reflective, so screen content is retained visually.
 *  - Seeed reTerminal E1001: wakes via EXT0 on GPIO3 (KEY0, the right
 *    green button, active-low).  The e-paper retains its image while
 *    powered down.
 *  - Waveshare E-Paper Driver HAT: wakes via EXT0 on the GPIO selected
 *    by CONFIG_DRAFTLING_HAT_WAKEUP_GPIO (default GPIO0 / BOOT button).
 *  - M5Stack PaperS3: wakes on a touch event via GPIO48 (the GT911
 *    touch-panel INT line, active-low). GPIO48 is *not* an RTC GPIO
 *    on the ESP32-S3, so EXT0 deep-sleep wake is not available;
 *    instead we use light sleep with gpio_wakeup_enable() and follow
 *    it with esp_restart() so callers see the same cold-boot
 *    semantics as on the other boards. (The previous code used GPIO21
 *    here -- the on-board buzzer pin -- which both has the wrong
 *    function and was floating low under some speaker-driver states,
 *    causing the device to wake instantly after every sleep.)
 *
 * The wake-up GPIO comes from app_config.h's WAKEUP_GPIO_NUM macro,
 * which is set per board (and may be Kconfig-driven for the HAT).
 *
 * The editor state lives in PSRAM/heap so it is lost on wake;
 * callers should auto-save before sleep.  The timeout value is persisted
 * in NVS.
 */

#include <cstring>
#include <cinttypes>
#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "sdkconfig.h"

#include "standby.h"

static const char *TAG = "Standby";

/* NVS namespace and key for the timeout value */
#define NVS_NAMESPACE  "standby"
#define NVS_KEY_TOUT   "timeout"

#if defined(CONFIG_DRAFTLING_MODEL_WAVESHARE_RLCD42)
/* GPIO used as EXT0 wake-up source (active-low) -- matches app_config.h */
#define WAKEUP_GPIO    ((gpio_num_t)18)
#elif defined(CONFIG_DRAFTLING_MODEL_SEEED_RETERMINAL_E1001)
/* KEY0 / right green button (active-low) -- matches app_config.h */
#define WAKEUP_GPIO    ((gpio_num_t)3)
#elif defined(CONFIG_DRAFTLING_MODEL_WAVESHARE_EPD_HAT)
/* HAT model: pin selected by Kconfig (default GPIO0 / BOOT button) */
#define WAKEUP_GPIO    ((gpio_num_t)CONFIG_DRAFTLING_HAT_WAKEUP_GPIO)
#elif defined(CONFIG_DRAFTLING_MODEL_M5STACK_PAPERS3)
/* PaperS3 GT911 touch INT (active-low). Not RTC-capable -> light
 * sleep + esp_restart() (see standby_enter_sleep). */
#define WAKEUP_GPIO    ((gpio_num_t)48)
#endif

static uint32_t s_timeout_sec = STANDBY_DEFAULT_TIMEOUT_SEC;
static esp_timer_handle_t s_timer = NULL;
static standby_pre_sleep_cb_t s_pre_sleep_cb = NULL;

/* ---- timer callback ---- */

static void inactivity_cb(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Inactivity timeout reached -- entering deep sleep");
    standby_enter_sleep();
}

/* ---- helpers ---- */

static void start_timer(void)
{
    if (!s_timer || s_timeout_sec == 0) return;
    /* (Re)start one-shot timer */
    esp_timer_stop(s_timer);   /* ignore error if not running */
    esp_timer_start_once(s_timer, (uint64_t)s_timeout_sec * 1000000ULL);
}

static void stop_timer(void)
{
    if (s_timer) esp_timer_stop(s_timer);
}

/* ---- public API ---- */

extern "C" void standby_init(void)
{
    /* Load timeout from NVS (default if not found) */
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        uint32_t val = 0;
        if (nvs_get_u32(h, NVS_KEY_TOUT, &val) == ESP_OK) {
            s_timeout_sec = val;
        }
        nvs_close(h);
    }

    /* Create the one-shot inactivity timer */
    esp_timer_create_args_t args = {};
    args.callback = inactivity_cb;
    args.name = "standby";
    ESP_ERROR_CHECK(esp_timer_create(&args, &s_timer));

    /* Start the timer if timeout is non-zero */
    if (s_timeout_sec > 0) {
        start_timer();
    }

    ESP_LOGI(TAG, "Standby initialized, timeout=%" PRIu32 " s", s_timeout_sec);
}

extern "C" void standby_reset_timer(void)
{
    if (s_timeout_sec > 0) {
        start_timer();
    }
}

extern "C" uint32_t standby_get_timeout(void)
{
    return s_timeout_sec;
}

extern "C" void standby_set_timeout(uint32_t seconds)
{
    s_timeout_sec = seconds;

    /* Persist to NVS */
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, NVS_KEY_TOUT, seconds);
        nvs_commit(h);
        nvs_close(h);
    }

    if (seconds == 0) {
        stop_timer();
    } else {
        start_timer();
    }

    ESP_LOGI(TAG, "Timeout set to %" PRIu32 " s", seconds);
}

extern "C" void standby_set_pre_sleep_cb(standby_pre_sleep_cb_t cb)
{
    s_pre_sleep_cb = cb;
}

extern "C" void standby_enter_sleep(void)
{
    stop_timer();

    if (s_pre_sleep_cb) {
        s_pre_sleep_cb();
    }

#if defined(CONFIG_DRAFTLING_MODEL_M5STACK_PAPERS3)
    /* PaperS3 wake source (GPIO48 -- GT911 touch INT) is not an RTC
     * GPIO, so EXT0 deep sleep is unavailable. Use light sleep with
     * gpio_wakeup_enable() instead, then esp_restart() on wake so
     * the rest of the codebase sees the same cold-boot semantics
     * the deep-sleep boards have. */
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << WAKEUP_GPIO;
    io.mode         = GPIO_MODE_INPUT;
    io.pull_up_en   = GPIO_PULLUP_ENABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type    = GPIO_INTR_DISABLE;
    gpio_config(&io);

    /* Wait for the wake pin to settle high so that we don't wake
     * immediately from a still-active touch / a finger held on the
     * panel when the timeout fired. Bounded so a permanently-low pin
     * doesn't hang us forever. */
    for (int i = 0; i < 200; i++) {
        if (gpio_get_level(WAKEUP_GPIO) == 1) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_ERROR_CHECK(gpio_wakeup_enable(WAKEUP_GPIO, GPIO_INTR_LOW_LEVEL));
    ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());

    ESP_LOGI(TAG,
             "Entering light sleep, wake on GPIO%d (touch); "
             "esp_restart() on wake.",
             (int)WAKEUP_GPIO);

    esp_light_sleep_start();

    /* Wake. Disable the GPIO wake source so it doesn't fire again
     * before the restart actually happens, then reboot to give the
     * application a clean cold-start (matching the deep-sleep
     * behaviour the rest of the firmware assumes -- gap buffer in
     * PSRAM is rebuilt from the autosaved file, BLE/WiFi stacks come
     * up fresh, etc.). */
    gpio_wakeup_disable(WAKEUP_GPIO);
    ESP_LOGI(TAG, "Woke from light sleep -- restarting");
    esp_restart();
    /* Does not return */
#elif defined(CONFIG_DRAFTLING_MODEL_WAVESHARE_RLCD42) || \
      defined(CONFIG_DRAFTLING_MODEL_SEEED_RETERMINAL_E1001) || \
      defined(CONFIG_DRAFTLING_MODEL_WAVESHARE_EPD_HAT)
    /* Configure wake-up GPIO as EXT0 wake-up source (wake on low level) */
    ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(WAKEUP_GPIO, 0));
    ESP_LOGI(TAG, "Entering deep sleep, wake on GPIO%d...", (int)WAKEUP_GPIO);
    esp_deep_sleep_start();
    /* Does not return */
#else
    esp_deep_sleep_start();
    /* Does not return */
#endif
}
