#pragma once
#include <time.h>

struct requests_per_second
{
    time_t time;
    int requests_quantity;
};

void init_http_info_requests_counter();
void http_info_request_happen();
void get_requests_quantity_information(struct requests_per_second *res);
int get_size();