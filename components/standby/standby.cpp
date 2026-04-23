/*
 * Standby / deep-sleep manager.
 *
 * Tracks user inactivity via an esp_timer.  When the configured
 * timeout expires the board enters deep sleep.
 *
 *  - Waveshare ESP32-S3-RLCD-4.2: wakes via EXT0 on GPIO18 (active-low).
 *    The RLCD is reflective, so screen content is retained visually.
 *  - Seeed reTerminal E1001: wakes via EXT0 on GPIO3 (KEY0, the right
 *    green button, active-low).  The e-paper retains its image while
 *    powered down.
 *  - Waveshare E-Paper Driver HAT: wakes via EXT0 on the GPIO selected
 *    by CONFIG_DRAFTLING_HAT_WAKEUP_GPIO (default GPIO0 / BOOT button).
 *  - M5Stack PaperS3: wakes via EXT0 on the BOOT button (GPIO0,
 *    active-low). Earlier revisions tried GPIO21 (wrong -- the on-board
 *    buzzer pin, floated low under some speaker-driver states and woke
 *    the device instantly) and GPIO48 (the GT911 touch INT). GPIO48
 *    also failed: M5GFX initializes only the e-paper panel, not the
 *    touch controller, so the GT911 is left uninitialized and holds
 *    its INT line low (the line doubles as I2C-address selection
 *    during reset), which fired GPIO_INTR_LOW_LEVEL immediately on
 *    every sleep attempt. GPIO0 is the only other digital input
 *    button on the PaperS3 (the hardware power switch is not a GPIO),
 *    is RTC-capable, and -- being an ESP32-S3 strapping pin with a
 *    board-level pull-up -- is guaranteed high while idle.
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
/* PaperS3 BOOT button (active-low, RTC-capable). See header comment
 * for why GPIO48 (touch INT) and GPIO21 (buzzer) were rejected. */
#define WAKEUP_GPIO    ((gpio_num_t)0)
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

    /* Configure wake-up GPIO as EXT0 wake-up source (wake on low level) */
    ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(WAKEUP_GPIO, 0));
    ESP_LOGI(TAG, "Entering deep sleep, wake on GPIO%d...", (int)WAKEUP_GPIO);
    esp_deep_sleep_start();
    /* Does not return */
}
