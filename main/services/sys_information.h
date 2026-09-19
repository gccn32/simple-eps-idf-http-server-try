// #include <esp_system.h>
#pragma once

struct interesting_system_information
{
    int cpu_frequency;
    const char *model;
    int cores;
    uint32_t cpu_freq;
    int flash_size;
    int total_sram;
    int total_psram;
    int wifi_signal;
    int free_ram;
    int free_psram;
    float cpu_temperature;
};

void initialize_int_sys_info();
int get_wifi_signal();
struct interesting_system_information get_sys_int_info();
