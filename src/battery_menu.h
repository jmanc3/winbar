//
// Created by jmanc3 on 3/29/20.
//

#ifndef APP_BATTERY_MENU_H
#define APP_BATTERY_MENU_H

#include "taskbar.h"
#include "utility.h"

#include <cairo.h>
#include <container.h>

extern double marker_position_scalar;

struct BatteryInfo : public IconButton {
    std::string status;
    std::string capacity;
    int capacity_index = 0;
    int animating_capacity_index = 0;
    bool already_expanded = false;
    long start_time = 0;
    int previous_volume_width = 0;
};

void start_battery_menu();

void
adjust_brightness_based_on_fine_scroll(AppClient *client, cairo_t *cr, Container *container, int scroll_x, int scroll_y,
                                       bool came_from_touchpad);

#endif// APP_BATTERY_MENU_H
