#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sys/stat.h>
#include <dirent.h>
#include <freertos/FreeRTOS.h>
#include <esp_log.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <driver/sdmmc_host.h>

#include "sd_card.h"

static const char *TAG = "SDCard";
static sdmmc_card_t *s_card = NULL;
static char s_mount[32] = "";

extern "C" esp_err_t sd_card_init(int clk_pin, int cmd_pin, int d0_pin, const char *mount_point)
{
    strncpy(s_mount, mount_point, sizeof(s_mount) - 1);

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {};
    mount_cfg.format_if_mount_failed = false;
    mount_cfg.max_files              = 10;
    mount_cfg.allocation_unit_size   = 16 * 1024;

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk   = (gpio_num_t)clk_pin;
    slot.cmd   = (gpio_num_t)cmd_pin;
    slot.d0    = (gpio_num_t)d0_pin;

    esp_err_t ret = esp_vfs_fat_sdmmc_mount(s_mount, &host, &slot, &mount_cfg, &s_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Mount failed: %s", esp_err_to_name(ret));
        s_card = NULL;
        return ret;
    }
    sdmmc_card_print_info(stdout, s_card);
    ESP_LOGI(TAG, "SD card mounted at %s", s_mount);
    return ESP_OK;
}

extern "C" esp_err_t sd_card_deinit(void)
{
    if (!s_card) return ESP_ERR_INVALID_STATE;
    esp_err_t ret = esp_vfs_fat_sdcard_unmount(s_mount, s_card);
    s_card = NULL;
    return ret;
}

extern "C" bool sd_card_is_ready(void)
{
    return s_card != NULL && sdmmc_get_status(s_card) == ESP_OK;
}

extern "C" const char *sd_card_get_mount_point(void)
{
    return s_mount;
}

extern "C" esp_err_t sd_card_read_file(const char *path, char **out_buf, size_t *out_len)
{
    if (!s_card) return ESP_ERR_INVALID_STATE;
    FILE *f = fopen(path, "rb");
    if (!f) { ESP_LOGE(TAG, "Open failed: %s", path); return ESP_ERR_NOT_FOUND; }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return ESP_FAIL; }

    char *buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(f); return ESP_ERR_NO_MEM; }

    size_t rd = fread(buf, 1, sz, f);
    fclose(f);
    buf[rd] = '\0';

    *out_buf = buf;
    if (out_len) *out_len = rd;
    return ESP_OK;
}

extern "C" esp_err_t sd_card_write_file(const char *path, const char *data, size_t len)
{
    if (!s_card) return ESP_ERR_INVALID_STATE;
    FILE *f = fopen(path, "wb");
    if (!f) { ESP_LOGE(TAG, "Open for write failed: %s", path); return ESP_FAIL; }
    size_t wr = fwrite(data, 1, len, f);
    fclose(f);
    return (wr == len) ? ESP_OK : ESP_FAIL;
}

extern "C" esp_err_t sd_card_append_file(const char *path, const char *data, size_t len)
{
    if (!s_card) return ESP_ERR_INVALID_STATE;
    FILE *f = fopen(path, "ab");
    if (!f) return ESP_FAIL;
    fwrite(data, 1, len, f);
    fclose(f);
    return ESP_OK;
}

extern "C" bool sd_card_file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

extern "C" esp_err_t sd_card_delete_file(const char *path)
{
    return (remove(path) == 0) ? ESP_OK : ESP_FAIL;
}

extern "C" esp_err_t sd_card_mkdir(const char *path)
{
    /* Create parent directories as needed */
    char tmp[256];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    return (mkdir(tmp, 0755) == 0 || errno == EEXIST) ? ESP_OK : ESP_FAIL;
}

extern "C" int sd_card_list_dir(const char *path, sd_card_file_entry_t *entries, int max_entries)
{
    DIR *dir = opendir(path);
    if (!dir) return -1;
    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && count < max_entries) {
        if (ent->d_name[0] == '.') continue;
        strncpy(entries[count].name, ent->d_name, sizeof(entries[count].name) - 1);
        entries[count].name[sizeof(entries[count].name) - 1] = '\0';
        entries[count].is_dir = (ent->d_type == DT_DIR);

        char full[512];
        snprintf(full, sizeof(full), "%s/%s", path, ent->d_name);
        struct stat st;
        entries[count].size = (stat(full, &st) == 0) ? st.st_size : 0;
        count++;
    }
    closedir(dir);
    return count;
}

extern "C" long sd_card_file_size(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0) ? st.st_size : -1;
}

extern "C" esp_err_t sd_card_rename(const char *old_path, const char *new_path)
{
    return (rename(old_path, new_path) == 0) ? ESP_OK : ESP_FAIL;
}
