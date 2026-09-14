#include "esp_http_server.h"
#include "esp_log.h"
#include "../../../services/request_counter.h"
#include "../../error_handlers/error_handlers.h"
#include "../../lib/api_lib.h"
#include "../../../helpers/fs_operations.h"
#include "string.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static SemaphoreHandle_t async_download_f_sem = NULL;
static const char *TAG = "FILES-DOWNLOAD";
int f_read_timeout = 3000;

void init_file_download()
{
    async_download_f_sem = xSemaphoreCreateBinary();
    xSemaphoreGive(async_download_f_sem);
}

static void finish_task(httpd_req_t *req)
{
    httpd_req_async_handler_complete(req);
    xSemaphoreGive(async_download_f_sem);
    vTaskDelete(NULL);
}

static void file_download_handler(void *data)
{
    httpd_req_t *req = (httpd_req_t *)data;

    char f_name[60];
    int parsed = sscanf(req->uri, "/api/file-download/%50s", f_name);
    if (parsed != 1 || !f_name[0])
    {
        http_400_error_handler(req, "Request is wrong");
        finish_task(req);
    }

    if (strchr(f_name, '\\') != NULL || strchr(f_name, '/') != NULL || f_name[0] == '.')
    {
        http_400_error_handler(req, "Symbols \\ and / are not allowed. File name should now start from '.'");
        finish_task(req);
    }

    httpd_resp_set_type(req, get_mime_type(f_name));

    int f_path_len = 150;
    char f_path[150];
    snprintf(f_path, f_path_len, "%.20s/%.90s", temp_dir_path, f_name);

    if (!is_file(f_path))
    {
        http_404_error_handler(req, HTTPD_404_NOT_FOUND);
        finish_task(req);
    }

    int buf_len = 1024 * 256;
    uint8_t *buf = heap_caps_malloc(buf_len, MALLOC_CAP_SPIRAM);

    FILE *f = fopen(f_path, "rb");
    int bytes_read = 0;

    while (st_fread(buf, buf_len, f, &bytes_read, f_read_timeout) != ESP_FAIL)
    {
        if (bytes_read <= 0 || httpd_resp_send_chunk(req, (char *)buf, bytes_read) != ESP_OK)
            break;
    }
    httpd_resp_send_chunk(req, NULL, 0);
    fclose(f);
    free(buf);
    finish_task(req);
}

esp_err_t file_download_async(httpd_req_t *req)
{
    http_info_request_happen();
    if (xSemaphoreTake(async_download_f_sem, 0) != pdTRUE)
    {
        ESP_LOGW(TAG, "Async handler busy! Rejecting new request.");

        http_429_error_handler(req, "Server is busy processing another request.");
        return ESP_OK;
    }

    httpd_req_t *async_req = NULL;

    esp_err_t err = httpd_req_async_handler_begin(req, &async_req);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initiate async handler");
        http_500_error_handler(req, "Error happen during async file upload handling");
        xSemaphoreGive(async_download_f_sem);
        return err;
    }

    BaseType_t ret = xTaskCreatePinnedToCore(file_download_handler, "async_file_download", 4 * 1024, async_req, 5, NULL, 1);
    if (ret != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create worker task");
        httpd_req_async_handler_complete(async_req);
        http_500_error_handler(req, "Error happen during async file upload handling");
        xSemaphoreGive(async_download_f_sem);
        return ESP_FAIL;
    }

    return ESP_OK;
}