#include "string.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "lwip/sockets.h"
#include "services/request_counter.h"
#include "services/zlib_compressor.h"
#include "api/requests_quantity/requests_quantity.h"
#include "api/sys_info/sys_info.h"
#include "api/led/led.h"
#include "helpers/led.h"
#include "helpers/touch_events_helper.h"
#include "web-modules/main-page/main_page.h"
#include "web-modules/temp-preview/temp_preview_page.h"
#include "web-modules/temp-preview/temp_download_file.h"
#include "api/performance_testing/performance_testing.h"
#include "api/lib/api_lib.h"
#include "api/static_files/static_files.h"
#include "api/error_handlers/error_handlers.h"
#include "api/files_upload/files_upload.h"

httpd_handle_t server = NULL;
static const char *TAG = "HTTP-SERVER";

static const char *hello_world_message = "<h1>Hello World</h1>"; // 20 symbols

static esp_err_t hello_get_handler(httpd_req_t *req)
{
    http_info_request_happen();
    // httpd_resp_send(req, hello_world_message, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send(req, hello_world_message, 20);
    return ESP_OK;
}

static char *hello_optimized_message = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 20\r\n\r\n<h1>Hello World</h1>";

static esp_err_t hello_optimized_get_handler(httpd_req_t *req)
{
    http_info_request_happen();
    int sockfd = httpd_req_to_sockfd(req);
    if (sockfd < 0)
        return ESP_FAIL;

    httpd_send(req, hello_optimized_message, 84);
    return ESP_OK;
}

static size_t file_provider(char *buf, size_t max_len, void *ctx)
{
    FILE *f = (FILE *)ctx;
    return fread(buf, 1, max_len, f); // Returns 0 automatically on EOF
}

static size_t simple_compress_cb(uint8_t *buf, size_t buf_len, void *context)
{
    httpd_req_t *req = (httpd_req_t *)context;
    return httpd_resp_send_chunk(req, (char *)buf, buf_len);
}

static esp_err_t file_stream_handler(httpd_req_t *req)
{
    http_info_request_happen();
    char *file_path = "/littlefs/static/index.html";

    const char *mime_type = get_mime_type(file_path);
    httpd_resp_set_type(req, mime_type);
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    FILE *f = fopen(file_path, "r");
    if (!f)
    {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    esp_err_t result = send_compressed_stream_cached(file_provider, f, simple_compress_cb, req);
    httpd_resp_send_chunk(req, NULL, 0);
    fclose(f);
    return result;
}

static esp_err_t open_fn(httpd_handle_t hd, int sockfd)
{
    int val = 1;
    // Disable Nagle's algorithm for instant packet delivery
    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val));
    return ESP_OK;
}

static void register_http_handlers()
{

    const httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = get_main_page_handler,
    };
    const httpd_uri_t file_stream_cached = {
        .uri = "/compressed-on-the-fly",
        .method = HTTP_GET,
        .handler = file_stream_handler,
    };

    const httpd_uri_t index_uri_hello_world = {
        .uri = "/hello-world",
        .method = HTTP_GET,
        .handler = hello_get_handler,
    };
    const httpd_uri_t index_uri_hello_world_optimized = {
        .uri = "/hello-world-optimized",
        .method = HTTP_GET,
        .handler = hello_optimized_get_handler,
    };

    const httpd_uri_t get_requests_quantity = {
        .uri = "/api/requests-quantity",
        .method = HTTP_GET,
        .handler = get_requests_quantity_handler,
    };
    const httpd_uri_t get_int_sys_info = {
        .uri = "/api/int-sys-info",
        .method = HTTP_GET,
        .handler = get_int_sys_info_handler,
    };
    const httpd_uri_t api_toggle_led_handled = {
        .uri = "/api/led/toggle",
        .method = HTTP_GET,
        .handler = toggle_led_handler,
    };
    const httpd_uri_t api_performance_testing_handled = {
        .uri = "/api/performance-testing",
        .method = HTTP_GET,
        .handler = performance_testing_api,
    };
    const httpd_uri_t api_image_upload_handler = {
        .uri = "/api/image-upload",
        .method = HTTP_POST,
        .handler = upload_file_handler,
    };
    const httpd_uri_t preview_temp_api_handler = {
        .uri = "/temp/",
        .method = HTTP_GET,
        .handler = get_temp_preview_handler,
    };
    const httpd_uri_t preview_temp_download_f_handler = {
        .uri = "/temp/*",
        .method = HTTP_GET,
        .handler = temp_download_f_handler,
    };
    const httpd_uri_t api_static_files_handled = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = static_files_api,
    };

    httpd_register_uri_handler(server, &index_uri);
    httpd_register_uri_handler(server, &file_stream_cached);
    httpd_register_uri_handler(server, &index_uri_hello_world);
    httpd_register_uri_handler(server, &index_uri_hello_world_optimized);
    httpd_register_uri_handler(server, &get_requests_quantity);
    httpd_register_uri_handler(server, &get_int_sys_info);
    httpd_register_uri_handler(server, &api_toggle_led_handled);
    httpd_register_uri_handler(server, &api_performance_testing_handled);
    httpd_register_uri_handler(server, &api_image_upload_handler);
    httpd_register_uri_handler(server, &preview_temp_api_handler);
    httpd_register_uri_handler(server, &preview_temp_download_f_handler);

    httpd_register_uri_handler(server, &api_static_files_handled);
}

void start_webserver()
{
    init_global_zlib();
    init_http_info_requests_counter();
    initialize_touch_events();
    initialize_led();

    initialize_main_page();
    initialize_performance_testing_api();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    // Looks like connections are not always closed, if false it can cause potential memory leak and stop accept connections at all
    config.lru_purge_enable = true; // must be true because of issues after too many connections if false it will stop accept new connections.
    config.max_uri_handlers = 20;
    config.max_open_sockets = 7;
    config.open_fn = open_fn; // THIS LINE IS SPEEDING UP esp_http_server RESPONSE FROM 65ms TO 10ms
    config.core_id = 1;       // Improves performance on "Hello world" page from 220/s to 350/s
    config.task_priority = 23;
    config.recv_wait_timeout = 60;
    config.send_wait_timeout = 5;
    config.uri_match_fn = httpd_uri_match_wildcard;
    // config.stack_size = 1024 * 8;
    if (httpd_start(&server, &config) == ESP_OK)
    {
        register_http_handlers();
        ESP_LOGI(TAG, "Server started successfully, registering URI handlers...");
    }
    else
    {
        ESP_LOGE(TAG, "Failed to start server");
    }
    initialize_static_files_cache();
}