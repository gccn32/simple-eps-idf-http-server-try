#include "../../services/zlib_compressor.h"
#include "esp_log.h"
#include <dirent.h>
#include <unistd.h>
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_err.h"
#include <sys/stat.h>
#include "../../services/request_counter.h"
#include "../lib/api_lib.h"
#include "../error_handlers/error_handlers.h"

static const char *TAG = "STATIC-FILES";
static const int file_name_len = 40;
static const int path_to_file_len = 150;

typedef struct
{
    uint8_t *buf;
    size_t buf_size;
    char *path;
    bool is_compressed;
} page_cache_entry_t;

static int pages_quantity = 0;
static page_cache_entry_t *pages_cache;
char *file_types_to_minify[] = {".css", ".js", ".html", ".json", ".ico"};

static size_t file_provider(char *buf, size_t max_len, void *ctx)
{
    FILE *f = (FILE *)ctx;
    return fread(buf, 1, max_len, f); // Returns 0 automatically on EOF
}

static void read_file_to_buffer(const char *filename, uint8_t **buf, size_t *out_size)
{
    FILE *file = fopen(filename, "rb");
    fseek(file, 0, SEEK_END);

    long file_size = ftell(file);

    fseek(file, 0, SEEK_SET);
    uint8_t *buffer = heap_caps_malloc(file_size, MALLOC_CAP_SPIRAM);

    size_t bytes_read = fread(buffer, 1, file_size, file);

    fclose(file);
    *out_size = bytes_read;
    *buf = buffer;
}

static int get_files_count_recursively(char *folder)
{
    int res = 0;
    DIR *dir = opendir(folder);
    if (dir)
    {
        struct dirent *en;
        char path_to_file[path_to_file_len];

        while ((en = readdir(dir)) != NULL)
        {
            snprintf((char *)path_to_file, path_to_file_len, "%s/%.70s", folder, (char *)en->d_name);
            struct stat st;
            if (stat((char *)path_to_file, &st) == 0)
            {
                if (S_ISREG(st.st_mode))
                    res++;
                else
                    res += get_files_count_recursively((char *)path_to_file);
            }
        }
        closedir(dir);
    }
    return res;
}

static bool should_minify(char *path_to_file)
{
    char *ext = strrchr(path_to_file, '.');
    for (int i = 0; i < sizeof(file_types_to_minify) / sizeof(file_types_to_minify[0]); i++)
        if (strcmp(ext, file_types_to_minify[i]) == 0)
            return true;
    return false;
}

static void add_file_to_cache(char *path_to_file, int *cur_handled_file, char *prefix)
{
    pages_cache[*cur_handled_file].path = strdup(prefix);
    // ESP_LOGI(TAG, "Path %s", prefix);
    if (should_minify(path_to_file))
    {
        pages_cache[*cur_handled_file].is_compressed = true;
        FILE *source_f = fopen(path_to_file, "rb");
        compress_stream_to_buffer(file_provider, source_f, &pages_cache[*cur_handled_file].buf, &pages_cache[*cur_handled_file].buf_size);
        fclose(source_f);
    }
    else
    {
        pages_cache[*cur_handled_file].is_compressed = false;
        read_file_to_buffer(path_to_file, &pages_cache[*cur_handled_file].buf, &pages_cache[*cur_handled_file].buf_size);
    }
}

static void generate_cache_from_folder(char *folder, int *cur_handled_file, char *prefix)
{
    DIR *dir = opendir(folder);
    if (dir)
    {
        struct dirent *en;
        char file_name[file_name_len];
        char path_to_file[path_to_file_len];

        while ((en = readdir(dir)) != NULL)
        {
            strlcpy(file_name, en->d_name, file_name_len);
            snprintf((char *)path_to_file, path_to_file_len, "%s/%.70s", folder, (char *)en->d_name);

            char new_prefix[path_to_file_len];
            if (!*prefix)
                stpcpy((char *)new_prefix, file_name);
            else
                snprintf((char *)new_prefix, path_to_file_len, "%s/%.70s", prefix, (char *)en->d_name);

            struct stat st;
            if (stat(path_to_file, &st) == 0)
            {
                if (S_ISREG(st.st_mode))
                {
                    add_file_to_cache((char *)path_to_file, cur_handled_file, (char *)new_prefix);
                    (*cur_handled_file)++;
                }
                else
                    generate_cache_from_folder((char *)path_to_file, cur_handled_file, (char *)new_prefix);
            }
        }
        closedir(dir);
    }
}

static page_cache_entry_t *get_page_data(char *prefix)
{
    for (int i = 0; i < pages_quantity; i++)
    {
        if (strcmp(prefix, pages_cache[i].path) == 0)
        {
            return &(pages_cache[i]);
        }
    }
    return NULL;
}

void initialize_static_files_cache()
{
    char *static_files_folder = "/littlefs/static";
    pages_quantity = get_files_count_recursively(static_files_folder);

    pages_cache = (page_cache_entry_t *)malloc(pages_quantity * sizeof(page_cache_entry_t));
    int cur_handled_file = 0;
    generate_cache_from_folder(static_files_folder, &cur_handled_file, "");
    ESP_LOGI(TAG, "Static files cache has been initialized with %d files", pages_quantity);
}

esp_err_t static_files_api(httpd_req_t *req)
{
    http_info_request_happen();
    char prefix[100];
    // ESP_LOGI(TAG, "Static files requested %s", req->uri);

    int parsed = sscanf(req->uri, "/%95s", prefix);
    page_cache_entry_t *page_data;
    if (parsed != 1 || (page_data = get_page_data(prefix)) == NULL)
    {
        return http_404_error_handler(req, HTTPD_404_NOT_FOUND);
    };

    if (page_data->is_compressed)
    {
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    }
    httpd_resp_set_type(req, get_mime_type(prefix));

    return httpd_resp_send(req, (char *)page_data->buf, page_data->buf_size);
}