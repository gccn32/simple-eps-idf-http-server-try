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
static const int max_payload_size = 1024 * 1024 * 5;
static char *f_dir_path = "/littlefs/temp";

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

static char *get_response(int post_size, char **f_list, int f_list_len)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "postSize", post_size);
    cJSON_AddStringToObject(root, "status", "ok");
    for (int i = 0; i < f_list_len; i++)
    {
        ESP_EARLY_LOGI(TAG, "File: \"%s\", file name length: %d", f_list[i], strlen(f_list[i]));
    }
    cJSON *json_f_list = cJSON_CreateStringArray((const char *const *)f_list, f_list_len);
    cJSON_AddItemToObject(root, "files", json_f_list);
    char *res = cJSON_Print(root);
    cJSON_Delete(root);
    return res;
}

static char *get_boundary(char *header)
{
    char boundary_left_part[] = "boundary=";
    char *res = strstr(header, boundary_left_part);
    if (res == NULL)
    {
        return NULL;
    }
    char *boundary = res + sizeof(boundary_left_part) - 1;
    char *payload_boundary = malloc(sizeof(boundary) + 2);
    sprintf(payload_boundary, "--%s", boundary);
    return payload_boundary;
}

static void get_file_path(const char *path_to_dir, char *f_name, char *f_path, int f_path_len)
{
    snprintf(f_path, f_path_len, "%s/%.90s", path_to_dir, f_name);
    ESP_EARLY_LOGI(TAG, "full name \"%s\", path \"%s\"", f_name, f_path);

    int i = 0;
    while (is_file(f_path))
    {
        i++;
        char *dot = strrchr(f_name, '.');
        if (dot == NULL)
            snprintf(f_path, f_path_len, "%s/%.90s(%d)", path_to_dir, f_name, i);
        else
        {
            char name[90];
            int name_len = dot - f_name;
            strncpy(name, f_name, name_len);
            name[name_len] = 0;
            snprintf(f_path, f_path_len, "%s/%.90s(%d)%.5s", path_to_dir, name, i, dot);
            ESP_EARLY_LOGI(TAG, "full name %s, dot %s, path %s", f_name, dot, f_path);
        }
    }
}

static b_header_parse_status_t retrieve_data(httpd_req_t *req, uint8_t *f_read_buf, uint8_t *buf, int buf_len, int *total_received, int *retrieved_for_parsing, int content_len)
{
    // int max_retrieve_len = 2048 > buf_len - (f_read_buf - buf) ? buf_len - (f_read_buf - buf) : 2048;
    int max_retrieve_len = buf_len - (f_read_buf - buf);
    int min_package_len = 1000;
    do
    {
        int requested_data_len = max_retrieve_len - *retrieved_for_parsing;
        ESP_EARLY_LOGI(TAG, "*retrieved_for_parsing %d, requested data len %d, f_read_buf %p, buf %p, used space %d ",
                       *retrieved_for_parsing, requested_data_len, f_read_buf, buf, f_read_buf - buf);

        int ret = httpd_req_recv(req, ((char *)f_read_buf) + *retrieved_for_parsing, requested_data_len);
        //  = fread(f_read_buf + *retrieved_for_parsing, 1, requested_data_len, f);
        if (ret <= 0)
        {
            // Check if it was a timeout (retrying is allowed)
            if (ret == HTTPD_SOCK_ERR_TIMEOUT)
            {
                continue;
            }
            // Real socket error occurred
            return RETRIEVE_DATA_FAIL;
        }
        *total_received += ret;
        *retrieved_for_parsing += ret;
        ESP_EARLY_LOGI(TAG, "*retrieved_for_parsing after data retrieved %d, requested data len %d", *retrieved_for_parsing, requested_data_len);

        // ((char *)f_read_buf)[*retrieved_for_parsing] = 0;
        // ESP_LOGI(TAG, "retrieved for parsing %d, requested data len %d, data: \n%s", *retrieved_for_parsing, requested_data_len, (char *)f_read_buf);

    } while (content_len > *total_received && min_package_len > *retrieved_for_parsing);
    return RETRIEVE_DATA_OK;
}

static b_header_parse_status_t parse_boundary_header(char *b_token, int b_token_len, int boundary_num, uint8_t *buf, int buf_len, char *f_name,
                                                     uint8_t **f_start, uint8_t **prev_f_end)
{
    uint8_t *boundary_start = memmem(buf, buf_len, b_token, b_token_len);

    if (boundary_start == NULL)
        return B_HEADER_START_NOT_FOUND;

    char boundary_header_end[] = "\r\n\r\n";
    int boundary_header_end_len = 4;
    uint8_t *boundary_end = memmem(boundary_start, buf_len - (boundary_start - buf), boundary_header_end, boundary_header_end_len);
    if (boundary_end == NULL)
        return B_HEADER_END_NOT_FOUND;

    char *file_name_attr = "filename=\"";
    int file_name_attr_len = 10;
    uint8_t *file_name_attr_start = memmem(boundary_start, boundary_end - boundary_start, file_name_attr, file_name_attr_len);
    if (file_name_attr_start == NULL)
    {
        char last_b[100];
        sprintf(last_b, "\r\n%s--", b_token);
        *prev_f_end = memmem(buf, buf_len, last_b, strlen(last_b));

        return *prev_f_end == NULL ? B_HEADER_PARSE_FAIL : B_HEADER_LAST_HEADER;
    }

    uint8_t *file_name_start = file_name_attr_start + file_name_attr_len;
    uint8_t *file_name_end = memchr(file_name_start, '"', boundary_end - file_name_start);
    if (file_name_end == NULL || file_name_end - file_name_start > 90)
        return B_HEADER_PARSE_FAIL;

    int file_name_len = file_name_end - file_name_start;
    strncpy(f_name, (char *)file_name_start, file_name_len);
    f_name[file_name_len] = 0;
    *f_start = boundary_end + boundary_header_end_len;

    if (boundary_num != 0)
        *prev_f_end = boundary_start - 2;
    return B_HEADER_PARSE_OK;
}

static char **update_f_list_len(char **f_list, int *f_list_len, int cur_f_in_list)
{
    if (cur_f_in_list >= *f_list_len)
    {
        *f_list_len = (*f_list_len / 5 + 1) * 5;
        char **temp_f_list = realloc(f_list, *f_list_len * sizeof(char *));
        return temp_f_list;
    }
    return f_list;
}

esp_err_t upload_image_handler(httpd_req_t *req)
{
    http_info_request_happen();

    if (req->content_len <= 0)
        return http_400_error_handler(req, "Content-Length is not provided in request");

    if (req->content_len > max_payload_size)
        return http_413_error_handler(req, max_payload_size, req->content_len);

    char content_type[200];
    if (httpd_req_get_hdr_value_str(req, "Content-Type", content_type, sizeof(content_type)) != ESP_OK)
        return http_400_error_handler(req, "Content-Type is not provided in request");

    char *b_token = get_boundary(content_type);
    if (b_token == NULL)
        return http_400_error_handler(req, "Boundary is not provided in request");

    ESP_EARLY_LOGI(TAG, "content-length %d, boundary %s", req->content_len, b_token);
    int b_token_len = strlen(b_token);

    int f_list_len = 10;
    char **f_list = malloc(f_list_len * sizeof(char *));
    int cur_f_in_list = 0;

    int max_boundary_len = 250;
    int buf_len = 1024 * 4 + max_boundary_len;
    uint8_t *buf = malloc(buf_len);
    uint8_t *f_start = buf;
    uint8_t *f_read_buf = buf;
    if (buf == NULL)
        ESP_EARLY_LOGI(TAG, "Buf was not allocated successfully");

    int f_buf_len = 1024 * 200;
    uint8_t *f_buf = (uint8_t *)heap_caps_malloc(f_buf_len, MALLOC_CAP_SPIRAM);
    uint8_t *cur_f_buf_position = f_buf;
    if (f_buf == NULL)
        ESP_EARLY_LOGI(TAG, "f_buf was not allocated successfully");

    char f_name[MAX_UPLOAD_FILE_NAME_LEN];
    int total_received = 0;
    int cur_boundary = 0;
    char prev_f_name[MAX_UPLOAD_FILE_NAME_LEN];

    int prev_f_path_len = 150;
    char prev_f_path[prev_f_path_len];
    uint8_t *prev_f_start = NULL;
    uint8_t *prev_f_end = NULL;

    int content_len = req->content_len;

    bool data_corrupted = false;
    bool is_last_b = false;
    bool is_in_file = false;
    FILE *file = NULL;
    int retrieved_for_parsing;

    do
    {
        retrieved_for_parsing = 0;
        b_header_parse_status_t status = retrieve_data(req, f_read_buf, buf, buf_len, &total_received, &retrieved_for_parsing, content_len);
        if (status == RETRIEVE_DATA_FAIL)
        {
            ESP_EARLY_LOGI(TAG, "Fail to retrieve data");
            if (file != NULL)
            {

                ESP_EARLY_LOGI(TAG, "Removing file %s", prev_f_path);
                fclose(file);
                remove(prev_f_path);
            }
            break;
        }

        do
        {
            // printf("buf len when parsing %d \n", ret + (f_read_buf - buf) - (f_start - buf));
            b_header_parse_status_t res =
                parse_boundary_header(b_token, b_token_len, cur_boundary, f_start, retrieved_for_parsing + (f_read_buf - buf) - (f_start - buf), f_name, &f_start, &prev_f_end);

            // printf("buf len when parsing %d \n", ret + (f_read_buf - buf) - (f_start - buf));
            if (res == B_HEADER_END_NOT_FOUND || res == B_HEADER_START_NOT_FOUND)
            {
                // char *message = res == B_HEADER_END_NOT_FOUND ? "Boundary header end not found\n" : "Boundary header start not found\n";
                // printf(message);
                if (total_received == content_len || cur_boundary == 0)
                {
                    data_corrupted = true;
                    if (file != NULL)
                    {
                        ESP_EARLY_LOGI(TAG, "Data is corrupted, removing file %s", prev_f_path);
                        fclose(file);
                        remove(prev_f_path);
                    }
                    break;
                }
                else
                {
                    prev_f_end = prev_f_start > f_read_buf + retrieved_for_parsing - max_boundary_len ? prev_f_start : f_read_buf + retrieved_for_parsing - max_boundary_len;

                    if (cur_f_buf_position - f_buf + (prev_f_end - prev_f_start) > f_buf_len)
                    {
                        if (file == NULL)
                        {
                            get_file_path(f_dir_path, prev_f_name, prev_f_path, prev_f_path_len);
                            // printf("prev_f_path saving unfinished file %s\n", prev_f_path);
                            file = fopen(prev_f_path, "wb");
                            if (file == NULL)
                                ESP_EARLY_LOGI(TAG, "Failed to open '%s'. Reason: %s (Code: %d)\n", prev_f_path, strerror(errno), errno);
                        }
                        fwrite(f_buf, 1, cur_f_buf_position - f_buf, file);
                        cur_f_buf_position = f_buf;
                    }
                    memcpy(cur_f_buf_position, prev_f_start, prev_f_end - prev_f_start);
                    cur_f_buf_position += prev_f_end - prev_f_start;

                    int shift = buf_len - (prev_f_end - buf);
                    memcpy(buf, prev_f_end, shift);
                    f_start = buf;
                    // printf("Shift %d\n", shift);
                    f_read_buf = buf + shift;
                    prev_f_start = buf;
                    is_in_file = true;
                }
            }
            else if (res == B_HEADER_PARSE_OK || res == B_HEADER_LAST_HEADER)
            {
                if (cur_boundary > 0)
                {
                    if (file == NULL)
                    {
                        get_file_path(f_dir_path, prev_f_name, prev_f_path, prev_f_path_len);
                        // printf("prev_f_path saving finished file %s\n", prev_f_path);
                        file = fopen(prev_f_path, "wb");
                        if (file == NULL)
                            ESP_EARLY_LOGI(TAG, "Failed to open '%s'. Reason: %s (Code: %d)\n", prev_f_path, strerror(errno), errno);
                    }
                    if (cur_f_buf_position == f_buf)
                    {
                        ESP_EARLY_LOGI(TAG, "Information written, file size: %d", prev_f_end - prev_f_start);
                        fwrite(prev_f_start, 1, prev_f_end - prev_f_start, file);
                    }
                    else
                    {
                        if (cur_f_buf_position - f_buf + (prev_f_end - prev_f_start) > f_buf_len)
                        {
                            fwrite(f_buf, 1, cur_f_buf_position - f_buf, file);
                            cur_f_buf_position = f_buf;
                            fwrite(prev_f_start, 1, prev_f_end - prev_f_start, file);
                        }
                        else
                        {
                            memcpy(cur_f_buf_position, prev_f_start, prev_f_end - prev_f_start);
                            cur_f_buf_position += prev_f_end - prev_f_start;
                            fwrite(f_buf, 1, cur_f_buf_position - f_buf, file);
                            cur_f_buf_position = f_buf;
                        }
                    }

                    f_list[cur_f_in_list] = strdup(prev_f_path);
                    cur_f_in_list++;
                    f_list = update_f_list_len(f_list, &f_list_len, cur_f_in_list);
                    fclose(file);
                    file = NULL;
                }
                prev_f_start = f_start;
                cur_boundary++;
                strcpy(prev_f_name, f_name);
                if (strlen(f_name) > MAX_UPLOAD_FILE_NAME_LEN)
                {
                    ESP_EARLY_LOGI(TAG, "Too long file name %s\n", f_name);
                    data_corrupted = true;
                }
                is_last_b = res == B_HEADER_LAST_HEADER;
                is_in_file = false;
            }
            else if (res == B_HEADER_PARSE_FAIL)
            {
                data_corrupted = true;
                if (file != NULL)
                {
                    fclose(file);
                    remove(prev_f_path);
                }
                break;
            }
        } while (!is_last_b && !is_in_file && !data_corrupted);
    } while (total_received < content_len && !data_corrupted && !is_last_b);

    free(buf);
    free(f_buf);
    free(b_token);

    httpd_resp_set_type(req, HTTPD_TYPE_JSON);

    char *response = get_response(req->content_len, f_list, cur_f_in_list);

    for (int i = 0; i < cur_f_in_list; i++)
        ESP_EARLY_LOGI(TAG, "file %d %s %d", i, f_list[i], strlen(f_list[i]));

    for (int i = 0; i < cur_f_in_list; i++)
        free(f_list[i]);
    free(f_list);

    ESP_EARLY_LOGI(TAG, "Response %s", response);
    esp_err_t result = httpd_resp_send(req, (char *)response, HTTPD_RESP_USE_STRLEN);
    free(response);
    return result;
}
