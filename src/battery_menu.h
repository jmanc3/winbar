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

struct data_battery_surfaces : public IconButton {
    std::vector<cairo_surface_t *> normal_surfaces;
    std::vector<cairo_surface_t *> charging_surfaces;
    std::string status;
    std::string capacity;
    int capacity_index = 0;
    int animating_capacity_index = 0;
    long previous_status_update_ms = -1;
    int animating_fd = -1;
    
    ~data_battery_surfaces() {
        for (auto surface: normal_surfaces) {
            cairo_surface_destroy(surface);
        }
        for (auto surface: charging_surfaces) {
            cairo_surface_destroy(surface);
        }
    }
};

void start_battery_menu();

#endif// APP_BATTERY_MENU_H
