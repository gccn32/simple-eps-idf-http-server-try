#include "esp_http_server.h"
#include "esp_log.h"
#include "../../services/request_counter.h"
#include "../../api/error_handlers/error_handlers.h"
#include "../../api/lib/api_lib.h"
#include "../../helpers/fs_operations.h"
#include "string.h"

char *f_dir_path = "/littlefs/temp";

esp_err_t temp_download_f_handler(httpd_req_t *req)
{
    http_info_request_happen();

    char f_name[60];
    int parsed = sscanf(req->uri, "/temp/%50s", f_name);
    if (parsed != 1 || !f_name[0])
        return http_400_error_handler(req, "Request is wrong");

    if (strchr(f_name, '\\') != NULL || strchr(f_name, '/') != NULL)
        return http_400_error_handler(req, "Symbols \\ and / are not allowed");

    httpd_resp_set_type(req, get_mime_type(f_name));

    int f_path_len = 150;
    char f_path[150];
    snprintf(f_path, f_path_len, "%.20s/%.90s", f_dir_path, f_name);

    if (!is_file(f_path))
        return http_404_error_handler(req, HTTPD_404_NOT_FOUND);

    int buf_len = 1024;
    uint8_t *buf = malloc(buf_len);
    FILE *f = fopen(f_path, "rb");
    size_t bytes_read = 0;
    esp_err_t ret = ESP_OK;
    while ((bytes_read = fread(buf, 1, buf_len, f)) > 0)
    {
        ret = httpd_resp_send_chunk(req, (char *)buf, bytes_read);
        if (ret != ESP_OK)
            return ret;
    }
    ret = httpd_resp_send_chunk(req, NULL, 0);
    fclose(f);
    free(buf);
    return ret;
}