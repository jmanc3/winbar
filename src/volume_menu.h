//
// Created by jmanc3 on 3/8/20.
//

#ifndef APP_VOLUME_MENU_H
#define APP_VOLUME_MENU_H

#include "taskbar.h"
#include <cairo.h>

class volume_surfaces : HoverableButton {
public:
    cairo_surface_t *none = nullptr;
    cairo_surface_t *low = nullptr;
    cairo_surface_t *medium = nullptr;
    cairo_surface_t *high = nullptr;
    cairo_surface_t *mute = nullptr;

    ~volume_surfaces() {
        if (none)
            cairo_surface_destroy(none);
        if (low)
            cairo_surface_destroy(low);
        if (medium)
            cairo_surface_destroy(medium);
        if (high)
            cairo_surface_destroy(high);
        if (mute)
            cairo_surface_destroy(mute);
    }
};

void open_volume_menu();

#endif// APP_VOLUME_MENU_H
