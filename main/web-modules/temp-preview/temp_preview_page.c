#include "esp_http_server.h"
#include "esp_log.h"
#include "temp_preview_template.h"
#include "../lib/template_helpers.h"
#include "../../services/request_counter.h"
#include "dirent.h"
#include "../../helpers/fs_operations.h"

// static const char *TAG = "TEMP-PREVIEW";

static void update_f_list_len(temp_preview_template_f_t ***f_list, int *f_list_len, int *cur_f_in_list)
{
    if (*cur_f_in_list >= *f_list_len)
    {
        *f_list_len = (*f_list_len / 5 + 1) * 5;
        temp_preview_template_f_t **temp_f_list = (temp_preview_template_f_t **)realloc(*f_list, *f_list_len * sizeof(temp_preview_template_f_t *));
        *f_list = temp_f_list;
    }
}

static void read_files_in_dir(temp_preview_template_f_t ***f_list, int *f_list_len, int *cur_f_in_list)
{
    int f_path_len = 150;
    char f_path[f_path_len];
    char *relative_url_path = "/api/file-download";

    DIR *dir = opendir(temp_dir_path);
    if (dir)
    {
        struct dirent *en;
        while ((en = readdir(dir)) != NULL)
        {
            update_f_list_len(f_list, f_list_len, cur_f_in_list);
            char *copied_name = strdup(en->d_name);
            snprintf(f_path, f_path_len, "%.20s/%.90s", relative_url_path, copied_name);
            temp_preview_template_f_t *link = malloc(sizeof(temp_preview_template_f_t));
            link->f_name = copied_name;
            link->url = strdup(f_path);
            (*f_list)[*cur_f_in_list] = link;
            (*cur_f_in_list)++;
        }
        closedir(dir);
    }
}

static void free_temp_context(temp_preview_template_context_t *temp_context)
{
    for (int i = 0; i < temp_context->cur_f_in_list; i++)
    {
        free(temp_context->f_list[i]->f_name);
        free(temp_context->f_list[i]->url);
        free(temp_context->f_list[i]);
    }
    free(temp_context->f_list);
}

esp_err_t get_temp_preview_handler(httpd_req_t *req)
{
    http_info_request_happen();
    temp_preview_template_context_t temp_context = {
        .cur_f_in_list = 0,
        .f_list_len = 0,
        .f_list = NULL,
    };

    read_files_in_dir(&(temp_context.f_list), &(temp_context.f_list_len), &(temp_context.cur_f_in_list));

    char *page_gen_content = NULL;
    size_t page_gen_content_len;

    get_generated_template(get_temp_preview_template, &temp_context, &page_gen_content, (size_t *)&page_gen_content_len);
    free_temp_context(&temp_context);
    return httpd_resp_send(req, (char *)page_gen_content, page_gen_content_len);
}