#include "esp_http_server.h"
#include "../../services/request_counter.h"

static const char *hello_world_message = "<h1>Hello World</h1>"; // 20 symbols

esp_err_t hello_get_handler(httpd_req_t *req)
{
    http_info_request_happen();
    return httpd_resp_send(req, hello_world_message, 20);
}

static char *hello_optimized_message = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 20\r\n\r\n<h1>Hello World</h1>";

esp_err_t hello_optimized_get_handler(httpd_req_t *req)
{
    http_info_request_happen();
    int sockfd = httpd_req_to_sockfd(req);
    if (sockfd < 0)
        return ESP_FAIL;

    httpd_send(req, hello_optimized_message, 84);
    return ESP_OK;
}