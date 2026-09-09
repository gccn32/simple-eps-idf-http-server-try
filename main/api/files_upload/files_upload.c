#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_err.h"
#include "esp_log.h"
#include "../error_handlers/error_handlers.h"
#include "../../services/request_counter.h"
#include "string.h"
#include "sys/stat.h"
#include "errno.h"
#include "../../helpers/fs_operations.h"

#define MAX_UPLOAD_FILE_NAME_LEN 100
static const char *TAG = "IMAGE-UPLOAD";
static char *f_dir_path = "/littlefs/temp";

static char *get_response(int received, char *f_name)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "received", received);
    cJSON_AddStringToObject(root, "fileName", f_name);
    cJSON_AddStringToObject(root, "status", "ok");
    char *res = cJSON_Print(root);
    cJSON_Delete(root);
    return res;
}

static void get_file_path(const char *path_to_dir, char *f_name, char *f_path, int f_path_len)
{
    snprintf(f_path, f_path_len, "%.50s/%.90s", path_to_dir, f_name);
    int i = 0;
    while (is_file(f_path))
    {
        i++;
        char *dot = strrchr(f_name, '.');
        if (dot == NULL)
            snprintf(f_path, f_path_len, "%.50s/%.90s(%d)", path_to_dir, f_name, i);
        else
        {
            char name[90];
            int name_len = dot - f_name;
            strncpy(name, f_name, name_len);
            name[name_len] = 0;
            snprintf(f_path, f_path_len, "%.50s/%.90s(%d)%.5s", path_to_dir, name, i, dot);
        }
    }
    ESP_EARLY_LOGI(TAG, "full name %s path %s", f_name, f_path);
}

static void remove_file(char *f_path)
{
    remove(f_dir_path);
}

esp_err_t upload_file_handler(httpd_req_t *req)
{
    int f_path_len = 150;
    char f_path[f_path_len];
    get_file_path(f_dir_path, "temp-file.txt", f_path, f_path_len);
    FILE *fd = fopen(f_path, "wb");

    int buf_len = 1024 * 16;
    uint8_t *buf = heap_caps_malloc(buf_len, MALLOC_CAP_SPIRAM);

    int remaining = req->content_len;
    int received = 0;

    ESP_LOGI(TAG, "Receiving file, total size: %d bytes", remaining);

    int i = 0;
    while (remaining > 0)
    {
        int bytes_to_read = remaining > buf_len ? buf_len : remaining;
        received = httpd_req_recv(req, (char *)buf, bytes_to_read);

        if (received <= 0)
        {
            i++;
            if (received == HTTPD_SOCK_ERR_TIMEOUT)
            {
                ESP_LOGW(TAG, "Network timeout flag caught, retrying packet fetch...");
                continue;
            }

            ESP_LOGE(TAG, "File reception failed or connection closed abruptly, remaining %d, content length %d, code %d, counter %d",
                     remaining, req->content_len, received, i);
            fclose(fd);
            free(buf);
            remove_file(f_path);
            return ESP_FAIL;
        }

        size_t written = fwrite(buf, 1, received, fd);
        remaining -= received;
    }

    fclose(fd);
    free(buf);

    ESP_LOGI(TAG, "Responded successfully");
    return httpd_resp_sendstr(req, "File uploaded successfully!");
}
