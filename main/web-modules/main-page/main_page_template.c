
#include "stdio.h"
#include "main_page_template.h"
#include "../lib/template_helpers.h"

void get_main_page_template(void *t_data, template_callback_context_t *cb_context,
template_callback_t cb) {
main_page_template_context_t *data = (main_page_template_context_t*) t_data;

cb(cb_context, "\r\n<!DOCTYPE html>\r\n<html lang=\"en\">\r\n\r\n<head>\r\n    <title>My Webpage</title>\r\n    <link rel=\"manifest\" href=\"/manifest.webmanifest\">\r\n    <link rel=\"icon\" href=\"/favicon.ico\" sizes=\"32x32\">>\r\n</head>\r\n\r\n<body>\r\n    <p>Hei, welcome to my blog! I'm ");
cb(cb_context, data->name);
cb(cb_context, " ");
cb(cb_context, data->surname);
cb(cb_context, " and I'm ");

char buf3[20];
sprintf(buf3, "%d", data->age);
cb(cb_context, buf3);

cb(cb_context, " years\r\n        old!</p>\r\n    <p>");
cb(cb_context, data->test1);
cb(cb_context, data->test2);
cb(cb_context, "</p>\r\n    <ul>\r\n        ");

        main_page_template_post_t *posts = data->posts;
        
cb(cb_context, "\r\n        ");
 for (int i = 0; i < data->posts_count; i++) {
            main_page_template_post_t post = posts[i]; 
cb(cb_context, "\r\n            <li>");
cb(cb_context, post.title);
cb(cb_context, " - ");
cb(cb_context, post.date);
cb(cb_context, "</li>\r\n            ");
 } 
cb(cb_context, "\r\n    </ul>\r\n</body>\r\n\r\n</html>\r\n\r\n");
 } 
