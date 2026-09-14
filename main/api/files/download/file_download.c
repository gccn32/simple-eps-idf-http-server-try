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
static int buf_len = 1024 * 64;
static int queue_capacity = 2;
static const char *TAG = "FILES-DOWNLOAD";
int f_read_timeout = 5000;

typedef struct
{
    uint8_t *ptr;
    int length;
} chunk_msg_t;

typedef struct
{
    FILE *file;
    QueueHandle_t data_queue;
    QueueHandle_t empty_queue;
} reader_ctx_t;

void init_file_download()
{
    async_download_f_sem = xSemaphoreCreateBinary();
    xSemaphoreGive(async_download_f_sem);
}

static void sd_reader_task(void *pvParameters)
{
    reader_ctx_t *ctx = (reader_ctx_t *)pvParameters;
    chunk_msg_t msg;

    while (1)
    {
        if (ctx != NULL && ctx->empty_queue != NULL && xQueueReceive(ctx->empty_queue, &msg, pdMS_TO_TICKS(f_read_timeout)) == pdTRUE)
        {
            if (msg.length == -1)
                break;

            int bytes_read;
            if (st_fread(msg.ptr, buf_len, ctx->file, &bytes_read, f_read_timeout) == ESP_FAIL)
                break;

            msg.length = bytes_read;

            if (xQueueSend(ctx->data_queue, &msg, pdMS_TO_TICKS(f_read_timeout)) != pdPASS)
                break;

            if (bytes_read == 0)
                break;
        }
        else
            break;
    }

    vTaskDelete(NULL);
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

    int f_path_len = 150;
    char f_path[150];
    snprintf(f_path, f_path_len, "%.20s/%.90s", temp_dir_path, f_name);

    if (!is_file(f_path))
    {
        http_404_error_handler(req, HTTPD_404_NOT_FOUND);
        finish_task(req);
    }

    uint8_t *buf_a = heap_caps_malloc(buf_len, MALLOC_CAP_SPIRAM);
    uint8_t *buf_b = heap_caps_malloc(buf_len, MALLOC_CAP_SPIRAM);

    FILE *f = fopen(f_path, "rb");

    if (!buf_a || !buf_b || !f)
    {
        ESP_LOGE(TAG, "Resource allocation failed");
        if (buf_a)
            free(buf_a);
        if (buf_b)
            free(buf_b);
        if (f)
            fclose(f);
        http_500_error_handler(req, "Resource allocation failed");
        finish_task(req);
    }
    httpd_resp_set_type(req, get_mime_type(f_name));

    QueueHandle_t data_queue = xQueueCreate(queue_capacity, sizeof(chunk_msg_t));
    QueueHandle_t empty_queue = xQueueCreate(queue_capacity, sizeof(chunk_msg_t));

    chunk_msg_t seed_a = {.ptr = buf_a, .length = 0};
    chunk_msg_t seed_b = {.ptr = buf_b, .length = 0};
    xQueueSend(empty_queue, &seed_a, 0);
    xQueueSend(empty_queue, &seed_b, 0);

    reader_ctx_t reader_ctx = {.file = f, .data_queue = data_queue, .empty_queue = empty_queue};
    TaskHandle_t reader_task_h = NULL;
    BaseType_t ret = xTaskCreatePinnedToCore(sd_reader_task, "sd_reader", 4 * 1024, &reader_ctx, 5, &reader_task_h, 1);
    if (ret != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create sd_task task");
        http_500_error_handler(req, "Error happen during async file download handling");
        fclose(f);
        vQueueDelete(data_queue);
        vQueueDelete(empty_queue);
        free(buf_a);
        free(buf_b);
        finish_task(req);
    }
    chunk_msg_t tx_msg;
    esp_err_t err = ESP_OK;

    while (1)
    {
        if (xQueueReceive(data_queue, &tx_msg, pdMS_TO_TICKS(f_read_timeout)) == pdTRUE)
        {
            if (tx_msg.length <= 0)
                break;

            err = httpd_resp_send_chunk(req, (char *)tx_msg.ptr, tx_msg.length);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Network chunk send failed or client disconnected");
                xQueueReceive(data_queue, &tx_msg, pdMS_TO_TICKS(f_read_timeout));
                tx_msg.length = -1;
                xQueueSend(empty_queue, &tx_msg, pdMS_TO_TICKS(100));
                break;
            }

            if (xQueueSend(empty_queue, &tx_msg, pdMS_TO_TICKS(f_read_timeout)) != pdTRUE)
                break;
        }
        else
            break;
    }

    // Finalise HTTP stream
    httpd_resp_send_chunk(req, NULL, 0);

    fclose(f);
    vQueueDelete(data_queue);
    vQueueDelete(empty_queue);
    free(buf_a);
    free(buf_b);

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