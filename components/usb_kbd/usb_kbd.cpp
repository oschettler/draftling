/*
 * USB HID keyboard host (boot-protocol only).
 *
 * Brings up the ESP-IDF USB Host stack and the espressif/usb_host_hid
 * managed component, opens any attached HID keyboard interface in
 * boot protocol mode, and translates the 8-byte boot keyboard input
 * report into the same kb_event_t event stream the BLE keyboard
 * component emits (modifier + keycode, character=0; the editor does
 * the keycode->char mapping itself).
 *
 * The USB Host PHY + VBUS power gate must already be enabled before
 * calling usb_kbd_init(). On M5Stack Tab5 main.cpp does this via
 * bsp_usb_host_start() from the espressif/m5stack_tab5 BSP.
 *
 * Reference: ESP-IDF examples/peripherals/usb/host/hid (boot-mode
 * keyboard branch). We deliberately do NOT handle the generic HID
 * path: this component is only here to feed the editor with key
 * events from a physically-attached keyboard.
 */

#include "usb_kbd.h"

#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_log.h>
#include <esp_err.h>

#include "usb/usb_host.h"
#include "usb/hid_host.h"
#include "usb/hid_usage_keyboard.h"

static const char *TAG = "USBKbd";

/* Public callback (mirrors ble_keyboard's s_callback) */
static kb_event_callback_t s_callback = NULL;

/* Set true while at least one HID keyboard interface is open */
static volatile bool s_kbd_connected = false;

/* Tasks + queue used to drive the HID host event pump from outside
 * the application thread. (The USB Host library task is owned by
 * the BSP -- see bsp_usb_host_start() in espressif/esp-bsp.) */
static TaskHandle_t  s_hid_event_task  = NULL;
static QueueHandle_t s_hid_event_queue = NULL;

/* Boot-protocol report state: track the previous 6 keycodes so we
 * emit one press/release event per change. */
#define KBD_BOOT_KEY_SLOTS 6
static uint8_t s_prev_keys[KBD_BOOT_KEY_SLOTS] = {0};
static uint8_t s_prev_mod = 0;

/* Queued HID-host driver event (we cannot call hid_host_device_open
 * from the driver callback itself -- it runs in the HID host driver
 * context). */
typedef struct {
    hid_host_device_handle_t handle;
    hid_host_driver_event_t  event;
    void                    *arg;
} hid_event_t;

/* ---- USB Host library task ---- */
/* Owned by the BSP on Tab5 (bsp_usb_host_start spawns usb_lib_task).
 * No-op in this component to avoid a duplicate usb_host_install
 * call. */

/* ---- HID host event helpers ---- */

static bool key_in_array(uint8_t kc, const uint8_t *keys, int n)
{
    for (int i = 0; i < n; i++) {
        if (keys[i] == kc) return true;
    }
    return false;
}

static void dispatch_key(uint8_t modifier, uint8_t keycode, bool pressed)
{
    if (!s_callback) return;
    kb_event_t ev = {};
    ev.modifier  = modifier;
    ev.keycode   = keycode;
    ev.character = 0;     /* editor does keycode->char mapping */
    ev.pressed   = pressed;
    s_callback(&ev);
}

/* Boot-protocol HID keyboard report: 8 bytes.
 *   [0]   = modifier bitmap (HID_*_CTRL / SHIFT / ALT / GUI, same
 *           bit layout as KB_MOD_*)
 *   [1]   = reserved (always 0)
 *   [2-7] = up to 6 simultaneous keycodes; 0 = empty slot
 */
static void process_boot_kbd_report(const uint8_t *data, size_t len)
{
    if (len < 3) return;     /* malformed; ignore */

    uint8_t mod = data[0];
    /* data[1] is the reserved byte; keycodes start at data[2]. */
    const uint8_t *keys = &data[2];
    int key_count = (int)len - 2;
    if (key_count > KBD_BOOT_KEY_SLOTS) key_count = KBD_BOOT_KEY_SLOTS;

    /* Newly pressed keys (in current report, not in previous) */
    for (int i = 0; i < key_count; i++) {
        uint8_t kc = keys[i];
        /* HID keycodes 0..3 are reserved error codes; skip. */
        if (kc < 0x04) continue;
        if (!key_in_array(kc, s_prev_keys, KBD_BOOT_KEY_SLOTS)) {
            dispatch_key(mod, kc, true);
        }
    }

    /* Released keys (in previous report, not in current) */
    for (int i = 0; i < KBD_BOOT_KEY_SLOTS; i++) {
        uint8_t kc = s_prev_keys[i];
        if (kc < 0x04) continue;
        if (!key_in_array(kc, keys, key_count)) {
            dispatch_key(mod, kc, false);
        }
    }

    /* Save state for the next diff. */
    memset(s_prev_keys, 0, sizeof(s_prev_keys));
    memcpy(s_prev_keys, keys, (size_t)key_count);
    s_prev_mod = mod;
}

/* ---- HID host driver callbacks ---- */

/* Per-interface callback: input reports, disconnect, transfer error. */
static void hid_iface_cb(hid_host_device_handle_t hid_dev_handle,
                          const hid_host_interface_event_t event,
                          void *arg)
{
    hid_host_dev_params_t dev_params = {};
    if (hid_host_device_get_params(hid_dev_handle, &dev_params) != ESP_OK) {
        return;
    }

    switch (event) {
    case HID_HOST_INTERFACE_EVENT_INPUT_REPORT: {
        uint8_t buf[64];
        size_t  buf_len = 0;
        if (hid_host_device_get_raw_input_report_data(hid_dev_handle,
                                                       buf, sizeof(buf),
                                                       &buf_len) != ESP_OK) {
            return;
        }
        if (dev_params.sub_class == HID_SUBCLASS_BOOT_INTERFACE &&
            dev_params.proto    == HID_PROTOCOL_KEYBOARD) {
            process_boot_kbd_report(buf, buf_len);
        }
        break;
    }
    case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "USB HID keyboard disconnected");
        s_kbd_connected = false;
        hid_host_device_close(hid_dev_handle);
        /* Release any held keys so the editor does not see stuck
         * modifiers after a hot-unplug. */
        for (int i = 0; i < KBD_BOOT_KEY_SLOTS; i++) {
            uint8_t kc = s_prev_keys[i];
            if (kc >= 0x04) dispatch_key(s_prev_mod, kc, false);
        }
        memset(s_prev_keys, 0, sizeof(s_prev_keys));
        s_prev_mod = 0;
        break;
    case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
        ESP_LOGW(TAG, "USB HID transfer error (proto=%d)",
                 (int)dev_params.proto);
        break;
    default:
        ESP_LOGD(TAG, "USB HID unhandled iface event %d", (int)event);
        break;
    }
}

/* Driver-level callback: device connected / disconnected. Runs in
 * the HID host driver task. We just push the event into a queue and
 * let s_hid_event_task open the device (the open call needs a
 * normal task context). */
static void hid_drv_cb(hid_host_device_handle_t hid_dev_handle,
                       const hid_host_driver_event_t event, void *arg)
{
    if (!s_hid_event_queue) return;
    hid_event_t evt = {
        .handle = hid_dev_handle,
        .event  = event,
        .arg    = arg,
    };
    xQueueSend(s_hid_event_queue, &evt, 0);
}

static void hid_event_task(void *arg)
{
    hid_event_t evt;
    while (true) {
        if (xQueueReceive(s_hid_event_queue, &evt, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (evt.event != HID_HOST_DRIVER_EVENT_CONNECTED) continue;

        hid_host_dev_params_t dev_params = {};
        if (hid_host_device_get_params(evt.handle, &dev_params) != ESP_OK) {
            continue;
        }
        ESP_LOGI(TAG, "USB HID device connected (sub_class=%d, proto=%d)",
                 (int)dev_params.sub_class, (int)dev_params.proto);

        /* We only care about boot-protocol keyboards. Skip mice and
         * generic HID. */
        if (dev_params.proto != HID_PROTOCOL_KEYBOARD) {
            ESP_LOGI(TAG, "Ignoring non-keyboard HID device");
            continue;
        }

        hid_host_device_config_t cfg = {
            .callback     = hid_iface_cb,
            .callback_arg = NULL,
        };
        if (hid_host_device_open(evt.handle, &cfg) != ESP_OK) {
            ESP_LOGE(TAG, "hid_host_device_open failed");
            continue;
        }
        if (dev_params.sub_class == HID_SUBCLASS_BOOT_INTERFACE) {
            /* Force boot protocol so we always get the fixed 8-byte
             * report layout regardless of the keyboard's native
             * report-descriptor preferences. */
            esp_err_t pe = hid_class_request_set_protocol(evt.handle,
                                                          HID_REPORT_PROTOCOL_BOOT);
            if (pe != ESP_OK) {
                ESP_LOGW(TAG, "set_protocol(BOOT) failed: %s "
                              "(keyboard may send report-descriptor format "
                              "instead of boot)", esp_err_to_name(pe));
            }
            /* SetIdle 0 -> only report on state change (no autorepeat
             * floods). */
            hid_class_request_set_idle(evt.handle, 0, 0);
        }
        if (hid_host_device_start(evt.handle) != ESP_OK) {
            ESP_LOGE(TAG, "hid_host_device_start failed");
            hid_host_device_close(evt.handle);
            continue;
        }
        ESP_LOGI(TAG, "USB HID keyboard ready");
        s_kbd_connected = true;
    }
}

/* ---- Public API ---- */

extern "C" int usb_kbd_init(void)
{
    /* NOTE: We deliberately do NOT call usb_host_install() or spawn a
     * usb_lib_task here. On Tab5 bsp_usb_host_start() already does
     * both (see espressif/esp-bsp bsp/m5stack_tab5/src/bsp_usb.c).
     * Calling usb_host_install a second time returns
     * ESP_ERR_INVALID_STATE. Callers on boards without that BSP
     * helper must install the host library themselves before
     * invoking usb_kbd_init(). */

    /* Spin up the queue + task that will service hid_drv_cb events. */
    s_hid_event_queue = xQueueCreate(8, sizeof(hid_event_t));
    if (!s_hid_event_queue) {
        ESP_LOGE(TAG, "hid event queue create failed");
        return -1;
    }
    if (xTaskCreate(hid_event_task, "usb_hid_evt", 4096, NULL, 4,
                    &s_hid_event_task) != pdTRUE) {
        ESP_LOGE(TAG, "hid_event task create failed");
        return -1;
    }

    /* Install the HID host driver. It spawns its own background task
     * to drive control transfers. */
    const hid_host_driver_config_t hid_cfg = {
        .create_background_task = true,
        .task_priority          = 5,
        .stack_size             = 4096,
        .core_id                = 0,
        .callback               = hid_drv_cb,
        .callback_arg           = NULL,
    };
    esp_err_t err = hid_host_install(&hid_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "hid_host_install failed: %s", esp_err_to_name(err));
        return -1;
    }

    ESP_LOGI(TAG, "HID host driver installed; waiting for keyboard");
    return 0;
}

extern "C" bool usb_kbd_is_connected(void)
{
    return s_kbd_connected;
}

extern "C" void usb_kbd_set_callback(kb_event_callback_t cb)
{
    s_callback = cb;
}
