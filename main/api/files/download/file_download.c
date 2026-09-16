#include "esp_http_server.h"
#include "esp_log.h"
#include "../../../services/request_counter.h"
#include "../../error_handlers/error_handlers.h"
#include "../../lib/api_lib.h"
#include "../../../helpers/fs_operations.h"
#include "string.h"
#include "freertos/semphr.h"
#include "ping_pong_file_reader.h"

static SemaphoreHandle_t async_download_f_sem = NULL;
static const char *TAG = "FILES-DOWNLOAD";

void init_file_download()
{
    async_download_f_sem = xSemaphoreCreateCounting(2, 2);
    // xSemaphoreGive(async_download_f_sem);
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
        http_400_error_handler(req, "Symbols \\ and / are not allowed. File name should not start from '.'");
        finish_task(req);
    }

    int f_path_len = 150;
    char f_path[150];
    snprintf(f_path, f_path_len, "%.40s/%.90s", temp_dir_path, f_name);

    if (!is_file(f_path))
    {
        http_404_error_handler(req, HTTPD_404_NOT_FOUND);
        finish_task(req);
    }

    p_p_descriptor_t *descriptor = init_p_p_reader(f_path);
    if (descriptor == NULL)
    {
        http_500_error_handler(req, "Resource allocation failed");
        finish_task(req);
    }

    httpd_resp_set_type(req, get_mime_type(f_name));

    chunk_msg_t msg;
    esp_err_t err = ESP_OK;

    while (1)
    {
        if (get_p_p_data(descriptor, &msg) == ESP_OK)
        {
            if (msg.length <= 0)
                break;

            err = httpd_resp_send_chunk(req, (char *)msg.ptr, msg.length);
            if (err != ESP_OK)
            {
                xSemaphoreGive(async_download_f_sem);
                break;
            }

            if (request_p_p_data(descriptor, &msg) != ESP_OK)
                break;
        }
        else
            break;
    }

    if (err == ESP_OK)
        httpd_resp_send_chunk(req, NULL, 0);
    delete_p_p_reader(descriptor);

    ESP_LOGI(TAG, "Download finished. Releasing lock.");

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
        http_500_error_handler(req, "Error happen during async file download handling");
        xSemaphoreGive(async_download_f_sem);
        return err;
    }

    BaseType_t ret = xTaskCreatePinnedToCore(file_download_handler, "async_file_download", 4 * 1024, async_req, 5, NULL, 1);
    if (ret != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create worker task");
        httpd_req_async_handler_complete(async_req);
        http_500_error_handler(req, "Error happen during async file download handling");
        xSemaphoreGive(async_download_f_sem);
        return ESP_FAIL;
    }

    return ESP_OK;
}