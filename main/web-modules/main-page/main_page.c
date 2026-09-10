#include "esp_http_server.h"
#include "esp_log.h"
#include "main_page_template.h"
#include "../lib/template_helpers.h"
#include "../../services/request_counter.h"

static const char *TAG = "MAIN-PAGE";
static uint8_t *main_page_compressed = NULL;
static size_t main_page_compressed_len;

void initialize_main_page()
{
    main_page_template_post_t posts[] = {
        {
            .title = "First-title",
            .date = "01-02-2020",
        },
        {
            .title = "Second-title",
            .date = "07-08-2921",
        },
    };
    main_page_template_context_t template_context = {
        .posts_count = 2,
        .age = 192838,
        .name = "My-First_name",
        .surname = "My-First-Surname",
        .test1 = "some-test-1",
        .test2 = "some-test-2",
        .posts = posts,
    };

    get_compressed_template(get_main_page_template, &template_context, &main_page_compressed, (size_t *)&main_page_compressed_len);
    ESP_LOGI(TAG, "Main page templage initialized, length %d", main_page_compressed_len);
}

esp_err_t get_main_page_handler(httpd_req_t *req)
{
    http_info_request_happen();
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    return httpd_resp_send(req, (char *)main_page_compressed, main_page_compressed_len);
}