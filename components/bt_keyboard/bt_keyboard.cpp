/*
 * BLE HID Host keyboard driver for ESP32-S3.
 *
 * Uses the ESP-IDF unified HID host component (esp_hidh.h) with BLE
 * transport via the NimBLE stack.  ESP32-S3 does not support Bluetooth
 * Classic, so only BLE is used.
 *
 * The driver scans for BLE HID devices whose HID appearance field
 * indicates a keyboard (0x03C1), connects automatically, and forwards
 * key events through a callback.
 */

#include <cstdio>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_log.h>
#include <esp_hidh.h>
#include <esp_hid_gap.h>

#include "bt_keyboard.h"

static const char *TAG = "BTKeyboard";

static kb_event_callback_t s_callback = NULL;
static bool s_connected = false;
static bool s_scanning  = false;
static char s_dev_name[64] = "";
static uint8_t s_prev_keys[6] = {0};

/* HID keycode to ASCII (unshifted) */
static const char KC_TO_ASCII[128] = {
    0,0,0,0,                                        /* 0x00-0x03 */
    'a','b','c','d','e','f','g','h','i','j','k','l', /* 0x04-0x0F */
    'm','n','o','p','q','r','s','t','u','v','w','x', /* 0x10-0x1B */
    'y','z',                                         /* 0x1C-0x1D */
    '1','2','3','4','5','6','7','8','9','0',         /* 0x1E-0x27 */
    0,0,0,0,' ',                                     /* 0x28-0x2C (enter,esc,bs,tab,space) */
    '-','=','[',']','\\',                            /* 0x2D-0x31 */
    0,';','\'','`',',','.','/',                      /* 0x32-0x38 */
};

/* Shifted equivalents */
static const char KC_TO_ASCII_SHIFT[128] = {
    0,0,0,0,
    'A','B','C','D','E','F','G','H','I','J','K','L',
    'M','N','O','P','Q','R','S','T','U','V','W','X',
    'Y','Z',
    '!','@','#','$','%','^','&','*','(',')',
    0,0,0,0,' ',
    '_','+','{','}','|',
    0,':','"','~','<','>','?',
};

static char keycode_to_char(uint8_t keycode, uint8_t modifier)
{
    if (keycode >= sizeof(KC_TO_ASCII)) return 0;
    bool shift = (modifier & (KB_MOD_LSHIFT | KB_MOD_RSHIFT)) != 0;
    return shift ? KC_TO_ASCII_SHIFT[keycode] : KC_TO_ASCII[keycode];
}

static bool key_in_report(uint8_t key, const uint8_t *keys, int count)
{
    for (int i = 0; i < count; i++) {
        if (keys[i] == key) return true;
    }
    return false;
}

static void process_keyboard_report(const uint8_t *data, int len)
{
    if (len < 8 || !s_callback) return;

    uint8_t modifier = data[0];
    const uint8_t *keys = &data[2]; /* keys[0..5] */

    /* Detect newly pressed keys */
    for (int i = 0; i < 6; i++) {
        uint8_t kc = keys[i];
        if (kc == 0) continue;
        if (!key_in_report(kc, s_prev_keys, 6)) {
            kb_event_t ev = {};
            ev.modifier  = modifier;
            ev.keycode   = kc;
            ev.character = keycode_to_char(kc, modifier);
            ev.pressed   = true;
            s_callback(&ev);
        }
    }

    /* Detect released keys */
    for (int i = 0; i < 6; i++) {
        uint8_t kc = s_prev_keys[i];
        if (kc == 0) continue;
        if (!key_in_report(kc, keys, 6)) {
            kb_event_t ev = {};
            ev.modifier  = modifier;
            ev.keycode   = kc;
            ev.character = keycode_to_char(kc, modifier);
            ev.pressed   = false;
            s_callback(&ev);
        }
    }

    memcpy(s_prev_keys, keys, 6);
}

/* ---- Unified HID Host callback ---- */

static void hidh_callback(void *handler_args, esp_event_base_t base,
                           int32_t id, void *event_data)
{
    esp_hidh_event_t event = (esp_hidh_event_t)id;
    esp_hidh_event_data_t *param = (esp_hidh_event_data_t *)event_data;

    switch (event) {
    case ESP_HIDH_OPEN_EVENT:
        if (param->open.status == ESP_OK) {
            s_connected = true;
            const uint8_t *addr = esp_hidh_dev_bda_get(param->open.dev);
            const char *name = esp_hidh_dev_name_get(param->open.dev);
            if (name && name[0]) {
                strncpy(s_dev_name, name, sizeof(s_dev_name) - 1);
                s_dev_name[sizeof(s_dev_name) - 1] = '\0';
            }
            ESP_LOGI(TAG, "HID device connected: %s ("
                     "%02x:%02x:%02x:%02x:%02x:%02x)",
                     s_dev_name,
                     addr ? addr[0] : 0, addr ? addr[1] : 0,
                     addr ? addr[2] : 0, addr ? addr[3] : 0,
                     addr ? addr[4] : 0, addr ? addr[5] : 0);
        } else {
            ESP_LOGE(TAG, "HID open failed: %d", param->open.status);
        }
        break;

    case ESP_HIDH_CLOSE_EVENT:
        s_connected = false;
        s_dev_name[0] = '\0';
        memset(s_prev_keys, 0, sizeof(s_prev_keys));
        ESP_LOGI(TAG, "HID device disconnected, restarting scan...");
        bt_keyboard_start_scan();
        break;

    case ESP_HIDH_INPUT_EVENT: {
        /* Boot keyboard report: 8 bytes (modifier, reserved, keys[6]) */
        if (param->input.usage == ESP_HID_USAGE_KEYBOARD &&
            param->input.length >= 8) {
            process_keyboard_report(param->input.data,
                                    (int)param->input.length);
        }
        break;
    }

    case ESP_HIDH_BATTERY_EVENT:
        ESP_LOGI(TAG, "Battery level: %d%%", param->battery.level);
        break;

    default:
        break;
    }
}

/* ---- BLE scan task ---- */

static void scan_task(void *arg)
{
    (void)arg;

    /* Scan for BLE HID devices */
    ESP_LOGI(TAG, "Scanning for BLE HID keyboards...");
    size_t result_count = 0;
    esp_hid_scan_result_t *results = NULL;

    /* Scan BLE only, 5-second window */
    esp_hid_scan(5, &result_count, &results);

    if (result_count == 0) {
        ESP_LOGI(TAG, "No HID devices found");
        s_scanning = false;
        /* Retry after a delay */
        vTaskDelay(pdMS_TO_TICKS(2000));
        bt_keyboard_start_scan();
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Found %u HID device(s)", (unsigned)result_count);

    /* Find the first keyboard among results */
    esp_hid_scan_result_t *kbd = NULL;
    esp_hid_scan_result_t *r = results;
    while (r) {
        /* HID appearance 0x03C1 = keyboard */
        if (r->transport == ESP_HID_TRANSPORT_BLE &&
            r->ble.appearance == ESP_HID_APPEARANCE_KEYBOARD) {
            kbd = r;
            break;
        }
        r = r->next;
    }

    if (kbd) {
        ESP_LOGI(TAG, "Keyboard found: \"%s\" (addr %02x:%02x:%02x:%02x:%02x:%02x)",
                 kbd->name ? kbd->name : "",
                 kbd->bda[0], kbd->bda[1], kbd->bda[2],
                 kbd->bda[3], kbd->bda[4], kbd->bda[5]);

        esp_hidh_dev_open(kbd->bda, kbd->transport, kbd->ble.addr_type);
    } else {
        ESP_LOGI(TAG, "No keyboard among discovered devices");
    }

    esp_hid_scan_results_free(results);
    s_scanning = false;
    vTaskDelete(NULL);
}

/* ---- Public API ---- */

extern "C" void bt_keyboard_init(void)
{
    /* Initialize BLE GAP (provided by esp_hid component) */
    ESP_ERROR_CHECK(esp_hid_gap_ble_init(ESP_BLE_SEC_ENCRYPT_MITM));

    /* Configure and start the unified HID host */
    esp_hidh_config_t config = {};
    config.callback = hidh_callback;
    config.event_stack_size = 4096;
    ESP_ERROR_CHECK(esp_hidh_init(&config));

    /* Start scanning */
    bt_keyboard_start_scan();

    ESP_LOGI(TAG, "BLE HID keyboard host initialized");
}

extern "C" void bt_keyboard_set_callback(kb_event_callback_t callback)
{
    s_callback = callback;
}

extern "C" bool bt_keyboard_is_connected(void)
{
    return s_connected;
}

extern "C" void bt_keyboard_start_scan(void)
{
    if (!s_connected && !s_scanning) {
        s_scanning = true;
        /* Scan runs blocking, so launch in a separate task */
        xTaskCreate(scan_task, "bt_scan", 4096, NULL, 2, NULL);
    }
}

extern "C" const char *bt_keyboard_get_device_name(void)
{
    return s_dev_name;
}
