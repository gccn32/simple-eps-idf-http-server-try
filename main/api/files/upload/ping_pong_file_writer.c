
#include "esp_http_server.h"
#include "esp_log.h"
#include "../../../helpers/fs_operations.h"
#include "string.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "stdatomic.h"
#include "esp_err.h"
#include "ping_pong_file_writer.h"

static int buf_len = 1024 * 256;
static int queue_capacity = 2;
static const char *TAG = "P-P-WRITER";
static int f_write_timeout = 5000;

static void get_file_path(const char *path_to_dir, char *f_name, char *f_path, int f_path_len)
{
    snprintf(f_path, f_path_len, "%.50s/%.90s", path_to_dir, f_name);
    uint16_t i = 0;
    char *dot = strrchr(f_name, '.');

    while (is_file(f_path))
    {
        i++;
        if (dot == NULL)
            snprintf(f_path, f_path_len, "%.50s/%.50s(%d)", path_to_dir, f_name, i);
        else
        {
            char name[90];
            int name_len = dot - f_name;
            strncpy(name, f_name, name_len);
            name[name_len] = 0;
            snprintf(f_path, f_path_len, "%.50s/%.50s(%d)%.5s", path_to_dir, name, i, dot);
        }
    }
    ESP_EARLY_LOGI(TAG, "full name %s path %s", f_name, f_path);
}

static void sd_writer_task(void *pvParameters)
{
    writer_ctx_t *ctx = (writer_ctx_t *)pvParameters;
    chunk_msg_t msg;
    FILE *file = NULL;
    while (1)
    {
        if (xQueueReceive(ctx->data_queue, &msg, pdMS_TO_TICKS(f_write_timeout)) == pdTRUE)
        {

            if (msg.close_prev_file && file != NULL)
            {
                ESP_EARLY_LOGI(TAG, "File has been closed");
                fclose(file);
            }
            if (msg.len == -1)
                break;

            file = msg.file;
            if (st_write(msg.ptr, msg.len, msg.file, f_write_timeout) == ESP_FAIL)
            {
                msg.len = -1;
                xQueueSend(ctx->empty_queue, &msg, pdMS_TO_TICKS(f_write_timeout));
                ESP_EARLY_LOGI(TAG, "sd_writer_task: Writing was not successful %d", msg.len);

                break;
            }
            // ESP_EARLY_LOGI(TAG, "Chunk has been saved from sd_writer_task %d", msg.len);

            msg.len = 0;

            if (xQueueSend(ctx->empty_queue, &msg, pdMS_TO_TICKS(f_write_timeout)) != pdPASS)
            {
                ESP_EARLY_LOGI(TAG, "sd_writer_task: sending back empty message was not successful");
                break;
            }
        }
        else
            break;
    }
    atomic_store(&ctx->worker_stopped, true);
    ESP_LOGI(TAG, "sd_writer_task: Writer task has been stopped");
    vTaskDelete(NULL);
}
esp_err_t write_p_p_data(p_p_descriptor_t *descriptor, char *f_name, uint8_t *buf, int data_len)
{
    int f_path_len = 150;
    char f_path[f_path_len];
    // ESP_EARLY_LOGI(TAG, "write_p_p_data f_name: %s, data_len %d", f_name, data_len);

    if (descriptor->f_path == NULL)
    {
        get_file_path(temp_dir_path, f_name, f_path, f_path_len);
        descriptor->f_path = strdup(f_path);
        descriptor->f_name = strdup(f_name);
        descriptor->file = fopen(descriptor->f_path, "wb");
        // ESP_EARLY_LOGI(TAG, "Fopen has been called %s", descriptor->f_path);

        if (xQueueReceive(descriptor->ctx->empty_queue, descriptor->cur_msg, pdMS_TO_TICKS(f_write_timeout)) == pdTRUE)
        {
            memcpy(descriptor->cur_msg->ptr, buf, data_len);
            descriptor->cur_msg->len = data_len;
            descriptor->cur_msg->file = descriptor->file;
            descriptor->cur_msg->close_prev_file = false;
        }
        else
            return ESP_FAIL;
    }
    else if (strcmp(descriptor->f_name, f_name) == 0)
    {
        if (buf_len - descriptor->cur_msg->len < data_len)
        {
            // descriptor->cur_msg->close_prev_file = false;
            // ESP_EARLY_LOGI(TAG, "write_p_p_data when files names are the same data chunk has been save %d", descriptor->cur_msg->len);
            if (xQueueSend(descriptor->ctx->data_queue, descriptor->cur_msg, pdMS_TO_TICKS(f_write_timeout)) != pdTRUE)
                return ESP_FAIL;
            if (xQueueReceive(descriptor->ctx->empty_queue, descriptor->cur_msg, pdMS_TO_TICKS(f_write_timeout)) == pdTRUE)
            {
                memcpy(descriptor->cur_msg->ptr, buf, data_len);
                descriptor->cur_msg->len = data_len;
                descriptor->cur_msg->close_prev_file = false;
                descriptor->cur_msg->file = descriptor->file;
            }
            else
                return ESP_FAIL;
        }
        else
        {
            memcpy(descriptor->cur_msg->ptr + descriptor->cur_msg->len, buf, data_len);
            descriptor->cur_msg->len += data_len;
        }
    }
    else if (strcmp(descriptor->f_name, f_name) != 0)
    {
        // ESP_EARLY_LOGI(TAG, "data chunk has been saved when names are different %d", descriptor->cur_msg->len);
        if (xQueueSend(descriptor->ctx->data_queue, descriptor->cur_msg, pdMS_TO_TICKS(f_write_timeout)) != pdTRUE)
            return ESP_FAIL;

        if (xQueueReceive(descriptor->ctx->empty_queue, descriptor->cur_msg, pdMS_TO_TICKS(f_write_timeout)) == pdTRUE)
        {
            free(descriptor->f_name);
            free(descriptor->f_path);
            get_file_path(temp_dir_path, f_name, f_path, f_path_len);
            descriptor->f_path = strdup(f_path);
            descriptor->f_name = strdup(f_name);

            memcpy(descriptor->cur_msg->ptr, buf, data_len);
            descriptor->cur_msg->len = data_len;
            descriptor->file = fopen(f_path, "wb");
            // ESP_EARLY_LOGI(TAG, "fopen was called when f_names are different %s, %s", f_path, f_name);

            descriptor->cur_msg->file = descriptor->file;
            descriptor->cur_msg->close_prev_file = true;
        }
        else
            return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t complete_p_p_upload(p_p_descriptor_t *descriptor)
{
    if (descriptor->cur_msg->file != NULL)
    {
        // ESP_EARLY_LOGI(TAG, "complete_p_p_upload Data chunk has been saved %d ", descriptor->cur_msg->len);

        if (xQueueSend(descriptor->ctx->data_queue, descriptor->cur_msg, pdMS_TO_TICKS(f_write_timeout)) != pdTRUE)
            return ESP_FAIL;

        descriptor->cur_msg->close_prev_file = true;
        descriptor->cur_msg->len = -1;
        if (xQueueSend(descriptor->ctx->data_queue, descriptor->cur_msg, pdMS_TO_TICKS(f_write_timeout)) != pdTRUE)
            return ESP_FAIL;
        ESP_EARLY_LOGI(TAG, "complete_p_p_upload Complete has been called, f_name %s, ", descriptor->f_name);
    }

    descriptor->cur_msg->file = NULL;
    descriptor->file = NULL;
    free(descriptor->f_path);
    descriptor->f_path = NULL;
    return ESP_OK;
}
static void remove_file(p_p_descriptor_t *descriptor)
{
    if (descriptor->file != NULL)
    {
        fclose(descriptor->file);
        descriptor->file = NULL;
    }
    if (descriptor->f_path != NULL)
    {
        ESP_EARLY_LOGI(TAG, "remove_file: File was removed %s, ", descriptor->f_path);
        remove(descriptor->f_path);
    }
}

static void stop_p_p(p_p_descriptor_t *descriptor)
{
    if (atomic_load(&descriptor->ctx->worker_stopped))
    {
        remove_file(descriptor);
        ESP_LOGI(TAG, "stop_p_p File has been removed from beginning");
        return;
    }

    chunk_msg_t msg = {.len = -1};

    xQueueSend(descriptor->ctx->data_queue, &msg, pdMS_TO_TICKS(f_write_timeout));
    int i = 0;
    int await_time = 20;
    while (!atomic_load(&descriptor->ctx->worker_stopped))
    {
        vTaskDelay(pdMS_TO_TICKS(await_time));
        i++;
        if (i > f_write_timeout / await_time + 5)
        {
            ESP_LOGI(TAG, "No way to wait until task is stopped");
            break;
        }
    }
    remove_file(descriptor);
}

void delete_p_p_writer(p_p_descriptor_t *descriptor)
{
    stop_p_p(descriptor);
    vQueueDelete(descriptor->ctx->data_queue);
    vQueueDelete(descriptor->ctx->empty_queue);
    free(descriptor->buf_a);
    free(descriptor->buf_b);
    if (descriptor->f_path != NULL)
        free(descriptor->f_path);
    if (descriptor->f_name != NULL)
        free(descriptor->f_name);
    free(descriptor->ctx);
    free(descriptor->cur_msg);
    free(descriptor);
}

p_p_descriptor_t *init_p_p_writer()
{
    uint8_t *buf_a = heap_caps_malloc(buf_len, MALLOC_CAP_SPIRAM);
    uint8_t *buf_b = heap_caps_malloc(buf_len, MALLOC_CAP_SPIRAM);

    if (!buf_a || !buf_b)
    {
        ESP_LOGE(TAG, "Resource allocation failed");
        if (buf_a)
            free(buf_a);
        if (buf_b)
            free(buf_b);
        return NULL;
    }

    QueueHandle_t data_queue = xQueueCreate(queue_capacity, sizeof(chunk_msg_t));
    QueueHandle_t empty_queue = xQueueCreate(queue_capacity, sizeof(chunk_msg_t));

    chunk_msg_t seed_a = {
        .ptr = buf_a,
        .len = 0,
        .close_prev_file = false,
    };
    chunk_msg_t seed_b = {
        .ptr = buf_b,
        .len = 0,
        .close_prev_file = false,
    };

    xQueueSend(empty_queue, &seed_a, 0);
    xQueueSend(empty_queue, &seed_b, 0);

    writer_ctx_t *writer_ctx = malloc(sizeof(writer_ctx_t));
    writer_ctx->data_queue = data_queue;
    writer_ctx->empty_queue = empty_queue;

    p_p_descriptor_t *descriptor = malloc(sizeof(p_p_descriptor_t));

    descriptor->buf_a = buf_a;
    descriptor->buf_b = buf_b;
    descriptor->ctx = writer_ctx;
    descriptor->file = NULL;
    descriptor->f_path = NULL;
    descriptor->cur_msg = malloc(sizeof(chunk_msg_t));
    descriptor->cur_msg->file = NULL;
    descriptor->cur_msg->close_prev_file = false;

    atomic_init(&descriptor->ctx->worker_stopped, false);
    BaseType_t ret = xTaskCreatePinnedToCore(sd_writer_task, "sd_writer", 4 * 1024, writer_ctx, 6, NULL, 1);

    if (ret != pdPASS)
    {
        atomic_store(&descriptor->ctx->worker_stopped, true);
        delete_p_p_writer(descriptor);
        return NULL;
    }
    return descriptor;
}