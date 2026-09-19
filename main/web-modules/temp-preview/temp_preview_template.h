#pragma once
#include "../lib/template_helpers.h"

typedef struct
{
    char *f_name;
    char *url;
} temp_preview_template_f_t;

typedef struct
{
    temp_preview_template_f_t **f_list;
    int cur_f_in_list;
    int f_list_len;
} temp_preview_template_context_t;

void get_temp_preview_template(void *data, template_callback_context_t *cb_context, template_callback_t cb);
