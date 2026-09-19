#pragma once
typedef enum
{
    TOUCH_CLICK,
    TOUCH_LONG_TOUCH_START,
    TOUCH_LONG_TOUCH_END,
} touch_click_events_helper_t;

typedef void (*touch_event_cb)(touch_click_events_helper_t event);
void add_touch_chanel(int chan_ids[], int length, touch_event_cb cb);

void initialize_touch_events();
char *get_event_name(touch_click_events_helper_t event);