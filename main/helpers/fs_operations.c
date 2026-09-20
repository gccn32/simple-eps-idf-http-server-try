#include "stdio.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_littlefs.h"
#include <sys/stat.h>
#include <errno.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "driver/gpio.h"
#include "freertos/semphr.h"

#define SD_PIN_CMD GPIO_NUM_38
#define SD_PIN_CLK GPIO_NUM_39
#define SD_PIN_D0 GPIO_NUM_40

#define SD_MOUNT_POINT "/sdcard"

static SemaphoreHandle_t fs_io_mutex;
static SemaphoreHandle_t open_f_mutex;

static const char *TAG = "FS-OPERATIONS";
static sdmmc_card_t *s_card = NULL;
const char temp_dir_path[] = "/sdcard/temp";

static int max_open_f_len = 5;
static int open_f_len = 0;
static char **open_f;

static int f_read_timeout = 5000;
static int f_open_timeout = 500;

static void create_temp_folder()
{
    mkdir(temp_dir_path, 0755);
}

static esp_err_t mount_littlefs()
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/littlefs",       // Your base VFS mount point
        .partition_label = "storage",   // Matches the name in partitions.csv
        .format_if_mount_failed = true, // Formats empty space automatically if needed
        .dont_mount = false,
    };

    // Use the ESP-IDF VFS registration system
    esp_err_t ret = esp_vfs_littlefs_register(&conf);

    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        }
        else if (ret == ESP_ERR_NOT_FOUND)
        {
            ESP_LOGE(TAG, "Failed to find LittleFS partition");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to initialize LittleFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }

    // Optional: Log diagnostic space info
    size_t total = 0, used = 0;
    ret = esp_littlefs_info(conf.partition_label, &total, &used);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Partition initialized successfully. Size: total: %d bytes, used: %d bytes", total, used);
        create_temp_folder();
    }
    open_f = malloc(max_open_f_len * sizeof(char *));
    return ESP_OK;
}

static esp_err_t mount_sd_card()
{
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false, // Set to false to avoid wiping the SD card
        .max_files = 5,
        .allocation_unit_size = 16 * 1024};
    // 1. Configure SDMMC Host (1-bit mode)
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_1BIT;        // 1-bit bus mode required
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED; // 20MHz default (or SDMMC_FREQ_HIGHSPEED 40MHz, SDMMC_FREQ_DEFAULT 20MHz)
    // 2. Configure Slot Pins for Freenove ESP32-S3 CAM
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1; // 1-bit width
    slot_config.clk = SD_PIN_CLK;
    slot_config.cmd = SD_PIN_CMD;
    slot_config.d0 = SD_PIN_D0;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP; // Enable internal pullups for CMD & D0
    // ESP_LOGI(TAG, "Mounting SD card at %s...", SD_MOUNT_POINT);
    esp_err_t ret = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot_config, &mount_config, &s_card);
    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
            ESP_LOGE(TAG, "Failed to mount filesystem.");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to initialize the card (%s).", esp_err_to_name(ret));
        }
        return ret;
    }
    ESP_LOGI(TAG, "SD Card mounted successfully. Size: %lluMB",
             (uint64_t)s_card->csd.capacity * s_card->csd.sector_size / (1024 * 1024));
    // sdmmc_card_print_info(stdout, s_card);
    return ESP_OK;
}

esp_err_t mount_fs()
{
    fs_io_mutex = xSemaphoreCreateMutex();
    open_f_mutex = xSemaphoreCreateMutex();
    esp_err_t res_sd = mount_sd_card();
    esp_err_t res_littlefs = mount_littlefs();
    return res_sd == ESP_FAIL || res_littlefs == ESP_FAIL ? ESP_FAIL : ESP_OK;
}

esp_err_t st_fread(uint8_t *buf, int buf_len, FILE *f, int *bytes_read, int f_read_timeout)
{
    if (xSemaphoreTake(fs_io_mutex, pdMS_TO_TICKS(f_read_timeout)) == pdTRUE)
    {
        *bytes_read = fread(buf, 1, buf_len, f);
        xSemaphoreGive(fs_io_mutex);
        return ESP_OK;
    }
    return ESP_FAIL;
}
esp_err_t st_write(uint8_t *buf, int buf_len, FILE *f, int f_read_timeout)
{
    if (xSemaphoreTake(fs_io_mutex, pdMS_TO_TICKS(f_read_timeout)) == pdTRUE)
    {
        fwrite(buf, 1, buf_len, f);
        xSemaphoreGive(fs_io_mutex);
        return ESP_OK;
    }
    return ESP_FAIL;
}

FILE *st_fopen(char *f_path, char *mode)
{
    if (xSemaphoreTake(open_f_mutex, pdMS_TO_TICKS(f_open_timeout)) == pdTRUE)
    {
        FILE *f = NULL;
        if (max_open_f_len > open_f_len)
        {
            bool is_in_list = false;
            for (char **ptr = open_f; ptr - open_f < open_f_len && !is_in_list; ptr++)
                is_in_list = strcmp(*ptr, f_path) == 0;

            if (!is_in_list)
            {
                open_f[open_f_len] = strdup(f_path);
                open_f_len++;
                f = fopen(f_path, mode);
            }
        }
        xSemaphoreGive(open_f_mutex);
        return f;
    }
    return NULL;
}

esp_err_t st_fclose(FILE *f, char *f_path)
{

    if (xSemaphoreTake(open_f_mutex, pdMS_TO_TICKS(f_open_timeout)) != pdTRUE)
        return ESP_FAIL;
    esp_err_t res = ESP_FAIL;
    if (fclose(f) == 0)
    {
        bool move_value = false;
        for (char **ptr = open_f; ptr - open_f < open_f_len - 1; ptr++)
            if (strcmp(*ptr, f_path) == 0)
            {
                free(*ptr);
                move_value = true;
                *ptr = *(ptr + 1);
            }
            else if (move_value)
                *ptr = *(ptr + 1);

        open_f_len--;
        if (!move_value) 
            free(open_f[open_f_len]);

        res = ESP_OK;
    }
    xSemaphoreGive(open_f_mutex);
    return ESP_FAIL;
}

esp_err_t st_remove(char *f_path)
{

    if (xSemaphoreTake(open_f_mutex, pdMS_TO_TICKS(f_open_timeout)) != pdTRUE)
        return ESP_FAIL;

    esp_err_t res = ESP_FAIL;

    bool is_in_list = false;
    for (char **ptr = open_f; ptr - open_f < open_f_len && !is_in_list; ptr++)
        is_in_list = strcmp(*ptr, f_path) == 0;

    if (!is_in_list && remove(f_path) == 0)
        res = ESP_OK;
    xSemaphoreGive(open_f_mutex);

    return res;
}

uint8_t *read_file_to_buffer(const char *filename, size_t *out_size)
{
    FILE *file = fopen(filename, "rb");
    fseek(file, 0, SEEK_END);

    long file_size = ftell(file);

    fseek(file, 0, SEEK_SET);
    uint8_t *buffer = malloc(file_size + 1);

    size_t bytes_read = fread(buffer, 1, file_size, file);

    fclose(file);

    // Null-terminate the buffer for C-string function compatibility
    buffer[bytes_read] = '\0';
    *out_size = bytes_read;

    return buffer;
}

bool is_file(const char *path)
{
    struct stat buffer;
    if (stat(path, &buffer) == 0)
        return S_ISREG(buffer.st_mode);
    return false;
}