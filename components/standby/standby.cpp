/*
 * Standby / deep-sleep manager.
 *
 * Tracks user inactivity via an esp_timer.  When the configured
 * timeout expires the board enters deep sleep, waking on GPIO18
 * (EXT0 wakeup, active-low).  The timeout value is persisted in NVS.
 *
 * Because deep sleep preserves RTC memory and the RLCD is a
 * reflective display the screen content is retained visually.
 * The editor state lives in PSRAM/heap so it is lost on wake;
 * callers should auto-save before sleep.
 */

#include <cstring>
#include <cinttypes>
#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>

#include "standby.h"

static const char *TAG = "Standby";

/* NVS namespace and key for the timeout value */
#define NVS_NAMESPACE  "standby"
#define NVS_KEY_TOUT   "timeout"

/* GPIO used as EXT0 wake-up source (active-low) */
#define WAKEUP_GPIO    GPIO_NUM_18

static uint32_t s_timeout_sec = STANDBY_DEFAULT_TIMEOUT_SEC;
static esp_timer_handle_t s_timer = NULL;

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

extern "C" void standby_enter_sleep(void)
{
    stop_timer();

    /* Configure GPIO18 as EXT0 wake-up source (wake on low level) */
    ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(WAKEUP_GPIO, 0));

    /* Isolate other RTC GPIOs to reduce current (optional) */
    ESP_LOGI(TAG, "Entering deep sleep, wake on GPIO%d...", (int)WAKEUP_GPIO);

    esp_deep_sleep_start();
    /* Does not return */
}
