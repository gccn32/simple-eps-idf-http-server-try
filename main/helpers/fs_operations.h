#include "esp_err.h"

esp_err_t mount_littlefs();
uint8_t *read_file_to_buffer(const char *filename, size_t *out_size);
bool is_file(const char *path);