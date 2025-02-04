//
// Created by jmanc3 on 5/21/21.
//

#ifndef WINBAR_GLOBALS_H
#define WINBAR_GLOBALS_H

#include <cairo.h>
#include <string>
#include <vector>
#include "taskbar.h"

class HistoricalNameUsed {
public:
    std::string text;
};

class globals {
public:
    
    gl_surface *u16 = new gl_surface;
    cairo_surface_t *unknown_icon_16 = nullptr;
    
    cairo_surface_t *unknown_icon_32 = nullptr;
    gl_surface *u32 = new gl_surface;
    
    cairo_surface_t *unknown_icon_24 = nullptr;
    gl_surface *u24 = new gl_surface;
    
    cairo_surface_t *unknown_icon_64 = nullptr;
    gl_surface *u64 = new gl_surface;
    
    std::vector<HistoricalNameUsed *> history_scripts;
    std::vector<HistoricalNameUsed *> history_apps;
    
    ~globals() {
        if (unknown_icon_16)
            cairo_surface_destroy(unknown_icon_16);
        if (unknown_icon_32)
            cairo_surface_destroy(unknown_icon_32);
        if (unknown_icon_24)
            cairo_surface_destroy(unknown_icon_24);
        if (unknown_icon_64)
            cairo_surface_destroy(unknown_icon_64);
        for (auto s: history_scripts)
            delete s;
        history_scripts.clear();
        history_scripts.shrink_to_fit();
        for (auto a: history_apps)
            delete a;
        history_apps.clear();
        history_apps.shrink_to_fit();
    }
};

extern globals *global;

#endif //WINBAR_GLOBALS_H
