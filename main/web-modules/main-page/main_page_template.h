#pragma once
#include "../lib/template_helpers.h"

typedef struct
{
    char *title;
    char *date;
} main_page_template_post_t;

typedef struct
{
    char *name;
    char *surname;
    int age;
    char *test1;
    char *test2;
    int posts_count;
    main_page_template_post_t *posts;
} main_page_template_context_t;

void get_main_page_template(void *data, template_callback_context_t *cb_context, template_callback_t cb);
