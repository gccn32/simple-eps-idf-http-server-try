#include "driver/touch_sens.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "touch_events_helper.h"

#define SAMPLE_NUM 1
#define MAX_TOUCH_GROUPS_COUNT 14
#define MIN_CLICK_TIME 10
#define MAX_CLICK_TIME 300
static const char *TAG = "TOUCH-EVENTS";
static touch_sensor_handle_t sensor_handle = NULL;

static touch_channel_config_t chan_cfg;

static QueueHandle_t touch_events_queue;
static bool is_touch_sensor_enabled = false;

typedef struct
{
    touch_event_cb *cb;
    int *channels;
    int channels_quantity;
    int64_t start_touch_time;
    int touch_counter;
    bool long_touch_started;
    TaskHandle_t touch_down_task_handle;
} touch_group_handler_data_t;

static touch_group_handler_data_t touch_groups[MAX_TOUCH_GROUPS_COUNT];
static int touch_groups_quantity = 0;
SemaphoreHandle_t touch_down_mutex;

typedef struct
{
    touch_event_cb *cb;
    touch_click_events_helper_t event;
} touch_click_handler_data_t;

char *get_event_name(touch_click_events_helper_t event)
{
    switch (event)
    {
    case TOUCH_CLICK:
        return "TOUCH_CLICK";
    case TOUCH_LONG_TOUCH_START:
        return "TOUCH_LONG_TOUCH_START";
    case TOUCH_LONG_TOUCH_END:
        return "TOUCH_LONG_TOUCH_END";
    };
    return "UNKNOWN_EVENT";
}

static void touch_down_watch_handler(void *pvParams)
{
    vTaskDelay(pdMS_TO_TICKS(MAX_CLICK_TIME));
    if (xSemaphoreTake(touch_down_mutex, portMAX_DELAY))
    {
        touch_group_handler_data_t *group_data = (touch_group_handler_data_t *)pvParams;
        group_data->long_touch_started = true;

        touch_event_cb cb = (touch_event_cb)group_data->cb;
        if (cb == NULL)
        {
            ESP_LOGI(TAG, "Callback is null in touch_down_watch_handler");
        }
        else
        {
            cb(TOUCH_LONG_TOUCH_START);
        }
        group_data->touch_down_task_handle = NULL;
        xSemaphoreGive(touch_down_mutex);
        vTaskDelete(NULL);
    }
}

static void touch_click_handler(void *pvParams)
{
    touch_click_handler_data_t *data;
    while (1)
    {
        if (xQueueReceive(touch_events_queue, &data, portMAX_DELAY))
        {
            touch_click_events_helper_t event = data->event;
            if (data->cb != NULL)
            {
                touch_event_cb cb = (touch_event_cb)data->cb;
                cb(event);
            }
            else
            {
                ESP_LOGI(TAG, "Click happen from touch_click_handler, callback is NULL");
            }
            free(data);
        }
    }
}
static bool is_channel_in_group(int *channels, int chan_quantity, int chan_id)
{
    for (int c = 0; c < chan_quantity; c++)
    {
        if (channels[c] == chan_id)
        {
            return true;
        }
    }
    return false;
}
static void get_touched_group(int chan_id, touch_group_handler_data_t **touched_group)
{
    for (int i = 0; i < touch_groups_quantity; i++)
    {
        *touched_group = &(touch_groups[i]);
        int *channels = (*touched_group)->channels;
        int length = (*touched_group)->channels_quantity;
        // ESP_EARLY_LOGI(TAG, "get_touched_group touch group, channel, quantity %d, %d, %d", i, chan_id, length);
        if (is_channel_in_group(channels, length, chan_id))
        {
            break;
        }
    }
}

static bool IRAM_ATTR touch_active_callback(touch_sensor_handle_t sens_handle, const touch_active_event_data_t *event, void *user_ctx)
{
    // ESP_EARLY_LOGI(TAG, "Handler touch_active_callback");
    touch_group_handler_data_t *touched_group = NULL;
    get_touched_group(event->chan_id, &touched_group);
    // if (touched_group->cb == NULL)
    if (touched_group->touch_counter == 0)
    {
        touched_group->long_touch_started = false;
        touched_group->start_touch_time = esp_timer_get_time();
        int cur_priority = uxTaskPriorityGet(NULL);
        int watch_task_priority = cur_priority > 5 ? 5 : cur_priority < 2 ? cur_priority
                                                                          : cur_priority - 1;
        xTaskCreate(touch_down_watch_handler, "touch-down-handler", 2048, touched_group, watch_task_priority, &(touched_group->touch_down_task_handle));
    }
    touched_group->touch_counter++;

    return false; // Return true if a higher priority task needs waking up
}

static bool IRAM_ATTR touch_inactive_callback(touch_sensor_handle_t sens_handle, const touch_active_event_data_t *event, void *user_ctx)
{
    // ESP_EARLY_LOGI(TAG, "Handler touch_inactive_callback");
    touch_group_handler_data_t *touched_group = NULL;
    get_touched_group(event->chan_id, &touched_group);
    touched_group->touch_counter--;
    BaseType_t high_priority_task_awoken = pdFALSE;
    if (touched_group->touch_counter == 0)
    {
        int time_diff = (esp_timer_get_time() - touched_group->start_touch_time) / 1000;
        // ESP_EARLY_LOGI(TAG, "Interrupt triggered! channel id %d, time diff %d", event->chan_id, time_diff);

        if (xSemaphoreTake(touch_down_mutex, portMAX_DELAY))
        {

            if ((time_diff > MIN_CLICK_TIME && time_diff < MAX_CLICK_TIME) || (time_diff > MAX_CLICK_TIME && touched_group->long_touch_started))
            {
                touch_click_handler_data_t *data = malloc(sizeof(touch_click_handler_data_t));
                data->cb = touched_group->cb;
                data->event = time_diff < MAX_CLICK_TIME ? TOUCH_CLICK : TOUCH_LONG_TOUCH_END;
                xQueueSendFromISR(touch_events_queue, &data, &high_priority_task_awoken);
            }

            if (touched_group->touch_down_task_handle != NULL)
            {
                vTaskDelete(touched_group->touch_down_task_handle);
                touched_group->touch_down_task_handle = NULL;
            }
            xSemaphoreGive(touch_down_mutex);
        }
    }
    return high_priority_task_awoken == pdTRUE;
}

static void initialize_touch_handling_task()
{
    int cur_priority = uxTaskPriorityGet(NULL);
    int click_handler_priority = cur_priority < 2 ? cur_priority : cur_priority - 1;
    xTaskCreate(touch_click_handler, "touch_click_handler", 2048, NULL, click_handler_priority, NULL);
}

void add_touch_chanel(int chan_ids[], int quantity, touch_event_cb cb)
{
    touch_group_handler_data_t *group = &(touch_groups[touch_groups_quantity]);
    group->channels = malloc(quantity * sizeof(int));
    group->channels_quantity = quantity;
    group->start_touch_time = 0;
    group->touch_counter = 0;
    group->cb = (touch_event_cb *)cb;
    if (touch_groups[touch_groups_quantity].cb == NULL)
        ESP_LOGI(TAG, "Handler is NULL when we add it");
    // ESP_LOGI(TAG, "Channel %d", length);

    int *chan = group->channels;
    if (is_touch_sensor_enabled)
    {
        touch_sensor_stop_continuous_scanning(sensor_handle);
        touch_sensor_disable(sensor_handle);
    }
    for (int i = 0; i < quantity; i++)
    {
        touch_channel_handle_t chan_handle = NULL;
        chan[i] = chan_ids[i];
        int channel_id = chan[i];
        // ESP_LOGI(TAG, "add_touch_chanel cannel id %d", channel_id);
        ESP_ERROR_CHECK(touch_sensor_new_channel(sensor_handle, channel_id, &chan_cfg, &chan_handle));
    }
    ESP_ERROR_CHECK(touch_sensor_enable(sensor_handle));
    ESP_ERROR_CHECK(touch_sensor_start_continuous_scanning(sensor_handle));
    is_touch_sensor_enabled = true;
    touch_groups_quantity++;
}

void initialize_touch_events()
{
    touch_down_mutex = xSemaphoreCreateMutex();

    touch_events_queue = xQueueCreate(5, sizeof(QueueHandle_t));
    initialize_touch_handling_task();

    touch_sensor_sample_config_t sample_cfg[SAMPLE_NUM] = {
        TOUCH_SENSOR_V2_DEFAULT_SAMPLE_CONFIG(1000, TOUCH_VOLT_LIM_L_0V5, TOUCH_VOLT_LIM_H_2V2),
    };

    touch_sensor_config_t touch_cfg = TOUCH_SENSOR_DEFAULT_BASIC_CONFIG(SAMPLE_NUM, sample_cfg);
    touch_cfg.meas_interval_us = 32;
    ESP_ERROR_CHECK(touch_sensor_new_controller(&touch_cfg, &sensor_handle));

    touch_sensor_filter_config_t filter_cfg = TOUCH_SENSOR_DEFAULT_FILTER_CONFIG();

    // Jitter filter step size: controls how quickly the benchmark tracks baseline changes
    filter_cfg.benchmark.jitter_step = 1;
    // Denoise level: 0 = off. 1 or higher provides strong environmental noise resistance
    filter_cfg.benchmark.denoise_lvl = 1;
    // Baseline filtering behavior (standard default)
    filter_cfg.benchmark.filter_mode = TOUCH_BM_JITTER_FILTER;

    // --- Active Data (Touch Read) Settings ---
    // Set the IIR smoothing filter coefficient. Higher values provide smoother data
    // Common options: TOUCH_SMOOTH_IIR_FILTER_2, _4, _8, etc.
    filter_cfg.data.smooth_filter = TOUCH_SMOOTH_IIR_FILTER_8;
    // Hysteresis threshold: prevents the value from flickering right at the threshold edge
    // filter_cfg.data.active_hysteresis = 200;
    filter_cfg.data.active_hysteresis = 50;
    // Debounce count: number of consecutive cycles required to confirm a state change
    filter_cfg.data.debounce_cnt = 1;
    ESP_ERROR_CHECK(touch_sensor_config_filter(sensor_handle, &filter_cfg));

    chan_cfg = (touch_channel_config_t){
        // .active_thresh = {4000},
        // .active_thresh = {2000}, // Sets active threshold relative to baseline
        .active_thresh = {5000}, // Sets active threshold relative to baseline
        .charge_speed = TOUCH_CHARGE_SPEED_7,
        .init_charge_volt = TOUCH_INIT_CHARGE_VOLT_DEFAULT,
    };

    touch_event_callbacks_t callbacks = {
        .on_active = touch_active_callback,
        .on_inactive = touch_inactive_callback,
    };
    ESP_ERROR_CHECK(touch_sensor_register_callbacks(sensor_handle, &callbacks, NULL));
    ESP_LOGI(TAG, "Touch is initialized");
}
