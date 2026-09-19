#pragma once
#include "esp_http_server.h"

void initialize_static_files_cache();
esp_err_t static_files_api(httpd_req_t *req);