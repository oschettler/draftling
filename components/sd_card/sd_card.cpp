#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <strings.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <driver/sdmmc_host.h>
#include <driver/sdspi_host.h>
#include <driver/spi_common.h>
#include <driver/gpio.h>

#include "sd_card.h"

static const char *TAG = "SDCard";
static sdmmc_card_t *s_card = NULL;
static char s_mount[32] = "";
static int  s_spi_host  = -1;     /* set when mounted via SPI, otherwise -1 */
static bool s_spi_bus_owned = false;

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

extern "C" esp_err_t sd_card_init_spi(int spi_host, int miso, int mosi, int sck,
                                      int cs, int enable_gpio,
                                      const char *mount_point)
{
    strncpy(s_mount, mount_point, sizeof(s_mount) - 1);
    s_spi_host = spi_host;

    /* Power-enable GPIO (active-high). The reTerminal E1001 needs this
     * driven HIGH to feed VBUS to the MicroSD slot. */
    if (enable_gpio >= 0) {
        gpio_config_t en_cfg = {};
        en_cfg.intr_type    = GPIO_INTR_DISABLE;
        en_cfg.mode         = GPIO_MODE_OUTPUT;
        en_cfg.pin_bit_mask = (1ULL << enable_gpio);
        en_cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
        en_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        gpio_config(&en_cfg);
        gpio_set_level((gpio_num_t)enable_gpio, 1);
        /* Allow the card a moment to power up before initialization. */
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    /* Initialize the SPI bus only if the caller has not already done so
     * (e.g. the e-paper driver may share the bus). */
    if (sck >= 0 && mosi >= 0) {
        spi_bus_config_t bus_cfg = {};
        bus_cfg.mosi_io_num     = mosi;
        bus_cfg.miso_io_num     = miso;
        bus_cfg.sclk_io_num     = sck;
        bus_cfg.quadwp_io_num   = -1;
        bus_cfg.quadhd_io_num   = -1;
        bus_cfg.max_transfer_sz = 4096;
        esp_err_t ret = spi_bus_initialize((spi_host_device_t)spi_host,
                                           &bus_cfg, SPI_DMA_CH_AUTO);
        if (ret == ESP_OK) {
            s_spi_bus_owned = true;
        } else if (ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
            return ret;
        }
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = spi_host;

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs   = (gpio_num_t)cs;
    slot.host_id   = (spi_host_device_t)spi_host;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {};
    mount_cfg.format_if_mount_failed = false;
    mount_cfg.max_files              = 10;
    mount_cfg.allocation_unit_size   = 16 * 1024;

    esp_err_t ret = esp_vfs_fat_sdspi_mount(s_mount, &host, &slot,
                                            &mount_cfg, &s_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD/SPI mount failed: %s", esp_err_to_name(ret));
        if (s_spi_bus_owned) {
            spi_bus_free((spi_host_device_t)spi_host);
            s_spi_bus_owned = false;
        }
        s_card = NULL;
        return ret;
    }
    sdmmc_card_print_info(stdout, s_card);
    ESP_LOGI(TAG, "SD card mounted at %s (SPI host %d, CS=%d)",
             s_mount, spi_host, cs);
    return ESP_OK;
}

extern "C" esp_err_t sd_card_deinit(void)
{
    if (!s_card) return ESP_ERR_INVALID_STATE;
    esp_err_t ret;
    if (s_spi_host >= 0) {
        ret = esp_vfs_fat_sdcard_unmount(s_mount, s_card);
        if (s_spi_bus_owned) {
            spi_bus_free((spi_host_device_t)s_spi_host);
            s_spi_bus_owned = false;
        }
        s_spi_host = -1;
    } else {
        ret = esp_vfs_fat_sdcard_unmount(s_mount, s_card);
    }
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
    fsync(fileno(f));
    fclose(f);
    return (wr == len) ? ESP_OK : ESP_FAIL;
}

extern "C" esp_err_t sd_card_append_file(const char *path, const char *data, size_t len)
{
    if (!s_card) return ESP_ERR_INVALID_STATE;
    FILE *f = fopen(path, "ab");
    if (!f) return ESP_FAIL;
    fwrite(data, 1, len, f);
    fsync(fileno(f));
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

/* qsort() comparator: directories first, then files; both groups
 * sorted by name case-insensitively so the file browser shows a
 * predictable, alphabetical listing regardless of FAT directory
 * order. */
static int sd_entry_cmp(const void *a, const void *b)
{
    const sd_card_file_entry_t *ea = (const sd_card_file_entry_t *)a;
    const sd_card_file_entry_t *eb = (const sd_card_file_entry_t *)b;
    if (ea->is_dir != eb->is_dir) return ea->is_dir ? -1 : 1;
    return strcasecmp(ea->name, eb->name);
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
    if (count > 1) {
        qsort(entries, (size_t)count, sizeof(entries[0]), sd_entry_cmp);
    }
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
