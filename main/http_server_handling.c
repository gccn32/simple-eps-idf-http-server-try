#include "string.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "lwip/sockets.h"
#include "services/request_counter.h"
#include "services/zlib_compressor.h"
#include "helpers/led.h"
#include "api/requests_quantity/requests_quantity.h"
#include "api/sys_info/sys_info.h"
#include "api/led/led.h"
#include "helpers/touch_events_helper.h"
#include "web-modules/main-page/main_page.h"
#include "web-modules/temp-preview/temp_preview_page.h"
#include "api/files/download/file_download.h"
#include "web-modules/hello_world/hello_world.h"
#include "api/performance_testing/performance_testing.h"
#include "api/lib/api_lib.h"
#include "api/static_files/static_files.h"
#include "api/error_handlers/error_handlers.h"
#include "api/files/upload/files_upload.h"
#include "api/files/delete/file_delete.h"

httpd_handle_t server = NULL;
static const char *TAG = "HTTP-SERVER";

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

    const httpd_uri_t preview_temp_api_handler = {
        .uri = "/temp/",
        .method = HTTP_GET,
        .handler = get_temp_preview_handler,
    };
    const httpd_uri_t api_upload_files_handler = {
        .uri = "/api/file/",
        .method = HTTP_POST,
        .handler = file_upload_async,
    };
    const httpd_uri_t download_f_handler = {
        .uri = "/api/file/*",
        .method = HTTP_GET,
        .handler = file_download_async,
    };
    const httpd_uri_t delete_f_handler = {
        .uri = "/api/file/*",
        .method = HTTP_DELETE,
        .handler = file_delete,
    };
    const httpd_uri_t api_static_files_handled = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = static_files_api,
    };

    httpd_register_uri_handler(server, &index_uri);
    httpd_register_uri_handler(server, &index_uri_hello_world);
    httpd_register_uri_handler(server, &index_uri_hello_world_optimized);
    httpd_register_uri_handler(server, &get_requests_quantity);
    httpd_register_uri_handler(server, &get_int_sys_info);
    httpd_register_uri_handler(server, &api_toggle_led_handled);
    httpd_register_uri_handler(server, &api_performance_testing_handled);
    httpd_register_uri_handler(server, &api_upload_files_handler);
    httpd_register_uri_handler(server, &preview_temp_api_handler);
    httpd_register_uri_handler(server, &download_f_handler);
    httpd_register_uri_handler(server, &delete_f_handler);

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
    initialize_file_upload();
    init_file_download();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    // Looks like connections are not always closed, if false it can cause potential memory leak and stop accept connections at all
    config.lru_purge_enable = true; // must be true because of issues after too many connections if false it will stop accept new connections.
    config.max_uri_handlers = 20;
    config.max_open_sockets = 7;
    config.open_fn = open_fn; // THIS LINE IS SPEEDING UP esp_http_server RESPONSE FROM 65ms TO 10ms
    config.core_id = 1;       // Improves performance on "Hello world" page from 220/s to 350/s
    // config.task_priority = 23;
    config.recv_wait_timeout = 60 * 5;
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