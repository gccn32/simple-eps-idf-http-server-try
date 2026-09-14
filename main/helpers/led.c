#include "led_strip.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "touch_events_helper.h"
#include "esp_log.h"
#include "freertos/semphr.h"
#include <stdatomic.h>

#define BLINK_GPIO 48
#define MAX_LED_BRIGHTNESS 255
#define LED_COLOR_COUNTS 3
#define LED_CHANGE_BRIGHT_STEP 5

static const char *TAG = "LED-HELPER";

static led_strip_handle_t led_strip;
static TimerHandle_t toggle_led_timer = NULL;
static TimerHandle_t change_brightness_timer = NULL;
SemaphoreHandle_t led_mutex;
typedef enum
{
    LED_SMOOTH_SHIFTING,
    LED_STEP_BY_STEP_JUMP,
} led_flashing_mode_t;

static bool is_led_on = false;
typedef struct
{
    int red;
    int green;
    int blue;
} r_g_b_descriptor;

static r_g_b_descriptor led_color = {
    .red = 0,
    .green = 0,
    .blue = 0,
};
static int cur_led_bright = 10;

static int top_button_chanels[] = {1};
static int bottom_button_chanels[] = {14};
static led_flashing_mode_t led_switching_mode;

static int get_next_index(int index)
{
    return index + 1 >= LED_COLOR_COUNTS ? 0 : index + 1;
}

static int get_cur_index(int **rgb)
{
    for (int i = 0; i < LED_COLOR_COUNTS; i++)
        if ((*rgb[i] != 0 && *rgb[get_next_index(i)] != 0) || (*rgb[i] != 0 && *rgb[get_next_index(i)] == 0 && *rgb[get_next_index(get_next_index(i))] == 0))
            return i;
    return 0;
}

static void switch_on_handle(TimerHandle_t xTimer)
{
    if (xSemaphoreTake(led_mutex, portMAX_DELAY))
    {
        led_strip_set_pixel(led_strip, 0, led_color.red, led_color.green, led_color.blue);
        led_strip_refresh(led_strip);

        int *rgb[] = {&led_color.red, &led_color.green, &led_color.blue};
        int cur_index = get_cur_index(rgb);

        int next_index = get_next_index(cur_index);
        float led_bright_step = 20.0;
        int cur_led_bright_step = ((float)cur_led_bright / MAX_LED_BRIGHTNESS) * led_bright_step;
        cur_led_bright_step = cur_led_bright_step < 1 ? 1 : cur_led_bright_step;

        *rgb[cur_index] = *rgb[cur_index] - cur_led_bright_step < 0 ? 0 : *rgb[cur_index] - cur_led_bright_step;

        *rgb[next_index] = *rgb[next_index] + cur_led_bright_step > cur_led_bright || *rgb[cur_index] == 0 ? cur_led_bright : *rgb[next_index] + cur_led_bright_step;
        xSemaphoreGive(led_mutex);
    }
}

static void step_by_step_handle(TimerHandle_t xTimer)
{
    if (xSemaphoreTake(led_mutex, portMAX_DELAY))
    {
        int *rgb[] = {&led_color.red, &led_color.green, &led_color.blue};
        int cur_index = get_cur_index(rgb);
        int next_index = get_next_index(cur_index);
        *rgb[cur_index] = 0;
        *rgb[next_index] = cur_led_bright;
        led_strip_set_pixel(led_strip, 0, led_color.red, led_color.green, led_color.blue);
        led_strip_refresh(led_strip);
        xSemaphoreGive(led_mutex);
    }
}

int get_new_brightness(int color_value, int cur_brightness, int new_brightness)
{
    int new_value = (float)color_value / cur_brightness * (float)new_brightness;
    return new_value;
}
void fade_in_handle(TimerHandle_t xTimer)
{
    if (xSemaphoreTake(led_mutex, portMAX_DELAY))
    {
        if (!is_led_on)
        {
            led_strip_set_pixel(led_strip, 0, led_color.red, led_color.green, led_color.blue);
            led_strip_refresh(led_strip);
        }
        int new_led_brightness = cur_led_bright + LED_CHANGE_BRIGHT_STEP > MAX_LED_BRIGHTNESS ? MAX_LED_BRIGHTNESS : cur_led_bright + LED_CHANGE_BRIGHT_STEP;
        led_color.red = get_new_brightness(led_color.red, cur_led_bright, new_led_brightness);
        led_color.green = get_new_brightness(led_color.green, cur_led_bright, new_led_brightness);
        led_color.blue = get_new_brightness(led_color.blue, cur_led_bright, new_led_brightness);
        cur_led_bright = new_led_brightness;
        xSemaphoreGive(led_mutex);
    }
}

void fade_out_handle(TimerHandle_t xTimer)
{
    if (xSemaphoreTake(led_mutex, portMAX_DELAY))
    {
        if (!is_led_on)
        {
            led_strip_set_pixel(led_strip, 0, led_color.red, led_color.green, led_color.blue);
            led_strip_refresh(led_strip);
        }
        int new_led_brightness = cur_led_bright - LED_CHANGE_BRIGHT_STEP < LED_CHANGE_BRIGHT_STEP ? LED_CHANGE_BRIGHT_STEP : cur_led_bright - LED_CHANGE_BRIGHT_STEP;
        led_color.red = get_new_brightness(led_color.red, cur_led_bright, new_led_brightness);
        led_color.green = get_new_brightness(led_color.green, cur_led_bright, new_led_brightness);
        led_color.blue = get_new_brightness(led_color.blue, cur_led_bright, new_led_brightness);
        cur_led_bright = new_led_brightness;
        xSemaphoreGive(led_mutex);
    }
}

void start_led_timer()
{
    TimerCallbackFunction_t cb = led_switching_mode == LED_SMOOTH_SHIFTING ? switch_on_handle : step_by_step_handle;
    int timeout = led_switching_mode == LED_SMOOTH_SHIFTING ? 20 : 400;

    toggle_led_timer = xTimerCreate("toggle-led-timer", pdMS_TO_TICKS(timeout), pdTRUE, (void *)0, cb);
    xTimerStart(toggle_led_timer, 0);
}

void switch_led_on()
{
    if (toggle_led_timer != NULL && xTimerIsTimerActive(toggle_led_timer))
    {
        return;
    }
    start_led_timer();
}

void switch_led_off()
{
    if (toggle_led_timer != NULL && xTimerIsTimerActive(toggle_led_timer))
    {
        xTimerStop(toggle_led_timer, portMAX_DELAY);
        xTimerDelete(toggle_led_timer, portMAX_DELAY);
    }
    toggle_led_timer = NULL;
    led_strip_clear(led_strip);
    // ESP_LOGI(TAG, "LED Status: OFF");
}

void change_led_switching_mode()
{
    if (xSemaphoreTake(led_mutex, portMAX_DELAY))
    {
        led_switching_mode = led_switching_mode == LED_SMOOTH_SHIFTING ? LED_STEP_BY_STEP_JUMP : LED_SMOOTH_SHIFTING;
        is_led_on = true;
        if (toggle_led_timer != NULL && xTimerIsTimerActive(toggle_led_timer))
        {
            xTimerStop(toggle_led_timer, portMAX_DELAY);
            xTimerDelete(toggle_led_timer, portMAX_DELAY);
        }
        start_led_timer();
        xSemaphoreGive(led_mutex);
    }
}
bool toggle_led()
{
    if (xSemaphoreTake(led_mutex, portMAX_DELAY))
    {
        is_led_on = !is_led_on;
        if (is_led_on)
            switch_led_on();
        else if (change_brightness_timer == NULL)
            switch_led_off();
        xSemaphoreGive(led_mutex);
    }
    return is_led_on;
}

static void start_fade_led_out()
{
    if (change_brightness_timer != NULL && xTimerIsTimerActive(change_brightness_timer))
    {
        return;
    }
    change_brightness_timer = xTimerCreate("change-brightnesstimer", pdMS_TO_TICKS(20), pdTRUE, (void *)0, fade_out_handle);
    xTimerStart(change_brightness_timer, 0);
}

static void start_fade_led_in()
{
    if (change_brightness_timer != NULL && xTimerIsTimerActive(change_brightness_timer))
    {
        return;
    }
    change_brightness_timer = xTimerCreate("change-brightnesstimer", pdMS_TO_TICKS(20), pdTRUE, (void *)0, fade_in_handle);
    xTimerStart(change_brightness_timer, 0);
}

static void stop_fade_lade_in_out()
{
    if (change_brightness_timer != NULL && xTimerIsTimerActive(change_brightness_timer))
    {
        xTimerStop(change_brightness_timer, portMAX_DELAY);
        xTimerDelete(change_brightness_timer, portMAX_DELAY);
    }
    change_brightness_timer = NULL;
    if (xSemaphoreTake(led_mutex, portMAX_DELAY))
    {
        if (!is_led_on)
        {
            led_strip_clear(led_strip);
        }
        xSemaphoreGive(led_mutex);
    }
}

static void top_click_handler(touch_click_events_helper_t event)
{
    // ESP_LOGI(TAG, "Up handler (Channel-GPIO 4) %s", get_event_name(event));
    if (event == TOUCH_CLICK)
    {
        toggle_led();
    }
    else if (event == TOUCH_LONG_TOUCH_START)
        start_fade_led_in();
    else if (event == TOUCH_LONG_TOUCH_END)
        stop_fade_lade_in_out();
}

static void bottom_click_handler(touch_click_events_helper_t event)
{
    // ESP_LOGI(TAG, "Down handler (Channel-GPIO 11) %s", get_event_name(event));
    if (event == TOUCH_CLICK)
        change_led_switching_mode();
    else if (event == TOUCH_LONG_TOUCH_START)
        start_fade_led_out();
    else if (event == TOUCH_LONG_TOUCH_END)
        stop_fade_lade_in_out();
}

void initialize_led()
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = BLINK_GPIO,
        .max_leds = 1, // one LED on board
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .flags.with_dma = false,
        .clk_src = RMT_CLK_SRC_DEFAULT,
    };

    led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);
    led_strip_clear(led_strip);
    led_color.red = cur_led_bright;

    add_touch_chanel(top_button_chanels, sizeof(top_button_chanels) / sizeof(int), top_click_handler);
    add_touch_chanel(bottom_button_chanels, sizeof(bottom_button_chanels) / sizeof(int), bottom_click_handler);
    led_mutex = xSemaphoreCreateMutex();

    ESP_LOGI(TAG, "LED touch initialized on pin 1(up click) and 14(down click)");
}
