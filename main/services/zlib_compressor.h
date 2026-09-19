#pragma once
#include "esp_err.h"

typedef size_t (*data_provider_cb)(char *buf, size_t max_len, void *user_context);
typedef size_t (*compress_data_cb)(uint8_t *buf, size_t buf_len, void *context);

esp_err_t init_global_zlib(void);
esp_err_t send_compressed_stream_cached(data_provider_cb provide_data, void *dp_context, compress_data_cb compress_cb, void *cc_context);

esp_err_t compress_string_to_buffer(char *input_string, int string_len, uint8_t **out_buffer, size_t *out_size);

esp_err_t compress_stream_to_file(FILE *dest_file, data_provider_cb provide_data, void *user_context);

esp_err_t compress_stream_to_buffer(data_provider_cb read_stream_func, void *cxt, uint8_t **out_buffer, size_t *out_size);

#define COMPRESSOR_BUFFER_SIZE 32768
