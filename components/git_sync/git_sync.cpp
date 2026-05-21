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

/* Per-operation counters, updated from do_pull() / do_push() and read
 * by sync_task() to compose a human-readable summary in the final
 * GIT_SYNC_SUCCESS status-bar message. Reset at the start of each
 * sync_task() run. */
static int s_pushed_count    = 0;  /* files uploaded to remote */
static int s_pulled_count    = 0;  /* files downloaded from remote */
static int s_remote_deleted  = 0;  /* files deleted on the remote */
static int s_local_deleted   = 0;  /* files deleted locally by pull */

/* Task stack + TCB allocated from SPIRAM so that the sync task does not
 * compete for scarce internal DRAM with BLE and WiFi.
 *
 * Sized for the streaming push path: mbedTLS/HTTPS alone burns several
 * KB during the TLS handshake and record encryption, and push_file() +
 * api_put_streamed() add ~3 KB of their own small stack buffers
 * (filename / url / sha / JSON prefix+suffix scratch). 8 KB used to
 * overflow with a ~300 KB file; 16 KB leaves comfortable headroom and
 * is essentially free since the stack lives in SPIRAM. */
#define GIT_SYNC_STACK_SIZE  (16 * 1024)
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
 * accommodate given the SPIRAM that is free at this moment. The push
 * path now stream-encodes the base64 into a temp file on the SD card
 * and streams the HTTP body, so the only large transient PSRAM
 * allocation during a push is the raw file content itself (~1x N).
 * The factor of 2 (rather than 1) leaves a safety margin for malloc
 * fragmentation, HTTPS/TLS buffers, and concurrent allocations from
 * BLE / WiFi / LVGL. The MIN floor keeps editor_init() from clamping
 * to a useless value on devices where almost no PSRAM is free at
 * startup (the push itself will still fail cleanly in that case). */
extern "C" size_t git_sync_max_file_size(void)
{
    const size_t MIN_CAP = 64 * 1024;
    const size_t RESERVE = 512 * 1024;
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t usable = (free_psram > RESERVE) ? (free_psram - RESERVE) : (free_psram / 2);
    size_t cap = usable / 2;
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
    notify(GIT_SYNC_IN_PROGRESS, "Listing remote files...");

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

    /* Count remote .md files so the per-file progress notifications
     * can show "i/N" rather than just a running counter. */
    int total_md = 0;
    {
        cJSON *it;
        cJSON_ArrayForEach(it, root) {
            cJSON *jn = cJSON_GetObjectItem(it, "name");
            cJSON *jt = cJSON_GetObjectItem(it, "type");
            if (!jn || !jt) continue;
            if (strcmp(jt->valuestring, "file") != 0) continue;
            const char *nm = jn->valuestring;
            size_t l = strlen(nm);
            if (l >= 4 && strcmp(nm + l - 3, ".md") == 0) total_md++;
        }
    }

    int pulled = 0;
    int skipped = 0;
    int seen_md = 0;
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

        seen_md++;

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
        {
            char pmsg[96];
            snprintf(pmsg, sizeof(pmsg), "Pulling %d/%d: %s",
                     seen_md, total_md, name);
            notify(GIT_SYNC_IN_PROGRESS, pmsg);
        }
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
            {
                char pmsg[96];
                snprintf(pmsg, sizeof(pmsg), "Deleting local: %s", name);
                notify(GIT_SYNC_IN_PROGRESS, pmsg);
            }
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
    s_pulled_count  = pulled;
    s_local_deleted = removed;
    return ESP_OK;
}

/* ---- Push: upload local .md files ---- */

/* Append `s` to `dst` with JSON string escaping for `"` and `\`.
 * Returns false on overflow.  Used for the small dynamic strings that
 * go into the manually-constructed JSON request body (no big strings
 * are passed through this -- the base64 file content is streamed
 * separately and is base64, which is JSON-safe by construction). */
static bool json_escape_append(char *dst, size_t cap, size_t *len, const char *s)
{
    size_t i = *len;
    for (const char *p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\') {
            if (i + 2 >= cap) return false;
            dst[i++] = '\\';
            dst[i++] = (char)c;
        } else if (c < 0x20) {
            /* control char -- \u00XX */
            if (i + 6 >= cap) return false;
            int n = snprintf(dst + i, cap - i, "\\u%04x", c);
            if (n < 0 || (size_t)n >= cap - i) return false;
            i += (size_t)n;
        } else {
            if (i + 1 >= cap) return false;
            dst[i++] = (char)c;
        }
    }
    dst[i] = '\0';
    *len = i;
    return true;
}

/* Stream-encode the raw file at `src_path` into base64 written to
 * `dst_path`.  Returns the number of base64 bytes written via
 * `out_b64_len`, or ESP_FAIL on any I/O / encode error.  Both paths are
 * absolute paths on the SD card mount (or any FAT-mounted VFS). */
static esp_err_t encode_file_base64(const char *src_path, const char *dst_path,
                                    size_t *out_b64_len)
{
    /* 3072 raw bytes -> 4096 base64 bytes per chunk; both are multiples
     * of the base64 group size (3 raw -> 4 b64), so only the very last
     * chunk produces padding. */
    const size_t RAW_CHUNK = 3072;
    const size_t B64_CHUNK = 4096 + 1; /* +1 for the NUL mbedtls writes */

    FILE *fin = fopen(src_path, "rb");
    if (!fin) {
        ESP_LOGE(TAG, "encode_file_base64: cannot open %s", src_path);
        return ESP_FAIL;
    }
    FILE *fout = fopen(dst_path, "wb");
    if (!fout) {
        ESP_LOGE(TAG, "encode_file_base64: cannot open %s for write", dst_path);
        fclose(fin);
        return ESP_FAIL;
    }

    unsigned char *raw = (unsigned char *)heap_caps_malloc(RAW_CHUNK, MALLOC_CAP_SPIRAM);
    unsigned char *enc = (unsigned char *)heap_caps_malloc(B64_CHUNK, MALLOC_CAP_SPIRAM);
    if (!raw || !enc) {
        if (raw) heap_caps_free(raw);
        if (enc) heap_caps_free(enc);
        fclose(fin);
        fclose(fout);
        return ESP_ERR_NO_MEM;
    }

    size_t total = 0;
    esp_err_t result = ESP_OK;
    while (true) {
        size_t n = fread(raw, 1, RAW_CHUNK, fin);
        if (n == 0) {
            if (ferror(fin)) result = ESP_FAIL;
            break;
        }
        size_t enc_len = 0;
        int rc = mbedtls_base64_encode(enc, B64_CHUNK, &enc_len, raw, n);
        if (rc != 0) {
            ESP_LOGE(TAG, "base64 encode failed: -0x%04x", -rc);
            result = ESP_FAIL;
            break;
        }
        if (fwrite(enc, 1, enc_len, fout) != enc_len) {
            result = ESP_FAIL;
            break;
        }
        total += enc_len;
        if (n < RAW_CHUNK) break; /* EOF (last chunk, included padding) */
    }

    heap_caps_free(raw);
    heap_caps_free(enc);
    if (fclose(fout) != 0) result = ESP_FAIL;
    fclose(fin);

    if (out_b64_len) *out_b64_len = total;
    return result;
}

/* Issue a streaming PUT to `url` whose request body is the
 * concatenation of `prefix` + the contents of `b64_path` + `suffix`.
 * The response body is accumulated into `s_resp_buf` exactly as
 * api_request() does.  This avoids ever holding the full base64
 * payload (or the full JSON body) in PSRAM. */
static esp_err_t api_put_streamed(const char *url,
                                  const char *prefix, size_t prefix_len,
                                  const char *b64_path, size_t b64_len,
                                  const char *suffix, size_t suffix_len,
                                  int *status_out)
{
    /* Reset response buffer */
    s_resp_len = 0;
    if (s_resp_buf) s_resp_buf[0] = '\0';

    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.method = HTTP_METHOD_PUT;
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
    esp_http_client_set_header(client, "Content-Type", "application/json");

    int total_len = (int)(prefix_len + b64_len + suffix_len);
    esp_err_t err = esp_http_client_open(client, total_len);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }

    esp_err_t result = ESP_OK;

    /* Write prefix */
    if (esp_http_client_write(client, prefix, (int)prefix_len) != (int)prefix_len) {
        result = ESP_FAIL;
        goto close_client;
    }

    /* Stream b64 file in chunks */
    {
        FILE *fb = fopen(b64_path, "rb");
        if (!fb) { result = ESP_FAIL; goto close_client; }

        const size_t CHUNK = 4096;
        char *buf = (char *)heap_caps_malloc(CHUNK, MALLOC_CAP_SPIRAM);
        if (!buf) { fclose(fb); result = ESP_ERR_NO_MEM; goto close_client; }

        size_t sent = 0;
        while (sent < b64_len) {
            size_t want = b64_len - sent;
            if (want > CHUNK) want = CHUNK;
            size_t got = fread(buf, 1, want, fb);
            if (got == 0) { result = ESP_FAIL; break; }
            if (esp_http_client_write(client, buf, (int)got) != (int)got) {
                result = ESP_FAIL;
                break;
            }
            sent += got;
        }

        heap_caps_free(buf);
        fclose(fb);
        if (result != ESP_OK) goto close_client;
    }

    /* Write suffix */
    if (esp_http_client_write(client, suffix, (int)suffix_len) != (int)suffix_len) {
        result = ESP_FAIL;
        goto close_client;
    }

    /* Read response */
    {
        int content_length = esp_http_client_fetch_headers(client);
        (void)content_length;

        char read_buf[512];
        while (true) {
            int n = esp_http_client_read(client, read_buf, sizeof(read_buf));
            if (n <= 0) break;
            int needed = s_resp_len + n + 1;
            if (needed > s_resp_cap) {
                int new_cap = needed + 4096;
                char *nb = (char *)heap_caps_realloc(s_resp_buf, new_cap, MALLOC_CAP_SPIRAM);
                if (!nb) { result = ESP_ERR_NO_MEM; break; }
                s_resp_buf = nb;
                s_resp_cap = new_cap;
            }
            memcpy(s_resp_buf + s_resp_len, read_buf, n);
            s_resp_len += n;
            s_resp_buf[s_resp_len] = '\0';
        }
    }

close_client:
    {
        int status = esp_http_client_get_status_code(client);
        if (status_out) *status_out = status;
        esp_http_client_close(client);
        esp_http_client_cleanup(client);

        if (result != ESP_OK) return result;
        if (status < 200 || status >= 300) {
            if (status == 404)
                ESP_LOGW(TAG, "HTTP 404 (not found) for %s", url);
            else
                ESP_LOGE(TAG, "HTTP %d for %s", status, url);
            return ESP_FAIL;
        }
        return ESP_OK;
    }
}

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

    /* The raw content is no longer needed: from here on we only need
     * its base64 form, which we will stream-encode straight from the
     * source file on the SD card into a temp file (also on SD).  Free
     * the PSRAM copy now so it does not coexist with the HTTPS / TLS
     * buffers during the upload. */
    free(content);
    content = NULL;

    /* Stream-encode the file into a temp file on SD card.  This keeps
     * the ~1.34x base64 expansion off PSRAM entirely. */
    char b64_path[512];
    snprintf(b64_path, sizeof(b64_path), "%.255s/.git_b64.tmp", s_cfg.local_path);

    size_t b64_len = 0;
    if (encode_file_base64(local, b64_path, &b64_len) != ESP_OK) {
        remove(b64_path);
        ESP_LOGE(TAG, "Failed to base64-encode %s to %s", local, b64_path);
        return ESP_FAIL;
    }

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
        remove(b64_path);
        return ESP_OK;
    }

    /* Build the JSON request body manually as a prefix + (streamed
     * base64 from SD) + suffix.  Only the small dynamic fields
     * (filename, branch, remote SHA) need JSON-escaping; the base64
     * payload is JSON-safe by construction. */
    char prefix[512];
    char suffix[256];
    size_t plen = 0, slen = 0;

    /* prefix: {"message":"Update <name> from Draftling","content":" */
    {
        const char *p1 = "{\"message\":\"Update ";
        size_t l1 = strlen(p1);
        if (l1 >= sizeof(prefix)) { remove(b64_path); return ESP_FAIL; }
        memcpy(prefix, p1, l1);
        plen = l1;
        prefix[plen] = '\0';

        /* Truncate name to ~100 chars before escaping, matching the
         * old "Update %.100s from Draftling" behavior. */
        char name_trunc[101];
        snprintf(name_trunc, sizeof(name_trunc), "%.100s", name);
        if (!json_escape_append(prefix, sizeof(prefix), &plen, name_trunc)) {
            remove(b64_path);
            return ESP_FAIL;
        }

        const char *p2 = " from Draftling\",\"content\":\"";
        size_t l2 = strlen(p2);
        if (plen + l2 >= sizeof(prefix)) { remove(b64_path); return ESP_FAIL; }
        memcpy(prefix + plen, p2, l2);
        plen += l2;
        prefix[plen] = '\0';
    }

    /* suffix: ","branch":"<branch>"[,"sha":"<sha>"]} */
    {
        const char *s1 = "\",\"branch\":\"";
        size_t l1 = strlen(s1);
        if (l1 >= sizeof(suffix)) { remove(b64_path); return ESP_FAIL; }
        memcpy(suffix, s1, l1);
        slen = l1;
        suffix[slen] = '\0';

        if (!json_escape_append(suffix, sizeof(suffix), &slen, s_cfg.branch)) {
            remove(b64_path);
            return ESP_FAIL;
        }

        if (sha[0]) {
            const char *s2 = "\",\"sha\":\"";
            size_t l2 = strlen(s2);
            if (slen + l2 >= sizeof(suffix)) { remove(b64_path); return ESP_FAIL; }
            memcpy(suffix + slen, s2, l2);
            slen += l2;
            suffix[slen] = '\0';

            if (!json_escape_append(suffix, sizeof(suffix), &slen, sha)) {
                remove(b64_path);
                return ESP_FAIL;
            }
        }

        const char *s3 = "\"}";
        size_t l3 = strlen(s3);
        if (slen + l3 >= sizeof(suffix)) { remove(b64_path); return ESP_FAIL; }
        memcpy(suffix + slen, s3, l3);
        slen += l3;
        suffix[slen] = '\0';
    }

    /* PUT the file with a streamed body */
    snprintf(url, sizeof(url), "%.255s/contents/%.127s%.255s",
             s_cfg.api_url, s_cfg.remote_path, name);
    ret = api_put_streamed(url, prefix, plen, b64_path, b64_len,
                           suffix, slen, &status);

    /* Always clean up the temp file -- even on failure, so a stale
     * .git_b64.tmp does not remain on the SD card. */
    remove(b64_path);

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

    /* First pass: count .md files so the per-file progress message can
     * show "i/N" rather than just a running counter. */
    int total_md = 0;
    for (int i = 0; i < count; i++) {
        if (entries[i].is_dir) continue;
        const char *n = entries[i].name;
        size_t l = strlen(n);
        if (l >= 4 && strcmp(n + l - 3, ".md") == 0) total_md++;
    }

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
        {
            char pmsg[96];
            snprintf(pmsg, sizeof(pmsg), "Pushing %d/%d: %s",
                     attempted, total_md, name);
            notify(GIT_SYNC_IN_PROGRESS, pmsg);
        }
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

        {
            char pmsg[96];
            snprintf(pmsg, sizeof(pmsg), "Deleting remote: %s", name);
            notify(GIT_SYNC_IN_PROGRESS, pmsg);
        }
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
    s_pushed_count   = pushed;
    s_remote_deleted = deleted;
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

    /* Reset per-operation counters so the final summary reflects only
     * this run. */
    s_pushed_count   = 0;
    s_pulled_count   = 0;
    s_remote_deleted = 0;
    s_local_deleted  = 0;

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

            /* Compose a summary that reflects what actually happened so
             * the user can tell at a glance whether any files moved.
             * Examples:
             *   "Sync complete (up to date)"
             *   "Sync complete (pushed 2)"
             *   "Sync complete (pushed 1, pulled 3)"
             *   "Sync complete (pulled 2, deleted 1 local)"
             *
             * Helper macro: snprintf returns the desired length (which
             * can exceed remaining capacity), so always advance `pos`
             * by the actual bytes written -- clamping to the buffer
             * size keeps `summary + pos` in-bounds even in the
             * unlikely event of a pathologically large counter. */
            char summary[96];
            size_t cap = sizeof(summary);
            size_t pos = 0;
            #define SUM_APPEND(...) do {                                   \
                if (pos < cap) {                                            \
                    int _n = snprintf(summary + pos, cap - pos, __VA_ARGS__);\
                    if (_n < 0) break;                                      \
                    pos += ((size_t)_n < cap - pos) ? (size_t)_n            \
                                                    : (cap - pos - 1);     \
                }                                                           \
            } while (0)

            SUM_APPEND("Sync complete");
            bool any = (s_pushed_count || s_pulled_count ||
                        s_remote_deleted || s_local_deleted);
            if (!any) {
                SUM_APPEND(" (up to date)");
            } else {
                SUM_APPEND(" (");
                const char *sep = "";
                if (s_pushed_count) {
                    SUM_APPEND("%spushed %d", sep, s_pushed_count);
                    sep = ", ";
                }
                if (s_pulled_count) {
                    SUM_APPEND("%spulled %d", sep, s_pulled_count);
                    sep = ", ";
                }
                if (s_remote_deleted) {
                    SUM_APPEND("%sdeleted %d remote", sep, s_remote_deleted);
                    sep = ", ";
                }
                if (s_local_deleted) {
                    SUM_APPEND("%sdeleted %d local", sep, s_local_deleted);
                    sep = ", ";
                }
                SUM_APPEND(")");
            }
            #undef SUM_APPEND
            notify(GIT_SYNC_SUCCESS, summary);
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
