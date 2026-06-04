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
 *
 * Targets without a usable Bluetooth controller (e.g. ESP32-P4 on
 * the M5Stack Tab5, where BLE is only available via the on-board
 * ESP32-C6 co-processor through ESP-Hosted and not natively) are
 * compiled to no-op stubs at the bottom of this file. They link
 * the same public API so Draftling startup code can call
 * ble_keyboard_init() unconditionally.
 */

#include "sdkconfig.h"
#include "ble_keyboard.h"

#if !defined(CONFIG_BT_ENABLED)

#include <esp_log.h>

extern "C" {

static const char *TAG_STUB = "BLEKeyboard";

void ble_keyboard_init(void)
{
    ESP_LOGW(TAG_STUB, "BT controller not enabled in sdkconfig; "
                       "BLE keyboard support is disabled on this build.");
}
void ble_keyboard_set_callback(kb_event_callback_t /*cb*/)               {}
void ble_keyboard_set_passkey_callback(ble_passkey_cb_t /*cb*/)          {}
void ble_keyboard_set_connect_callback(ble_connect_cb_t /*cb*/)          {}
void ble_keyboard_set_status_text_callback(ble_status_text_cb_t /*cb*/)  {}
bool ble_keyboard_is_connected(void)                                     { return false; }
void ble_keyboard_start_scan(void)                                       {}
const char *ble_keyboard_get_device_name(void)                           { return ""; }
int ble_keyboard_get_battery_level(void)                                 { return -1; }

} /* extern "C" */

#else /* CONFIG_BT_ENABLED */

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <esp_log.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_gap_ble_api.h>
#include <esp_gattc_api.h>
#include <esp_hidh.h>
#include <nvs_flash.h>
#include <nvs.h>

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

/* Safety-net timer period (ms).  Fires periodically until scanning
 * starts or a connection is established. */
#define STARTUP_SAFETY_TIMER_MS   3000

/* Stack size for the BLE connect task.  Needs room for
 * esp_hidh_dev_open() which uses significant stack space. */
#define CONNECT_TASK_STACK        6144

/* Stack size for the BLE init task.  Runs the full BT stack
 * bring-up including esp_hidh_init() which blocks on a BTC
 * semaphore -- must not run in app_main. */
#define BLE_INIT_TASK_STACK       8192


/* Maximum number of bonded keyboards stored in NVS */
#define MAX_BONDED  8

/* NVS namespace and keys */
#define NVS_NAMESPACE   "ble_kb"
#define NVS_KEY_COUNT   "bond_cnt"
#define NVS_KEY_LAST    "bond_last"
/* Per-device keys: "bond_0" .. "bond_7", each stores 7 bytes
 * (6-byte BDA + 1-byte address type).
 * Per-device name keys: "name_0" .. "name_7", each stores a
 * NUL-terminated device name string (up to 63 chars). */

/* ---- Bonded device storage ---- */

#define BONDED_NAME_LEN 64

typedef struct {
    esp_bd_addr_t       bda;
    esp_ble_addr_type_t addr_type;
    char                name[BONDED_NAME_LEN];
} bonded_dev_t;

static bonded_dev_t s_bonded[MAX_BONDED];
static int          s_bonded_count = 0;
static int          s_last_bonded  = -1; /* index into s_bonded */

/* Forward-declare status text callback (full declaration with other callbacks below) */
static ble_status_text_cb_t s_status_text_cb = NULL;

/* Device name -- declared here (before bonded_add) so it is visible
 * throughout the file.  Sized to match BONDED_NAME_LEN. */
static char s_dev_name[BONDED_NAME_LEN] = "";

/* Helper: notify the UI of a human-readable status message */
static void notify_status(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));
static void notify_status(const char *fmt, ...)
{
    if (!s_status_text_cb) return;
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    s_status_text_cb(buf);
}

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
            /* Load stored device name (if any) */
            char name_key[12];
            snprintf(name_key, sizeof(name_key), "name_%d", i);
            size_t nlen = sizeof(s_bonded[s_bonded_count].name);
            if (nvs_get_str(h, name_key,
                            s_bonded[s_bonded_count].name, &nlen) != ESP_OK) {
                s_bonded[s_bonded_count].name[0] = '\0';
            }
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
        /* Save device name */
        char name_key[12];
        snprintf(name_key, sizeof(name_key), "name_%d", i);
        if (s_bonded[i].name[0]) {
            nvs_set_str(h, name_key, s_bonded[i].name);
        } else {
            nvs_erase_key(h, name_key); /* no name stored */
        }
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
        s_bonded[idx].name[0] = '\0';
        s_bonded_count++;
    }
    /* Update stored name from the current device name */
    if (s_dev_name[0]) {
        /* Use snprintf to copy with guaranteed NUL termination and
         * without tripping GCC's -Wstringop-truncation, which fires on
         * the strncpy(dst, src, sizeof(dst)-1) idiom under newer
         * toolchains (seen with ESP-IDF 5.5.4). */
        snprintf(s_bonded[idx].name, sizeof(s_bonded[idx].name),
                 "%s", s_dev_name);
    }
    s_last_bonded = idx;
    bonded_save();
    ESP_LOGI(TAG, "Bonded device saved at index %d (\"%s\")",
             idx, s_bonded[idx].name);
}

/* ---- State ---- */

static kb_event_callback_t  s_callback = NULL;
static ble_passkey_cb_t     s_passkey_cb = NULL;
static ble_connect_cb_t     s_connect_cb = NULL;
static volatile bool s_connected  = false;
static volatile bool s_connecting = false;
static volatile bool s_scan_params_ready = false;
static volatile bool s_hidh_ready = false;
/* Maximum number of keycode slots in an extended boot / key-array
 * report.  Most NKRO key-array keyboards use up to 15 slots. */
#define MAX_KEY_SLOTS 20
static uint8_t s_prev_keys[MAX_KEY_SLOTS] = {0};
static int     s_prev_key_count = 0;
/* Previous full report buffer for NKRO bitmap comparison */
static uint8_t s_prev_report[32] = {0};
static int     s_prev_report_len = 0;

/* Detected keyboard report format (sticky after first detection).
 * REPORT_FMT_UNKNOWN  -- not yet determined for this connection
 * REPORT_FMT_BOOT     -- standard boot protocol (8 bytes, reserved byte)
 * REPORT_FMT_BOOT_EXT -- extended boot / key-array (variable length)
 * REPORT_FMT_NKRO_BM  -- NKRO bitmap
 */
typedef enum {
    REPORT_FMT_UNKNOWN = 0,
    REPORT_FMT_BOOT,
    REPORT_FMT_BOOT_EXT,
    REPORT_FMT_NKRO_BM,
} report_fmt_t;
static report_fmt_t s_report_fmt = REPORT_FMT_UNKNOWN;

static volatile int s_battery_level = -1;  /* -1 = unknown */

/* Active HIDH device handle (valid while connected) */
static esp_hidh_dev_t *s_hidh_dev = NULL;

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

/* Periodic startup timer: safety net that re-registers the GAP
 * callback and retries scanning until connected. */
static TimerHandle_t  s_startup_timer = NULL;

/* Forward declarations */
static void start_reconnection(void);
static void reconn_timer_cb(TimerHandle_t timer);
static void startup_timer_cb(TimerHandle_t timer);
static void gap_event_handler(esp_gap_ble_cb_event_t event,
                               esp_ble_gap_cb_param_t *param);
static void hidh_callback(void *handler_args, esp_event_base_t base,
                           int32_t id, void *event_data);

/* Helper: fill in standard BLE scan parameters */
static void fill_scan_params(esp_ble_scan_params_t *p)
{
    memset(p, 0, sizeof(*p));
    p->scan_type          = BLE_SCAN_TYPE_ACTIVE;
    p->own_addr_type      = BLE_ADDR_TYPE_PUBLIC;
    p->scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL;
    p->scan_interval      = 0x0060;  /* 60 ms */
    p->scan_window        = 0x0030;  /* 30 ms */
    p->scan_duplicate     = BLE_SCAN_DUPLICATE_DISABLE;
}

static bool key_in_report(uint8_t key, const uint8_t *keys, int count)
{
    for (int i = 0; i < count; i++) {
        if (keys[i] == key) return true;
    }
    return false;
}

/* Dispatch a single key event to the registered callback */
static void dispatch_key(uint8_t modifier, uint8_t keycode, bool pressed)
{
    if (!s_callback) return;
    kb_event_t ev = {};
    ev.modifier  = modifier;
    ev.keycode   = keycode;
    ev.character = 0;
    ev.pressed   = pressed;
    ESP_LOGD(TAG, "Key %s: kc=0x%02x mod=0x%02x",
             pressed ? "DOWN" : "UP", keycode, modifier);
    s_callback(&ev);
}

/* Standard boot-protocol keyboard report (or extended key-array).
 * Boot protocol: [0]=modifier, [1]=reserved(0), [2..N]=keycodes
 * Key-array (no reserved byte): [0]=modifier, [1..N]=keycodes
 * The caller provides a pointer to the key array and its count. */
static void process_key_array(const uint8_t *keys, int key_count,
                              uint8_t modifier)
{
    if (key_count > MAX_KEY_SLOTS) key_count = MAX_KEY_SLOTS;

    /* Detect newly pressed keys */
    for (int i = 0; i < key_count; i++) {
        uint8_t kc = keys[i];
        if (kc == 0) continue;
        if (!key_in_report(kc, s_prev_keys, s_prev_key_count)) {
            dispatch_key(modifier, kc, true);
        }
    }

    /* Detect released keys */
    for (int i = 0; i < s_prev_key_count; i++) {
        uint8_t kc = s_prev_keys[i];
        if (kc == 0) continue;
        if (!key_in_report(kc, keys, key_count)) {
            dispatch_key(modifier, kc, false);
        }
    }

    memset(s_prev_keys, 0, sizeof(s_prev_keys));
    int copy_count = key_count < MAX_KEY_SLOTS ? key_count : MAX_KEY_SLOTS;
    memcpy(s_prev_keys, keys, (size_t)copy_count);
    s_prev_key_count = copy_count;
}

/* Legacy wrapper: standard boot protocol [mod, reserved, k0..k5] */
static void process_boot_report(const uint8_t *data, int len)
{
    uint8_t modifier = data[0];
    const uint8_t *keys = &data[2]; /* skip reserved byte */
    int key_count = len - 2;
    if (key_count < 0) key_count = 0;
    process_key_array(keys, key_count, modifier);
}

/* NKRO bitmap keyboard report: modifier + N bitmap bytes.
 * Each bit in the bitmap represents a keycode (bit 0 of byte 1 =
 * keycode 0, bit 7 of byte 1 = keycode 7, etc.).
 * Compare with previous bitmap to detect press/release. */
static void process_nkro_report(const uint8_t *data, int len)
{
    uint8_t modifier = data[0];
    /* Bitmap bytes start at data[1], up to data[len-1] */
    int bm_len = len - 1;
    const uint8_t *bm = &data[1];

    /* Previous bitmap starts at s_prev_report[1] */
    int prev_bm_len = s_prev_report_len > 0 ? s_prev_report_len - 1 : 0;
    const uint8_t *prev_bm = &s_prev_report[1];

    /* Scan for changed bits */
    int max_bm = bm_len > prev_bm_len ? bm_len : prev_bm_len;
    if (max_bm > 30) max_bm = 30;  /* safety cap: 240 keycodes */

    for (int i = 0; i < max_bm; i++) {
        uint8_t cur  = (i < bm_len)     ? bm[i] : 0;
        uint8_t prev = (i < prev_bm_len) ? prev_bm[i] : 0;
        uint8_t diff = cur ^ prev;
        if (diff == 0) continue;

        for (int bit = 0; bit < 8; bit++) {
            if (diff & (1 << bit)) {
                uint8_t keycode = (uint8_t)(i * 8 + bit);
                /* HID keycodes 0-3 are reserved (ErrorRollOver,
                 * POSTFail, ErrorUndefined).  Valid keys start
                 * at 0x04 (KC_A). */
                if (keycode < 0x04) continue;
                bool pressed = (cur & (1 << bit)) != 0;
                dispatch_key(modifier, keycode, pressed);
            }
        }
    }

    /* Save current report for next comparison */
    int save_len = len;
    if (save_len > (int)sizeof(s_prev_report))
        save_len = (int)sizeof(s_prev_report);
    memcpy(s_prev_report, data, (size_t)save_len);
    s_prev_report_len = save_len;
}

static void process_keyboard_report(const uint8_t *data, int len,
                                    uint8_t report_id)
{
    if (len < 3 || !s_callback) return;

    /* Log raw report bytes for diagnosis */
    {
        char hex[96];
        int pos = 0;
        int dump_len = len > 30 ? 30 : len;
        for (int i = 0; i < dump_len && pos < (int)sizeof(hex) - 4; i++) {
            pos += snprintf(hex + pos, sizeof(hex) - (size_t)pos,
                            "%02x ", data[i]);
        }
        ESP_LOGD(TAG, "Report (%d bytes, id=%d): %s", len, report_id, hex);
    }

    /* Determine report format.
     *
     * Standard boot-protocol keyboard: 8 bytes.
     *   [0]=modifier  [1]=reserved(0)  [2..7]=keycodes (6 slots)
     *
     * Extended boot protocol (some keyboards): >8 bytes with
     *   the same layout, just more keycode slots.
     *   [0]=modifier  [1]=reserved(0)  [2..N]=keycodes
     *
     * Key-array without reserved byte (NKRO key-array): variable.
     *   [0]=modifier  [1..N]=keycodes  (no reserved byte)
     *
     * NKRO bitmap: modifier + N bitmap bytes where each BIT is a
     *   keycode.
     *
     * Format detection uses a "sticky" approach: once a format is
     * identified for this connection, it is reused for all subsequent
     * reports.  This is checked FIRST to avoid false positives where
     * modifier byte values (e.g. 0x01=LCtrl) coincide with report
     * IDs or reserved-byte patterns. */

    /* ---- Sticky format: use previously detected format ---- */
    switch (s_report_fmt) {
    case REPORT_FMT_BOOT:
        ESP_LOGD(TAG, "Boot protocol (sticky, %d bytes)", len);
        process_boot_report(data, len);
        return;
    case REPORT_FMT_BOOT_EXT:
        ESP_LOGD(TAG, "Key-array (sticky, %d bytes)", len);
        process_key_array(&data[1], len - 1, data[0]);
        return;
    case REPORT_FMT_NKRO_BM:
        ESP_LOGD(TAG, "NKRO bitmap (sticky, %d bytes)", len);
        process_nkro_report(data, len);
        return;
    case REPORT_FMT_UNKNOWN:
    default:
        break;  /* fall through to first-time detection */
    }

    /* ---- First-time format detection ---- */

    /* Case: exactly 8 bytes with reserved byte == 0 -> boot protocol */
    if (len == 8 && data[1] == 0) {
        s_report_fmt = REPORT_FMT_BOOT;
        ESP_LOGI(TAG, "Detected boot protocol (8 bytes)");
        process_boot_report(data, len);
        return;
    }

    /* Case: extended boot protocol (reserved byte present, > 8 bytes).
     * data[1]==0 is the reserved byte that distinguishes boot-style
     * reports from key-array / NKRO bitmap formats. */
    if (data[1] == 0 && len > 8) {
        s_report_fmt = REPORT_FMT_BOOT_EXT;
        ESP_LOGI(TAG, "Detected extended boot protocol (%d bytes)", len);
        process_boot_report(data, len);
        return;
    }

    /* For reports where byte[1] != 0 we must distinguish between
     * key-array and NKRO bitmap.
     *
     * Key insight: in a key-array, each non-zero byte IS a HID keycode
     * (0x04..0xE7).  In a bitmap, each byte is a bit-field where
     * individual bits represent keycodes.
     *
     * Heuristic: if any payload byte is 0x01-0x03, those are reserved
     * HID keycodes that would never appear in a key-array, so it must
     * be a bitmap.  Otherwise, check if any non-zero byte has multiple
     * bits set AND is a plausible keycode (>= 0x04).  In a bitmap,
     * having multiple bits set in one byte means multiple keys in the
     * same 8-keycode group are pressed simultaneously -- uncommon for
     * a first-report heuristic.  A key-array byte for common keys
     * (like 0x11=N) naturally has multiple bits set.
     *
     * If all non-zero bytes have exactly 1 bit set, it is ambiguous.
     * Default to NKRO bitmap to preserve backward compatibility. */
    {
        bool has_low_value = false;  /* any byte 0x01-0x03 */
        bool has_multi_bit = false;  /* any byte with >1 bit set */
        for (int i = 1; i < len; i++) {
            uint8_t b = data[i];
            if (b == 0) continue;
            if (b >= 0x01 && b <= 0x03) { has_low_value = true; break; }
            /* __builtin_popcount counts set bits */
            if (__builtin_popcount(b) > 1) has_multi_bit = true;
        }
        if (has_low_value) {
            /* Definitely bitmap: 0x01-0x03 are impossible as keycodes */
            s_report_fmt = REPORT_FMT_NKRO_BM;
            ESP_LOGI(TAG, "Detected NKRO bitmap format (%d bytes)", len);
            process_nkro_report(data, len);
            return;
        }
        if (has_multi_bit) {
            /* Byte like 0x11 (N) has bits 0 and 4 set -- common as a
             * keycode value but very unusual as a bitmap byte (would
             * need keycodes 0 and 4 pressed simultaneously).  Treat
             * as key-array. */
            s_report_fmt = REPORT_FMT_BOOT_EXT;
            ESP_LOGI(TAG, "Detected key-array format (%d bytes)", len);
            uint8_t modifier = data[0];
            process_key_array(&data[1], len - 1, modifier);
            return;
        }
        /* Ambiguous: all non-zero bytes are power-of-two values >= 4.
         * Default to NKRO bitmap for backward compatibility. */
        s_report_fmt = REPORT_FMT_NKRO_BM;
        ESP_LOGI(TAG, "Assuming NKRO bitmap format (%d bytes)", len);
        process_nkro_report(data, len);
    }
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
            s_hidh_dev   = param->open.dev;
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

            /* Proactively initiate BLE encryption.  HID keyboards
             * require an encrypted link for notifications to flow.
             * Without this, CCCD writes during HIDH open may fail
             * with "insufficient authentication" and the keyboard
             * will never send input reports.
             *
             * If the link is already encrypted (re-connection with
             * stored bonding keys), this call returns immediately. */
            if (addr) {
                esp_bd_addr_t enc_addr;
                memcpy(enc_addr, addr, sizeof(esp_bd_addr_t));
                esp_err_t enc_err = esp_ble_set_encryption(
                    enc_addr, ESP_BLE_SEC_ENCRYPT_MITM);
                ESP_LOGI(TAG, "Encryption requested: %s",
                         esp_err_to_name(enc_err));
            }

            if (s_connect_cb) s_connect_cb(true);
        } else {
            ESP_LOGE(TAG, "HID open failed: %d", param->open.status);
            notify_status("Connection failed, retrying...");
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
        s_hidh_dev   = NULL;
        s_battery_level = -1;
        memset(s_prev_keys, 0, sizeof(s_prev_keys));
        s_prev_key_count = 0;
        s_report_fmt = REPORT_FMT_UNKNOWN;
        memset(s_prev_report, 0, sizeof(s_prev_report));
        s_prev_report_len = 0;
        s_dev_name[0] = '\0';
        ESP_LOGI(TAG, "HID device disconnected, reconnecting...");
        if (s_connect_cb) s_connect_cb(false);
        /* Reset reconnection to start with last-known device */
        s_reconn_phase = RECONN_LAST;
        s_reconn_idx   = 0;
        start_reconnection();
        break;

    case ESP_HIDH_INPUT_EVENT: {
        ESP_LOGD(TAG, "INPUT: usage=%d, map=%d, id=%d, len=%d",
                 (int)param->input.usage,
                 (int)param->input.map_index,
                 (int)param->input.report_id,
                 (int)param->input.length);
        /* Keyboard report: standard boot protocol (8 bytes) or
         * NKRO bitmap (variable length, typically >8 bytes).
         * process_keyboard_report handles both formats. */
        if (param->input.usage == ESP_HID_USAGE_KEYBOARD &&
            param->input.length >= 3) {
            process_keyboard_report(param->input.data,
                                    (int)param->input.length,
                                    (uint8_t)param->input.report_id);
        }
        break;
    }

    case ESP_HIDH_BATTERY_EVENT:
        s_battery_level = param->battery.level;
        ESP_LOGI(TAG, "Battery: %d%%", param->battery.level);
        break;

    default:
        ESP_LOGI(TAG, "HIDH event: %d", (int)event);
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

/* ---- HIDH readiness check ---- */

/* HIDH is initialized during ble_init_task() before scanning starts.
 * This helper is kept so connect_task can verify init succeeded. */
static inline bool is_hidh_ready(void)
{
    return s_hidh_ready;
}

/* ---- Connect task (runs esp_hidh_dev_open which blocks) ---- */

/* Maximum number of open-connection retries.  esp_hidh_dev_open()
 * can fail transiently if the remote device is slow to respond.
 * A short retry loop handles this.  Keep this low (2) so that
 * re-pairing scenarios (where the old address is unreachable) do
 * not block for too long before scanning for the new address. */
#define CONNECT_RETRIES     1
#define CONNECT_RETRY_MS  250

static void connect_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Connecting to keyboard \"%s\" "
             "(%02x:%02x:%02x:%02x:%02x:%02x)...",
             s_dev_name,
             s_target_bda[0], s_target_bda[1], s_target_bda[2],
             s_target_bda[3], s_target_bda[4], s_target_bda[5]);
    notify_status("Connecting to %s...", s_dev_name);

    /* HIDH was initialized during ble_init_task().  If it failed,
     * we cannot connect. */
    if (!is_hidh_ready()) {
        ESP_LOGE(TAG, "HIDH not available, cannot connect");
        notify_status("BLE HID not ready, retrying...");
        s_connecting = false;
        if (s_reconn_timer) {
            xTimerStart(s_reconn_timer, 0);
        }
        vTaskDelete(NULL);
        return;
    }

    esp_hidh_dev_t *dev = NULL;
    for (int attempt = 0; attempt < CONNECT_RETRIES; attempt++) {
        dev = esp_hidh_dev_open(s_target_bda,
                                ESP_HID_TRANSPORT_BLE,
                                s_target_addr_type);
        if (dev) break;
        ESP_LOGW(TAG, "esp_hidh_dev_open attempt %d/%d failed, "
                 "retrying in %d ms...",
                 attempt + 1, CONNECT_RETRIES, CONNECT_RETRY_MS);
        notify_status("Connect attempt %d/%d failed,\nretrying...",
                      attempt + 1, CONNECT_RETRIES);
        vTaskDelay(pdMS_TO_TICKS(CONNECT_RETRY_MS));
    }

    if (!dev) {
        ESP_LOGE(TAG, "esp_hidh_dev_open failed after %d attempts "
                 "(GATT client may not be registered)",
                 CONNECT_RETRIES);
        notify_status("Connection failed, retrying...");
        s_connecting = false;
        /* Retry reconnection after a delay */
        if (s_reconn_timer) {
            xTimerStart(s_reconn_timer, 0);
        }
    }
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
    /* Use stored device name, falling back to BDA if unknown */
    if (s_bonded[idx].name[0]) {
        strncpy(s_dev_name, s_bonded[idx].name, sizeof(s_dev_name) - 1);
        s_dev_name[sizeof(s_dev_name) - 1] = '\0';
    } else {
        snprintf(s_dev_name, sizeof(s_dev_name),
                 "%02x:%02x:%02x:%02x:%02x:%02x",
                 s_target_bda[0], s_target_bda[1], s_target_bda[2],
                 s_target_bda[3], s_target_bda[4], s_target_bda[5]);
    }
    ESP_LOGI(TAG, "Trying bonded device %d "
             "(%02x:%02x:%02x:%02x:%02x:%02x)...",
             idx,
             s_target_bda[0], s_target_bda[1], s_target_bda[2],
             s_target_bda[3], s_target_bda[4], s_target_bda[5]);
    xTaskCreate(connect_task, "ble_connect", CONNECT_TASK_STACK,
                NULL, 2, NULL);
}

static void start_reconnection(void)
{
    if (s_connected || s_connecting) return;

    switch (s_reconn_phase) {
    case RECONN_LAST:
        if (s_last_bonded >= 0 && s_last_bonded < s_bonded_count) {
            ESP_LOGI(TAG, "Reconnect phase: trying last-connected");
            int li = s_last_bonded;
            const char *lname = s_bonded[li].name[0]
                                ? s_bonded[li].name : "last keyboard";
            notify_status("Trying %s...", lname);
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
            const char *kname = s_bonded[idx].name[0]
                                ? s_bonded[idx].name : "known keyboard";
            notify_status("Trying %s...", kname);
            try_connect_bonded(idx);
            return;
        }
        s_reconn_phase = RECONN_SCAN;
        /* fall through */

    case RECONN_SCAN:
        ESP_LOGI(TAG, "Reconnect phase: scanning for new keyboards");
        notify_status("Scanning for keyboards...");
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

/* Safety-net timer: fires periodically after init until scanning
 * starts or a connection is established.  Handles the (unlikely) case
 * where the initial set_scan_params or start_scanning fails silently. */
static void startup_timer_cb(TimerHandle_t timer)
{
    (void)timer;
    if (s_connected || s_connecting) {
        /* Goal achieved -- stop the periodic timer */
        xTimerStop(s_startup_timer, 0);
        return;
    }

    ESP_LOGW(TAG, "Safety-net timer fired (scan_params_ready=%d)",
             (int)s_scan_params_ready);

    /* Re-register our GAP callback as a precaution. */
    esp_ble_gap_register_callback(gap_event_handler);

    if (!s_scan_params_ready) {
        ESP_LOGW(TAG, "Scan params not ready; re-sending");
        esp_ble_scan_params_t scan_params;
        fill_scan_params(&scan_params);
        esp_err_t err = esp_ble_gap_set_scan_params(&scan_params);
        if (err == ESP_OK) {
            /* The call succeeded (queued to controller).  Force params
             * ready -- the completion event will arrive at our GAP
             * callback and confirm, but we proceed immediately to avoid
             * another 3-second wait. */
            s_scan_params_ready = true;
        } else {
            ESP_LOGE(TAG, "Re-set scan params failed: %s",
                     esp_err_to_name(err));
            /* Don't force ready; next timer tick will retry */
            return;
        }
    }

    s_reconn_phase = RECONN_LAST;
    s_reconn_idx   = 0;
    start_reconnection();
}

/* ---- BLE GAP callback ---- */

static void gap_event_handler(esp_gap_ble_cb_event_t event,
                               esp_ble_gap_cb_param_t *param)
{
    ESP_LOGI(TAG, "GAP event: %d", (int)event);

    switch (event) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        if (param->scan_param_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "Scan parameters set");
            s_scan_params_ready = true;
            /* Scan params configured via normal path -- stop safety-net */
            if (s_startup_timer) {
                xTimerStop(s_startup_timer, 0);
            }
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
            /* Scanning started -- stop the periodic safety-net timer */
            if (s_startup_timer) {
                xTimerStop(s_startup_timer, 0);
            }
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
                notify_status("Found: %s\nConnecting...", s_dev_name);
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
                notify_status("No keyboard found.\nRetrying...");
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
            xTaskCreate(connect_task, "ble_connect", CONNECT_TASK_STACK,
                        NULL, 2, NULL);
        }
        break;

    case ESP_GAP_BLE_SEC_REQ_EVT:
        ESP_LOGI(TAG, "Security request from peer");
        esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
        break;

    case ESP_GAP_BLE_KEY_EVT:
        ESP_LOGI(TAG, "BLE key exchange, type=%d",
                 (int)param->ble_security.ble_key.key_type);
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

    case ESP_GAP_BLE_NC_REQ_EVT: {
        /* Numeric Comparison -- used by some keyboards with Secure
         * Connections.  Show the number on the display and auto-accept.
         * The user verifies the same number is shown on the keyboard. */
        uint32_t nc_num = param->ble_security.key_notif.passkey;
        ESP_LOGI(TAG, "Numeric comparison: %06lu",
                 (unsigned long)nc_num);
        if (s_passkey_cb) {
            s_passkey_cb(nc_num);
        }
        esp_ble_confirm_reply(param->ble_security.key_notif.bd_addr, true);
        break;
    }

    case ESP_GAP_BLE_AUTH_CMPL_EVT:
        if (param->ble_security.auth_cmpl.success) {
            ESP_LOGI(TAG, "BLE authentication success");
            /* Dismiss passkey overlay */
            if (s_passkey_cb) {
                s_passkey_cb(BLE_PASSKEY_DISMISS);
            }
            /* Encryption is now established.  On ESP-IDF Bluedroid the
             * HIDH library discovers services and writes CCCDs after
             * the BLE connection is up; with "Just Works" or fast
             * pairing the encryption is established before CCCD writes,
             * so they succeed on the first open.  No close-and-reopen
             * is needed -- it would waste GATT notification slots
             * (CONFIG_BT_GATTC_NOTIF_REG_MAX). */
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

/* ---- BLE init task (runs in its own FreeRTOS task) ---- */

/* HIDH bring-up runs in a dedicated task because esp_hidh_init()
 * internally calls esp_ble_gattc_app_register() which blocks on a
 * semaphore waiting for the BTC task to confirm the registration.
 * Running this from app_main can deadlock because app_main sits at
 * priority 1 on core 0 and the BTC task may not get enough CPU
 * time to process the request while app_main is blocked.  A
 * dedicated task at priority 3 avoids this and also allows the
 * rest of app_main to proceed.
 *
 * The earlier steps (Classic mem release, BT controller init/enable
 * and Bluedroid init/enable) are now performed synchronously from
 * ble_keyboard_init() instead of from this task.  Doing them
 * synchronously is important on memory-constrained boards (notably
 * the M5Stack PaperS3, where M5GFX/LovyanGFX statics leave only
 * ~191 KB of internal heap on boot): if Bluedroid bring-up races
 * with esp_wifi_init() in main(), WiFi can grab enough internal RAM
 * that BTU_StartUp later fails to allocate its workqueue and the
 * device asserts and reboots in a loop. */

static void ble_init_task(void *arg)
{
    (void)arg;

    /* Initialize HIDH (HID Host).  The GATTC callback must already
     * be registered (in ble_keyboard_init) so that esp_ble_hidh_init
     * can receive the ESP_GATTC_REG_EVT and unblock. */
    {
        esp_hidh_config_t hidh_cfg = {};
        hidh_cfg.callback = hidh_callback;
        hidh_cfg.event_stack_size = 4096;
        ESP_LOGI(TAG, "Initializing HIDH...");
        esp_err_t hidh_err = esp_hidh_init(&hidh_cfg);
        if (hidh_err != ESP_OK) {
            ESP_LOGE(TAG, "esp_hidh_init failed: %s",
                     esp_err_to_name(hidh_err));
        } else {
            s_hidh_ready = true;
            ESP_LOGI(TAG, "HIDH initialized");
        }
    }

    /* Register our GAP callback AFTER HIDH init so it is not
     * overwritten by esp_hidh_init()'s internal registration. */
    esp_err_t gap_err = esp_ble_gap_register_callback(gap_event_handler);
    if (gap_err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ble_gap_register_callback failed: %s",
                 esp_err_to_name(gap_err));
    } else {
        ESP_LOGI(TAG, "GAP callback registered");
    }

    /* Set BLE security parameters.
     * IO capability = DisplayOnly so the stack generates a random
     * 6-digit passkey for each pairing attempt and delivers it via
     * ESP_GAP_BLE_PASSKEY_NOTIF_EVT.  The user types this number on
     * the keyboard to complete pairing.  MITM flag is set so the
     * stack actually uses the passkey entry protocol.
     *
     * These are set AFTER HIDH init and GAP callback registration
     * to keep the BTC queue empty during HIDH's synchronous GATTC
     * registration. */
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
    ESP_LOGI(TAG, "Security parameters configured");

    /* Set scan parameters -- triggers async
     * SCAN_PARAM_SET_COMPLETE_EVT which starts the reconnection /
     * scan sequence. */
    esp_ble_scan_params_t scan_params;
    fill_scan_params(&scan_params);
    esp_err_t sp_err = esp_ble_gap_set_scan_params(&scan_params);
    if (sp_err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ble_gap_set_scan_params failed: %s",
                 esp_err_to_name(sp_err));
    } else {
        ESP_LOGI(TAG, "Scan params sent to controller");
    }

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

    /* Start a periodic safety-net timer.  Retries scanning if the
     * initial SCAN_PARAM_SET_COMPLETE_EVT is somehow missed. */
    s_startup_timer = xTimerCreate("ble_start",
                                   pdMS_TO_TICKS(STARTUP_SAFETY_TIMER_MS),
                                   pdTRUE, NULL, startup_timer_cb);
    if (s_startup_timer) {
        xTimerStart(s_startup_timer, 0);
    }

    ESP_LOGI(TAG, "BLE keyboard host initialized");
    vTaskDelete(NULL);
}

/* ---- Public API ---- */

extern "C" void ble_keyboard_init(void)
{
    /* Synchronous bring-up of the BT controller and Bluedroid stack.
     *
     * These steps are deliberately performed in the caller's
     * context (not in ble_init_task) so that ble_keyboard_init()
     * does not return until the Bluedroid workqueues and timers
     * have been allocated.  This guarantees that any subsequent
     * subsystem the caller brings up cannot starve Bluedroid of
     * internal RAM mid-bring-up.  On boards with little internal
     * heap (e.g. M5Stack PaperS3) the previous fully-async layout
     * caused BTU_StartUp to fail allocating its workqueue and the
     * device rebooted in a loop. */

    /* Load bonded device list from NVS */
    bonded_load();

    /* Release Classic BT memory (ESP32-S3 is BLE-only) */
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
    ESP_LOGI(TAG, "Classic BT memory released");

    /* Initialize BT controller in BLE mode */
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
    ESP_LOGI(TAG, "BT controller enabled (BLE mode)");

    /* Initialize Bluedroid */
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());
    ESP_LOGI(TAG, "Bluedroid enabled");

    /* Register the GATTC callback BEFORE esp_hidh_init().
     *
     * esp_ble_hidh_init() (called internally by esp_hidh_init)
     * calls esp_ble_gattc_app_register() and then waits on a
     * semaphore for the ESP_GATTC_REG_EVT callback.  However,
     * esp_ble_hidh_init() does NOT register the GATTC callback
     * itself -- it relies on the caller to attach
     * esp_hidh_gattc_event_handler via
     * esp_ble_gattc_register_callback() beforehand.
     *
     * Without this registration, the GATTC registration event is
     * never delivered, the semaphore is never signalled, and
     * esp_hidh_init() blocks forever.
     *
     * See esp_hidh_gattc.h and the official ESP-IDF HID host
     * example (examples/bluetooth/esp_hid_host). */
    ESP_ERROR_CHECK(
        esp_ble_gattc_register_callback(esp_hidh_gattc_event_handler));
    ESP_LOGI(TAG, "GATTC callback registered");

    /* Spawn ble_init_task to handle esp_hidh_init() and the
     * subsequent scan/security setup asynchronously.  HIDH init
     * itself blocks on a semaphore that the BTC task signals, so
     * it must not run on the priority-1 main task. */
    xTaskCreate(ble_init_task, "ble_init", BLE_INIT_TASK_STACK,
                NULL, 3, NULL);
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

extern "C" void ble_keyboard_set_status_text_callback(ble_status_text_cb_t cb)
{
    s_status_text_cb = cb;
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
        } else {
            ESP_LOGI(TAG, "Scan started (%d s)", SCAN_DURATION_SEC);
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

#endif /* CONFIG_BT_ENABLED */
