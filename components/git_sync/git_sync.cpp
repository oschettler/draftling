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
#include <mbedtls/sha1.h>

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

/* Task stack + TCB allocated from SPIRAM so that the sync task does not
 * compete for scarce internal DRAM with BLE and WiFi. */
#define GIT_SYNC_STACK_SIZE  (8 * 1024)
static StackType_t *s_stack_buf = NULL;
static StaticTask_t s_task_tcb;

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

/* See doc comment on git_sync_max_file_size() in include/git_sync.h.
 *
 * Returns the largest file size (raw bytes) that the next push could
 * accommodate given the SPIRAM that is free at this moment. The factor
 * of 4 (rather than the ~3x actual peak) leaves a small safety margin
 * for malloc fragmentation and concurrent allocations from BLE / WiFi /
 * LVGL. The MIN floor keeps editor_init() from clamping to a useless
 * value on devices where almost no PSRAM is free at startup (the push
 * itself will still fail cleanly in that case). */
extern "C" size_t git_sync_max_file_size(void)
{
    const size_t MIN_CAP = 64 * 1024;
    const size_t RESERVE = 512 * 1024;
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t usable = (free_psram > RESERVE) ? (free_psram - RESERVE) : (free_psram / 2);
    size_t cap = usable / 4;
    if (cap < MIN_CAP) cap = MIN_CAP;
    return cap;
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
        /* 404 is expected when checking whether a file exists before
         * creating it; log at WARN level so it does not look like a
         * real failure.  Other non-2xx codes are genuine errors. */
        if (status == 404)
            ESP_LOGW(TAG, "HTTP 404 (not found) for %s", url);
        else
            ESP_LOGE(TAG, "HTTP %d for %s", status, url);
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* ---- Git blob SHA helper ---- */

static void compute_blob_sha(const char *content, size_t len, char out_hex[41])
{
    unsigned char hash[20];
    char header[32];
    int hlen = snprintf(header, sizeof(header), "blob %zu", len);

    mbedtls_sha1_context ctx;
    mbedtls_sha1_init(&ctx);
    mbedtls_sha1_starts(&ctx);
    mbedtls_sha1_update(&ctx, (const unsigned char *)header, hlen + 1);
    mbedtls_sha1_update(&ctx, (const unsigned char *)content, len);
    mbedtls_sha1_finish(&ctx, hash);
    mbedtls_sha1_free(&ctx);

    for (int i = 0; i < 20; i++)
        snprintf(out_hex + i * 2, 3, "%02x", hash[i]);
    out_hex[40] = '\0';
}

/* ---- Sync state: tracks per-file blob SHAs from the last successful sync
 *      so we can do a 3-way comparison (saved vs local vs remote) and avoid
 *      overwriting local edits or creating redundant commits. ---- */

#define MAX_TRACKED_FILES 64
#define STATE_NAME_MAX 128

typedef struct {
    char name[STATE_NAME_MAX];
    char sha[41];
    /* Transient flag set during a sync session when the entry is matched
     * against a file present on the local or remote side.  Used after the
     * pull/push loop to detect entries whose underlying file disappeared,
     * which indicates a deletion that has to be propagated to the other
     * side.  Not persisted to disk. */
    bool seen;
} sync_state_entry_t;

static sync_state_entry_t *s_sync_state = NULL;
static int s_sync_state_count = 0;

static void load_sync_state(void)
{
    if (s_sync_state) {
        heap_caps_free(s_sync_state);
        s_sync_state = NULL;
    }
    s_sync_state_count = 0;

    char path[512];
    snprintf(path, sizeof(path), "%.255s/.git_state", s_cfg.local_path);

    char *data = NULL;
    size_t len = 0;
    if (sd_card_read_file(path, &data, &len) != ESP_OK) {
        /* No state file yet -- first sync */
        return;
    }

    s_sync_state = (sync_state_entry_t *)heap_caps_calloc(
        MAX_TRACKED_FILES, sizeof(sync_state_entry_t), MALLOC_CAP_SPIRAM);
    if (!s_sync_state) {
        free(data);
        return;
    }

    /* Parse lines: name=sha */
    const char *p = data;
    while (*p && s_sync_state_count < MAX_TRACKED_FILES) {
        const char *line_end = strchr(p, '\n');
        if (!line_end) line_end = p + strlen(p);
        int line_len = (int)(line_end - p);

        int trimmed = line_len;
        if (trimmed > 0 && p[trimmed - 1] == '\r') trimmed--;

        const char *eq = (const char *)memchr(p, '=', trimmed);
        if (eq) {
            int name_len = (int)(eq - p);
            const char *val = eq + 1;
            int val_len = trimmed - name_len - 1;

            if (name_len > 0 && name_len < STATE_NAME_MAX && val_len == 40) {
                sync_state_entry_t *e = &s_sync_state[s_sync_state_count++];
                memcpy(e->name, p, name_len);
                e->name[name_len] = '\0';
                memcpy(e->sha, val, 40);
                e->sha[40] = '\0';
            }
        }

        p = (*line_end) ? line_end + 1 : line_end;
    }

    free(data);
    ESP_LOGI(TAG, "Loaded sync state: %d file(s)", s_sync_state_count);
}

static void save_sync_state(void)
{
    char path[512];
    snprintf(path, sizeof(path), "%.255s/.git_state", s_cfg.local_path);

    /* When the in-memory state is empty (e.g. every tracked file was
     * deleted on both sides) we still want to persist that fact -- a
     * stale on-disk state file would otherwise resurrect the entries on
     * the next sync.  Write an empty file in that case. */
    if (!s_sync_state || s_sync_state_count == 0) {
        sd_card_write_file(path, "", 0);
        ESP_LOGI(TAG, "Saved sync state: 0 file(s)");
        return;
    }

    size_t buf_size = (size_t)s_sync_state_count * (STATE_NAME_MAX + 42);
    char *buf = (char *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    if (!buf) return;

    size_t offset = 0;
    for (int i = 0; i < s_sync_state_count; i++) {
        if (s_sync_state[i].name[0] && s_sync_state[i].sha[0]) {
            int written = snprintf(buf + offset, buf_size - offset,
                                   "%s=%s\n",
                                   s_sync_state[i].name,
                                   s_sync_state[i].sha);
            if (written > 0) offset += (size_t)written;
        }
    }

    sd_card_write_file(path, buf, offset);
    heap_caps_free(buf);
    ESP_LOGI(TAG, "Saved sync state: %d file(s)", s_sync_state_count);
}

static void free_sync_state(void)
{
    if (s_sync_state) {
        heap_caps_free(s_sync_state);
        s_sync_state = NULL;
    }
    s_sync_state_count = 0;
}

static const char *get_saved_sha(const char *name)
{
    for (int i = 0; i < s_sync_state_count; i++) {
        if (strcmp(s_sync_state[i].name, name) == 0) {
            return s_sync_state[i].sha;
        }
    }
    return NULL;
}

static void set_saved_sha(const char *name, const char *sha)
{
    /* Update existing entry */
    for (int i = 0; i < s_sync_state_count; i++) {
        if (strcmp(s_sync_state[i].name, name) == 0) {
            strncpy(s_sync_state[i].sha, sha, 40);
            s_sync_state[i].sha[40] = '\0';
            s_sync_state[i].seen = true;
            return;
        }
    }
    /* Add new entry */
    if (!s_sync_state) {
        s_sync_state = (sync_state_entry_t *)heap_caps_calloc(
            MAX_TRACKED_FILES, sizeof(sync_state_entry_t), MALLOC_CAP_SPIRAM);
        if (!s_sync_state) return;
    }
    if (s_sync_state_count < MAX_TRACKED_FILES) {
        sync_state_entry_t *e = &s_sync_state[s_sync_state_count++];
        strncpy(e->name, name, STATE_NAME_MAX - 1);
        e->name[STATE_NAME_MAX - 1] = '\0';
        strncpy(e->sha, sha, 40);
        e->sha[40] = '\0';
        e->seen = true;
    }
}

/* Mark the saved-state entry for `name` as having been observed on either
 * the local or the remote side during this sync session.  Used so that
 * after iterating one side we can tell which tracked files have vanished
 * (deletions to be propagated). */
static void mark_seen(const char *name)
{
    for (int i = 0; i < s_sync_state_count; i++) {
        if (strcmp(s_sync_state[i].name, name) == 0) {
            s_sync_state[i].seen = true;
            return;
        }
    }
}

static void reset_seen_flags(void)
{
    for (int i = 0; i < s_sync_state_count; i++) {
        s_sync_state[i].seen = false;
    }
}

/* Remove the named entry from the in-memory sync state by swapping it
 * with the last entry and decrementing the count.  Safe to call while
 * iterating the array in reverse. */
static void remove_saved_sha(const char *name)
{
    for (int i = 0; i < s_sync_state_count; i++) {
        if (strcmp(s_sync_state[i].name, name) == 0) {
            if (i != s_sync_state_count - 1) {
                s_sync_state[i] = s_sync_state[s_sync_state_count - 1];
            }
            s_sync_state_count--;
            memset(&s_sync_state[s_sync_state_count], 0,
                   sizeof(sync_state_entry_t));
            return;
        }
    }
}

/* ---- Pull: download remote .md files ---- */

static esp_err_t do_pull(void)
{
    notify(GIT_SYNC_IN_PROGRESS, "Pulling remote files...");

    /* List remote files.  When remote_path is empty the files live at the
     * repository root, so the URL must be ".../contents?ref=..." (no
     * trailing slash).  When a subdirectory is configured the path already
     * ends with '/' thanks to parse_config(), so we strip it for the
     * listing URL because the GitHub API returns 404 for paths that have
     * a trailing slash and do not exist yet. */
    char url[512];
    if (s_cfg.remote_path[0]) {
        /* remote_path always ends with '/', trim it for the listing */
        char trimmed[128];
        strncpy(trimmed, s_cfg.remote_path, sizeof(trimmed) - 1);
        trimmed[sizeof(trimmed) - 1] = '\0';
        size_t tlen = strlen(trimmed);
        if (tlen > 0 && trimmed[tlen - 1] == '/') trimmed[tlen - 1] = '\0';
        snprintf(url, sizeof(url), "%.255s/contents/%.127s?ref=%.63s",
                 s_cfg.api_url, trimmed, s_cfg.branch);
    } else {
        snprintf(url, sizeof(url), "%.255s/contents?ref=%.63s",
                 s_cfg.api_url, s_cfg.branch);
    }

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

    /* Clear "seen" flags before walking the remote listing; any saved
     * entry that remains unseen at the end was deleted on the remote. */
    reset_seen_flags();

    int pulled = 0;
    int skipped = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, root) {
        cJSON *jname = cJSON_GetObjectItem(item, "name");
        cJSON *jtype = cJSON_GetObjectItem(item, "type");
        cJSON *jurl  = cJSON_GetObjectItem(item, "download_url");
        cJSON *jsha  = cJSON_GetObjectItem(item, "sha");

        if (!jname || !jtype || !jurl) continue;
        if (strcmp(jtype->valuestring, "file") != 0) continue;

        const char *name = jname->valuestring;
        size_t nlen = strlen(name);
        if (nlen < 4 || strcmp(name + nlen - 3, ".md") != 0) continue;

        /* Record that this saved entry (if any) was seen on the remote. */
        mark_seen(name);

        const char *remote_sha = (jsha && jsha->valuestring)
                                 ? jsha->valuestring : NULL;

        /* 3-way check: compare remote SHA with saved (last-synced) SHA.
         * If they match the file was not changed on the remote side since
         * our last sync -- no need to download it. */
        const char *saved = get_saved_sha(name);
        if (saved && remote_sha && strcmp(remote_sha, saved) == 0) {
            ESP_LOGD(TAG, "Pull skip (unchanged on remote): %s", name);
            skipped++;
            continue;
        }

        /* Remote was updated (or this is the first sync for this file).
         * Before overwriting, check whether the local copy was also
         * modified -- that would be a conflict. */
        if (saved) {
            char local_path[512];
            snprintf(local_path, sizeof(local_path), "%.255s/%.255s",
                     s_cfg.local_path, name);

            char *local_content = NULL;
            size_t local_len = 0;
            if (sd_card_read_file(local_path, &local_content, &local_len)
                    == ESP_OK) {
                char local_sha[41];
                compute_blob_sha(local_content, local_len, local_sha);
                free(local_content);

                if (strcmp(local_sha, saved) != 0) {
                    /* Both local and remote changed -- keep local edits. */
                    ESP_LOGW(TAG,
                             "Conflict (both modified): %s -- keeping local",
                             name);
                    skipped++;
                    continue;
                }
            }
        }

        /* Download file */
        s_resp_len = 0;
        ret = api_request(jurl->valuestring, HTTP_METHOD_GET, NULL, &status);
        if (ret != ESP_OK) continue;

        /* Save locally */
        char local[512];
        snprintf(local, sizeof(local), "%.255s/%.255s", s_cfg.local_path, name);
        sd_card_write_file(local, s_resp_buf, s_resp_len);
        pulled++;

        if (remote_sha) set_saved_sha(name, remote_sha);
        ESP_LOGI(TAG, "Pulled: %s", name);
    }

    cJSON_Delete(root);

    /* Propagate remote deletions to the local SD card.  Walk the saved
     * state in reverse because remove_saved_sha() shrinks the array. */
    int removed = 0;
    int conflicts = 0;
    for (int i = s_sync_state_count - 1; i >= 0; i--) {
        if (s_sync_state[i].seen) continue;

        /* Copy out of the array so the values survive remove_saved_sha(),
         * which compacts the array in place. */
        char name[STATE_NAME_MAX];
        char saved_sha[41];
        strncpy(name, s_sync_state[i].name, sizeof(name));
        name[sizeof(name) - 1] = '\0';
        strncpy(saved_sha, s_sync_state[i].sha, sizeof(saved_sha));
        saved_sha[sizeof(saved_sha) - 1] = '\0';

        /* Only consider .md files -- the saved state should never hold
         * anything else, but stay defensive. */
        size_t nlen = strlen(name);
        if (nlen < 4 || strcmp(name + nlen - 3, ".md") != 0) {
            remove_saved_sha(name);
            continue;
        }

        char local_path[512];
        snprintf(local_path, sizeof(local_path), "%.255s/%.255s",
                 s_cfg.local_path, name);

        char *local_content = NULL;
        size_t local_len = 0;
        if (sd_card_read_file(local_path, &local_content, &local_len) != ESP_OK) {
            /* Already gone locally too -- just forget about it. */
            ESP_LOGI(TAG, "Pull: dropping stale state for %s", name);
            remove_saved_sha(name);
            continue;
        }

        char local_sha[41];
        compute_blob_sha(local_content, local_len, local_sha);
        free(local_content);

        if (strcmp(local_sha, saved_sha) == 0) {
            /* Local matches last sync -- safe to delete locally. */
            if (sd_card_delete_file(local_path) == ESP_OK) {
                ESP_LOGI(TAG, "Pull: deleted locally (gone on remote): %s",
                         name);
                removed++;
            } else {
                ESP_LOGW(TAG, "Pull: failed to delete %s", local_path);
            }
            remove_saved_sha(name);
        } else {
            /* Locally modified after the remote deletion -- keep the
             * local copy and forget the saved SHA so the next push
             * recreates it on the remote as a new file. */
            ESP_LOGW(TAG,
                     "Conflict (remote deleted, local modified): %s -- "
                     "keeping local; will be re-uploaded on next push",
                     name);
            conflicts++;
            remove_saved_sha(name);
        }
    }

    ESP_LOGI(TAG, "Pulled %d file(s), skipped %d, deleted %d, conflicts %d",
             pulled, skipped, removed, conflicts);
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

    /* Upper bound on pushable file size; computed dynamically from
     * currently-free SPIRAM. See git_sync_max_file_size() in
     * include/git_sync.h for the rationale. */
    size_t max_size = git_sync_max_file_size();
    if (content_len > max_size) {
        free(content);
        ESP_LOGW(TAG, "File too large to push: %s (%u bytes, max %u)",
                 name, (unsigned)content_len, (unsigned)max_size);
        return ESP_ERR_NO_MEM;
    }

    /* Compute local git blob SHA */
    char local_sha[41];
    compute_blob_sha(content, content_len, local_sha);

    /* If the local SHA matches the saved state the file has not been
     * modified since the last sync -- skip the push entirely. */
    const char *saved = get_saved_sha(name);
    if (saved && strcmp(local_sha, saved) == 0) {
        ESP_LOGI(TAG, "Skipped (not modified locally): %s", name);
        free(content);
        return ESP_OK;
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

    /* If local SHA matches remote SHA the content is already in sync. */
    if (sha[0] && strcmp(local_sha, sha) == 0) {
        ESP_LOGI(TAG, "Skipped (unchanged): %s", name);
        set_saved_sha(name, local_sha);
        heap_caps_free(b64);
        return ESP_OK;
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
        set_saved_sha(name, local_sha);
    } else {
        ESP_LOGE(TAG, "Failed to push: %s (HTTP %d)", name, status);
    }
    return ret;
}

/* Delete a file on the remote repository via the GitHub Contents API.
 *
 * Verifies that the remote blob SHA still matches `expected_sha` (the
 * SHA recorded the last time we synced).  If it does not, the remote
 * has been edited since our last sync and we MUST NOT delete it -- we
 * return ESP_FAIL so the caller can drop the saved-state entry and let
 * the next pull re-download the modified content as a "new" file.
 *
 * Returns ESP_OK on a successful DELETE.  Returns ESP_FAIL for both
 * "remote was modified" and "remote file is missing" cases; the caller
 * cannot meaningfully distinguish them and treats both as "stop
 * tracking this name". */
static esp_err_t delete_remote_file(const char *name, const char *expected_sha)
{
    /* Look up the current remote SHA. */
    char url[768];
    snprintf(url, sizeof(url), "%.255s/contents/%.127s%.255s?ref=%.63s",
             s_cfg.api_url, s_cfg.remote_path, name, s_cfg.branch);

    int status = 0;
    if (api_request(url, HTTP_METHOD_GET, NULL, &status) != ESP_OK) {
        /* Most commonly a 404 -- already gone. */
        ESP_LOGI(TAG, "Delete: remote %s already missing (HTTP %d)",
                 name, status);
        return ESP_FAIL;
    }

    char remote_sha[64] = "";
    cJSON *r = cJSON_Parse(s_resp_buf);
    if (r) {
        cJSON *jsha = cJSON_GetObjectItem(r, "sha");
        if (jsha && jsha->valuestring) {
            strncpy(remote_sha, jsha->valuestring, sizeof(remote_sha) - 1);
        }
        cJSON_Delete(r);
    }

    if (!remote_sha[0]) {
        ESP_LOGW(TAG, "Delete: could not read remote SHA for %s", name);
        return ESP_FAIL;
    }

    if (expected_sha[0] && strcmp(remote_sha, expected_sha) != 0) {
        /* Remote changed since our last sync -- do not delete. */
        ESP_LOGW(TAG,
                 "Conflict (local deleted, remote modified): %s -- "
                 "keeping remote; will be re-downloaded on next pull",
                 name);
        return ESP_FAIL;
    }

    /* Build the DELETE body.  GitHub requires the current blob SHA so
     * concurrent modifications fail loudly rather than silently
     * clobbering somebody else's commit. */
    cJSON *body = cJSON_CreateObject();
    char msg[128];
    snprintf(msg, sizeof(msg), "Delete %.100s from Draftling", name);
    cJSON_AddStringToObject(body, "message", msg);
    cJSON_AddStringToObject(body, "sha", remote_sha);
    cJSON_AddStringToObject(body, "branch", s_cfg.branch);

    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!body_str) return ESP_ERR_NO_MEM;

    snprintf(url, sizeof(url), "%.255s/contents/%.127s%.255s",
             s_cfg.api_url, s_cfg.remote_path, name);
    esp_err_t ret = api_request(url, HTTP_METHOD_DELETE, body_str, &status);
    cJSON_free(body_str);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Deleted on remote: %s", name);
    } else {
        ESP_LOGE(TAG, "Failed to delete remote %s (HTTP %d)", name, status);
    }
    return ret;
}

static esp_err_t do_push(void)
{
    notify(GIT_SYNC_IN_PROGRESS, "Pushing local files...");

    /* Allocate from SPIRAM -- the array is ~17 KB which is too large
     * for the task stack when BLE + WiFi are active. */
    static const int MAX_ENTRIES = 64;
    sd_card_file_entry_t *entries = (sd_card_file_entry_t *)heap_caps_malloc(
        MAX_ENTRIES * sizeof(sd_card_file_entry_t), MALLOC_CAP_SPIRAM);
    if (!entries) { set_error("Out of memory"); return ESP_ERR_NO_MEM; }

    int count = sd_card_list_dir(s_cfg.local_path, entries, MAX_ENTRIES);
    if (count < 0) { heap_caps_free(entries); set_error("Cannot list local files"); return ESP_FAIL; }

    /* Reset seen flags before walking the local directory; any saved
     * entry left unseen at the end was deleted locally and the deletion
     * needs to be propagated to the remote. */
    reset_seen_flags();

    int pushed = 0;
    int attempted = 0;
    for (int i = 0; i < count; i++) {
        if (entries[i].is_dir) continue;
        const char *name = entries[i].name;
        size_t nlen = strlen(name);
        if (nlen < 4 || strcmp(name + nlen - 3, ".md") != 0) continue;

        /* Note that this saved entry (if any) is still present locally. */
        mark_seen(name);

        attempted++;
        if (push_file(name) == ESP_OK) pushed++;
    }

    heap_caps_free(entries);
    ESP_LOGI(TAG, "Pushed %d/%d files", pushed, attempted);

    /* Propagate local deletions to the remote.  Walk in reverse because
     * remove_saved_sha() compacts the array in place. */
    int deleted = 0;
    int conflicts = 0;
    for (int i = s_sync_state_count - 1; i >= 0; i--) {
        if (s_sync_state[i].seen) continue;

        /* Copy out of the array so the values survive remove_saved_sha(),
         * which compacts the array in place. */
        char name[STATE_NAME_MAX];
        char saved_sha[41];
        strncpy(name, s_sync_state[i].name, sizeof(name));
        name[sizeof(name) - 1] = '\0';
        strncpy(saved_sha, s_sync_state[i].sha, sizeof(saved_sha));
        saved_sha[sizeof(saved_sha) - 1] = '\0';

        if (delete_remote_file(name, saved_sha) == ESP_OK) {
            deleted++;
        } else {
            /* Either the remote was modified since our last sync (so we
             * refuse to delete) or the remote is already gone.  In both
             * cases we drop the saved entry: a modified remote will be
             * downloaded fresh by the next pull, an already-deleted one
             * just no longer needs tracking. */
            conflicts++;
        }
        remove_saved_sha(name);
    }

    if (deleted || conflicts) {
        ESP_LOGI(TAG, "Push: deleted %d remote file(s), conflicts %d",
                 deleted, conflicts);
    }

    if (attempted > 0 && pushed == 0 && deleted == 0) {
        set_error("All files failed to push");
        return ESP_FAIL;
    }
    if (pushed < attempted) {
        ESP_LOGW(TAG, "%d file(s) failed to push", attempted - pushed);
    }
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

    /* Load last-synced per-file SHAs for 3-way comparison. */
    load_sync_state();

    esp_err_t pull_ret = ESP_OK;
    esp_err_t push_ret = ESP_OK;

    /* Push before pull so that local edits are uploaded to the remote
     * before pull overwrites local files with the remote versions. */
    if (dir == GIT_SYNC_PUSH || dir == GIT_SYNC_BOTH) {
        push_ret = do_push();
        if (push_ret != ESP_OK && dir == GIT_SYNC_PUSH) goto done;
    }

    if (dir == GIT_SYNC_PULL || dir == GIT_SYNC_BOTH) {
        pull_ret = do_pull();
    }

done:
    {
        /* Determine overall result.  For BOTH: push failure is always
         * fatal, pull failure alone is tolerable (empty remote). */
        bool ok = false;
        if (dir == GIT_SYNC_PULL) {
            ok = (pull_ret == ESP_OK);
        } else if (dir == GIT_SYNC_PUSH) {
            ok = (push_ret == ESP_OK);
        } else {
            ok = (push_ret == ESP_OK);
        }

        if (ok) {
            snprintf(s_last_sync, sizeof(s_last_sync), "OK");
            notify(GIT_SYNC_SUCCESS, "Sync complete");
        } else {
            /* set_error() was already called by the failing operation;
             * just make sure the state is ERROR so a new sync can be
             * triggered later. */
            if (s_state != GIT_SYNC_ERROR) {
                set_error("Sync failed");
            }
        }
    }

    /* Persist sync state so the next sync can diff properly. */
    save_sync_state();
    free_sync_state();

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
                /* Strip trailing .git and/or '/' if present -- the GitHub
                 * API does not accept .git in repository paths and a
                 * trailing slash would produce malformed URLs. */
                int effective_len = val_len;
                while (effective_len > 0 && val[effective_len - 1] == '/')
                    effective_len--;
                if (effective_len >= 4 && memcmp(val + effective_len - 4, ".git", 4) == 0) {
                    effective_len -= 4;
                }
                while (effective_len > 0 && val[effective_len - 1] == '/')
                    effective_len--;
                /* Convert github.com URL to API URL */
                if (effective_len > 19 && memcmp(val, "https://github.com/", 19) == 0) {
                    snprintf(s_cfg.api_url, sizeof(s_cfg.api_url),
                             "https://api.github.com/repos/%.*s", effective_len - 19, val + 19);
                } else {
                    int clen = effective_len < (int)sizeof(s_cfg.api_url) - 1 ? effective_len : (int)sizeof(s_cfg.api_url) - 1;
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

    /* Allocate the task stack from SPIRAM so we do not depend on scarce
     * contiguous internal DRAM (BLE + WiFi consume most of it). */
    if (!s_stack_buf) {
        s_stack_buf = (StackType_t *)heap_caps_malloc(
            GIT_SYNC_STACK_SIZE, MALLOC_CAP_SPIRAM);
        if (!s_stack_buf) {
            set_error("Failed to alloc sync stack");
            return ESP_ERR_NO_MEM;
        }
    }

    TaskHandle_t h = xTaskCreateStaticPinnedToCore(
        sync_task, "git_sync", GIT_SYNC_STACK_SIZE / sizeof(StackType_t),
        (void *)(intptr_t)direction, 3, s_stack_buf, &s_task_tcb, 0);
    if (!h) {
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
