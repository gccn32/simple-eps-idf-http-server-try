#include "esp_err.h"

void mount_fs();
uint8_t *read_file_to_buffer(const char *filename, size_t *out_size);
bool is_file(const char *path);

extern const char temp_dir_path[];

