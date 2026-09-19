#pragma once
#include "esp_http_server.h"

void initialize_performance_testing_api();
esp_err_t performance_testing_api(httpd_req_t *req);