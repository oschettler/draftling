/*
 * BLE HID Host keyboard driver for ESP32-S3.
 *
 * Uses NimBLE stack for BLE scanning and the ESP-IDF esp_hidh component
 * for HID-over-GATT host.  ESP32-S3 does not support Bluetooth Classic,
 * so only BLE is used.
 *
 * The driver scans for BLE HID devices that advertise the HID service
 * UUID (0x1812) or a keyboard appearance (0x03C1), connects
 * automatically, and forwards key events through a callback.
 */

#include <cstdio>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_bt.h>
#include <esp_hidh.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <host/ble_hs.h>
#include <host/ble_gap.h>
#include <host/util/util.h>

#include "ble_keyboard.h"

static const char *TAG = "BLEKeyboard";

/* BLE HID service UUID */
#define BLE_SVC_HID_UUID16         0x1812
/* BLE appearance: keyboard */
#define BLE_APPEARANCE_KEYBOARD    0x03C1

static kb_event_callback_t s_callback = NULL;
static volatile bool s_connected  = false;
static volatile bool s_connecting = false;
static char s_dev_name[64] = "";
static uint8_t s_prev_keys[6] = {0};

/* Scan state shared between NimBLE host task and scan task */
static ble_addr_t s_kbd_addr;
static volatile bool s_kbd_found = false;
static TaskHandle_t s_scan_task_handle = NULL;

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
            s_connected  = true;
            s_connecting = false;
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
            s_connecting = false;
            ble_keyboard_start_scan();
        }
        break;

    case ESP_HIDH_CLOSE_EVENT:
        s_connected  = false;
        s_connecting = false;
        s_dev_name[0] = '\0';
        memset(s_prev_keys, 0, sizeof(s_prev_keys));
        ESP_LOGI(TAG, "HID device disconnected, restarting scan...");
        ble_keyboard_start_scan();
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
        ESP_LOGI(TAG, "Battery: %d%%", param->battery.level);
        break;

    default:
        break;
    }
}

/* ---- NimBLE BLE scan callback (runs in NimBLE host task) ---- */

static int ble_scan_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        struct ble_hs_adv_fields fields = {};
        int rc = ble_hs_adv_parse_fields(&fields, event->disc.data,
                                          event->disc.length_data);
        if (rc != 0) break;

        bool is_kbd = false;

        /* Check appearance for keyboard (0x03C1) */
        if (fields.appearance_is_present &&
            fields.appearance == BLE_APPEARANCE_KEYBOARD) {
            is_kbd = true;
        }

        /* Check for HID service UUID (0x1812) */
        if (!is_kbd) {
            for (int i = 0; i < fields.num_uuids16; i++) {
                if (ble_uuid_u16(&fields.uuids16[i].u) ==
                    BLE_SVC_HID_UUID16) {
                    is_kbd = true;
                    break;
                }
            }
        }

        if (is_kbd && !s_kbd_found && !s_connected && !s_connecting) {
            s_kbd_found = true;
            s_kbd_addr = event->disc.addr;

            /* Grab advertised name if available */
            if (fields.name_len > 0 && fields.name != NULL) {
                int len = (int)fields.name_len;
                if (len > (int)sizeof(s_dev_name) - 1)
                    len = (int)sizeof(s_dev_name) - 1;
                memcpy(s_dev_name, fields.name, (size_t)len);
                s_dev_name[len] = '\0';
            }

            ble_gap_disc_cancel();

            /* Wake scan task so it can connect */
            if (s_scan_task_handle) {
                xTaskNotifyGive(s_scan_task_handle);
            }
        }
        break;
    }

    case BLE_GAP_EVENT_DISC_COMPLETE:
        /* Scan finished (timeout or cancelled) -- wake scan task */
        if (s_scan_task_handle) {
            xTaskNotifyGive(s_scan_task_handle);
        }
        break;

    default:
        break;
    }
    return 0;
}

/* ---- Scan-and-connect task ---- */

static void scan_connect_task(void *arg)
{
    (void)arg;

    while (!s_connected) {
        s_kbd_found = false;

        struct ble_gap_disc_params params = {};
        params.passive = 0;           /* active scan */
        params.itvl = 0x0060;         /* 60 ms */
        params.window = 0x0030;       /* 30 ms */
        params.filter_duplicates = 1;
        params.limited = 0;

        ESP_LOGI(TAG, "Scanning for BLE HID keyboards...");
        int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, 10000, &params,
                              ble_scan_cb, NULL);
        if (rc != 0 && rc != BLE_HS_EALREADY) {
            ESP_LOGE(TAG, "BLE scan start failed: %d", rc);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        /* Wait for scan callback to find a keyboard or for timeout */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(15000));

        if (s_kbd_found && !s_connected) {
            s_connecting = true;
            ESP_LOGI(TAG, "Connecting to keyboard \"%s\" "
                     "(%02x:%02x:%02x:%02x:%02x:%02x)...",
                     s_dev_name,
                     s_kbd_addr.val[5], s_kbd_addr.val[4],
                     s_kbd_addr.val[3], s_kbd_addr.val[2],
                     s_kbd_addr.val[1], s_kbd_addr.val[0]);

            /* esp_hidh_dev_open blocks until connection + GATT discovery */
            esp_hidh_dev_open(s_kbd_addr.val, ESP_HID_TRANSPORT_BLE,
                              s_kbd_addr.type);
        }

        if (!s_connected) {
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }

    s_scan_task_handle = NULL;
    vTaskDelete(NULL);
}

/* ---- NimBLE host helpers ---- */

static void ble_host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void on_ble_sync(void)
{
    /* Make sure we have an address */
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to configure BLE address: %d", rc);
        return;
    }
    ESP_LOGI(TAG, "BLE host synced, starting keyboard scan");
    ble_keyboard_start_scan();
}

static void on_ble_reset(int reason)
{
    ESP_LOGE(TAG, "BLE host reset, reason: %d", reason);
}

/* ---- Public API ---- */

extern "C" void ble_keyboard_init(void)
{
    /* Release Classic BT memory (ESP32-S3 is BLE-only) */
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    /* Initialize NimBLE */
    ESP_ERROR_CHECK(nimble_port_init());

    /* Security manager: no IO, bonding, secure connections */
    ble_hs_cfg.sync_cb  = on_ble_sync;
    ble_hs_cfg.reset_cb = on_ble_reset;
    ble_hs_cfg.sm_io_cap         = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding        = 1;
    ble_hs_cfg.sm_mitm           = 0;
    ble_hs_cfg.sm_sc             = 1;
    ble_hs_cfg.sm_our_key_dist   = BLE_SM_PAIR_KEY_DIST_ENC;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC;

    /* Initialize HID Host (esp_hid component) */
    esp_hidh_config_t config = {};
    config.callback = hidh_callback;
    config.event_stack_size = 4096;
    ESP_ERROR_CHECK(esp_hidh_init(&config));

    /* Start NimBLE host task -- on_ble_sync will trigger scanning */
    nimble_port_freertos_init(ble_host_task);

    ESP_LOGI(TAG, "BLE HID keyboard host initialized");
}

extern "C" void ble_keyboard_set_callback(kb_event_callback_t callback)
{
    s_callback = callback;
}

extern "C" bool ble_keyboard_is_connected(void)
{
    return s_connected;
}

extern "C" void ble_keyboard_start_scan(void)
{
    if (!s_connected && !s_connecting && s_scan_task_handle == NULL) {
        xTaskCreate(scan_connect_task, "ble_scan", 4096, NULL, 2,
                    &s_scan_task_handle);
    }
}

extern "C" const char *ble_keyboard_get_device_name(void)
{
    return s_dev_name;
}
