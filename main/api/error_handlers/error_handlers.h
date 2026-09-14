#include "esp_http_server.h"

esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err);
esp_err_t http_400_error_handler(httpd_req_t *req, char *message);
esp_err_t http_413_error_handler(httpd_req_t *req, int content_max_size, int content_size);
esp_err_t http_500_error_handler(httpd_req_t *req, char *message);
esp_err_t http_429_error_handler(httpd_req_t *req, char *message);
esp_err_t http_503_error_handler(httpd_req_t *req, char *message);
