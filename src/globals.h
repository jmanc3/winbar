//
// Created by jmanc3 on 5/21/21.
//

#ifndef WINBAR_GLOBALS_H
#define WINBAR_GLOBALS_H

#include <cairo.h>

class globals {
public:
    cairo_surface_t *unknown_icon_16 = nullptr;
    cairo_surface_t *unknown_icon_32 = nullptr;
    cairo_surface_t *unknown_icon_24 = nullptr;
    cairo_surface_t *unknown_icon_64 = nullptr;

    ~globals() {
        if (unknown_icon_16)
            cairo_surface_destroy(unknown_icon_16);
        if (unknown_icon_32)
            cairo_surface_destroy(unknown_icon_32);
        if (unknown_icon_24)
            cairo_surface_destroy(unknown_icon_24);
        if (unknown_icon_64)
            cairo_surface_destroy(unknown_icon_64);
    }
};

extern globals *global;

#endif //WINBAR_GLOBALS_H
