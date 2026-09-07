#include "stdio.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_littlefs.h"
#include <sys/stat.h>
#include <errno.h>

static const char *TAG = "LITTLEFS_INIT";

static void create_temp_folder()
{
    char path[] = "/littlefs/temp";
    mkdir(path, 0755);
}

esp_err_t mount_littlefs()
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