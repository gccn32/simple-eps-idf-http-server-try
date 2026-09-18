
#include "esp_http_server.h"
#include "esp_log.h"
#include "../../../helpers/fs_operations.h"
#include "string.h"
#include "freertos/semphr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include "ping_pong_file_reader.h"

static int buf_len = 1024 * 64;
static int queue_capacity = 2;
static const char *TAG = "P-P-READER";
static int f_read_timeout = 5000;

static void sd_reader_task(void *pvParameters)
{
    reader_ctx_t *ctx = (reader_ctx_t *)pvParameters;
    chunk_msg_t msg;

    while (1)
    {
        if (xQueueReceive(ctx->empty_queue, &msg, pdMS_TO_TICKS(f_read_timeout)) == pdTRUE)
        {
            if (msg.len == -1)
                break;

            int bytes_read;
            if (st_fread(msg.ptr, buf_len, ctx->file, &bytes_read, f_read_timeout) == ESP_FAIL)
            {
                msg.len = -1;
                xQueueSend(ctx->data_queue, &msg, pdMS_TO_TICKS(f_read_timeout));
                break;
            }

            msg.len = bytes_read;

            if (xQueueSend(ctx->data_queue, &msg, pdMS_TO_TICKS(f_read_timeout)) != pdPASS)
                break;

            if (bytes_read == 0)
                break;
        }
        else
            break;
    }
    atomic_store(&ctx->worker_stopped, true);
    vTaskDelete(NULL);
}

esp_err_t get_p_p_data(p_p_descriptor_t *descriptor, chunk_msg_t *msg)
{
    if (xQueueReceive(descriptor->ctx->data_queue, msg, pdMS_TO_TICKS(f_read_timeout)) == pdTRUE)
        return ESP_OK;

    return ESP_FAIL;
}

esp_err_t request_p_p_data(p_p_descriptor_t *descriptor, chunk_msg_t *msg)
{
    if (xQueueSend(descriptor->ctx->empty_queue, msg, pdMS_TO_TICKS(f_read_timeout)) == pdTRUE)
        return ESP_OK;

    return ESP_FAIL;
}

static void stop_p_p(p_p_descriptor_t *descriptor)
{
    if (atomic_load(&descriptor->ctx->worker_stopped)) 
        return;
        
    xQueueReset(descriptor->ctx->empty_queue);
    xQueueReset(descriptor->ctx->data_queue);
    chunk_msg_t msg = {.len = -1};

    xQueueSend(descriptor->ctx->empty_queue, &msg, pdMS_TO_TICKS(100));
    int i = 0;
    int await_time = 20;
    while (!atomic_load(&descriptor->ctx->worker_stopped))
    {
        vTaskDelay(pdMS_TO_TICKS(await_time));
        i++;
        if (i > f_read_timeout/await_time + 5)
        {
            ESP_LOGI(TAG, "No way to wait until task is stopped");
            break;
        }
    }
    ESP_LOGI(TAG, "Task was stopped");
}

void delete_p_p_reader(p_p_descriptor_t *descriptor)
{
    stop_p_p(descriptor);

    fclose(descriptor->ctx->file);
    vQueueDelete(descriptor->ctx->data_queue);
    vQueueDelete(descriptor->ctx->empty_queue);
    free(descriptor->buf_a);
    free(descriptor->buf_b);
    free(descriptor->ctx);
    free(descriptor);
}

p_p_descriptor_t *init_p_p_reader(char *f_path)
{
    FILE *f = fopen(f_path, "rb");
    uint8_t *buf_a = heap_caps_malloc(buf_len, MALLOC_CAP_SPIRAM);
    uint8_t *buf_b = heap_caps_malloc(buf_len, MALLOC_CAP_SPIRAM);

    if (!buf_a || !buf_b || !f)
    {
        ESP_LOGE(TAG, "Resource allocation failed");
        if (buf_a)
            free(buf_a);
        if (buf_b)
            free(buf_b);
        if (f)
            fclose(f);
        return NULL;
    }

    QueueHandle_t data_queue = xQueueCreate(queue_capacity, sizeof(chunk_msg_t));
    QueueHandle_t empty_queue = xQueueCreate(queue_capacity, sizeof(chunk_msg_t));

    chunk_msg_t seed_a = {.ptr = buf_a, .len = 0};
    chunk_msg_t seed_b = {.ptr = buf_b, .len = 0};

    xQueueSend(empty_queue, &seed_a, 0);
    xQueueSend(empty_queue, &seed_b, 0);

    reader_ctx_t *reader_ctx = malloc(sizeof(reader_ctx_t));
    reader_ctx->file = f;
    reader_ctx->data_queue = data_queue;
    reader_ctx->empty_queue = empty_queue;

    p_p_descriptor_t *descriptor = malloc(sizeof(p_p_descriptor_t));

    descriptor->buf_a = buf_a;
    descriptor->buf_b = buf_b;
    descriptor->ctx = reader_ctx;
    atomic_init(&descriptor->ctx->worker_stopped, false);
    BaseType_t ret = xTaskCreatePinnedToCore(sd_reader_task, "sd_reader", 4 * 1024, reader_ctx, 6, NULL, 1);
    
    if (ret != pdPASS)
    {
        atomic_store(&descriptor->ctx->worker_stopped, true);
        delete_p_p_reader(descriptor);
        return NULL;
    }
    return descriptor;
}