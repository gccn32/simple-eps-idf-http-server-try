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

#define SD_PIN_CMD GPIO_NUM_38
#define SD_PIN_CLK GPIO_NUM_39
#define SD_PIN_D0 GPIO_NUM_40

#define SD_MOUNT_POINT "/sdcard"

static const char *TAG = "FS-OPERATIONS";
static sdmmc_card_t *s_card = NULL;
const char temp_dir_path[] = "/sdcard/temp";

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
    host.flags = SDMMC_HOST_FLAG_1BIT;      // 1-bit bus mode required
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
    esp_err_t res_sd = mount_sd_card();
    esp_err_t res_littlefs = mount_littlefs();
    return res_sd == ESP_FAIL || res_littlefs == ESP_FAIL ? ESP_FAIL : ESP_OK;
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