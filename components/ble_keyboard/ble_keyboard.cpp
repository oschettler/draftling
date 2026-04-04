/*
 * BLE HID Host keyboard driver for ESP32-S3.
 *
 * Uses ESP-IDF Bluedroid BLE stack for scanning and the unified HID
 * host component (esp_hidh) for HID-over-GATT.  ESP32-S3 does not
 * support Bluetooth Classic, so only BLE is used.
 *
 * The driver scans for BLE HID devices that advertise the HID service
 * UUID (0x1812) or a keyboard appearance (0x03C1), connects
 * automatically, and forwards key events through a callback.
 */

#include <cstdio>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <esp_log.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_gap_ble_api.h>
#include <esp_hidh.h>

#include "ble_keyboard.h"

static const char *TAG = "BLEKeyboard";

/* BLE HID service UUID */
#define BLE_SVC_HID_UUID16         0x1812
/* BLE appearance: keyboard */
#define BLE_APPEARANCE_KEYBOARD    0x03C1

/* Scan duration in seconds */
#define SCAN_DURATION_SEC          30

static kb_event_callback_t s_callback = NULL;
static volatile bool s_connected  = false;
static volatile bool s_connecting = false;
static char s_dev_name[64] = "";
static uint8_t s_prev_keys[6] = {0};

/* Target device address saved during scan for deferred connection */
static esp_bd_addr_t s_target_bda;
static esp_ble_addr_type_t s_target_addr_type;

/* Timer used to retry scanning after a delay */
static TimerHandle_t s_scan_timer = NULL;

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

/* ---- Advertisement data parser ---- */

static bool adv_is_hid_keyboard(uint8_t *adv_data, uint8_t adv_len)
{
    uint8_t *field;
    uint8_t field_len;

    /* Check appearance for keyboard (0x03C1) */
    field = esp_ble_resolve_adv_data(adv_data,
                                     ESP_BLE_AD_TYPE_APPEARANCE, &field_len);
    if (field && field_len >= 2) {
        uint16_t appearance = (uint16_t)field[0] |
                              ((uint16_t)field[1] << 8);
        if (appearance == BLE_APPEARANCE_KEYBOARD) return true;
    }

    /* Check complete 16-bit service UUID list for HID (0x1812) */
    field = esp_ble_resolve_adv_data(adv_data,
                                     ESP_BLE_AD_TYPE_16SRV_CMPL, &field_len);
    for (int i = 0; field && i + 1 < (int)field_len; i += 2) {
        uint16_t uuid = (uint16_t)field[i] | ((uint16_t)field[i + 1] << 8);
        if (uuid == BLE_SVC_HID_UUID16) return true;
    }

    /* Check incomplete 16-bit service UUID list */
    field = esp_ble_resolve_adv_data(adv_data,
                                     ESP_BLE_AD_TYPE_16SRV_PART, &field_len);
    for (int i = 0; field && i + 1 < (int)field_len; i += 2) {
        uint16_t uuid = (uint16_t)field[i] | ((uint16_t)field[i + 1] << 8);
        if (uuid == BLE_SVC_HID_UUID16) return true;
    }

    return false;
}

/* ---- Connect task (runs esp_hidh_dev_open which blocks) ---- */

static void connect_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Connecting to keyboard \"%s\" "
             "(%02x:%02x:%02x:%02x:%02x:%02x)...",
             s_dev_name,
             s_target_bda[0], s_target_bda[1], s_target_bda[2],
             s_target_bda[3], s_target_bda[4], s_target_bda[5]);

    esp_hidh_dev_open(s_target_bda, ESP_HID_TRANSPORT_BLE,
                      s_target_addr_type);
    vTaskDelete(NULL);
}

/* ---- Scan retry timer callback ---- */

static void scan_timer_cb(TimerHandle_t timer)
{
    (void)timer;
    if (!s_connected && !s_connecting) {
        esp_ble_gap_start_scanning(SCAN_DURATION_SEC);
    }
}

/* ---- BLE GAP callback ---- */

static void gap_event_handler(esp_gap_ble_cb_event_t event,
                               esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        if (param->scan_param_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "Scan parameters set");
        }
        break;

    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        if (param->scan_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "Scanning for BLE HID keyboards...");
        } else {
            ESP_LOGE(TAG, "Scan start failed: 0x%x",
                     param->scan_start_cmpl.status);
        }
        break;

    case ESP_GAP_BLE_SCAN_RESULT_EVT: {
        esp_ble_gap_cb_param_t::ble_scan_result_evt_param *scan =
            &param->scan_rst;

        if (scan->search_evt == ESP_GAP_SEARCH_INQ_RES_EVT) {
            if (s_connected || s_connecting) break;

            uint8_t total_len = scan->adv_data_len + scan->scan_rsp_len;
            if (adv_is_hid_keyboard(scan->ble_adv, total_len)) {
                /* Extract device name from advertisement */
                uint8_t name_len = 0;
                uint8_t *name = esp_ble_resolve_adv_data(
                    scan->ble_adv, ESP_BLE_AD_TYPE_NAME_CMPL, &name_len);
                if (!name) {
                    name = esp_ble_resolve_adv_data(
                        scan->ble_adv, ESP_BLE_AD_TYPE_NAME_SHORT,
                        &name_len);
                }
                if (name && name_len > 0) {
                    int len = (int)name_len;
                    if (len > (int)sizeof(s_dev_name) - 1)
                        len = (int)sizeof(s_dev_name) - 1;
                    memcpy(s_dev_name, name, len);
                    s_dev_name[len] = '\0';
                }

                ESP_LOGI(TAG, "Keyboard found: \"%s\" "
                         "(%02x:%02x:%02x:%02x:%02x:%02x)",
                         s_dev_name,
                         scan->bda[0], scan->bda[1], scan->bda[2],
                         scan->bda[3], scan->bda[4], scan->bda[5]);

                /* Save target address and stop scanning */
                s_connecting = true;
                memcpy(s_target_bda, scan->bda, sizeof(esp_bd_addr_t));
                s_target_addr_type = scan->ble_addr_type;
                esp_ble_gap_stop_scanning();
            }
        } else if (scan->search_evt == ESP_GAP_SEARCH_INQ_CMPL_EVT) {
            /* Scan complete -- retry after a delay if not connected */
            if (!s_connected && !s_connecting) {
                ESP_LOGI(TAG, "Scan complete, no keyboard found. "
                         "Retrying...");
                if (s_scan_timer) {
                    xTimerStart(s_scan_timer, 0);
                }
            }
        }
        break;
    }

    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        /* Scanning stopped -- initiate HID connection if target found */
        if (s_connecting) {
            xTaskCreate(connect_task, "ble_connect", 4096, NULL, 2, NULL);
        }
        break;

    case ESP_GAP_BLE_SEC_REQ_EVT:
        esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
        break;

    case ESP_GAP_BLE_AUTH_CMPL_EVT:
        if (param->ble_security.auth_cmpl.success) {
            ESP_LOGI(TAG, "BLE authentication success");
        } else {
            ESP_LOGE(TAG, "BLE authentication failed, reason: 0x%x",
                     param->ble_security.auth_cmpl.fail_reason);
        }
        break;

    default:
        break;
    }
}

/* ---- Public API ---- */

extern "C" void ble_keyboard_init(void)
{
    /* Release Classic BT memory (ESP32-S3 is BLE-only) */
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    /* Initialize BT controller in BLE mode */
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));

    /* Initialize Bluedroid */
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    /* Set BLE security parameters (NoIO, bonding, secure connections) */
    esp_ble_auth_req_t auth_req = ESP_LE_AUTH_REQ_SC_BOND;
    esp_ble_io_cap_t io_cap = ESP_IO_CAP_NONE;
    uint8_t key_size = 16;
    uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t rsp_key  = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;

    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE,
                                   &auth_req, sizeof(auth_req));
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE,
                                   &io_cap, sizeof(io_cap));
    esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE,
                                   &key_size, sizeof(key_size));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY,
                                   &init_key, sizeof(init_key));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY,
                                   &rsp_key, sizeof(rsp_key));

    /* Register BLE GAP callback */
    esp_ble_gap_register_callback(gap_event_handler);

    /* Set scan parameters */
    esp_ble_scan_params_t scan_params = {};
    scan_params.scan_type          = BLE_SCAN_TYPE_ACTIVE;
    scan_params.own_addr_type      = BLE_ADDR_TYPE_PUBLIC;
    scan_params.scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL;
    scan_params.scan_interval      = 0x0060;  /* 60 ms */
    scan_params.scan_window        = 0x0030;  /* 30 ms */
    scan_params.scan_duplicate     = BLE_SCAN_DUPLICATE_ENABLE;
    esp_ble_gap_set_scan_params(&scan_params);

    /* Initialize HID Host (esp_hid component) */
    esp_hidh_config_t hidh_cfg = {};
    hidh_cfg.callback = hidh_callback;
    hidh_cfg.event_stack_size = 4096;
    ESP_ERROR_CHECK(esp_hidh_init(&hidh_cfg));

    /* Create scan retry timer (one-shot, 2-second delay) */
    s_scan_timer = xTimerCreate("scan_retry", pdMS_TO_TICKS(2000),
                                pdFALSE, NULL, scan_timer_cb);

    /* Start initial scan */
    ble_keyboard_start_scan();

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
    if (!s_connected && !s_connecting) {
        esp_ble_gap_start_scanning(SCAN_DURATION_SEC);
    }
}

extern "C" const char *ble_keyboard_get_device_name(void)
{
    return s_dev_name;
}
