/*
 * Standby / deep-sleep manager.
 *
 * Tracks user inactivity via an esp_timer. When the configured
 * timeout expires the board enters one of three power-save modes,
 * picked at build time:
 *
 *   * Default (deep sleep + button wake):
 *     esp_deep_sleep_start(); wake via EXT0 on
 *     CONFIG_DRAFTLING_WAKEUP_GPIO (the per-board BOOT button).
 *
 *   * CONFIG_DRAFTLING_STANDBY_WAKE_ON_TOUCH (deep sleep + touch wake):
 *     same as above but EXT0 is armed on the touchscreen INT pin
 *     reported by touchscreen_get_int_gpio(), so any tap wakes the
 *     device. Preferred on boards with no user buttons.
 *
 *   * CONFIG_DRAFTLING_STANDBY_DISPLAY_OFF (display off, MCU
 *     running): blank the display via display_sleep() and block in
 *     a low-priority FreeRTOS poll loop until any input arrives
 *     (touch INT low, or BLE keyboard press), then display_wake()
 *     and return without losing editor state. Draws more power than
 *     deep sleep but works on hardware with no RTC-capable wake
 *     source.
 *
 * This file is otherwise board-agnostic. The editor state lives in
 * PSRAM/heap and IS preserved across the display-off path; deep
 * sleep loses it (callers should auto-save first). The timeout
 * value is persisted in NVS.
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
#include "ble_keyboard.h"
#include "display.h"
#if defined(CONFIG_DRAFTLING_TOUCHSCREEN)
#include "touchscreen.h"
#endif

static const char *TAG = "Standby";

/* NVS namespace and key for the timeout value */
#define NVS_NAMESPACE  "standby"
#define NVS_KEY_TOUT   "timeout"

/* GPIO used as EXT0 wake-up source (active-low). The numeric value
 * is selected per board in main/Kconfig.projbuild via the hidden
 * DRAFTLING_WAKEUP_GPIO symbol so the standby code itself stays
 * board-agnostic. */
#define WAKEUP_GPIO    ((gpio_num_t)CONFIG_DRAFTLING_WAKEUP_GPIO)

static uint32_t s_timeout_sec = STANDBY_DEFAULT_TIMEOUT_SEC;
static esp_timer_handle_t s_timer = NULL;
static standby_pre_sleep_cb_t s_pre_sleep_cb = NULL;

/* "No keyboard connected" countdown. Armed in standby_init() from
 * CONFIG_DRAFTLING_NO_KEYBOARD_SLEEP_SEC. When it fires it checks
 * ble_keyboard_is_connected() and only enters deep sleep if no
 * keyboard is paired by then. Independent of the regular inactivity
 * timer above so a long inactivity timeout does not stop us from
 * sleeping when no keyboard is around at all. We poll the BLE state
 * (rather than registering a connect callback) because the BLE
 * connect-callback slot is already used by the editor UI. */
static esp_timer_handle_t s_kb_wait_timer = NULL;

#if defined(CONFIG_DRAFTLING_STANDBY_DISPLAY_OFF)
/* Forward declaration -- the display-off wait loop is implemented
 * further down, but standby_reset_timer() needs to flip this flag
 * to wake the loop when a BLE key event arrives. */
static volatile bool s_display_off_wake_req = false;
#endif

/* ---- timer callback ---- */

static void inactivity_cb(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Inactivity timeout reached -- entering deep sleep");
    standby_enter_sleep();
}

#if CONFIG_DRAFTLING_NO_KEYBOARD_SLEEP_SEC > 0 && defined(CONFIG_DRAFTLING_HAS_BATTERY)
static void kb_wait_cb(void *arg)
{
    (void)arg;
    if (ble_keyboard_is_connected()) {
        ESP_LOGI(TAG, "No-keyboard timer fired but a keyboard is connected -- staying awake");
        return;
    }
    ESP_LOGI(TAG, "No keyboard connected within %d s -- entering deep sleep",
             CONFIG_DRAFTLING_NO_KEYBOARD_SLEEP_SEC);
    standby_enter_sleep();
}
#endif

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

    /* Arm the "no keyboard connected" deep-sleep countdown. When the
     * timer fires kb_wait_cb checks ble_keyboard_is_connected() and
     * only sleeps if no keyboard is paired by then. A value of 0
     * disables the feature (keep scanning indefinitely).
     *
     * The entire purpose of this countdown is to conserve battery on
     * portable boards that may have been powered on accidentally or
     * out of range of their paired keyboard. On USB-powered dev
     * boards with no battery (Waveshare Touch-LCD-3.49, Guition
     * JC3248W535) deep-sleeping after 3 minutes of no keyboard just
     * blanks the display unexpectedly while the user is still
     * setting things up, so we gate the arming on
     * CONFIG_DRAFTLING_HAS_BATTERY. */
#if CONFIG_DRAFTLING_NO_KEYBOARD_SLEEP_SEC > 0 && defined(CONFIG_DRAFTLING_HAS_BATTERY)
    {
        esp_timer_create_args_t kb_args = {};
        kb_args.callback = kb_wait_cb;
        kb_args.name = "standby_kb";
        ESP_ERROR_CHECK(esp_timer_create(&kb_args, &s_kb_wait_timer));
        esp_timer_start_once(s_kb_wait_timer,
            (uint64_t)CONFIG_DRAFTLING_NO_KEYBOARD_SLEEP_SEC * 1000000ULL);
        ESP_LOGI(TAG, "No-keyboard sleep armed (%d s)",
                 CONFIG_DRAFTLING_NO_KEYBOARD_SLEEP_SEC);
    }
#else
    ESP_LOGI(TAG, "No-keyboard sleep disabled "
                  "(no battery on this board, or timer = 0)");
#endif

    ESP_LOGI(TAG, "Standby initialized, timeout=%" PRIu32 " s", s_timeout_sec);
}

extern "C" void standby_reset_timer(void)
{
#if defined(CONFIG_DRAFTLING_STANDBY_DISPLAY_OFF)
    /* If the display is currently off, signal the wait loop to
     * exit. Whichever code path delivered this activity (BLE key,
     * touch, "Sleep now" cancelled) will then return through
     * enter_display_off and continue normally. */
    s_display_off_wake_req = true;
#endif
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

/* Pick the EXT0 wake GPIO. By default this is the per-board BOOT
 * button from Kconfig; when wake-on-touch is enabled and the
 * touchscreen driver has been initialised with an INT pin, that
 * INT pin overrides the default. */
static gpio_num_t resolve_wake_gpio(void)
{
    gpio_num_t g = WAKEUP_GPIO;
#if defined(CONFIG_DRAFTLING_STANDBY_WAKE_ON_TOUCH) && defined(CONFIG_DRAFTLING_TOUCHSCREEN)
    int tg = touchscreen_get_int_gpio();
    if (tg >= 0 && rtc_gpio_is_valid_gpio((gpio_num_t)tg)) {
        g = (gpio_num_t)tg;
    } else if (tg >= 0) {
        ESP_LOGW(TAG, "Touch INT GPIO%d is not RTC-capable; "
                      "falling back to GPIO%d for EXT0 wake",
                 tg, (int)WAKEUP_GPIO);
    }
#endif
    return g;
}

#if defined(CONFIG_DRAFTLING_STANDBY_DISPLAY_OFF)

/* Display-off standby: blank the display and idle the MCU until any
 * input arrives. Implemented as a busy wait inline on the caller's
 * task (the only caller is standby_enter_sleep, invoked from either
 * the LVGL timer callback or the inactivity esp_timer). The poll
 * loop checks:
 *   - touch INT GPIO low (any tap), or
 *   - BLE keyboard key event (ble_keyboard_is_connected -> reset
 *     timer in editor_ui resumes us via standby_reset_timer)
 * To keep this simple we poll only the touch INT and rely on
 * standby_reset_timer() being called from the BLE callback path
 * to invoke standby_display_off_wake() externally; in practice
 * the polling loop returns as soon as a touch is detected, which
 * is the primary input on display-off boards. */

static void enter_display_off(void)
{
    ESP_LOGI(TAG, "Standby: turning display off");
    if (s_pre_sleep_cb) {
        s_pre_sleep_cb();
    }
    display_sleep();

    s_display_off_wake_req = false;

#if defined(CONFIG_DRAFTLING_TOUCHSCREEN)
    int int_gpio = touchscreen_get_int_gpio();
#else
    int int_gpio = -1;
#endif

    /* Block until we see a wake signal. Poll every 100 ms; that is
     * fast enough to feel instant after a tap (a touch frame is
     * 10-20 ms) without burning the CPU. */
    while (!s_display_off_wake_req) {
        if (int_gpio >= 0 &&
            gpio_get_level((gpio_num_t)int_gpio) == 0) {
            break;
        }
        if (ble_keyboard_is_connected()) {
            /* BLE keyboard events route through editor_ui's
             * key handler which calls standby_reset_timer(); that
             * function flips s_display_off_wake_req. */
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_LOGI(TAG, "Standby: waking display");
    display_wake();
    /* Rearm the inactivity timer so we go back to sleep after the
     * user finishes interacting. */
    start_timer();
}

#endif /* CONFIG_DRAFTLING_STANDBY_DISPLAY_OFF */

extern "C" void standby_enter_sleep(void)
{
    stop_timer();
    if (s_kb_wait_timer) {
        esp_timer_stop(s_kb_wait_timer);
    }

#if defined(CONFIG_DRAFTLING_STANDBY_DISPLAY_OFF)
    enter_display_off();
    return;
#else

    if (s_pre_sleep_cb) {
        s_pre_sleep_cb();
    }

    /* Turn the panel off (BL off + SLPIN/DISPOFF on color backends)
     * and latch the BL GPIO LOW so the external pull-up does not
     * re-light the panel through deep sleep. No-op on reflective /
     * e-paper backends that have no backlight. */
    display_deep_sleep_prepare();

    gpio_num_t wake_gpio = resolve_wake_gpio();

    /* Enable the internal RTC pull-up on the wake pin and disable any
     * pull-down so the EXT0 wake-on-low source does not fire
     * immediately. The supported buttons (RLCD-4.2 button on GPIO18
     * and PaperS3 BOOT on GPIO0 / strapping pin) already have
     * external pull-ups, so adding an internal pull-up here is
     * harmless: the two pull-ups simply parallel. We always enable
     * it so the behaviour is consistent across boards.
     *
     * On wake-on-touch boards (CONFIG_DRAFTLING_STANDBY_WAKE_ON_TOUCH)
     * the same pull-up keeps the AXS5106L's open-drain INT line
     * idle-high while no touch is active, so EXT0 only fires when
     * the controller actually pulls INT low.
     *
     * rtc_gpio_pullup_en() routes through the RTC IO mux, so the
     * pull-up survives into deep sleep. Errors are logged but not
     * fatal: a non-RTC-capable GPIO will be rejected here, and we
     * still want to attempt deep sleep with whatever wake-up
     * configuration we can make work. */
    if (rtc_gpio_is_valid_gpio(wake_gpio)) {
        esp_err_t err = rtc_gpio_pullup_en(wake_gpio);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "rtc_gpio_pullup_en(GPIO%d) failed: %s",
                     (int)wake_gpio, esp_err_to_name(err));
        }
        err = rtc_gpio_pulldown_dis(wake_gpio);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "rtc_gpio_pulldown_dis(GPIO%d) failed: %s",
                     (int)wake_gpio, esp_err_to_name(err));
        }
    } else {
        ESP_LOGW(TAG, "GPIO%d is not RTC-capable; cannot enable internal pull-up",
                 (int)wake_gpio);
    }

    /* Configure wake-up GPIO as EXT0 wake-up source (wake on low level) */
    ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(wake_gpio, 0));
    ESP_LOGI(TAG, "Entering deep sleep, wake on GPIO%d...", (int)wake_gpio);
    esp_deep_sleep_start();
    /* Does not return */
#endif /* !CONFIG_DRAFTLING_STANDBY_DISPLAY_OFF */
}
