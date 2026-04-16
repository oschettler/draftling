#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <cJSON.h>
#include <mbedtls/base64.h>

#include "git_sync.h"
#include "sd_card.h"
#include "wifi_manager.h"

static const char *TAG = "GitSync";

/* Configuration loaded from /sdcard/git.cfg */
static struct {
    char api_url[256];     /* e.g. https://api.github.com/repos/user/repo */
    char branch[64];
    char token[128];
    char remote_path[128]; /* subdirectory on remote */
    char local_path[256];  /* local mount, e.g. /sdcard */
    bool configured;
} s_cfg;

static git_sync_state_t s_state = GIT_SYNC_IDLE;
static git_sync_callback_t s_callback = NULL;
static char s_last_error[128] = "";
static char s_last_sync[32]   = "";

/* HTTP response accumulator */
static char *s_resp_buf = NULL;
static int   s_resp_len = 0;
static int   s_resp_cap = 0;

static void set_error(const char *msg)
{
    strncpy(s_last_error, msg, sizeof(s_last_error) - 1);
    s_last_error[sizeof(s_last_error) - 1] = '\0';
    s_state = GIT_SYNC_ERROR;
    if (s_callback) s_callback(GIT_SYNC_ERROR, msg);
}

static void notify(git_sync_state_t st, const char *msg)
{
    s_state = st;
    if (s_callback) s_callback(st, msg);
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        int needed = s_resp_len + evt->data_len + 1;
        if (needed > s_resp_cap) {
            int new_cap = needed + 4096;
            char *nb = (char *)heap_caps_realloc(s_resp_buf, new_cap, MALLOC_CAP_SPIRAM);
            if (!nb) return ESP_ERR_NO_MEM;
            s_resp_buf = nb;
            s_resp_cap = new_cap;
        }
        memcpy(s_resp_buf + s_resp_len, evt->data, evt->data_len);
        s_resp_len += evt->data_len;
        s_resp_buf[s_resp_len] = '\0';
    }
    return ESP_OK;
}

static esp_err_t api_request(const char *url, esp_http_client_method_t method,
                              const char *body, int *status_out)
{
    /* Reset response buffer */
    s_resp_len = 0;
    if (s_resp_buf) s_resp_buf[0] = '\0';

    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.method = method;
    cfg.event_handler = http_event_handler;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.timeout_ms = 30000;
    cfg.buffer_size = 4096;
    cfg.buffer_size_tx = 4096;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_FAIL;

    char auth[180];
    snprintf(auth, sizeof(auth), "Bearer %.127s", s_cfg.token);
    esp_http_client_set_header(client, "Authorization", auth);
    esp_http_client_set_header(client, "Accept", "application/vnd.github.v3+json");
    esp_http_client_set_header(client, "User-Agent", "Draftling/1.0");

    if (body) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, body, strlen(body));
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (status_out) *status_out = status;
    if (err != ESP_OK) return err;
    if (status < 200 || status >= 300) {
        ESP_LOGE(TAG, "HTTP %d for %s", status, url);
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* ---- Pull: download remote .md files ---- */

static esp_err_t do_pull(void)
{
    notify(GIT_SYNC_IN_PROGRESS, "Pulling remote files...");

    /* List remote files */
    char url[512];
    snprintf(url, sizeof(url), "%.255s/contents/%.127s?ref=%.63s",
             s_cfg.api_url, s_cfg.remote_path, s_cfg.branch);

    int status = 0;
    esp_err_t ret = api_request(url, HTTP_METHOD_GET, NULL, &status);
    if (ret != ESP_OK) {
        set_error("Failed to list remote files");
        return ret;
    }

    cJSON *root = cJSON_Parse(s_resp_buf);
    if (!root) { set_error("Bad JSON from remote"); return ESP_FAIL; }

    /* Ensure local directory exists */
    sd_card_mkdir(s_cfg.local_path);

    int count = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, root) {
        cJSON *jname = cJSON_GetObjectItem(item, "name");
        cJSON *jtype = cJSON_GetObjectItem(item, "type");
        cJSON *jurl  = cJSON_GetObjectItem(item, "download_url");

        if (!jname || !jtype || !jurl) continue;
        if (strcmp(jtype->valuestring, "file") != 0) continue;

        const char *name = jname->valuestring;
        size_t nlen = strlen(name);
        if (nlen < 4 || strcmp(name + nlen - 3, ".md") != 0) continue;

        /* Download file */
        s_resp_len = 0;
        ret = api_request(jurl->valuestring, HTTP_METHOD_GET, NULL, &status);
        if (ret != ESP_OK) continue;

        /* Save locally */
        char local[512];
        snprintf(local, sizeof(local), "%.255s/%.255s", s_cfg.local_path, name);
        sd_card_write_file(local, s_resp_buf, s_resp_len);
        count++;
        ESP_LOGI(TAG, "Pulled: %s", name);
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "Pulled %d files", count);
    return ESP_OK;
}

/* ---- Push: upload local .md files ---- */

static esp_err_t push_file(const char *name)
{
    /* Read local file */
    char local[512];
    snprintf(local, sizeof(local), "%.255s/%.255s", s_cfg.local_path, name);

    char *content = NULL;
    size_t content_len = 0;
    esp_err_t ret = sd_card_read_file(local, &content, &content_len);
    if (ret != ESP_OK) return ret;

    /* Limit file size for base64 encoding (256KB source -> ~350KB encoded) */
    if (content_len > 256 * 1024) {
        free(content);
        ESP_LOGW(TAG, "File too large to push: %s", name);
        return ESP_ERR_NO_MEM;
    }

    /* Base64 encode */
    size_t b64_len = 0;
    mbedtls_base64_encode(NULL, 0, &b64_len, (const unsigned char *)content, content_len);
    char *b64 = (char *)heap_caps_malloc(b64_len + 1, MALLOC_CAP_SPIRAM);
    if (!b64) { free(content); return ESP_ERR_NO_MEM; }
    mbedtls_base64_encode((unsigned char *)b64, b64_len + 1, &b64_len,
                          (const unsigned char *)content, content_len);
    b64[b64_len] = '\0';
    free(content);

    /* Check if file exists remotely to get its SHA */
    char url[768];
    snprintf(url, sizeof(url), "%.255s/contents/%.127s%.255s?ref=%.63s",
             s_cfg.api_url, s_cfg.remote_path, name, s_cfg.branch);

    char sha[64] = "";
    int status = 0;
    if (api_request(url, HTTP_METHOD_GET, NULL, &status) == ESP_OK) {
        cJSON *r = cJSON_Parse(s_resp_buf);
        if (r) {
            cJSON *jsha = cJSON_GetObjectItem(r, "sha");
            if (jsha && jsha->valuestring) {
                strncpy(sha, jsha->valuestring, sizeof(sha) - 1);
            }
            cJSON_Delete(r);
        }
    }

    /* Build PUT body */
    cJSON *body = cJSON_CreateObject();
    char msg[128];
    snprintf(msg, sizeof(msg), "Update %.100s from Draftling", name);
    cJSON_AddStringToObject(body, "message", msg);
    cJSON_AddStringToObject(body, "content", b64);
    cJSON_AddStringToObject(body, "branch", s_cfg.branch);
    if (sha[0]) cJSON_AddStringToObject(body, "sha", sha);

    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    heap_caps_free(b64);

    if (!body_str) return ESP_ERR_NO_MEM;

    /* PUT the file */
    snprintf(url, sizeof(url), "%.255s/contents/%.127s%.255s",
             s_cfg.api_url, s_cfg.remote_path, name);
    ret = api_request(url, HTTP_METHOD_PUT, body_str, &status);
    cJSON_free(body_str);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Pushed: %s", name);
    } else {
        ESP_LOGE(TAG, "Failed to push: %s (HTTP %d)", name, status);
    }
    return ret;
}

static esp_err_t do_push(void)
{
    notify(GIT_SYNC_IN_PROGRESS, "Pushing local files...");

    sd_card_file_entry_t entries[64];
    int count = sd_card_list_dir(s_cfg.local_path, entries, 64);
    if (count < 0) { set_error("Cannot list local files"); return ESP_FAIL; }

    int pushed = 0;
    for (int i = 0; i < count; i++) {
        if (entries[i].is_dir) continue;
        const char *name = entries[i].name;
        size_t nlen = strlen(name);
        if (nlen < 4 || strcmp(name + nlen - 3, ".md") != 0) continue;

        if (push_file(name) == ESP_OK) pushed++;
    }

    ESP_LOGI(TAG, "Pushed %d files", pushed);
    return ESP_OK;
}

/* ---- Sync task ---- */

static void sync_task(void *arg)
{
    git_sync_direction_t dir = (git_sync_direction_t)(intptr_t)arg;

    if (!wifi_manager_is_connected()) {
        set_error("WiFi not connected");
        vTaskDelete(NULL);
        return;
    }

    esp_err_t ret = ESP_OK;

    if (dir == GIT_SYNC_PULL || dir == GIT_SYNC_BOTH) {
        ret = do_pull();
        if (ret != ESP_OK && dir != GIT_SYNC_BOTH) goto done;
    }

    if (dir == GIT_SYNC_PUSH || dir == GIT_SYNC_BOTH) {
        ret = do_push();
    }

done:
    if (ret == ESP_OK) {
        snprintf(s_last_sync, sizeof(s_last_sync), "OK");
        notify(GIT_SYNC_SUCCESS, "Sync complete");
    } else {
        /* Ensure the UI is notified even when do_pull/do_push already
         * called set_error() -- reset the state so a new sync can be
         * triggered later. */
        s_state = GIT_SYNC_ERROR;
    }

    /* Free the HTTP response buffer to reclaim memory */
    if (s_resp_buf) {
        heap_caps_free(s_resp_buf);
        s_resp_buf = NULL;
        s_resp_len = 0;
        s_resp_cap = 0;
    }

    vTaskDelete(NULL);
}

/* ---- Config parser ---- */

static void parse_config(const char *data)
{
    memset(&s_cfg, 0, sizeof(s_cfg));
    strncpy(s_cfg.local_path, sd_card_get_mount_point(), sizeof(s_cfg.local_path) - 1);
    strncpy(s_cfg.branch, "main", sizeof(s_cfg.branch) - 1);

    /* Parse key=value lines */
    const char *p = data;
    while (*p) {
        const char *line_end = strchr(p, '\n');
        if (!line_end) line_end = p + strlen(p);
        int line_len = (int)(line_end - p);

        /* Trim trailing \r */
        int trimmed = line_len;
        if (trimmed > 0 && p[trimmed - 1] == '\r') trimmed--;

        const char *eq = (const char *)memchr(p, '=', trimmed);
        if (eq) {
            int key_len = (int)(eq - p);
            const char *val = eq + 1;
            int val_len = trimmed - key_len - 1;

            if (key_len == 8 && memcmp(p, "repo_url", 8) == 0) {
                /* Convert github.com URL to API URL */
                if (val_len > 19 && memcmp(val, "https://github.com/", 19) == 0) {
                    snprintf(s_cfg.api_url, sizeof(s_cfg.api_url),
                             "https://api.github.com/repos/%.*s", val_len - 19, val + 19);
                } else {
                    int clen = val_len < (int)sizeof(s_cfg.api_url) - 1 ? val_len : (int)sizeof(s_cfg.api_url) - 1;
                    memcpy(s_cfg.api_url, val, clen);
                    s_cfg.api_url[clen] = '\0';
                }
            } else if (key_len == 6 && memcmp(p, "branch", 6) == 0) {
                int clen = val_len < (int)sizeof(s_cfg.branch) - 1 ? val_len : (int)sizeof(s_cfg.branch) - 1;
                memcpy(s_cfg.branch, val, clen);
                s_cfg.branch[clen] = '\0';
            } else if (key_len == 5 && memcmp(p, "token", 5) == 0) {
                int clen = val_len < (int)sizeof(s_cfg.token) - 1 ? val_len : (int)sizeof(s_cfg.token) - 1;
                memcpy(s_cfg.token, val, clen);
                s_cfg.token[clen] = '\0';
            } else if (key_len == 4 && memcmp(p, "path", 4) == 0) {
                int clen = val_len < (int)sizeof(s_cfg.remote_path) - 1 ? val_len : (int)sizeof(s_cfg.remote_path) - 1;
                memcpy(s_cfg.remote_path, val, clen);
                s_cfg.remote_path[clen] = '\0';
            }
        }

        p = (*line_end) ? line_end + 1 : line_end;
    }

    /* Ensure remote_path ends with '/' when non-empty */
    if (s_cfg.remote_path[0]) {
        size_t rlen = strlen(s_cfg.remote_path);
        if (s_cfg.remote_path[rlen - 1] != '/' && rlen < sizeof(s_cfg.remote_path) - 1) {
            s_cfg.remote_path[rlen] = '/';
            s_cfg.remote_path[rlen + 1] = '\0';
        }
    }

    s_cfg.configured = (s_cfg.api_url[0] && s_cfg.token[0]);
}

/* ---- Public API ---- */

extern "C" esp_err_t git_sync_init(void)
{
    char *data = NULL;
    size_t len = 0;
    if (sd_card_read_file("/sdcard/git.cfg", &data, &len) == ESP_OK) {
        parse_config(data);
        free(data);
        if (s_cfg.configured) {
            ESP_LOGI(TAG, "Git sync configured: %s branch=%s", s_cfg.api_url, s_cfg.branch);
        }
    } else {
        ESP_LOGI(TAG, "No /sdcard/git.cfg found, sync disabled");
    }
    return ESP_OK;
}

extern "C" esp_err_t git_sync_start(git_sync_direction_t direction)
{
    if (!s_cfg.configured) { set_error("Not configured"); return ESP_ERR_INVALID_STATE; }
    if (s_state == GIT_SYNC_IN_PROGRESS) { set_error("Sync already in progress"); return ESP_ERR_INVALID_STATE; }

    s_state = GIT_SYNC_IN_PROGRESS;

    BaseType_t rc = xTaskCreatePinnedToCore(sync_task, "git_sync", 32 * 1024,
                                            (void *)(intptr_t)direction, 3, NULL, 0);
    if (rc != pdPASS) {
        set_error("Failed to start sync task");
        return ESP_FAIL;
    }
    return ESP_OK;
}

extern "C" git_sync_state_t git_sync_get_state(void) { return s_state; }
extern "C" void git_sync_set_callback(git_sync_callback_t cb) { s_callback = cb; }
extern "C" bool git_sync_is_configured(void) { return s_cfg.configured; }
extern "C" const char *git_sync_get_last_error(void) { return s_last_error; }
extern "C" const char *git_sync_get_last_sync_time(void) { return s_last_sync; }
