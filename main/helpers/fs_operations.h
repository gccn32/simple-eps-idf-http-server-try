#include "esp_err.h"
#include "freertos/semphr.h"

void mount_fs();
uint8_t *read_file_to_buffer(const char *filename, size_t *out_size);
bool is_file(const char *path);
esp_err_t st_fread(uint8_t *buf, int buf_len, FILE *f, int *bytes_read, int f_read_timeout);
esp_err_t st_write(uint8_t *buf, int buf_len, FILE *f, int f_read_timeout);

extern const char temp_dir_path[];
extern SemaphoreHandle_t fs_operations_mutex;


