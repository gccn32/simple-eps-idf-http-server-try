#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "string.h"
#include "errno.h"
#include "../../error_handlers/error_handlers.h"
#include "../../../services/request_counter.h"
#include "../../../helpers/fs_operations.h"
#include "freertos/semphr.h"
#include "ping_pong_file_writer.h"

#define MAX_UPLOAD_FILE_NAME_LEN 50
static const char *TAG = "FILES-UPLOAD";
static int max_payload_size = 1024 * 1024 * 100;
static SemaphoreHandle_t async_upload_f_sem = NULL;

typedef enum
{
    B_HEADER_PARSE_OK,
    B_HEADER_PARSE_FAIL,
    B_HEADER_END_NOT_FOUND,
    B_HEADER_LAST_HEADER,
    B_HEADER_START_NOT_FOUND,
    RETRIEVE_DATA_FAIL,
    RETRIEVE_DATA_OK,

} b_header_parse_status_t;

static void replace_all_chars(char *str)
{
    // TODO: add replacing for / and \;
    char old_char[] = {' ', '/', '\\'};
    char new_char[] = {'-', '-', '-'};

    for (int i = 0; str[i] != '\0'; i++)
        for (int c = 0; c < sizeof(old_char); c++)
            if (str[i] == old_char[c])
                str[i] = new_char[c];
}

static char *get_response(int post_size, char **f_list, int f_list_len, bool data_corrupted)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "postSize", post_size);
    cJSON_AddStringToObject(root, "status", "ok");
    for (int i = 0; i < f_list_len; i++)
        ESP_EARLY_LOGI(TAG, "File: \"%s\", file name length: %d", f_list[i], strlen(f_list[i]));

    cJSON *json_f_list = cJSON_CreateStringArray((const char *const *)f_list, f_list_len);
    cJSON_AddItemToObject(root, "files", json_f_list);
    cJSON_AddBoolToObject(root, "dataCorrupted", data_corrupted);
    char *res = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return res;
}

static b_header_parse_status_t parse_boundary_header(char *b_token, int b_token_len, int boundary_num, uint8_t **f_start, int buf_len, char *f_name,
                                                     uint8_t **prev_f_end)
{
    uint8_t *boundary_start = memmem(*f_start, buf_len, b_token, b_token_len);

    if (boundary_start == NULL)
        return B_HEADER_START_NOT_FOUND;

    if (boundary_start - *f_start + b_token_len + 2 <= buf_len && (boundary_start + b_token_len)[0] == '-' && (boundary_start + b_token_len)[1] == '-')
    {
        if (boundary_num != 0)
            *prev_f_end = boundary_start - 2;
        return B_HEADER_LAST_HEADER;
    }

    char boundary_header_end[] = "\r\n\r\n";
    int boundary_header_end_len = 4;
    uint8_t *boundary_end = memmem(boundary_start, buf_len - (boundary_start - *f_start), boundary_header_end, boundary_header_end_len);
    if (boundary_end == NULL)
        return B_HEADER_END_NOT_FOUND;

    char *file_name_attr = "filename=\"";
    int file_name_attr_len = 10;
    uint8_t *file_name_attr_start = memmem(boundary_start, boundary_end - boundary_start, file_name_attr, file_name_attr_len);
    if (file_name_attr_start == NULL)
        return B_HEADER_PARSE_FAIL;

    uint8_t *file_name_start = file_name_attr_start + file_name_attr_len;
    uint8_t *file_name_end = memchr(file_name_start, '"', boundary_end - file_name_start);
    if (file_name_end == NULL || file_name_end - file_name_start > 70 || file_name_end - file_name_start == 1)
        return B_HEADER_PARSE_FAIL;

    int file_name_len = file_name_end - file_name_start;
    if (file_name_len >= MAX_UPLOAD_FILE_NAME_LEN - 1)
    {
        file_name_len = MAX_UPLOAD_FILE_NAME_LEN - 1;
        file_name_start = file_name_end - file_name_len;
    }
    // do not change
    strncpy(f_name, (char *)file_name_start, file_name_len);
    f_name[file_name_len] = 0;
    replace_all_chars(f_name);

    *f_start = boundary_end + boundary_header_end_len;

    if (boundary_num != 0)
        *prev_f_end = boundary_start - 2;
    return B_HEADER_PARSE_OK;
}

static void add_file_to_list(char *f_name, char ***f_list, int *f_list_len, int *cur_f_in_list)
{
    if (*cur_f_in_list >= *f_list_len)
    {
        *f_list_len = (*f_list_len / 5 + 1) * 5;
        char **temp_f_list = realloc(*f_list, *f_list_len * sizeof(char *));
        *f_list = temp_f_list;
    }
    (*f_list)[*cur_f_in_list] = strdup(f_name);
    // ESP_LOGI(TAG, "f_name %s, cur_f_in_list %d, f_list[cur_f_in_list] %s", f_name, *cur_f_in_list, (*f_list)[*cur_f_in_list]);

    (*cur_f_in_list)++;
}

static char *get_boundary(char *header)
{
    char *boundary_left_part = "boundary=";
    char *res = strstr(header, boundary_left_part);
    if (res == NULL)
        return NULL;
    char *boundary = res + strlen(boundary_left_part) - 2;
    boundary[0] = '-';
    boundary[1] = '-';
    return boundary;
}

static b_header_parse_status_t retrieve_data(httpd_req_t *req, uint8_t *f_read_buf, uint8_t *buf, int buf_len, int *remaining, int *received)
{
    int requested_data_len = buf_len - (f_read_buf - buf);
    int ret = 0;
    *received = 0;
    int i = 0;
    do
    {
        ret = httpd_req_recv(req, (char *)f_read_buf + *received, requested_data_len - *received);
        //  = fread(f_read_buf + *retrieved_for_parsing, 1, requested_data_len, f);
        if (ret <= 0)
        {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT)
            {
                i++;
                if (i > 2)
                    return RETRIEVE_DATA_FAIL;

                continue;
            }
            return RETRIEVE_DATA_FAIL;
        }
        *received += ret;
        *remaining -= ret;
    } while (*remaining > 0 && *received < requested_data_len);
    // ESP_EARLY_LOGI(TAG, "*retrieved_for_parsing after data received %d, requested data len %d", *received, requested_data_len);
    return RETRIEVE_DATA_OK;
}

static void finish_task(httpd_req_t *req)
{
    httpd_req_async_handler_complete(req);
    xSemaphoreGive(async_upload_f_sem);
    vTaskDelete(NULL);
}

static void upload_file_handler(void *arg)
{
    httpd_req_t *req = (httpd_req_t *)arg;
    if (req->content_len <= 0)
    {
        http_400_error_handler(req, "Content-Length is not provided in request");
        finish_task(req);
    }
    const int content_len = req->content_len;
    if (req->content_len > max_payload_size)
    {
        http_413_error_handler(req, max_payload_size, req->content_len);
        finish_task(req);
    }

    int content_type_len = 201;
    char content_type[content_type_len];
    if (httpd_req_get_hdr_value_str(req, "Content-Type", content_type, content_type_len - 1) != ESP_OK)
    {
        http_400_error_handler(req, "Content-Type is not provided in request");
        finish_task(req);
    }

    char *b_token = get_boundary(content_type);
    if (b_token == NULL)
    {
        http_400_error_handler(req, "Boundary is not provided in request");
        finish_task(req);
    }

    int b_token_len = strlen(b_token);

    int f_list_len = 0;
    char **f_list = NULL;
    int cur_f_in_list = 0;

    char f_name[MAX_UPLOAD_FILE_NAME_LEN];
    char prev_f_name[MAX_UPLOAD_FILE_NAME_LEN];
    int cur_boundary = 0;

    int max_boundary_len = 250;
    int buf_len = 1024 * 15;
    uint8_t *buf = malloc(buf_len + max_boundary_len);

    uint8_t *f_start = buf;
    uint8_t *f_read_buf = buf;
    uint8_t *prev_f_start = NULL;
    uint8_t *prev_f_end = NULL;
    int remaining = req->content_len;
    int received = 0;

    ESP_LOGI(TAG, "Receiving file, total size: %d bytes, token %s", remaining, b_token);

    bool data_corrupted = false;
    bool is_last_b = false;
    bool is_in_file = false;
    p_p_descriptor_t *descriptor = init_p_p_writer();
    if (descriptor == NULL)
    {
        http_500_error_handler(req, "Internal server error");
        free(buf);
        finish_task(req);
    }

    do
    {
        received = 0;
        b_header_parse_status_t status = retrieve_data(req, f_read_buf, buf, buf_len, &remaining, &received);
        // ESP_LOGI(TAG, "remaining %d received %d, requested data len", remaining, received, buf_len - (f_read_buf - buf));

        if (status == RETRIEVE_DATA_FAIL)
        {
            ESP_EARLY_LOGI(TAG, "Fail to retrieve data");
            data_corrupted = true;
            break;
        }

        do
        {
            const int parsed_data_len = received + (f_read_buf - buf) - (f_start - buf);
            b_header_parse_status_t res =
                parse_boundary_header(b_token, b_token_len, cur_boundary, &f_start, parsed_data_len, f_name, &prev_f_end);
            // ESP_LOGI(TAG, "res %d, parsed_data_len %d f_name %s", res, parsed_data_len, f_name);

            if (res == B_HEADER_END_NOT_FOUND || res == B_HEADER_START_NOT_FOUND)
            {
                if (remaining == 0 || cur_boundary == 0)
                {
                    data_corrupted = true;
                    break;
                }
                else
                {
                    prev_f_end = prev_f_start > f_read_buf + received - max_boundary_len ? prev_f_start : f_read_buf + received - max_boundary_len;
                    int chunk_len = prev_f_end - prev_f_start;

                    write_p_p_data(descriptor, prev_f_name, prev_f_start, chunk_len);

                    int shift = f_read_buf + received - prev_f_end;
                    memmove(buf, prev_f_end, shift);
                    f_start = buf;
                    f_read_buf = buf + shift;
                    prev_f_start = buf;
                    is_in_file = true;
                }
            }
            else if (res == B_HEADER_PARSE_OK || res == B_HEADER_LAST_HEADER)
            {
                if (cur_boundary > 0)
                {
                    int chunk_len = prev_f_end - prev_f_start;
                    if (chunk_len < 0)
                    {
                        data_corrupted = true;
                        break;
                    }
                    write_p_p_data(descriptor, prev_f_name, prev_f_start, chunk_len);

                    add_file_to_list(prev_f_name, &f_list, &f_list_len, &cur_f_in_list);
                }
                prev_f_start = f_start;
                cur_boundary++;

                strncpy(prev_f_name, f_name, MAX_UPLOAD_FILE_NAME_LEN - 1);
                prev_f_name[MAX_UPLOAD_FILE_NAME_LEN - 1] = 0;

                is_last_b = res == B_HEADER_LAST_HEADER;
                is_in_file = false;
            }
            else if (res == B_HEADER_PARSE_FAIL)
            {
                data_corrupted = true;
                break;
            }
        } while (!is_last_b && !is_in_file && !data_corrupted);
    } while (remaining > 0 && !data_corrupted && !is_last_b);

    if (remaining == 0 && !data_corrupted)
        complete_p_p_upload(descriptor);

    char *response = get_response(content_len, f_list, cur_f_in_list, data_corrupted);

    for (int i = 0; i < cur_f_in_list; i++)
        free(f_list[i]);

    free(buf);
    if (f_list != NULL)
        free(f_list);
    delete_p_p_writer(descriptor);

    ESP_LOGI(TAG, "Responded successfully");
    httpd_resp_set_type(req, HTTPD_TYPE_JSON);

    httpd_resp_sendstr(req, response);
    free(response);

    finish_task(req);
}

esp_err_t file_upload_async(httpd_req_t *req)
{
    http_info_request_happen();

    if (xSemaphoreTake(async_upload_f_sem, 0) != pdTRUE)
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
        xSemaphoreGive(async_upload_f_sem);
        return err;
    }

    BaseType_t ret = xTaskCreatePinnedToCore(upload_file_handler, "async_file_upload", 4 * 1024, async_req, 5, NULL, 1);
    if (ret != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create worker task");
        httpd_req_async_handler_complete(async_req);
        http_500_error_handler(req, "Error happen during async file upload handling");
        xSemaphoreGive(async_upload_f_sem);
        return ESP_FAIL;
    }

    return ESP_OK;
}

void initialize_file_upload()
{
    async_upload_f_sem = xSemaphoreCreateBinary();
    xSemaphoreGive(async_upload_f_sem);
}