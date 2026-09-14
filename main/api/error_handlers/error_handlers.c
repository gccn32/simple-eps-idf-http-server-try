#include "esp_http_server.h"
#include "esp_heap_caps.h"

esp_err_t http_413_error_handler(httpd_req_t *req, int content_max_size, int content_size)
{
    int error_message_size = 200;
    char error_message[error_message_size];

    httpd_resp_set_status(req, "413 Content too large");
    snprintf(error_message, error_message_size - 1, "413 Request content is too large. %d bytes exceeds max content size %d", content_size, content_max_size);
    return httpd_resp_send(req, error_message, HTTPD_RESP_USE_STRLEN);
}

esp_err_t http_400_error_handler(httpd_req_t *req, char *message)
{
    int error_message_size = 250;
    char error_message[error_message_size];

    httpd_resp_set_status(req, HTTPD_400);
    snprintf(error_message, error_message_size - 1, "400 Bad request. %s", message);
    return httpd_resp_send(req, error_message, HTTPD_RESP_USE_STRLEN);
}

esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    httpd_resp_set_status(req, HTTPD_404);
    return httpd_resp_send(req, "Error happen, please check URL", HTTPD_RESP_USE_STRLEN);
}

esp_err_t http_500_error_handler(httpd_req_t *req, char *message)
{
    httpd_resp_set_status(req, HTTPD_500);
    return httpd_resp_send(req, message, HTTPD_RESP_USE_STRLEN);
}
esp_err_t http_503_error_handler(httpd_req_t *req, char *message)
{
    httpd_resp_set_status(req, "503 Service Unavailable");
    return httpd_resp_send(req, message, HTTPD_RESP_USE_STRLEN);
}

esp_err_t http_429_error_handler(httpd_req_t *req, char *message)
{
    httpd_resp_set_status(req, "429 Too Many Requests");
    return httpd_resp_send(req, message, HTTPD_RESP_USE_STRLEN);
}
