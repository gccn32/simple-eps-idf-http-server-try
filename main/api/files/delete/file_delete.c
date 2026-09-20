#include "esp_http_server.h"
#include "esp_log.h"
#include "../../../services/request_counter.h"
#include "../../error_handlers/error_handlers.h"
#include "../../../helpers/fs_operations.h"
#include "string.h"
#include "freertos/semphr.h"
static const char *TAG = "FILE-DELETE";

esp_err_t file_delete(httpd_req_t *req)
{
    char f_name[60];
    int parsed = sscanf(req->uri, "/api/file/%50s", f_name);
    if (parsed != 1 || !f_name[0])
        return http_400_error_handler(req, "Request is wrong");

    if (strchr(f_name, '\\') != NULL || strchr(f_name, '/') != NULL || f_name[0] == '.')
        return http_400_error_handler(req, "Symbols \\ and / are not allowed. File name should not start from '.'");

    int f_path_len = 150;
    char f_path[150];
    snprintf(f_path, f_path_len, "%.40s/%.90s", temp_dir_path, f_name);

    if (!is_file(f_path))
        return http_404_error_handler(req, HTTPD_404_NOT_FOUND);

    if (st_remove(f_path) == ESP_FAIL)
        http_500_error_handler(req, "Internal service error");

    ESP_LOGI(TAG, "File %s was removed", f_path);
    return httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
}