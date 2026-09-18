
#include <stdatomic.h>
#include "esp_err.h"

typedef struct
{
    uint8_t *ptr;
    int len;
    FILE *file;
    bool close_prev_file;
} chunk_msg_t;

typedef struct
{
    QueueHandle_t data_queue;
    QueueHandle_t empty_queue;
    atomic_bool worker_stopped;
} writer_ctx_t;

typedef struct
{
    uint8_t *buf_a;
    uint8_t *buf_b;
    writer_ctx_t *ctx;
    char *f_path;
    char *f_name;
    FILE *file;
    chunk_msg_t *cur_msg;
} p_p_descriptor_t;

p_p_descriptor_t *init_p_p_writer();
esp_err_t write_p_p_data(p_p_descriptor_t *descriptor, char *f_name, uint8_t *buf, int buf_len);
void delete_p_p_writer(p_p_descriptor_t *descriptor);
esp_err_t complete_p_p_upload(p_p_descriptor_t *descriptor);