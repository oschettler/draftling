/*
 * BLE HID Host keyboard driver for ESP32-S3.
 *
 * Uses ESP-IDF Bluedroid BLE stack for scanning and the unified HID
 * host component (esp_hidh) for HID-over-GATT.  ESP32-S3 does not
 * support Bluetooth Classic, so only BLE is used.
 *
 * Note: esp_bt.h is the BT *controller* API, required for both
 * Classic BT and BLE.  On ESP32-S3 it provides BLE-only controller
 * functions (init, enable, memory release).  Classic BT memory is
 * explicitly released at startup so only BLE resources are kept.
 *
 * Multi-pairing: stores up to MAX_BONDED bonded device addresses in
 * NVS.  On startup it tries the last-connected device first, then
 * other known devices, then scans for any new HID keyboard.
 *
 * Pairing: uses DisplayOnly IO capability.  When a new keyboard
 * pairs, a random 6-digit passkey is generated and shown on the
 * display; the user types it on the keyboard to confirm.
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
#include <nvs_flash.h>
#include <nvs.h>

#include "ble_keyboard.h"

static const char *TAG = "BLEKeyboard";

/* BLE HID service UUID */
#define BLE_SVC_HID_UUID16         0x1812
/* BLE appearance values -- HID category */
#define BLE_APPEARANCE_HID_GENERIC 0x03C0
#define BLE_APPEARANCE_KEYBOARD    0x03C1

/* 128-bit BLE base UUID for HID service (little-endian):
 * 00001812-0000-1000-8000-00805f9b34fb */
static const uint8_t BLE_SVC_HID_UUID128[16] = {
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0x12, 0x18, 0x00, 0x00
};

/* Scan duration in seconds */
#define SCAN_DURATION_SEC          30

/* Maximum number of bonded keyboards stored in NVS */
#define MAX_BONDED  8

/* NVS namespace and keys */
#define NVS_NAMESPACE   "ble_kb"
#define NVS_KEY_COUNT   "bond_cnt"
#define NVS_KEY_LAST    "bond_last"
/* Per-device keys: "bond_0" .. "bond_7", each stores 7 bytes
 * (6-byte BDA + 1-byte address type) */

/* ---- Bonded device storage ---- */

typedef struct {
    esp_bd_addr_t       bda;
    esp_ble_addr_type_t addr_type;
} bonded_dev_t;

static bonded_dev_t s_bonded[MAX_BONDED];
static int          s_bonded_count = 0;
static int          s_last_bonded  = -1; /* index into s_bonded */

static void bonded_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return;

    uint8_t cnt = 0;
    if (nvs_get_u8(h, NVS_KEY_COUNT, &cnt) != ESP_OK) cnt = 0;
    if (cnt > MAX_BONDED) cnt = MAX_BONDED;

    int8_t last = -1;
    if (nvs_get_i8(h, NVS_KEY_LAST, &last) != ESP_OK) last = -1;

    s_bonded_count = 0;
    for (int i = 0; i < cnt; i++) {
        char key[12];
        snprintf(key, sizeof(key), "bond_%d", i);
        uint8_t buf[7];
        size_t len = sizeof(buf);
        if (nvs_get_blob(h, key, buf, &len) == ESP_OK && len == 7) {
            memcpy(s_bonded[s_bonded_count].bda, buf, 6);
            s_bonded[s_bonded_count].addr_type =
                (esp_ble_addr_type_t)buf[6];
            s_bonded_count++;
        }
    }
    s_last_bonded = (last >= 0 && last < s_bonded_count) ? last : -1;
    nvs_close(h);
    ESP_LOGI(TAG, "Loaded %d bonded device(s), last=%d",
             s_bonded_count, s_last_bonded);
}

static void bonded_save(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;

    nvs_set_u8(h, NVS_KEY_COUNT, (uint8_t)s_bonded_count);
    nvs_set_i8(h, NVS_KEY_LAST, (int8_t)s_last_bonded);

    for (int i = 0; i < s_bonded_count; i++) {
        char key[12];
        snprintf(key, sizeof(key), "bond_%d", i);
        uint8_t buf[7];
        memcpy(buf, s_bonded[i].bda, 6);
        buf[6] = (uint8_t)s_bonded[i].addr_type;
        nvs_set_blob(h, key, buf, sizeof(buf));
    }
    nvs_commit(h);
    nvs_close(h);
}

/* Find a bonded device by BDA; returns index or -1 */
static int bonded_find(const esp_bd_addr_t bda)
{
    for (int i = 0; i < s_bonded_count; i++) {
        if (memcmp(s_bonded[i].bda, bda, 6) == 0) return i;
    }
    return -1;
}

/* Add or promote a bonded device to "last used" */
static void bonded_add(const esp_bd_addr_t bda,
                        esp_ble_addr_type_t addr_type)
{
    int idx = bonded_find(bda);
    if (idx < 0) {
        /* New device -- evict oldest if full */
        if (s_bonded_count >= MAX_BONDED) {
            /* Shift array down, dropping slot 0 */
            memmove(&s_bonded[0], &s_bonded[1],
                    (size_t)(MAX_BONDED - 1) * sizeof(bonded_dev_t));
            s_bonded_count = MAX_BONDED - 1;
        }
        idx = s_bonded_count;
        memcpy(s_bonded[idx].bda, bda, 6);
        s_bonded[idx].addr_type = addr_type;
        s_bonded_count++;
    }
    s_last_bonded = idx;
    bonded_save();
    ESP_LOGI(TAG, "Bonded device saved at index %d", idx);
}

/* ---- State ---- */

static kb_event_callback_t  s_callback = NULL;
static ble_passkey_cb_t     s_passkey_cb = NULL;
static ble_connect_cb_t     s_connect_cb = NULL;
static volatile bool s_connected  = false;
static volatile bool s_connecting = false;
static volatile bool s_scan_params_ready = false;
static char s_dev_name[64] = "";
static uint8_t s_prev_keys[6] = {0};
static volatile int s_battery_level = -1;  /* -1 = unknown */

/* Target device address saved during scan for deferred connection */
static esp_bd_addr_t s_target_bda;
static esp_ble_addr_type_t s_target_addr_type;

/* Timer used to retry scanning after a delay */
static TimerHandle_t s_scan_timer = NULL;

/* Reconnection state machine */
typedef enum {
    RECONN_LAST,     /* trying last-connected device */
    RECONN_KNOWN,    /* trying other known devices */
    RECONN_SCAN,     /* scanning for any HID keyboard */
} reconn_phase_t;

static reconn_phase_t s_reconn_phase = RECONN_LAST;
static int            s_reconn_idx   = 0; /* index for RECONN_KNOWN */
static TimerHandle_t  s_reconn_timer = NULL;

/* Forward declarations */
static void start_reconnection(void);
static void reconn_timer_cb(TimerHandle_t timer);

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
            ev.character = 0;
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
            ev.character = 0;
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
            /* Remember this device as bonded / last-used */
            if (addr) {
                bonded_add(addr, s_target_addr_type);
            }
            ESP_LOGI(TAG, "HID device connected: %s ("
                     "%02x:%02x:%02x:%02x:%02x:%02x)",
                     s_dev_name,
                     addr ? addr[0] : 0, addr ? addr[1] : 0,
                     addr ? addr[2] : 0, addr ? addr[3] : 0,
                     addr ? addr[4] : 0, addr ? addr[5] : 0);
            if (s_connect_cb) s_connect_cb(true);
        } else {
            ESP_LOGE(TAG, "HID open failed: %d", param->open.status);
            s_connecting = false;
            /* Continue reconnection sequence after a short delay */
            if (s_reconn_timer) {
                xTimerStart(s_reconn_timer, 0);
            }
        }
        break;

    case ESP_HIDH_CLOSE_EVENT:
        s_connected  = false;
        s_connecting = false;
        s_dev_name[0] = '\0';
        s_battery_level = -1;
        memset(s_prev_keys, 0, sizeof(s_prev_keys));
        ESP_LOGI(TAG, "HID device disconnected, reconnecting...");
        if (s_connect_cb) s_connect_cb(false);
        /* Reset reconnection to start with last-known device */
        s_reconn_phase = RECONN_LAST;
        s_reconn_idx   = 0;
        start_reconnection();
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
        s_battery_level = param->battery.level;
        ESP_LOGI(TAG, "Battery: %d%%", param->battery.level);
        break;

    default:
        break;
    }
}

/* ---- Advertisement data parser ---- */

/* Check whether a 16-bit UUID list (AD type) contains the HID service UUID */
static bool adv_has_hid_uuid16(uint8_t *adv_data, uint8_t adv_len,
                                uint8_t ad_type)
{
    uint8_t field_len = 0;
    uint8_t *field = esp_ble_resolve_adv_data(adv_data, ad_type, &field_len);
    for (int i = 0; field && i + 1 < (int)field_len; i += 2) {
        uint16_t uuid = (uint16_t)field[i] | ((uint16_t)field[i + 1] << 8);
        if (uuid == BLE_SVC_HID_UUID16) return true;
    }
    return false;
}

/* Check whether a 128-bit UUID list (AD type) contains the HID service UUID */
static bool adv_has_hid_uuid128(uint8_t *adv_data, uint8_t adv_len,
                                 uint8_t ad_type)
{
    uint8_t field_len = 0;
    uint8_t *field = esp_ble_resolve_adv_data(adv_data, ad_type, &field_len);
    for (int i = 0; field && i + 15 < (int)field_len; i += 16) {
        if (memcmp(&field[i], BLE_SVC_HID_UUID128, 16) == 0) return true;
    }
    return false;
}

static bool adv_is_hid_keyboard(uint8_t *adv_data, uint8_t adv_len)
{
    uint8_t *field;
    uint8_t field_len;

    /* Check appearance for keyboard (0x03C1) or generic HID (0x03C0) */
    field = esp_ble_resolve_adv_data(adv_data,
                                     ESP_BLE_AD_TYPE_APPEARANCE, &field_len);
    if (field && field_len >= 2) {
        uint16_t appearance = (uint16_t)field[0] |
                              ((uint16_t)field[1] << 8);
        if (appearance == BLE_APPEARANCE_KEYBOARD ||
            appearance == BLE_APPEARANCE_HID_GENERIC) return true;
    }

    /* Check 16-bit service UUID lists (complete, incomplete, solicitation) */
    if (adv_has_hid_uuid16(adv_data, adv_len,
                           ESP_BLE_AD_TYPE_16SRV_CMPL)) return true;
    if (adv_has_hid_uuid16(adv_data, adv_len,
                           ESP_BLE_AD_TYPE_16SRV_PART)) return true;
    if (adv_has_hid_uuid16(adv_data, adv_len,
                           ESP_BLE_AD_TYPE_SOL_SRV_UUID)) return true;

    /* Check 128-bit service UUID lists (complete, incomplete) */
    if (adv_has_hid_uuid128(adv_data, adv_len,
                            ESP_BLE_AD_TYPE_128SRV_CMPL)) return true;
    if (adv_has_hid_uuid128(adv_data, adv_len,
                            ESP_BLE_AD_TYPE_128SRV_PART)) return true;

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

/* ---- Reconnection to known devices ---- */

/* Try to directly connect to a known bonded device (no scan needed) */
static void try_connect_bonded(int idx)
{
    if (idx < 0 || idx >= s_bonded_count) return;
    s_connecting = true;
    memcpy(s_target_bda, s_bonded[idx].bda, 6);
    s_target_addr_type = s_bonded[idx].addr_type;
    snprintf(s_dev_name, sizeof(s_dev_name), "(bonded #%d)", idx);
    ESP_LOGI(TAG, "Trying bonded device %d "
             "(%02x:%02x:%02x:%02x:%02x:%02x)...",
             idx,
             s_target_bda[0], s_target_bda[1], s_target_bda[2],
             s_target_bda[3], s_target_bda[4], s_target_bda[5]);
    xTaskCreate(connect_task, "ble_connect", 4096, NULL, 2, NULL);
}

static void start_reconnection(void)
{
    if (s_connected || s_connecting) return;

    switch (s_reconn_phase) {
    case RECONN_LAST:
        if (s_last_bonded >= 0 && s_last_bonded < s_bonded_count) {
            ESP_LOGI(TAG, "Reconnect phase: trying last-connected");
            s_reconn_phase = RECONN_KNOWN;
            s_reconn_idx   = 0;
            try_connect_bonded(s_last_bonded);
            return;
        }
        s_reconn_phase = RECONN_KNOWN;
        s_reconn_idx   = 0;
        /* fall through */

    case RECONN_KNOWN:
        while (s_reconn_idx < s_bonded_count) {
            int idx = s_reconn_idx++;
            if (idx == s_last_bonded) continue; /* already tried */
            ESP_LOGI(TAG, "Reconnect phase: trying known device %d", idx);
            try_connect_bonded(idx);
            return;
        }
        s_reconn_phase = RECONN_SCAN;
        /* fall through */

    case RECONN_SCAN:
        ESP_LOGI(TAG, "Reconnect phase: scanning for new keyboards");
        ble_keyboard_start_scan();
        break;
    }
}

/* ---- Scan retry timer callback ---- */

static void scan_timer_cb(TimerHandle_t timer)
{
    (void)timer;
    if (!s_connected && !s_connecting) {
        /* Reset reconnection to try all phases again */
        s_reconn_phase = RECONN_LAST;
        s_reconn_idx   = 0;
        start_reconnection();
    }
}

static void reconn_timer_cb(TimerHandle_t timer)
{
    (void)timer;
    start_reconnection();
}

/* ---- BLE GAP callback ---- */

static void gap_event_handler(esp_gap_ble_cb_event_t event,
                               esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        if (param->scan_param_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "Scan parameters set");
            s_scan_params_ready = true;
            /* Now that scan params are ready, start the reconnection
             * sequence (which may trigger a scan). */
            start_reconnection();
        } else {
            ESP_LOGE(TAG, "Scan param set failed: 0x%x",
                     param->scan_param_cmpl.status);
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

            /* Extract device name for logging */
            char tmp_name[64] = "";
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
                if (len > (int)sizeof(tmp_name) - 1)
                    len = (int)sizeof(tmp_name) - 1;
                memcpy(tmp_name, name, (size_t)len);
                tmp_name[len] = '\0';
            }

            if (adv_is_hid_keyboard(scan->ble_adv, total_len)) {
                if (tmp_name[0]) {
                    strncpy(s_dev_name, tmp_name, sizeof(s_dev_name) - 1);
                    s_dev_name[sizeof(s_dev_name) - 1] = '\0';
                }

                /* Prioritize already-bonded devices during scan */
                bool is_known = (bonded_find(scan->bda) >= 0);
                if (!is_known) {
                    ESP_LOGI(TAG, "New keyboard found: \"%s\" "
                             "(%02x:%02x:%02x:%02x:%02x:%02x)",
                             s_dev_name,
                             scan->bda[0], scan->bda[1], scan->bda[2],
                             scan->bda[3], scan->bda[4], scan->bda[5]);
                } else {
                    ESP_LOGI(TAG, "Known keyboard found: \"%s\" "
                             "(%02x:%02x:%02x:%02x:%02x:%02x)",
                             s_dev_name,
                             scan->bda[0], scan->bda[1], scan->bda[2],
                             scan->bda[3], scan->bda[4], scan->bda[5]);
                }

                /* Save target address and stop scanning */
                s_connecting = true;
                memcpy(s_target_bda, scan->bda, sizeof(esp_bd_addr_t));
                s_target_addr_type = scan->ble_addr_type;
                esp_ble_gap_stop_scanning();
            } else {
                /* Log non-HID devices at DEBUG level for diagnostics */
                ESP_LOGD(TAG, "Skipping non-HID device: \"%s\" "
                         "(%02x:%02x:%02x:%02x:%02x:%02x) "
                         "adv=%d rsp=%d",
                         tmp_name,
                         scan->bda[0], scan->bda[1], scan->bda[2],
                         scan->bda[3], scan->bda[4], scan->bda[5],
                         scan->adv_data_len, scan->scan_rsp_len);
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

    case ESP_GAP_BLE_PASSKEY_NOTIF_EVT: {
        /* We are DisplayOnly -- the stack generated a passkey for us
         * to show.  The user types this number on the keyboard. */
        uint32_t passkey = param->ble_security.key_notif.passkey;
        ESP_LOGI(TAG, "Passkey notification: %06lu",
                 (unsigned long)passkey);
        if (s_passkey_cb) {
            s_passkey_cb(passkey);
        }
        break;
    }

    case ESP_GAP_BLE_AUTH_CMPL_EVT:
        if (param->ble_security.auth_cmpl.success) {
            ESP_LOGI(TAG, "BLE authentication success");
            /* Dismiss passkey overlay */
            if (s_passkey_cb) {
                s_passkey_cb(BLE_PASSKEY_DISMISS);
            }
        } else {
            ESP_LOGE(TAG, "BLE authentication failed, reason: 0x%x",
                     param->ble_security.auth_cmpl.fail_reason);
            if (s_passkey_cb) {
                s_passkey_cb(BLE_PASSKEY_DISMISS);
            }
        }
        break;

    default:
        break;
    }
}

/* ---- Public API ---- */

extern "C" void ble_keyboard_init(void)
{
    /* Load bonded device list from NVS */
    bonded_load();

    /* Release Classic BT memory (ESP32-S3 is BLE-only) */
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    /* Initialize BT controller in BLE mode */
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));

    /* Initialize Bluedroid */
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    /* Set BLE security parameters.
     * IO capability = DisplayOnly so the stack generates a random
     * 6-digit passkey for each pairing attempt and delivers it via
     * ESP_GAP_BLE_PASSKEY_NOTIF_EVT.  The user types this number on
     * the keyboard to complete pairing.  MITM flag is set so the
     * stack actually uses the passkey entry protocol. */
    esp_ble_auth_req_t auth_req = ESP_LE_AUTH_REQ_SC_MITM_BOND;
    esp_ble_io_cap_t io_cap = ESP_IO_CAP_OUT;
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
    scan_params.scan_duplicate     = BLE_SCAN_DUPLICATE_DISABLE;
    esp_ble_gap_set_scan_params(&scan_params);

    /* Initialize HID Host (esp_hid component) */
    esp_hidh_config_t hidh_cfg = {};
    hidh_cfg.callback = hidh_callback;
    hidh_cfg.event_stack_size = 4096;
    ESP_ERROR_CHECK(esp_hidh_init(&hidh_cfg));

    /* Create scan retry timer (one-shot, 2-second delay) */
    s_scan_timer = xTimerCreate("scan_retry", pdMS_TO_TICKS(2000),
                                pdFALSE, NULL, scan_timer_cb);

    /* Create reconnection timer (one-shot, 1-second delay) */
    s_reconn_timer = xTimerCreate("reconn", pdMS_TO_TICKS(1000),
                                  pdFALSE, NULL, reconn_timer_cb);

    /* Reconnection will be started from the SCAN_PARAM_SET_COMPLETE_EVT
     * callback once scan parameters are ready. */
    s_reconn_phase = RECONN_LAST;
    s_reconn_idx   = 0;

    ESP_LOGI(TAG, "BLE HID keyboard host initialized");
}

extern "C" void ble_keyboard_set_callback(kb_event_callback_t callback)
{
    s_callback = callback;
}

extern "C" void ble_keyboard_set_passkey_callback(ble_passkey_cb_t cb)
{
    s_passkey_cb = cb;
}

extern "C" void ble_keyboard_set_connect_callback(ble_connect_cb_t cb)
{
    s_connect_cb = cb;
}

extern "C" bool ble_keyboard_is_connected(void)
{
    return s_connected;
}

extern "C" void ble_keyboard_start_scan(void)
{
    if (!s_connected && !s_connecting) {
        if (!s_scan_params_ready) {
            ESP_LOGW(TAG, "Scan params not ready yet, deferring scan");
            return;
        }
        esp_err_t err = esp_ble_gap_start_scanning(SCAN_DURATION_SEC);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ble_gap_start_scanning failed: %s",
                     esp_err_to_name(err));
        }
    }
}

extern "C" const char *ble_keyboard_get_device_name(void)
{
    return s_dev_name;
}

extern "C" int ble_keyboard_get_battery_level(void)
{
    return s_battery_level;
}
