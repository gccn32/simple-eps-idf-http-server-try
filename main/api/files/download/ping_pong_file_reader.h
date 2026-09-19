#pragma once
#include "esp_err.h"
#include <stdatomic.h>

typedef struct
{
    uint8_t *ptr;
    int len;
} chunk_msg_t;

typedef struct
{
    FILE *file;
    QueueHandle_t data_queue;
    QueueHandle_t empty_queue;
    atomic_bool worker_stopped;
} reader_ctx_t;

typedef struct
{
    uint8_t *buf_a;
    uint8_t *buf_b;
    reader_ctx_t *ctx;
} p_p_descriptor_t;

esp_err_t get_p_p_data(p_p_descriptor_t *descriptor, chunk_msg_t *msg);
esp_err_t request_p_p_data(p_p_descriptor_t *descriptor, chunk_msg_t *msg);
void delete_p_p_reader(p_p_descriptor_t *descriptor);
p_p_descriptor_t *init_p_p_reader(char *f_path);