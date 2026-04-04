#include <cstdio>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_bt_device.h>
#include <esp_gap_bt_api.h>
#include <esp_hidh_api.h>

#include "bt_keyboard.h"

static const char *TAG = "BTKeyboard";

static kb_event_callback_t s_callback = NULL;
static bool s_connected = false;
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

/* ---- HID Host callback ---- */

static void hidh_callback(esp_hidh_cb_event_t event, esp_hidh_cb_param_t *param)
{
    switch (event) {
    case ESP_HIDH_OPEN_EVT:
        if (param->open.status == ESP_HIDH_OK) {
            s_connected = true;
            /* param->open.bd_addr available but name comes from GAP */
            ESP_LOGI(TAG, "HID device connected");
        } else {
            ESP_LOGE(TAG, "HID open failed: %d", param->open.status);
        }
        break;

    case ESP_HIDH_CLOSE_EVT:
        s_connected = false;
        ESP_LOGI(TAG, "HID device disconnected, restarting scan...");
        bt_keyboard_start_scan();
        break;

    case ESP_HIDH_DATA_IND_EVT:
        if (param->data_ind.proto_mode == ESP_HIDH_BOOT_MODE &&
            param->data_ind.len >= 8) {
            process_keyboard_report(param->data_ind.data, param->data_ind.len);
        }
        break;

    default:
        break;
    }
}

/* ---- GAP callback ---- */

static void gap_callback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_BT_GAP_DISC_RES_EVT: {
        /* Check if this is a keyboard (Major=Peripheral, minor bit 4=keyboard) */
        uint32_t cod = 0;
        char name[64] = "";
        for (int i = 0; i < param->disc_res.num_prop; i++) {
            esp_bt_gap_dev_prop_t *prop = &param->disc_res.prop[i];
            if (prop->type == ESP_BT_GAP_DEV_PROP_COD) {
                cod = *(uint32_t *)prop->val;
            } else if (prop->type == ESP_BT_GAP_DEV_PROP_BDNAME && prop->len > 0) {
                int clen = prop->len < (int)sizeof(name) - 1 ? prop->len : (int)sizeof(name) - 1;
                memcpy(name, prop->val, clen);
                name[clen] = '\0';
            }
        }
        uint8_t major = (cod >> 8) & 0x1F;
        uint8_t minor = (cod >> 2) & 0x3F;
        if (major == 5 && (minor & 0x10)) {
            ESP_LOGI(TAG, "Keyboard found: %s", name);
            strncpy(s_dev_name, name, sizeof(s_dev_name) - 1);
            esp_bt_gap_cancel_discovery();
            /* Connect via HID host */
            esp_bt_hid_host_connect(param->disc_res.bda);
        }
        break;
    }

    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
        if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED && !s_connected) {
            ESP_LOGI(TAG, "Discovery stopped, restarting in 2s...");
            vTaskDelay(pdMS_TO_TICKS(2000));
            bt_keyboard_start_scan();
        }
        break;

    case ESP_BT_GAP_PIN_REQ_EVT: {
        esp_bt_pin_code_t pin = {'0','0','0','0'};
        esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin);
        break;
    }

    case ESP_BT_GAP_CFM_REQ_EVT:
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;

    case ESP_BT_GAP_KEY_NOTIF_EVT:
        ESP_LOGI(TAG, "SSP passkey: %06lu", (unsigned long)param->key_notif.passkey);
        break;

    case ESP_BT_GAP_AUTH_CMPL_EVT:
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "Auth success: %s", param->auth_cmpl.device_name);
        } else {
            ESP_LOGE(TAG, "Auth failed: %d", param->auth_cmpl.stat);
        }
        break;

    default:
        break;
    }
}

/* ---- Public API ---- */

extern "C" void bt_keyboard_init(void)
{
    /* Init BT controller */
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));

    /* Init Bluedroid */
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    /* Set device name */
    esp_bt_gap_set_device_name("WriterDeck");

    /* SSP: no I/O capability (auto-accept) */
    esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_NONE;
    esp_bt_gap_set_security_param(param_type, &iocap, sizeof(iocap));

    /* Register GAP callback */
    esp_bt_gap_register_callback(gap_callback);
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);

    /* Init HID Host */
    ESP_ERROR_CHECK(esp_bt_hid_host_init());
    ESP_ERROR_CHECK(esp_bt_hid_host_register_callback(hidh_callback));

    /* Start scanning */
    bt_keyboard_start_scan();

    ESP_LOGI(TAG, "Bluetooth keyboard host initialized");
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
    if (!s_connected) {
        ESP_LOGI(TAG, "Scanning for keyboards...");
        esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);
    }
}

extern "C" const char *bt_keyboard_get_device_name(void)
{
    return s_dev_name;
}
