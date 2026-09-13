#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#include "helpers/wifi_config.h"
#include "helpers/fs_operations.h"
#include "services/sys_information.h"
#include "helpers/time_set_up.h"
#include "http_server_handling.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "Memory Usage Initial Free Heap: %u bytes", xPortGetFreeHeapSize());

    wifi_init_sta();
    // wifi_init_softap();
    set_up_time();

    mount_fs();

    initialize_int_sys_info();

    start_webserver();

    ESP_LOGI(TAG, "Free memory left: %u bytes", xPortGetFreeHeapSize());

    // esp_log_level_set("*", ESP_LOG_NONE);
}