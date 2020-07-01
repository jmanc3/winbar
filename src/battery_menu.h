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

struct data_fading_button : public UserData {
    double fade_amount = 1;
    ArgbColor pre_fade_color = {0, 0, 0, 0};

    ~data_fading_button() {}
};

struct data_battery_surfaces : public data_fading_button {
    std::vector<cairo_surface_t *> normal_surfaces;
    std::vector<cairo_surface_t *> charging_surfaces;
    std::string status;
    std::string capacity;

    ~data_battery_surfaces() {
        for (auto surface : normal_surfaces) {
            cairo_surface_destroy(surface);
        }
        for (auto surface : charging_surfaces) {
            cairo_surface_destroy(surface);
        }
    }
};

void
start_battery_menu();

#endif // APP_BATTERY_MENU_H
