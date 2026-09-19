#pragma once
#include "esp_http_server.h"

esp_err_t hello_get_handler(httpd_req_t *req);
esp_err_t hello_optimized_get_handler(httpd_req_t *req);