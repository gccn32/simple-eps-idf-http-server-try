#pragma once
#include "esp_http_server.h"

esp_err_t get_main_page_handler(httpd_req_t *req);
void initialize_main_page();
