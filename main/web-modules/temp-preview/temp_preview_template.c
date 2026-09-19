
#include "stdio.h"
#include "temp_preview_template.h"
#include "../lib/template_helpers.h"

void get_temp_preview_template(void *t_data, template_callback_context_t *cb_context,
template_callback_t cb) {
temp_preview_template_context_t *data = (temp_preview_template_context_t*) t_data;

cb(cb_context, "\r\n<!DOCTYPE html>\r\n<html lang=\"en\">\r\n\r\n<head>\r\n  <meta charset=\"UTF-8\">\r\n  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\r\n  <title>Temp preview</title>\r\n</head>\r\n\r\n<body>\r\n  ");
 if (data->cur_f_in_list > 0) { 
cb(cb_context, "\r\n  <ul>\r\n    ");
 for (int i = 0; i < data->cur_f_in_list; i++) {
      temp_preview_template_f_t *file = data->f_list[i]; 
cb(cb_context, "\r\n      <li><a href=\"");
cb(cb_context, file->url);
cb(cb_context, "\" target=\"_blank\">");
cb(cb_context, file->f_name);
cb(cb_context, "</a> <button>Delete</button></li>\r\n      ");
 } 
cb(cb_context, "\r\n  </ul>\r\n  ");
 } else { 
cb(cb_context, "\r\n  <b>No available files</b>\r\n  ");
 } 
cb(cb_context, "\r\n</body>\r\n<script>\r\n  document.querySelectorAll('button').forEach(e =>\r\n    e\r\n      .addEventListener('click', (ev) => {\r\n        const url = ev.target.parentElement.querySelector('a').href;\r\n        fetch(url, { method: \"DELETE\" }).then(() => window.location.reload());\r\n      })\r\n  );\r\n</script>\r\n\r\n</html>\r\n");
 } 
