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
};

void start_battery_menu();

#endif// APP_BATTERY_MENU_H
