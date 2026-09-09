#include "esp_err.h"
#include "stdio.h"
#include "string.h"
#include "template_helpers.h"
#include "esp_heap_caps.h"
#include "../../services/zlib_compressor.h"
#include "esp_log.h"

static const int mem_alloc_step = 1024;
// static const char *TAG = "TEMPLATE-HELPERS";

static void template_callback(template_callback_context_t *context, char *template_part)
{
    int len = strlen(template_part);
    int new_len = context->content_size + len;
    if (context->buf_size < new_len)
    {
        int new_capacity = new_len + mem_alloc_step;
        uint8_t *new_dest = heap_caps_realloc(context->buf, new_capacity, MALLOC_CAP_SPIRAM);
        context->buf = (char *)new_dest;
    }
    memcpy(context->buf + context->content_size, template_part, len);
    context->content_size = new_len;
}

void get_compressed_template(page_templage_t template, void *template_context, uint8_t **page_content, size_t *page_content_len)
{
    template_callback_context_t tem_cal_context = {
        .buf = NULL,
        .buf_size = 0,
        .content_size = 0,
    };
    template(template_context, &tem_cal_context, template_callback);

    compress_string_to_buffer(tem_cal_context.buf, tem_cal_context.content_size, page_content, (size_t *)page_content_len);
    heap_caps_free(tem_cal_context.buf);
}

void get_generated_template(page_templage_t template, void *template_context, char **page_content, size_t *page_content_len)
{
    template_callback_context_t tem_cal_context = {
        .buf = NULL,
        .buf_size = 0,
        .content_size = 0,
    };
    template(template_context, &tem_cal_context, template_callback);
    *page_content = tem_cal_context.buf;
    *page_content_len = tem_cal_context.content_size;
}