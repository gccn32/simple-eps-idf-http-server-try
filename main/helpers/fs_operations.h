#pragma once
#include "esp_err.h"
#include "freertos/semphr.h"

void mount_fs();
uint8_t *read_file_to_buffer(const char *filename, size_t *out_size);
bool is_file(const char *path);

extern const char temp_dir_path[];

esp_err_t st_fread(uint8_t *buf, int buf_len, FILE *f, int *bytes_read, int f_read_timeout);
esp_err_t st_write(uint8_t *buf, int buf_len, FILE *f, int f_read_timeout);
FILE *st_fopen(char *f_path, char *mode);
esp_err_t st_fclose(FILE *f, char *f_path);
esp_err_t st_remove(char *f_path);
